#!/usr/bin/env python3
"""Generates win32/UnleashX.sln, win32/UnleashX.vcxproj and its .filters: the
Windows build (UnleashX as a native Windows program, xbox/win32) for Visual
Studio 2022 or later, from the same sources and flags as Makefile.win32.
Rerun after adding or removing source files, or after changing
assets/media.zip or assets/internal_skins.zip (their sizes are compiled in).

  python3 tools/gen_vcxproj_win32.py

Visual Studio itself doesn't need Python: the generated files are checked in.
"""
import glob
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUTDIR = os.path.join(ROOT, 'win32')

PROJECT_GUID = '{9D2C6B4A-1E7F-4A35-B8C2-6F0E3D5A7C19}'
VCXPROJ_TYPE = '{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}'
CONFIGS = ('Debug', 'Release')
HOST_ONLY = {'src/common/PlatformHost.cpp', 'src/net/XNetHost.cpp'}
XBOX_ONLY = {'xbox/RxdkCompat.cpp'}

# Makefile.win32's COMMON_FLAGS and UX_FLAGS defines that apply here (the
# rest of those flags are clang's).
DEFINES = ['_XBOX', 'XBOX', 'UX_WIN32', 'WIN32', '_WINDOWS', '_CRT_SECURE_NO_WARNINGS',
           '_CRT_NONSTDC_NO_WARNINGS', '_WINSOCK_DEPRECATED_NO_WARNINGS', 'DEBUG_MOUSE']
# Warnings the decompiled code would bury the output in (the same ones the
# Makefile turns off for clang, and their MSVC cousins).
DISABLED_WARNINGS = ['4244', '4267', '4305', '4309', '4838', '4996', '4018', '4101', '4102', '4146', '4065',
                     '4302', '4311', '4312', '4533', '4800', '4351', '4091', '4068', '4554', '4005']
LIBS = ['d3d9.lib', 'winmm.lib', 'ws2_32.lib', 'ole32.lib', 'uuid.lib', 'windowscodecs.lib', 'shlwapi.lib',
        'shell32.lib', 'advapi32.lib', 'gdi32.lib', 'user32.lib', 'kernel32.lib']


def makefile_var(name):
    """A variable from Makefile.win32, so the two builds can't drift apart."""
    text = open(os.path.join(ROOT, 'Makefile.win32')).read()
    m = re.search(r'^%s :?= ((?:[^\n]*\\\n)*[^\n]*)' % name, text, re.M)
    return m.group(1).replace('\\\n', ' ').split()


def win(path):
    return '..\\' + path.replace('/', '\\')


def esc(s):
    return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('"', '&quot;')


class Source:
    def __init__(self, path, group, meta=None):
        self.path, self.group, self.meta = path, group, dict(meta or {})
        # Objects are named after the folder too: some names repeat
        # (src/system/Md5.cpp and src/thirdparty/Md5.cpp, xbox/Zip.cpp and
        # thirdparty/zip_utils/zip.cpp).
        folder = os.path.dirname(path).replace('/', '_')
        self.meta['ObjectFileName'] = '$(IntDir)%s_%s.obj' % (folder, os.path.splitext(os.path.basename(path))[0])


def sources():
    # Third-party code: quiet, without the Xbox headers unless it needs them.
    def tp(defines=(), includes=(), forced=(), xbox=False):
        m = {'WarningLevel': 'TurnOffAllWarnings'}
        if defines:
            m['PreprocessorDefinitions'] = ';'.join(list(defines) + ['%(PreprocessorDefinitions)'])
        dirs = ['$(ProjectDir)' + win(i) for i in includes]
        if xbox:
            dirs += ['$(ProjectDir)..\\xbox\\win32\\include', '$(ProjectDir)..\\xbox\\win32']
        m['AdditionalIncludeDirectories'] = ';'.join(dirs)
        if forced:
            m['ForcedIncludeFiles'] = ';'.join('$(ProjectDir)' + win(f) for f in forced)
        return m

    # fat_io_lib's options: forced in, rather than xbox/include on the path
    # (its windows.h stand-in would hide the SDK's).
    fat_forced = ['xbox/include/fat_custom.h']

    out = []
    for sub in sorted(os.listdir(os.path.join(ROOT, 'src'))):
        for p in sorted(glob.glob(os.path.join(ROOT, 'src', sub, '*.cpp'))):
            rel = os.path.relpath(p, ROOT).replace(os.sep, '/')
            if rel not in HOST_ONLY:
                out.append(Source(rel, 'src\\' + sub))
    for p in sorted(glob.glob(os.path.join(ROOT, 'xbox', '*.cpp'))):
        rel = os.path.relpath(p, ROOT).replace(os.sep, '/')
        if rel in XBOX_ONLY:
            continue
        meta = {}
        if rel == 'xbox/Fat32.cpp':
            meta = {'ForcedIncludeFiles': ';'.join('$(ProjectDir)' + win(f) for f in fat_forced)}
        out.append(Source(rel, 'xbox', meta))
    # xbox/win32: X*.cpp with the Xbox headers (the project's include path),
    # Win*.cpp with Windows' own only.
    for p in sorted(glob.glob(os.path.join(ROOT, 'xbox', 'win32', '*.cpp'))):
        rel = os.path.relpath(p, ROOT).replace(os.sep, '/')
        meta = {}
        if os.path.basename(p).startswith('Win'):
            meta = {'AdditionalIncludeDirectories': '$(ProjectDir)..\\xbox\\win32',
                    'UndefinePreprocessorDefinitions': '_XBOX;XBOX'}
        out.append(Source(rel, 'xbox\\win32', meta))

    # The third-party code that opens files gets the Xbox paths mapped too.
    filemap = 'xbox/win32/UxFileMap.h'
    for f in ('unzip.cpp', 'zip.cpp'):
        out.append(Source('thirdparty/zip_utils/' + f, 'thirdparty\\zip_utils', tp(forced=[filemap])))
    out.append(Source('thirdparty/ftpparse/ftpparse.c', 'thirdparty\\ftpparse', tp()))
    for f in makefile_var('FATIO_SRCS'):
        out.append(Source('thirdparty/fat_io_lib/%s.c' % f, 'thirdparty\\fat_io_lib', tp(forced=fat_forced)))
    for f in makefile_var('MAD_SRCS'):
        out.append(Source('thirdparty/libmad/%s.c' % f, 'thirdparty\\libmad',
                          tp(['FPM_INTEL', 'HAVE_CONFIG_H', 'ASO_ZEROCHECK'], ['thirdparty/libmad'])))
    for f in makefile_var('RAR_SRCS'):
        out.append(Source('thirdparty/unrar/%s.cpp' % f, 'thirdparty\\unrar',
                          tp(['_LIB', '_HAS_AUTO_PTR_ETC=1', 'UX_FILEMAP_POSIX'], ['thirdparty/unrar/shim'],
                             ['thirdparty/unrar/shim/unrar_xbox.h', filemap], xbox=True)))
    return out


def headers():
    out = []
    for sub in sorted(os.listdir(os.path.join(ROOT, 'src'))):
        for p in sorted(glob.glob(os.path.join(ROOT, 'src', sub, '*.h'))):
            out.append((os.path.relpath(p, ROOT).replace(os.sep, '/'), 'src\\' + sub))
    for p in sorted(glob.glob(os.path.join(ROOT, 'xbox', 'win32', '*.h'))):
        out.append((os.path.relpath(p, ROOT).replace(os.sep, '/'), 'xbox\\win32'))
    for p in sorted(glob.glob(os.path.join(ROOT, 'xbox', 'win32', 'include', '*.h'))):
        out.append((os.path.relpath(p, ROOT).replace(os.sep, '/'), 'xbox\\win32\\include'))
    return out


def vcxproj(srcs):
    media = os.path.getsize(os.path.join(ROOT, 'assets/media.zip'))
    skins = os.path.getsize(os.path.join(ROOT, 'assets/internal_skins.zip'))
    defines = DEFINES + ['MEDIA_FILE_SIZE=%d' % media, 'INT_SKINS_SIZE=%d' % skins]
    o = []
    w = o.append
    w('<?xml version="1.0" encoding="utf-8"?>')
    w('<!-- Generated by tools/gen_vcxproj_win32.py from Makefile.win32; rerun it rather than editing. -->')
    w('<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">')
    w('  <ItemGroup Label="ProjectConfigurations">')
    for c in CONFIGS:
        w('    <ProjectConfiguration Include="%s|Win32">' % c)
        w('      <Configuration>%s</Configuration>' % c)
        w('      <Platform>Win32</Platform>')
        w('    </ProjectConfiguration>')
    w('  </ItemGroup>')
    w('  <PropertyGroup Label="Globals">')
    w('    <VCProjectVersion>17.0</VCProjectVersion>')
    w('    <ProjectGuid>%s</ProjectGuid>' % PROJECT_GUID)
    w('    <Keyword>Win32Proj</Keyword>')
    w('    <RootNamespace>UnleashX</RootNamespace>')
    w('    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>')
    w('    <ProjectName>UnleashX</ProjectName>')
    w('    <VcpkgEnabled>false</VcpkgEnabled>')
    w('    <VcpkgEnableManifest>false</VcpkgEnableManifest>')
    w('  </PropertyGroup>')
    w('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />')
    for c in CONFIGS:
        w('  <PropertyGroup Condition="\'$(Configuration)|$(Platform)\'==\'%s|Win32\'" Label="Configuration">' % c)
        w('    <ConfigurationType>Application</ConfigurationType>')
        w('    <UseDebugLibraries>%s</UseDebugLibraries>' % ('true' if c == 'Debug' else 'false'))
        w('    <PlatformToolset>v143</PlatformToolset>')
        w('    <CharacterSet>MultiByte</CharacterSet>')
        if c == 'Release':
            w('    <WholeProgramOptimization>false</WholeProgramOptimization>')
        w('  </PropertyGroup>')
    w('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />')
    w('  <ImportGroup Label="ExtensionSettings" />')
    w('  <ImportGroup Label="Shared" />')
    w('  <ImportGroup Label="PropertySheets">')
    w('    <Import Project="$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props" '
      'Condition="exists(\'$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props\')" Label="LocalAppDataPlatform" />')
    w('  </ImportGroup>')
    w('  <PropertyGroup Label="UserMacros" />')
    w('  <PropertyGroup>')
    w('    <OutDir>$(ProjectDir)..\\build\\vs-win32\\$(Configuration)\\</OutDir>')
    w('    <IntDir>$(ProjectDir)..\\build\\vs-win32\\$(Configuration)\\obj\\</IntDir>')
    w('    <TargetName>UnleashX</TargetName>')
    w('    <LocalDebuggerWorkingDirectory>$(OutDir)</LocalDebuggerWorkingDirectory>')
    w('  </PropertyGroup>')
    for c in CONFIGS:
        debug = c == 'Debug'
        w('  <ItemDefinitionGroup Condition="\'$(Configuration)|$(Platform)\'==\'%s|Win32\'">' % c)
        w('    <ClCompile>')
        w('      <LanguageStandard>stdcpp17</LanguageStandard>')
        # The decompiled code is MSVC 7.1's C++: no /permissive-.
        w('      <ConformanceMode>false</ConformanceMode>')
        w('      <RuntimeTypeInfo>true</RuntimeTypeInfo>')
        w('      <WarningLevel>Level3</WarningLevel>')
        w('      <SDLCheck>false</SDLCheck>')
        w('      <MultiProcessorCompilation>true</MultiProcessorCompilation>')
        w('      <DisableSpecificWarnings>%s;%%(DisableSpecificWarnings)</DisableSpecificWarnings>'
          % ';'.join(DISABLED_WARNINGS))
        w('      <AdditionalIncludeDirectories>$(ProjectDir)..\\xbox\\win32\\include;$(ProjectDir)..\\xbox\\win32;'
          '%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>')
        w('      <PreprocessorDefinitions>%s;%s;%%(PreprocessorDefinitions)</PreprocessorDefinitions>'
          % (';'.join(defines), '_DEBUG' if debug else 'NDEBUG'))
        w('      <RuntimeLibrary>%s</RuntimeLibrary>' % ('MultiThreadedDebug' if debug else 'MultiThreaded'))
        if debug:
            w('      <Optimization>Disabled</Optimization>')
            w('      <BasicRuntimeChecks>Default</BasicRuntimeChecks>')
        else:
            w('      <Optimization>MaxSpeed</Optimization>')
            w('      <FunctionLevelLinking>true</FunctionLevelLinking>')
            w('      <IntrinsicFunctions>true</IntrinsicFunctions>')
        w('      <FloatingPointModel>Precise</FloatingPointModel>')
        w('      <AdditionalOptions>/bigobj /Zc:strictStrings- %(AdditionalOptions)</AdditionalOptions>')
        w('    </ClCompile>')
        w('    <ResourceCompile>')
        w('      <AdditionalIncludeDirectories>$(ProjectDir)..;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>')
        w('    </ResourceCompile>')
        w('    <Link>')
        w('      <SubSystem>Windows</SubSystem>')
        w('      <GenerateDebugInformation>true</GenerateDebugInformation>')
        w('      <AdditionalDependencies>%s;%%(AdditionalDependencies)</AdditionalDependencies>' % ';'.join(LIBS))
        w('      <ImageHasSafeExceptionHandlers>false</ImageHasSafeExceptionHandlers>')
        w('      <LargeAddressAware>true</LargeAddressAware>')
        if not debug:
            w('      <EnableCOMDATFolding>true</EnableCOMDATFolding>')
            w('      <OptimizeReferences>true</OptimizeReferences>')
        w('    </Link>')
        w('  </ItemDefinitionGroup>')
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
    w('  <ItemGroup>')
    w('    <ResourceCompile Include="..\\xbox\\win32\\UnleashX.rc" />')
    w('  </ItemGroup>')
    w('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />')
    # UnleashX's own files (Config.xml, the languages, Skins) next to the
    # program, in "disc"; the first run copies them to its folder on E:.
    # MSBuild's Copy task, so nothing has to be on the path.
    w('  <Target Name="UnleashXCopyDisc" AfterTargets="Build">')
    w('    <ItemGroup>')
    w('      <UnleashXDisc Include="$(ProjectDir)..\\assets\\disc\\**\\*" />')
    w('    </ItemGroup>')
    w('    <Copy SourceFiles="@(UnleashXDisc)" '
      'DestinationFiles="@(UnleashXDisc->\'$(OutDir)disc\\%(RecursiveDir)%(Filename)%(Extension)\')" '
      'SkipUnchangedFiles="true" />')
    w('  </Target>')
    w('</Project>')
    return '\r\n'.join(o) + '\r\n'


def filters(srcs):
    groups = sorted({s.group for s in srcs} | {g for _, g in headers()} | {'xbox\\win32'})
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
        o.append('      <UniqueIdentifier>{7A2E%04X-4C1B-4D3A-8F62-1B5C4D3E2F10}</UniqueIdentifier>' % i)
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
    o.append('  <ItemGroup>')
    o.append('    <ResourceCompile Include="..\\xbox\\win32\\UnleashX.rc">')
    o.append('      <Filter>xbox\\win32</Filter>')
    o.append('    </ResourceCompile>')
    o.append('  </ItemGroup>')
    o.append('</Project>')
    return '\r\n'.join(o) + '\r\n'


def solution():
    lines = ['', 'Microsoft Visual Studio Solution File, Format Version 12.00',
             '# Visual Studio Version 17', 'VisualStudioVersion = 17.0.31903.59',
             'MinimumVisualStudioVersion = 10.0.40219.1',
             'Project("%s") = "UnleashX", "UnleashX.vcxproj", "%s"' % (VCXPROJ_TYPE, PROJECT_GUID),
             'EndProject', 'Global',
             '\tGlobalSection(SolutionConfigurationPlatforms) = preSolution']
    for c in CONFIGS:
        lines.append('\t\t%s|x86 = %s|x86' % (c, c))
    lines.append('\tEndGlobalSection')
    lines.append('\tGlobalSection(ProjectConfigurationPlatforms) = postSolution')
    for c in CONFIGS:
        lines.append('\t\t%s.%s|x86.ActiveCfg = %s|Win32' % (PROJECT_GUID, c, c))
        lines.append('\t\t%s.%s|x86.Build.0 = %s|Win32' % (PROJECT_GUID, c, c))
    lines += ['\tEndGlobalSection', '\tGlobalSection(SolutionProperties) = preSolution',
              '\t\tHideSolutionNode = FALSE', '\tEndGlobalSection', 'EndGlobal']
    return '﻿' + '\r\n'.join(lines) + '\r\n'


def main():
    srcs = sources()
    os.makedirs(OUTDIR, exist_ok=True)
    with open(os.path.join(OUTDIR, 'UnleashX.vcxproj'), 'w', newline='') as f:
        f.write(vcxproj(srcs))
    with open(os.path.join(OUTDIR, 'UnleashX.vcxproj.filters'), 'w', newline='') as f:
        f.write(filters(srcs))
    with open(os.path.join(OUTDIR, 'UnleashX.sln'), 'w', newline='', encoding='utf-8') as f:
        f.write(solution())
    print('win32/UnleashX.vcxproj: %d sources' % len(srcs))


if __name__ == '__main__':
    main()
