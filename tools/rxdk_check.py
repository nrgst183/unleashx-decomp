#!/usr/bin/env python3
"""Checks a PE linked by Makefile.rxdk before imagebld turns it into an XBE.

  rxdk_check.py <default.exe> [llvm-objdump]

* The .eh_frame bracket: libcpp's unwinder finds the title's unwind tables
  between ___eh_frame_start and ___eh_frame_end, so both must sit exactly at
  the edges of the merged .eh_frame section (RXDK's engine checks the same,
  Rxdk.Engine EhFrameCheck). An FDE outside them makes a throw through that
  function terminate on the console.
* With an llvm-objdump given: no SSE2 or later instructions in .text. The
  Xbox's Pentium III has SSE but not SSE2; one stray instruction is an
  invalid-opcode crash the first time it runs.
"""
import re
import struct
import subprocess
import sys


def read_pe(data):
    if data[:2] != b'MZ':
        raise ValueError('not a PE image')
    pe, = struct.unpack_from('<I', data, 0x3C)
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('not a PE image')
    nsec, = struct.unpack_from('<H', data, pe + 6)
    symptr, nsym = struct.unpack_from('<II', data, pe + 12)
    optsize, = struct.unpack_from('<H', data, pe + 20)
    strtab = symptr + nsym * 18

    def name(field, symbol):
        if symbol and field[:4] == b'\0\0\0\0':
            off, = struct.unpack_from('<I', field, 4)
        elif not symbol and field[:1] == b'/':
            off = int(field[1:].rstrip(b'\0'))
        else:
            return field.rstrip(b'\0').decode('latin-1')
        end = data.index(b'\0', strtab + off)
        return data[strtab + off:end].decode('latin-1')

    sections = []
    base = pe + 24 + optsize
    for i in range(nsec):
        s = data[base + i * 40:base + i * 40 + 40]
        vsize, = struct.unpack_from('<I', s, 8)
        sections.append((name(s[:8], False), vsize))
    symbols = {}
    i = 0
    while symptr and i < nsym:
        e = data[symptr + i * 18:symptr + i * 18 + 18]
        value, secnum = struct.unpack_from('<Ih', e, 8)
        symbols[name(e[:8], True)] = (value, secnum)
        i += 1 + e[17]
    return sections, symbols


def check_eh_frame(sections, symbols):
    start = symbols.get('___eh_frame_start')
    end = symbols.get('___eh_frame_end')
    if start is None and end is None:
        return None
    if start is None or end is None:
        return 'only one of ___eh_frame_start/___eh_frame_end is defined'
    if start[1] != end[1] or not 1 <= start[1] <= len(sections):
        return '___eh_frame_start and ___eh_frame_end are not in the same section'
    secname, size = sections[start[1] - 1]
    if not secname.startswith('.eh_fram'):
        return '___eh_frame_start is in %s, not .eh_frame' % secname
    if start[0] != 0 or end[0] != size:
        return ('.eh_frame is 0x%X bytes but the markers bracket +0x%X..+0x%X'
                % (size, start[0], end[0]))
    return None


# What SSE1 (and MMX-extension) has on xmm registers; anything else touching
# xmm is SSE2 or later. Plus the SSE2 instructions that don't name xmm.
SSE1 = {
    'addps', 'addss', 'andnps', 'andps', 'cmpps', 'cmpss', 'comiss', 'cvtpi2ps',
    'cvtps2pi', 'cvtsi2ss', 'cvtsi2ssl', 'cvtss2si', 'cvttps2pi', 'cvttss2si', 'divps',
    'divss', 'ldmxcsr', 'maxps', 'maxss', 'minps', 'minss', 'movaps', 'movhlps',
    'movhps', 'movlhps', 'movlps', 'movmskps', 'movntps', 'movss', 'movups', 'mulps',
    'mulss', 'orps', 'rcpps', 'rcpss', 'rsqrtps', 'rsqrtss', 'shufps', 'sqrtps',
    'sqrtss', 'stmxcsr', 'subps', 'subss', 'ucomiss', 'unpckhps', 'unpcklps',
    'xorps', 'cmpeqss', 'cmpltss', 'cmpless', 'cmpunordss', 'cmpneqss', 'cmpnltss',
    'cmpnless', 'cmpordss', 'cmpeqps', 'cmpltps', 'cmpleps', 'cmpunordps', 'cmpneqps',
    'cmpnltps', 'cmpnleps', 'cmpordps',
}
SSE2_NO_XMM = {'lfence', 'mfence', 'clflush', 'movnti'}


def check_sse2(exe, objdump):
    out = subprocess.run([objdump, '-d', '--no-show-raw-insn', '-j', '.text', exe],
                         capture_output=True, text=True, check=True).stdout
    bad = {}
    func = '?'
    for line in out.splitlines():
        m = re.match(r'^[0-9a-f]+ <(.*)>:$', line)
        if m:
            func = m.group(1)
            continue
        m = re.match(r'^\s*[0-9a-f]+:\s+(\S+)\s*(.*)$', line)
        if not m:
            continue
        mnem, ops = m.group(1), m.group(2)
        if mnem in SSE2_NO_XMM or ('%xmm' in ops and mnem not in SSE1):
            bad.setdefault(func, set()).add(mnem)
    return bad


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    exe = sys.argv[1]
    data = open(exe, 'rb').read()
    sections, symbols = read_pe(data)
    error = check_eh_frame(sections, symbols)
    if error:
        sys.exit('%s: .eh_frame bracket: %s' % (exe, error))
    if len(sys.argv) > 2:
        bad = check_sse2(exe, sys.argv[2])
        if bad:
            for func, mnems in sorted(bad.items())[:40]:
                print('%s: SSE2 in %s: %s' % (exe, func, ' '.join(sorted(mnems))), file=sys.stderr)
            sys.exit('%s: %d functions use instructions the Xbox CPU lacks' % (exe, len(bad)))


if __name__ == '__main__':
    main()
