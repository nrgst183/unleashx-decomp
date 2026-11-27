#!/usr/bin/env python3
"""Generates UnleashX.sln at the root: the Xbox build (rxdk/UnleashX-Xbox.vcxproj),
the Windows build (win32/UnleashX-Windows.vcxproj) and its unit tests
(win32/UnitTests.vcxproj), for Visual Studio 2022 or later. The platform
picks the build: Xbox for the Xbox, x86 for Windows (and the tests).

  python3 tools/gen_sln.py

tools/gen_vcxproj_rxdk.py and tools/gen_vcxproj_win32.py run this too.
(The Visual Studio .NET 2003 build keeps its own, vs2003/UnleashX.sln.)
"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

VCXPROJ_TYPE = '{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}'
CONFIGS = ('Debug', 'Release')
# (solution name, path, GUID, the solution platform it builds on, its own platform)
# (Windows first: Visual Studio's default startup project, and the one
# IntelliSense takes the shared sources' context from.)
PROJECTS = [
    ('UnleashX-Windows', 'win32\\UnleashX-Windows.vcxproj', '{9D2C6B4A-1E7F-4A35-B8C2-6F0E3D5A7C19}', 'x86', 'Win32'),
    ('UnleashX-Xbox', 'rxdk\\UnleashX-Xbox.vcxproj', '{3B7D9E21-6A4C-4F1E-8D53-2C9A0E7B4F61}', 'Xbox', 'Xbox'),
    ('UnitTests', 'win32\\UnitTests.vcxproj', '{5B8E1F3C-2A4D-4E6B-9C7A-3D1F0E2B4A68}', 'x86', 'Win32'),
]
PLATFORMS = ('Xbox', 'x86')


def solution():
    lines = ['', 'Microsoft Visual Studio Solution File, Format Version 12.00', '# Visual Studio Version 17',
             'VisualStudioVersion = 17.0.31903.59', 'MinimumVisualStudioVersion = 10.0.40219.1']
    for name, path, guid, _, _ in PROJECTS:
        lines += ['Project("%s") = "%s", "%s", "%s"' % (VCXPROJ_TYPE, name, path, guid), 'EndProject']
    lines += ['Global', '\tGlobalSection(SolutionConfigurationPlatforms) = preSolution']
    for c in CONFIGS:
        for p in PLATFORMS:
            lines.append('\t\t%s|%s = %s|%s' % (c, p, c, p))
    lines += ['\tEndGlobalSection', '\tGlobalSection(ProjectConfigurationPlatforms) = postSolution']
    for _, _, guid, builds_on, own in PROJECTS:
        for c in CONFIGS:
            for p in PLATFORMS:
                # On the other platform the project is there but not built.
                lines.append('\t\t%s.%s|%s.ActiveCfg = %s|%s' % (guid, c, p, c, own))
                if p == builds_on:
                    lines.append('\t\t%s.%s|%s.Build.0 = %s|%s' % (guid, c, p, c, own))
    lines += ['\tEndGlobalSection', '\tGlobalSection(SolutionProperties) = preSolution',
              '\t\tHideSolutionNode = FALSE', '\tEndGlobalSection', 'EndGlobal']
    return '﻿' + '\r\n'.join(lines) + '\r\n'


def main():
    with open(os.path.join(ROOT, 'UnleashX.sln'), 'w', newline='', encoding='utf-8') as f:
        f.write(solution())
    print('UnleashX.sln: %d projects' % len(PROJECTS))


if __name__ == '__main__':
    main()
