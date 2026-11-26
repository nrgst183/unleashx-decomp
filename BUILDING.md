# Building UnleashX

The result is a new `Default.xbe` built from `src/`: it behaves like
UnleashX 0.39.0528A but is not a byte-for-byte copy of the original (see
the [README](README.md)).

There are two ways to build it:

* **[With RXDK](#with-rxdk)**, Team Resurgent's open Xbox SDK and its clang
  toolchain. Nothing of Microsoft's is needed, and it runs natively on
  Windows, Linux and macOS.
* **[With the XDK](#with-the-xdk)** 5933, Microsoft's SDK and compiler (what
  UnleashX itself was built with).

There's also **[a Windows build](#for-windows)**: the same code as a native
Windows program, with the Xbox's drives as folders on the PC.

## With RXDK

[RXDK](https://github.com/Team-Resurgent/RXDK-VS20XX) replaces the XDK with
open libraries (picolibc, libc++, xAPI, D3D8, DirectSound, XNet, XMV) built
with a clang/LLVM toolchain for the Xbox's Pentium III.

### What you need

* **Visual Studio 2022 or 2026** with the
  [RXDK extension](https://github.com/Team-Resurgent/RXDK-VS20XX/releases/latest):
  open the RXDK tool window and click **Install Prerequisites**, which puts
  the SDK, the LLVM toolchain and the host tools (imagebld) in
  `%ProgramData%\RXDK`.
* **Or, for make** (Linux, macOS, or MSYS2/Git Bash on Windows): the same three
  pieces in `$RXDK` (`~/.local/share/rxdk` on Linux,
  `~/Library/Application Support/RXDK` on macOS, `%ProgramData%\RXDK` on
  Windows; the RXDK VS Code extension installs them there too), or by hand:
  * `sdk/`: [RXDK-SDK](https://github.com/Team-Resurgent/RXDK-SDK) (tested with v1.3.12);
  * `llvm/xbox-<os>-<arch>/`: the toolchain, `xbox-<os>-<arch>.zip` from
    [Team-Resurgent/llvm-project's releases](https://github.com/Team-Resurgent/llvm-project/releases);
  * `tools/imagebld`: from [RXDK-Tools](https://github.com/Team-Resurgent/RXDK-Tools)
    (`src/Rxdk.ImageBld`, .NET 8).

  `RXDK_SDK=`, `RXDK_LLVM=` and `IMAGEBLD=` point at each one elsewhere.
* **Python 3**, for make's last step (`tools/xbe.py retail`). Visual Studio
  doesn't need it: its project does that step itself (an inline MSBuild
  task).

### Visual Studio

Open `rxdk/UnleashX.sln` and build **Release|Xbox**. The output is in
`build\vs-rxdk\Release\`: `default.xbe` (rescrambled for retail kernels right
after imagebld) and the disc image in
`XISO\`. The project is generated from Makefile.rxdk's lists and flags by
`python tools/gen_vcxproj_rxdk.py`; rerun that after adding or removing
files.

### make

```sh
make -f Makefile.rxdk info          # check what it found
make -f Makefile.rxdk -j8 xiso      # build/rxdk/default.xbe and UnleashX.iso
make -f Makefile.rxdk -j8 exe       # just the linked default.exe (no imagebld needed)
make -f Makefile.rxdk DEBUG=1 -j8   # -O0 -g against the SDK's debug libraries
```

Outputs, in `build/rxdk/`: the same files as the XDK build below
(`default.xbe`, `debug.xbe`, `UnleashX.iso`, `default.map`, the per-file
`obj/**/*.log`). After linking, `tools/rxdk_check.py` checks the executable:
the C++ unwind tables are where libc++ looks for them, and no SSE2
instruction (which the Xbox's CPU doesn't have) made it in.

### How the RXDK build differs

The code is the same; `UX_RXDK` selects the few places where RXDK differs
from the XDK (`src/common/PlatformRxdk.h`, `xbox/RxdkCompat.cpp`,
`xbox/rxdk/`):

* **`new` returns NULL** when memory runs out, as MSVC 7.1's did (libc++'s
  throws, and UnleashX checks for NULL and catches nothing), compiled with
  `-fcheck-new` so clang keeps those checks.
* **`-fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks`**: MSVC
  7.1 never optimized on these, and the decompiled code relies on that.
* **Wide `printf` formats** keep MSVC's meaning (`%s` in `swprintf` is a wide
  string): RXDK's libc translates them.
* RXDK's XACT has no file I/O hook; UnleashX's was a no-op.
* `.wmv` videos don't play (the WMV decoder was only ever in XDK 4xxx);
  `.xmv` does.

## With the XDK

### What you need

* **The Xbox XDK**, build 5933 (the one UnleashX was built with; nearby 5xxx
  builds should work too: the XDK 5788 headers and libraries also build
  cleanly). It isn't included. Either an installed one
  (`C:\Program Files\Microsoft Xbox SDK`, with `xbox\bin`, `xbox\include`,
  `xbox\lib`) or a copy of that folder anywhere, or a flat copy of `bin`,
  `include` and `lib`. Only its compiler (`bin\vc71`), `imagebld`, headers
  and libraries are used.
* **Optional, for `.wmv` videos:** the WMV decoder, `wmvdec.lib` and
  `wmvxmo.h`. They only came with XDK 4xxx releases (UnleashX linked 4627's;
  4361's works), not with 5933. Point `WMVDEC=` at such an XDK or at a
  folder holding the two files; by default `./xdk-wmv` is used. Without it
  the build works, but `.wmv` files don't open (`.xmv` still plays).
  `make -f Makefile.xbox info` says whether it was found. (The VS2003
  project doesn't pick it up; add `UX_WMVDEC`, the header folder and the
  library by hand.)
* **Python 3**, for the last step (rescrambling the XBE for retail
  kernels, `tools/xbe.py retail`).
* On **Linux or macOS**: GNU make, Wine (to run the XDK's tools) and a C
  compiler (for extract-xiso).
* On **Windows**: Visual Studio .NET 2003 with the XDK installed, or GNU
  make from MSYS2 or Git Bash.

Everything else is in the repository.

### Linux and macOS

```sh
make -f Makefile.xbox XDK=/path/to/xdk info     # check what it found
make -f Makefile.xbox XDK=/path/to/xdk -j4 xiso
```

(`make xiso XDK=...` and `make xbox XDK=...` from the main Makefile do the
same.)

Instead of `XDK=` you can set the `XDK` environment variable, or put the
XDK (or a symlink to it) at `./xdk`; do that if its path has spaces,
which make can't handle.

Outputs, in `build/xbox/`:

| File | What |
|---|---|
| `default.xbe` | UnleashX, scrambled for retail kernels (consoles, xemu) |
| `debug.xbe` | The same as imagebld made it, for debug kits |
| `UnleashX.iso` | A disc image: `default.xbe`, the XML files and the sample skin (`assets/disc/`) |
| `default.map` | The linker map, for addresses in crash reports |
| `obj/**/*.log` | The compiler's full output for each file |

A full build takes a minute or two (`-j4`). The first time, Wine may set up
its prefix, which takes a little longer.

### Windows

**Visual Studio .NET 2003**: open `vs2003/UnleashX.sln` and build the
Release configuration. The XDK's Visual Studio integration provides the Xbox
platform, compiler and libraries; the project's post-build step runs
imagebld with UnleashX's sections and then `tools\xbe.py retail` (if Python
is on the path). The output is `build\vs2003\default.xbe` (the
`UnleashX.xbe` the IDE's own image step makes next to it lacks UnleashX's
resources). The project is generated
from the same file lists as the Makefile by `python tools/gen_vcproj.py`;
rerun that after adding or removing files, or changing the zips in
`assets/`.

**GNU make**: from an MSYS2 or Git Bash shell, the same command as above
runs the XDK's tools directly (no Wine). Make can't handle spaces in paths,
so link the XDK into the repository first (a junction, from `cmd`):

```bat
mklink /J xdk "C:\Program Files\Microsoft Xbox SDK"
```

and then just `make -f Makefile.xbox -j4 xiso`.

## For Windows

The dash compiled for the PC: a 32-bit Windows program that runs UnleashX's
own code, with the Xbox APIs it calls provided by `xbox/win32/` (Direct3D 8
on Direct3D 9, XInput, the sound on waveOut, the drives as folders, the
network on Winsock). It's for working on the dash without a console or an
emulator, and for fun; it doesn't replace either.

**Visual Studio 2022** (or later, with "Desktop development with C++"): open
`win32/UnleashX.sln`, pick `Release` and `x86`, and build. The program is
`build\vs-win32\Release\UnleashX.exe`, with UnleashX's own files in `disc\`
next to it. Nothing else is needed (not Python, not the DirectX SDK).

**make**, with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (on
Linux, macOS, or Windows with MSYS2): `make -f Makefile.win32 -j8` builds
`build/win32/UnleashX.exe` the same way; `make -f Makefile.win32 run` runs it
(with Wine, off Windows).

After adding or removing source files, `python3 tools/gen_vcxproj_win32.py`
regenerates the Visual Studio project from `Makefile.win32`.

### Running it

The Xbox's drives are folders in `XboxHDD\` next to the program (or in the
folder `UNLEASHX_XBOX_ROOT` names): `C`, `E`, `F`, `G`, `X`, `Y`, `Z` for the
hard disk's partitions and `DVD` for the disc drive. The first start puts
UnleashX's files in `E\Apps\UnleashX` (from `disc\`, without overwriting
anything) and runs from there, as if launched from `E:\Apps\UnleashX`.
What the console keeps in its EEPROM (language, video settings...) goes in
`XboxHDD\eeprom.txt`, and what happens (drive mappings, launches, crashes) in
`UnleashX.log` next to the program.

Controllers work through XInput (an Xbox 360 or One pad: LB is white, RB
black). The keyboard is the Xbox's debug keyboard, which UnleashX supports
itself (arrows, Enter, Esc, typing), plus the first controller's buttons:

| Key | Button | Key | Button |
|---|---|---|---|
| F1 | A | F5 | White |
| F2 | B | F6 | Black |
| F3 | X | Page Up / Page Down | Left / right trigger |
| F4 | Y | Tab / F9 | Back / Start |

Alt+Enter switches between a window and the full screen.

What doesn't work on a PC: launching Xbox programs (it says so; a game or
app "launched" restarts the dash), flashing, locking the hard disk, memory
units, soundtracks and WMA music (MP3 plays), XMV/WMV videos, TrueType skin
fonts, and anything the dash draws into a texture rather than the screen.

## Running it

**xemu**: set the disc image as the DVD (Machine → Load Disc) and boot. xemu
needs an MCPX boot ROM and a flash BIOS (your own dumps). The XBE isn't
signed, so the BIOS has to be one that runs unsigned code (a modded BIOS;
the 4627 retail BIOS in xemu has worked in our tests). A blank hard disk
image is at <https://github.com/xemu-project/xemu-hdd-image>.

FTP into xemu: turn networking on (NAT) and forward the guest's port 21 to
a host port. Either add this to `xemu.toml` (`~/.local/share/xemu/xemu/` on
Linux) before starting xemu:

```toml
[net]
enable = true
backend = 'nat'

[net.nat]
forward_ports = [ { host = 2121, guest = 21, protocol = 'tcp' } ]
```

or, on a running xemu started with a monitor (`-qmp unix:qmp.sock,server,nowait`),
send the HMP command `hostfwd_add xemu-netdev tcp:127.0.0.1:2121-:21`. Then
`python3 tools/xemu_ftp.py ls /E` (login `xbox`/`xbox`). The script uses
active mode with the address the guest sees the host at (10.0.2.2), so the
data ports need no forwarding; a normal client in passive mode would need the
whole `MinPASVPort`..`MaxPASVPort` range forwarded.

To try the game saves manager without real game data,
`python3 tools/make_test_save.py out` writes a made-up game with three saves
in the Xbox's format (`UDATA`/`TDATA`, meta files, DXT1 pictures), and
`python3 tools/xemu_ftp.py putdir out /E` copies it to the hard disk.

For the item editor's icon download, `tools/fake_xboxskins_proxy.py` runs an
HTTP proxy on the host that adds icons to xbox-skins.net's answers; set
UnleashX's proxy to `10.0.2.2:8888` (Control Panel > Network).

To boot UnleashX as the dashboard with no disc, upload `default.xbe` as
`C:\xboxdash.xbe`, with `Config.xml`, the language files and `Skins` from
`assets/disc/` next to it, then eject the disc from xemu's Machine menu and
reset. (Ejecting through the monitor removes the image without telling the
SMC, so UnleashX keeps reporting a disc of unknown type.)

**A modded console**: run `default.xbe` from the hard disk (FTP it to, say,
`E:\Apps\UnleashX\` with the XML files and `Skins` from `assets/disc/`), or
burn the disc image. As a dashboard, it replaces your current one the usual
way; try it as an app first.

On first start UnleashX unpacks its media into a save game on `E:`
(`E:\UDATA\9e115330\`).

## Changing things

* **The media** (`assets/media.zip`: fonts, the globe and orb models and
  textures, the splash image, sounds): unzip, change, zip again. The size
  of the zip is compiled in and recorded on the Xbox, so a changed zip is
  unpacked again on the next start. The **built-in skins**
  (`assets/internal_skins.zip`, the `Skin.xml` files) work the same way.
* **The default font** and **title images** are `.xpr` files
  (`assets/default_font.xpr`, `title_image.xpr`, `save_image.xpr`); the
  XDK's bundler makes those.
* **Files on the disc**: anything in `assets/disc/` goes onto the disc
  image (`Config.xml`, the languages, `Skins/<name>/`).
* **The code**: `src/` by subsystem, see the [docs](docs/). `make test`
  builds the host tests with any modern g++.

## Troubleshooting

* `No XDK found`: `make -f Makefile.xbox XDK=... info` lists what was found;
  the compiler is looked for in `bin/vc71`, imagebld in `bin`.
* A compile error: the full output is in `build/xbox/obj/<file>.obj.log`.
* Black screen or a reboot loop on the Xbox or in xemu: UnleashX's crash
  handler writes the exception to `Z:\debug.txt` and reboots. The address in
  it can be looked up in `default.map` (the map's addresses are 0x3EF480
  above the XBE's for `.text`: the PE was linked at 0x400000 with `.text`
  at 0x480, imagebld moves it to 0x11000).
