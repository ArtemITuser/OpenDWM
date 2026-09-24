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

if (-not $vcVer) {
    throw "no toolchain at $ToolsRoot so run tools\setup-toolchain.ps1 to provision it"
}

$paths = New-Object System.Collections.Generic.List[string]
$inc   = New-Object System.Collections.Generic.List[string]
$lib   = New-Object System.Collections.Generic.List[string]

if ($vcVer) {
    $vc = Join-Path $msvcRoot "VC\Tools\MSVC\$vcVer"
    $paths.Add((Join-Path $vc 'bin\Hostx64\x64'))
    $inc.Add((Join-Path $vc 'include'))
    $inc.Add((Join-Path $msvcRoot 'VC\Auxiliary\VS\include'))
    $lib.Add((Join-Path $vc 'lib\x64'))
    $atlInc = Join-Path $msvcRoot ('VC\Tools\MSVC\{0}\atlmfc\include' -f $vcVer)
    $atlLib = Join-Path $msvcRoot ('VC\Tools\MSVC\{0}\atlmfc\lib\x64' -f $vcVer)
    if (Test-Path $atlInc) { $inc.Add($atlInc) }
    if (Test-Path $atlLib) { $lib.Add($atlLib) }
    $env:VCToolsVersion    = $vcVer
    $env:VCToolsInstallDir = "$vc\"
}
if ($sdkVer) {
    $paths.Add((Join-Path $kits "bin\$sdkVer\x64"))
    foreach ($s in 'ucrt','shared','um','winrt','cppwinrt','km') {
        $p = Join-Path $kits "Include\$sdkVer\$s"; if (Test-Path $p) { $inc.Add($p) }
    }
    foreach ($s in 'ucrt\x64','um\x64','km\x64') {
        $p = Join-Path $kits "Lib\$sdkVer\$s"; if (Test-Path $p) { $lib.Add($p) }
    }
    $env:WindowsSdkDir        = "$kits\"
    $env:WindowsSDKVersion    = "$sdkVer\"
    $env:UCRTVersion          = $sdkVer
    $env:UniversalCRTSdkDir   = "$kits\"
}
$dbg = Join-Path $kits 'Debuggers\x64'; if (Test-Path $dbg) { $paths.Add($dbg) }

$kmdfLibRoot = Join-Path $kits 'Lib\wdf\kmdf\x64'
if (Test-Path $kmdfLibRoot) {
    $kmdfVer = if (Test-Path (Join-Path $kmdfLibRoot '1.31')) { '1.31' } else { Get-LatestVersionDir $kmdfLibRoot }
    if ($kmdfVer) {
        $env:KMDF_VERSION = $kmdfVer
        $lib.Add((Join-Path $kmdfLibRoot $kmdfVer))
        $kmdfInc = Join-Path $kits "Include\wdf\kmdf\$kmdfVer"; if (Test-Path $kmdfInc) { $inc.Add($kmdfInc) }
    }
}

foreach ($t in 'cmake\bin','ninja','git\cmd','git\usr\bin','jdk\bin','ghidra','x64dbg\release\x64','7zip','sysinternals','dependencies','pebear','python','python\Scripts') {
    $p = Join-Path $ToolsRoot $t; if (Test-Path $p) { $paths.Add($p) }
}
$vbox = 'C:\Program Files\Oracle\VirtualBox'; if (Test-Path $vbox) { $paths.Add($vbox) }

$jdk = Join-Path $ToolsRoot 'jdk'; if (Test-Path $jdk) { $env:JAVA_HOME = $jdk }
$env:KEEL_TOOLS = $ToolsRoot
if (-not $env:KEEL_ROOT) { $env:KEEL_ROOT = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path }
if (-not $env:_NT_SYMBOL_PATH) { $env:_NT_SYMBOL_PATH = 'SRV*C:\Keel-symbols*https://msdl.microsoft.com/download/symbols' }
$env:DETOURS_ROOT = Join-Path $ToolsRoot 'detours'
$env:CMAKE_GENERATOR = 'Ninja'
$env:KEEL_HOSTREF = Join-Path (Split-Path $PSScriptRoot) 'host-ref\raw\Windows'

# the Win10 reference a generated seam forwards to is the supplied LTSC image, or this machine if it was not extracted
function Get-KeelHostDll([string]$rel) {
    $p = Join-Path $env:KEEL_HOSTREF $rel
    if (Test-Path $p) { return $p }
    return (Join-Path $env:SystemRoot $rel)
}

# a build dot-sources this many times over, and re-prepending would overflow PATH so cmd.exe would lose cmake
$have = @($env:PATH -split ';')
$add = @($paths | Where-Object { $_ -and $have -notcontains $_ })
if ($add.Count) { $env:PATH = ($add -join ';') + ';' + $env:PATH }
$env:INCLUDE = ($inc -join ';')
$env:LIB     = ($lib -join ';')
$env:LIBPATH = $env:LIB
$env:Platform = 'x64'
$env:VSCMD_ARG_TGT_ARCH = 'x64'

Write-Host ("Keel toolchain MSVC {0}  SDK {1}  KMDF {2}" -f $vcVer, $sdkVer, $env:KMDF_VERSION)
