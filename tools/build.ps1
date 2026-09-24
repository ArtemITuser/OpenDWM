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

param([ValidateSet('debug','release')][string]$Preset = 'debug', [switch]$Clean)
$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'env.ps1')
$root = Split-Path $PSScriptRoot
$src = Join-Path $root 'src'
$out = Join-Path $root "out\$Preset"
if ($Clean -and (Test-Path -LiteralPath $out)) { Remove-Item -LiteralPath $out -Recurse -Force }
Set-Location $src
$log = Join-Path $env:KEEL_TOOLS "_dl\build-$Preset.log"
cmd.exe /c "cmake --preset $Preset > `"$log`" 2>&1 && cmake --build --preset $Preset >> `"$log`" 2>&1"
$rc = $LASTEXITCODE
if ($rc -ne 0) { Get-Content $log | Select-Object -Last 60 } else { Get-Content $log | Select-String 'warning|Linking' | ForEach-Object { $_.Line } }
"build exit $rc"
if ($rc -eq 0) {

    $stage = Join-Path $out 'stage'
    New-Item -ItemType Directory -Force $stage | Out-Null
    foreach ($f in "keelldr\keelldr.exe","keelldr\keelldr.pdb","keelexp\keelexp.exe","keelexp\keelexp.pdb","keelshim\keelshim.dll","keelshim\keelshim.pdb","keeltest\keeltest.exe","keeltest\keeltest.pdb","keelbroker\keelbroker.exe","keelbroker\keelbroker.pdb","keeluxsms\keeluxsms.exe","keeluxsms\keeluxsms.pdb","keeldrv\keeldrv.sys","keeldrv\keeldrv.pdb","keeldrv\keeldrv.inf") {
        $p = Join-Path $out $f; if (Test-Path -LiteralPath $p) { Copy-Item -LiteralPath $p $stage -Force }
    }
    Get-ChildItem $stage | Select-Object Name, Length | Format-Table -AutoSize | Out-String | Write-Host
}
exit $rc
