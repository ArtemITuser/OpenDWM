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

[CmdletBinding()]
param(
    [string]$Hive = 'donor\rtm\4\Windows\System32\config\SOFTWARE',
    [string]$Out  = 'vm\cpl-reg-win7.tsv'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not [IO.Path]::IsPathRooted($Hive)) { $Hive = Join-Path $root $Hive }
if (-not [IO.Path]::IsPathRooted($Out))  { $Out  = Join-Path $root $Out }
if (-not (Test-Path $Hive)) { throw "donor hive not found $Hive" }

$mount = 'KeelCplExport'
reg load "HKLM\$mount" $Hive 2>&1 | Out-Null
try {
    $nsPath = "HKLM:\$mount\Microsoft\Windows\CurrentVersion\Explorer\ControlPanel\NameSpace"
    $clsids = @(Get-ChildItem $nsPath -ErrorAction SilentlyContinue | ForEach-Object { $_.PSChildName })
    Write-Host "Win7 Control Panel namespace items $($clsids.Count)"

    $rows = New-Object System.Collections.Generic.List[string]
    $rows.Add("CLSID`tSubKey`tName`tType`tValue")

    foreach ($c in $clsids) {
        $base = "HKLM:\$mount\Classes\CLSID\$c"
        if (-not (Test-Path $base)) { continue }

        $keys = @(Get-Item $base) + @(Get-ChildItem $base -Recurse -ErrorAction SilentlyContinue)
        foreach ($k in $keys) {
            $sub = $k.Name -replace [regex]::Escape("HKEY_LOCAL_MACHINE\$mount\Classes\CLSID\$c"), ''
            $sub = $sub.TrimStart('\')
            foreach ($n in $k.GetValueNames()) {
                $kind = $k.GetValueKind($n)

                $v = if ($kind -eq 'ExpandString') { $k.GetValue($n, $null, 'DoNotExpandEnvironmentNames') } else { $k.GetValue($n) }
                if ($v -is [byte[]]) { $v = [BitConverter]::ToString($v) -replace '-', '' }
                $v = "$v" -replace "`t", ' ' -replace "`r?`n", ' '
                $rows.Add(("{0}`t{1}`t{2}`t{3}`t{4}" -f $c, $sub, $n, $kind, $v))
            }
        }
    }
    $dir = Split-Path $Out
    if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
    Set-Content -Path $Out -Value $rows -Encoding UTF8
    Write-Host "wrote $($rows.Count - 1) values -> $Out"
} finally {
    [GC]::Collect(); [GC]::WaitForPendingFinalizers()
    reg unload "HKLM\$mount" 2>&1 | Out-Null
}
