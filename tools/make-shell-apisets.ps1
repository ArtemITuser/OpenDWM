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

param([string]$Binary = 'shell32.dll')
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $here 'env.ps1') | Out-Null
$root = Split-Path -Parent $here
$donor = Join-Path $root 'donor\raw\Windows\System32'
$path = Join-Path $donor $Binary
$deps = & dumpbin.exe /dependents $path 2>&1 | Select-String '\.dll' | ForEach-Object { $_.Line.Trim() } |
        Where-Object { $_ -match '^(api|ext)-ms-.*\.dll$' }
Write-Host "$Binary imports $($deps.Count) api-set contracts"
$made = 0
foreach ($d in $deps) {
    if (-not (Test-Path (Join-Path $donor $d))) { Write-Warning "no Win7 stub for $d"; continue }
    try { & (Join-Path $here 'make-apiset-shim.ps1') -Stub $d 2>&1 | Select-String 'built|unresolved' | ForEach-Object { "  $($_.Line)" }; $made++ }
    catch { Write-Warning "failed $d  $($_.Exception.Message)" }
}
Write-Host "generated $made shim(s) into donor\cut3"
