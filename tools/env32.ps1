# KeelShim - Windows 7 user-mode UI translation for the Windows 10 kernel
# Copyright (C) 2026 Kevin Dalli <projectkeel@gmail.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

param([string]$ToolsRoot = $(if ($env:KEEL_TOOLS) { $env:KEEL_TOOLS } else { 'C:\Keel-tools' }))

$ErrorActionPreference = 'Stop'
$msvcRoot = Join-Path $ToolsRoot 'msvc'
$kits     = Join-Path $msvcRoot 'Windows Kits\10'

function Get-LatestVersionDir($parent) {
    if (-not (Test-Path $parent)) { return $null }
    $d = Get-ChildItem $parent -Directory | Where-Object { $_.Name -match '^\d+(\.\d+)+$' } |
         Sort-Object { [version]$_.Name } | Select-Object -Last 1
    if ($d) { return $d.Name } else { return $null }
}

$vcVer  = Get-LatestVersionDir (Join-Path $msvcRoot 'VC\Tools\MSVC')
$sdkVer = Get-LatestVersionDir (Join-Path $kits 'Include')

$paths = New-Object System.Collections.Generic.List[string]
$inc   = New-Object System.Collections.Generic.List[string]
$lib   = New-Object System.Collections.Generic.List[string]

if ($vcVer) {
    $vc = Join-Path $msvcRoot "VC\Tools\MSVC\$vcVer"
    $x86bin = Join-Path $vc 'bin\Hostx64\x86'
    if (-not (Test-Path (Join-Path $x86bin 'cl.exe'))) {
        throw "x86-target cl.exe missing at $x86bin so install Microsoft.VC.*.Tools.HostX64.TargetX86.base"
    }
    $paths.Add($x86bin)
    $paths.Add((Join-Path $vc 'bin\Hostx64\x64'))
    $inc.Add((Join-Path $vc 'include'))
    $lib.Add((Join-Path $vc 'lib\x86'))
    $env:VCToolsVersion    = $vcVer
    $env:VCToolsInstallDir = "$vc\"
}
if ($sdkVer) {
    $paths.Add((Join-Path $kits "bin\$sdkVer\x86"))
    foreach ($s in 'ucrt','shared','um','winrt','cppwinrt') {
        $p = Join-Path $kits "Include\$sdkVer\$s"; if (Test-Path $p) { $inc.Add($p) }
    }
    foreach ($s in 'ucrt\x86','um\x86') {
        $p = Join-Path $kits "Lib\$sdkVer\$s"; if (Test-Path $p) { $lib.Add($p) }
    }
    $env:WindowsSdkDir      = "$kits\"
    $env:WindowsSDKVersion  = "$sdkVer\"
    $env:UCRTVersion        = $sdkVer
    $env:UniversalCRTSdkDir = "$kits\"
}

foreach ($t in 'cmake\bin','ninja','git\cmd','git\usr\bin','python','python\Scripts') {
    $p = Join-Path $ToolsRoot $t; if (Test-Path $p) { $paths.Add($p) }
}

$env:KEEL_TOOLS = $ToolsRoot
if (-not $env:KEEL_ROOT) { $env:KEEL_ROOT = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path }
$env:CMAKE_GENERATOR = 'Ninja'

$have = @($env:PATH -split ';')
$add = @($paths | Where-Object { $_ -and $have -notcontains $_ })
if ($add.Count) { $env:PATH = ($add -join ';') + ';' + $env:PATH }
$env:INCLUDE = ($inc -join ';')
$env:LIB     = ($lib -join ';')
$env:LIBPATH = $env:LIB
$env:Platform = 'x86'
$env:VSCMD_ARG_TGT_ARCH = 'x86'

Write-Host ("Keel x86 toolchain MSVC {0}  SDK {1}" -f $vcVer, $sdkVer)
