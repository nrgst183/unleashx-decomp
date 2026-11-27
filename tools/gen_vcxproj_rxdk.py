#!/usr/bin/env python3
"""Generates rxdk/UnleashX-Xbox.vcxproj and its .filters (and UnleashX.sln at the root, gen_sln.py): the
RXDK build for Visual Studio 2022/2026 with the RXDK extension
(github.com/Team-Resurgent/RXDK-VS20XX), from the same sources and flags as
Makefile.rxdk. Rerun after adding or removing source files, or after changing
assets/media.zip or assets/internal_skins.zip (their sizes are compiled in).

  python3 tools/gen_vcxproj_rxdk.py

The project uses the extension's RXDK application type (PlatformToolset
RXDK), which compiles each file with RXDK's clang and honours per-file
options, so the third-party code keeps its own flags as in the Makefile.
"""
import glob
import os
import re

import gen_sln

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUTDIR = os.path.join(ROOT, 'rxdk')

PROJECT_GUID = '{3B7D9E21-6A4C-4F1E-8D53-2C9A0E7B4F61}'
VCXPROJ_TYPE = '{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}'
CONFIGS = ('Debug', 'Release')
HOST_ONLY = {'src/common/PlatformHost.cpp', 'src/net/XNetHost.cpp'}



RETAIL_TASK = r"""  <UsingTask TaskName="UnleashXRetailXbe" TaskFactory="RoslynCodeTaskFactory" AssemblyFile="$(MSBuildToolsPath)\Microsoft.Build.Tasks.Core.dll">
    <ParameterGroup>
      <Xbe ParameterType="System.String" Required="true" />
      <InitFlags ParameterType="System.String" />
    </ParameterGroup>
    <Task>
      <Using Namespace="System" />
      <Using Namespace="System.IO" />
      <Code Type="Fragment" Language="cs"><![CDATA[
        // tools/xbe.py retail: imagebld scrambles the entry point and kernel
        // thunk with the debug keys; retail kernels need the retail ones.
        // Also sets the XAPI init flags, which RXDK's imagebld step can't.
        const uint EpRetail = 0xA8FC57AB, EpDebug = 0x94859D4B;
        const uint KtRetail = 0x5B6D40B6, KtDebug = 0xEFB1F152;
        byte[] d = File.ReadAllBytes(Xbe);
        if (d.Length < 0x180 || d[0] != 'X' || d[1] != 'B' || d[2] != 'E' || d[3] != 'H')
        {
            Log.LogError("{0}: not an XBE", Xbe);
            return false;
        }
        ulong baseVa = BitConverter.ToUInt32(d, 0x104), size = BitConverter.ToUInt32(d, 0x10C);
        uint ep = BitConverter.ToUInt32(d, 0x128), kt = BitConverter.ToUInt32(d, 0x158);
        ulong va = ep ^ EpRetail;
        bool retail = va >= baseVa && va < baseVa + size;
        uint entry = ep ^ (retail ? EpRetail : EpDebug);
        uint thunk = kt ^ (retail ? KtRetail : KtDebug);
        bool changed = false;
        Action<int, uint> set = (offset, value) =>
        {
            if (BitConverter.ToUInt32(d, offset) == value)
                return;
            Array.Copy(BitConverter.GetBytes(value), 0, d, offset, 4);
            changed = true;
        };
        set(0x128, entry ^ EpRetail);
        set(0x158, thunk ^ KtRetail);
        if (!string.IsNullOrEmpty(InitFlags))
            set(0x124, Convert.ToUInt32(InitFlags, 16));
        // (Not rewritten when already right, so the disc image stays up to date.)
        if (changed)
            File.WriteAllBytes(Xbe, d);
        Log.LogMessage(MessageImportance.High, "{0}: {1}", Xbe, changed ? "rescrambled for retail kernels" : "already retail");
      ]]></Code>
    </Task>
  </UsingTask>
  <Target Name="UnleashXRetailXbe" AfterTargets="ImageBld" Condition="'@(ImageBld)' != ''">
    <UnleashXRetailXbe Xbe="%(ImageBld.OutputFile)" InitFlags="0x1C" />
  </Target>"""


def makefile_var(name):
    """A variable from Makefile.rxdk, so the two builds can't drift apart."""
    text = open(os.path.join(ROOT, 'Makefile.rxdk')).read()
    m = re.search(r'^%s :?= ((?:[^\n]*\\\n)*[^\n]*)' % name, text, re.M)
    return m.group(1).replace('\\\n', ' ').split()


def flags(name):
    """UX_FLAGS/TP_FLAGS from Makefile.rxdk: the defines become
    PreprocessorDefinitions, everything else AdditionalOptions."""
    defines, options = [], []
    for f in makefile_var(name):
        if '$(' in f or f.endswith(')'):
            continue   # make expressions (the asset sizes, added separately)
        if f.startswith('-D'):
            if not f.startswith(('-DMEDIA_FILE_SIZE', '-DINT_SKINS_SIZE', '-DNDEBUG')):
                defines.append(f[2:])
        elif f != '-Wall' and f != '-w':
            options.append(f)
    return defines, options


def win(path):
    return '..\\' + path.replace('/', '\\')


def esc(s):
    return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('"', '&quot;')


class Source:
    def __init__(self, path, group, meta=None):
        self.path, self.group, self.meta = path, group, dict(meta or {})
        # Objects are named after the folder too: some names repeat
        # (src/system/Md5.cpp and src/thirdparty/Md5.cpp, xbox/Zip.cpp and
        # thirdparty/zip_utils/zip.cpp), and the toolset's default is the bare name.
        folder = os.path.dirname(path).replace('/', '_')
        self.meta['ObjectFileName'] = '$(IntDir)%s_%s.obj' % (folder, os.path.splitext(os.path.basename(path))[0])


def sources():
    ux_defines, ux_options = flags('UX_FLAGS')
    tp_defines, tp_options = flags('TP_FLAGS')
    tp = {'WarningLevel': 'TurnOffAllWarnings',
          'AdditionalOptions': ' '.join(tp_options)}

    def tp_meta(defines=(), includes=(), forced=()):
        m = dict(tp)
        m['PreprocessorDefinitions'] = ';'.join(tp_defines + list(defines) + ['%(PreprocessorDefinitions)'])
        m['RxdkAdditionalIncludeDirectories'] = ';'.join(['$(ProjectDir)' + win(i) for i in includes] +
                                                         ['%(RxdkAdditionalIncludeDirectories)'])
        if forced:
            m['ForcedIncludeFiles'] = ';'.join(['picolibc.h'] + ['$(ProjectDir)' + win(f) for f in forced])
        return m

    out = []
    for sub in sorted(os.listdir(os.path.join(ROOT, 'src'))):
        for p in sorted(glob.glob(os.path.join(ROOT, 'src', sub, '*.cpp'))):
            rel = os.path.relpath(p, ROOT).replace(os.sep, '/')
            if rel not in HOST_ONLY:
                out.append(Source(rel, 'src\\' + sub))
    for p in sorted(glob.glob(os.path.join(ROOT, 'xbox', '*.cpp'))):
        rel = os.path.relpath(p, ROOT).replace(os.sep, '/')
        meta = {}
        if rel == 'xbox/Fat32.cpp':
            meta = {'PreprocessorDefinitions': 'FATFS_USE_CUSTOM_OPTS_FILE;%(PreprocessorDefinitions)',
                    'RxdkAdditionalIncludeDirectories': '$(ProjectDir)..\\xbox\\include;%(RxdkAdditionalIncludeDirectories)'}
        out.append(Source(rel, 'xbox', meta))

    # The third-party code, quietly and with its own flags. xbox/include's
    # <windows.h> (what zip_utils needs beyond <xtl.h>) is forced in, as the
    # SDK's own windows.h comes first on this toolset's include path.
    for f in ('unzip.cpp', 'zip.cpp'):
        out.append(Source('thirdparty/zip_utils/' + f, 'thirdparty\\zip_utils',
                          tp_meta(forced=['xbox/include/windows.h'])))
    out.append(Source('thirdparty/ftpparse/ftpparse.c', 'thirdparty\\ftpparse', tp_meta()))
    for f in makefile_var('FATIO_SRCS'):
        out.append(Source('thirdparty/fat_io_lib/%s.c' % f, 'thirdparty\\fat_io_lib',
                          tp_meta(['FATFS_USE_CUSTOM_OPTS_FILE'], ['xbox/include'])))
    for f in makefile_var('MAD_SRCS'):
        out.append(Source('thirdparty/libmad/%s.c' % f, 'thirdparty\\libmad',
                          tp_meta(['FPM_INTEL', 'HAVE_CONFIG_H', 'ASO_ZEROCHECK'], ['thirdparty/libmad'])))
    for f in makefile_var('RAR_SRCS'):
        out.append(Source('thirdparty/unrar/%s.cpp' % f, 'thirdparty\\unrar',
                          tp_meta(['_LIB'], ['thirdparty/unrar/shim'], ['thirdparty/unrar/shim/unrar_xbox.h'])))
    # C files: no unwind tables (Makefile.rxdk's C_FLAGS).
    c_flags = [f for f in makefile_var('C_FLAGS') if f.startswith('-fno-')]
    for source in out:
        if source.path.endswith('.c'):
            source.meta = dict(source.meta)
            source.meta['AdditionalOptions'] = (source.meta.get('AdditionalOptions', '') + ' ' +
                                                ' '.join(c_flags)).strip()
    return out, ux_defines, ux_options


def headers():
    out = []
    for sub in sorted(os.listdir(os.path.join(ROOT, 'src'))):
        for p in sorted(glob.glob(os.path.join(ROOT, 'src', sub, '*.h'))):
            out.append((os.path.relpath(p, ROOT).replace(os.sep, '/'), 'src\\' + sub))
    for p in sorted(glob.glob(os.path.join(ROOT, 'xbox', 'rxdk', '*.h'))):
        out.append((os.path.relpath(p, ROOT).replace(os.sep, '/'), 'xbox\\rxdk'))
    return out


def vcxproj(srcs, ux_defines, ux_options):
    media = os.path.getsize(os.path.join(ROOT, 'assets/media.zip'))
    skins = os.path.getsize(os.path.join(ROOT, 'assets/internal_skins.zip'))
    # (_XBOX and XBOX: RXDK's clang command line has them, but IntelliSense
    # sees only the project's own; without them Platform.h took its host
    # branch there and nothing in the code resolved.)
    defines = ['_XBOX', 'XBOX'] + ux_defines + ['MEDIA_FILE_SIZE=%d' % media, 'INT_SKINS_SIZE=%d' % skins]
    libs = [l + '$(D)' for l in makefile_var('SDK_LIBS')]
    o = []
    w = o.append
    w('<?xml version="1.0" encoding="utf-8"?>')
    w('<!-- Generated by tools/gen_vcxproj_rxdk.py from Makefile.rxdk; rerun it rather than editing. -->')
    w('<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">')
    w('  <ItemGroup Label="ProjectConfigurations">')
    for c in CONFIGS:
        w('    <ProjectConfiguration Include="%s|Xbox">' % c)
        w('      <Configuration>%s</Configuration>' % c)
        w('      <Platform>Xbox</Platform>')
        w('    </ProjectConfiguration>')
    w('  </ItemGroup>')
    w('  <PropertyGroup Label="Globals">')
    w('    <VCProjectVersion>16.0</VCProjectVersion>')
    w('    <ProjectGuid>%s</ProjectGuid>' % PROJECT_GUID)
    w('    <Keyword>RXDK</Keyword>')
    w('    <MinimumVisualStudioVersion>15.0</MinimumVisualStudioVersion>')
    w('    <ApplicationType>RXDK</ApplicationType>')
    w('    <ApplicationTypeRevision>Current</ApplicationTypeRevision>')
    w('    <RootNamespace>UnleashX</RootNamespace>')
    w('    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>')
    w('    <ProjectName>UnleashX-Xbox</ProjectName>')
    # A machine-wide `vcpkg integrate install` would otherwise add its
    # (Windows) libraries to the Xbox link.
    w('    <VcpkgEnabled>false</VcpkgEnabled>')
    w('    <VcpkgEnableManifest>false</VcpkgEnableManifest>')
    w('  </PropertyGroup>')
    w('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />')
    for c in CONFIGS:
        w('  <PropertyGroup Condition="\'$(Configuration)|$(Platform)\'==\'%s|Xbox\'" Label="Configuration">' % c)
        w('    <UseDebugLibraries>%s</UseDebugLibraries>' % ('true' if c == 'Debug' else 'false'))
        w('    <PlatformToolset>RXDK</PlatformToolset>')
        w('    <ConfigurationType>Application</ConfigurationType>')
        w('  </PropertyGroup>')
    w('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />')
    w('  <ImportGroup Label="ExtensionSettings" />')
    w('  <ImportGroup Label="Shared" />')
    w('  <ImportGroup Label="PropertySheets" />')
    w('  <PropertyGroup Label="UserMacros" />')
    w('  <PropertyGroup>')
    w('    <OutDir>$(ProjectDir)..\\build\\vs-rxdk\\$(Configuration)\\</OutDir>')
    w('    <IntDir>$(ProjectDir)..\\build\\vs-rxdk\\$(Configuration)\\obj\\</IntDir>')
    w('    <TargetName>default</TargetName>')
    # assets/disc goes onto the ISO's root (Config.xml, the languages, Skins), as
    # in Makefile.rxdk: one "destination=source" entry per top-level item, since
    # a folder alone would land under its own path.
    disc = sorted(os.listdir(os.path.join(ROOT, 'assets', 'disc')))
    w('    <RxdkDeployPaths>%s</RxdkDeployPaths>' % ';'.join('%s=..\\assets\\disc\\%s' % (n, n) for n in disc))
    w('  </PropertyGroup>')
    w('  <ItemDefinitionGroup>')
    w('    <ClCompile>')
    w('      <CppLanguageStandard>c++17</CppLanguageStandard>')
    w('      <CLanguageStandard>gnu17</CLanguageStandard>')
    w('      <RuntimeTypeInfo>true</RuntimeTypeInfo>')
    w('      <WarningLevel>Level3</WarningLevel>')
    w('      <PreprocessorDefinitions>%s;%%(PreprocessorDefinitions)</PreprocessorDefinitions>' % ';'.join(defines))
    w('      <RxdkAdditionalIncludeDirectories>$(ProjectDir)..\\xbox\\rxdk</RxdkAdditionalIncludeDirectories>')
    w('      <AdditionalOptions>%s %%(AdditionalOptions)</AdditionalOptions>' % esc(' '.join(ux_options)))
    w('    </ClCompile>')
    w('    <Link>')
    w('      <LibraryDependencies>%s</LibraryDependencies>' % ';'.join(libs))
    w('      <AdditionalOptions>%(Link.AdditionalOptions) -Wl,--whole-archive -llibcompat$(D) '
      '-Wl,--no-whole-archive</AdditionalOptions>')
    w('      <GenerateMapFile>$(OutDir)default.map</GenerateMapFile>')
    w('    </Link>')
    # imagebld as in Makefile.rxdk. The init flags (0x1C: no hard disk setup or
    # changes, 64 MB on a devkit) have no switch here, so the retail
    # rescramble task at the end of the project sets them.
    w('    <ImageBld>')
    w('      <StackSize>262144</StackSize>')
    w('      <TestId>0x9E115330</TestId>')
    w('      <TestName>UnleashX Xbox Launcher</TestName>')
    w('      <TestMediaTypes>0x800003FF</TestMediaTypes>')
    w('      <TestRegion>0x80000007</TestRegion>')
    w('      <TestRatings>0xFFFFFFFF</TestRatings>')
    w('      <TitleImage>$(ProjectDir)..\\assets\\title_image.xpr</TitleImage>')
    w('      <DefaultSaveImage>$(ProjectDir)..\\assets\\save_image.xpr</DefaultSaveImage>')
    w('      <NoPreload>MEDIA_FILE;DEF_FONT;INT_SKINS</NoPreload>')
    w('    </ImageBld>')
    w('  </ItemDefinitionGroup>')
    # Release: the dashboard's own code for size, as Makefile.rxdk's UX_OPT
    # (the third-party files set their own options and keep full speed).
    w('  <ItemDefinitionGroup Condition="\'$(Configuration)\'==\'Release\'">')
    w('    <ClCompile>')
    w('      <AdditionalOptions>-Os %(AdditionalOptions)</AdditionalOptions>')
    w('    </ClCompile>')
    w('  </ItemDefinitionGroup>')
    w('  <ItemGroup>')
    for name, path in (('MEDIA_FILE', 'assets/media.zip'), ('DEF_FONT', 'assets/default_font.xpr'),
                       ('INT_SKINS', 'assets/internal_skins.zip')):
        w('    <RxdkEmbed Include="%s">' % win(path))
        w('      <Name>%s</Name>' % name)
        w('    </RxdkEmbed>')
    w('  </ItemGroup>')
    w('  <ItemGroup>')
    for s in srcs:
        w('    <ClCompile Include="%s">' % win(s.path))
        for k, v in s.meta.items():
            w('      <%s>%s</%s>' % (k, esc(v), k))
        w('    </ClCompile>')
    w('  </ItemGroup>')
    w('  <ItemGroup>')
    for path, _ in headers():
        w('    <ClInclude Include="%s" />' % win(path))
    w('  </ItemGroup>')
    w('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />')
    # The retail rescramble (tools/xbe.py retail) as an inline MSBuild task,
    # so nothing (Python, PowerShell) has to be on the path. It runs right
    # after imagebld (which RXDK runs after the post-build event, so that
    # can't do it) and before the XBE is copied into the disc image.
    o.extend(RETAIL_TASK.split('\n'))
    w('</Project>')
    return '\r\n'.join(o) + '\r\n'


def filters(srcs):
    groups = sorted({s.group for s in srcs} | {g for _, g in headers()})
    # Every parent folder needs its own Filter entry.
    allg = set()
    for g in groups:
        parts = g.split('\\')
        for i in range(1, len(parts) + 1):
            allg.add('\\'.join(parts[:i]))
    o = ['<?xml version="1.0" encoding="utf-8"?>',
         '<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">',
         '  <ItemGroup>']
    for i, g in enumerate(sorted(allg)):
        o.append('    <Filter Include="%s">' % g)
        o.append('      <UniqueIdentifier>{6E1F%04X-0D3B-4C2A-9E71-5A4B3C2D1E0F}</UniqueIdentifier>' % i)
        o.append('    </Filter>')
    o.append('  </ItemGroup>')
    o.append('  <ItemGroup>')
    for s in srcs:
        o.append('    <ClCompile Include="%s">' % win(s.path))
        o.append('      <Filter>%s</Filter>' % s.group)
        o.append('    </ClCompile>')
    o.append('  </ItemGroup>')
    o.append('  <ItemGroup>')
    for path, g in headers():
        o.append('    <ClInclude Include="%s">' % win(path))
        o.append('      <Filter>%s</Filter>' % g)
        o.append('    </ClInclude>')
    o.append('  </ItemGroup>')
    o.append('</Project>')
    return '\r\n'.join(o) + '\r\n'


def main():
    srcs, ux_defines, ux_options = sources()
    os.makedirs(OUTDIR, exist_ok=True)
    with open(os.path.join(OUTDIR, 'UnleashX-Xbox.vcxproj'), 'w', newline='') as f:
        f.write(vcxproj(srcs, ux_defines, ux_options))
    with open(os.path.join(OUTDIR, 'UnleashX-Xbox.vcxproj.filters'), 'w', newline='') as f:
        f.write(filters(srcs))
    print('rxdk/UnleashX-Xbox.vcxproj: %d sources' % len(srcs))
    gen_sln.main()   # (the solution at the root, with the Windows build)


if __name__ == '__main__':
    main()
