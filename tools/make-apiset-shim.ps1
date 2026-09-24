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

param(
    [Parameter(Mandatory)][string]$Stub,
    [string]$DonorRoot = '',
    [string]$HostRoot  = '',
    [string]$OutDir    = ''
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here
if (-not $DonorRoot) { $DonorRoot = Join-Path $root 'donor\raw\Windows' }
if (-not $HostRoot)  { $HostRoot  = Join-Path $root 'host-ref\raw\Windows' }
# forwarders are resolved against the pinned host build, and without a captured host-ref this machine is it
if (-not (Test-Path (Join-Path $HostRoot 'System32'))) { $HostRoot = $env:SystemRoot }
if (-not $OutDir)    { $OutDir    = Join-Path $root 'donor\cut3' }
. (Join-Path $here 'env.ps1') | Out-Null

$stubPath = Join-Path $DonorRoot "System32\$Stub"
if (-not (Test-Path $stubPath)) { throw "Win7 stub not found $stubPath" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$work = Join-Path $env:KEEL_TOOLS '_dl\apiset'
New-Item -ItemType Directory -Force $work | Out-Null

$base = [IO.Path]::GetFileNameWithoutExtension($Stub)
$def = Join-Path $work "$base.def"
python (Join-Path $here 'census\gen_apiset_def.py') $stubPath $HostRoot $def
if ($LASTEXITCODE -ne 0) { throw 'def generation failed' }

$content = Get-Content $def
Set-Content $def (@("LIBRARY $base") + $content) -Encoding ascii

$out = Join-Path $OutDir $Stub
& link.exe /DLL /NOENTRY /MACHINE:X64 "/DEF:$def" "/OUT:$out" 2>&1 |
    Select-String 'Creating library|error|LNK' | ForEach-Object { $_.Line }
if (-not (Test-Path $out)) { throw "link failed, no $out" }

Remove-Item ([IO.Path]::ChangeExtension($out, 'lib')), ([IO.Path]::ChangeExtension($out, 'exp')) -ErrorAction SilentlyContinue
$n = (& dumpbin.exe /exports $out 2>&1 | Select-String '\(forwarded to').Count
Write-Host ("built {0} with {1} forwarders" -f $out, $n) -ForegroundColor Green
