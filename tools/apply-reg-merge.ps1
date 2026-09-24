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

param([string]$Tsv = 'C:\Keel\regmerge\merge.tsv', [string]$Root = 'SOFTWARE', [string]$Hive = 'LocalMachine')
$ErrorActionPreference = 'SilentlyContinue'
$base = switch ($Hive) {
    'CurrentUser'  { [Microsoft.Win32.Registry]::CurrentUser }
    'Users'        { [Microsoft.Win32.Registry]::Users }
    default        { [Microsoft.Win32.Registry]::LocalMachine }
}
$kindMap = @{ 0 = [Microsoft.Win32.RegistryValueKind]::Unknown; 1 = [Microsoft.Win32.RegistryValueKind]::String
              2 = [Microsoft.Win32.RegistryValueKind]::ExpandString; 3 = [Microsoft.Win32.RegistryValueKind]::Binary
              4 = [Microsoft.Win32.RegistryValueKind]::DWord; 7 = [Microsoft.Win32.RegistryValueKind]::MultiString
              11 = [Microsoft.Win32.RegistryValueKind]::QWord }
$openCache = @{}
function Get-Key([string]$sub, [bool]$create) {
    if ($openCache.ContainsKey($sub)) { return $openCache[$sub] }
    $full = if ($Root -and $sub) { "$Root\$sub" } elseif ($Root) { $Root } else { $sub }
    if (-not $full) { return $base }
    $k = $null
    try { $k = $base.OpenSubKey($full, $true) } catch { $k = $null }
    if (-not $k -and $create) { try { $k = $base.CreateSubKey($full) } catch { $k = $null } }
    if ($openCache.Count -lt 400) { $openCache[$sub] = $k }
    return $k
}
$keysMade = 0; $keysFailed = 0; $valsSet = 0; $valsSkipped = 0; $valsFailed = 0; $valsRepaired = 0; $n = 0
$failKeys = New-Object System.Collections.ArrayList
$sr = New-Object System.IO.StreamReader($Tsv, [System.Text.Encoding]::UTF8)
while ($null -ne ($line = $sr.ReadLine())) {
    $n++
    $p = $line.Split("`t")
    if ($p[0] -eq 'K') {
        $k = Get-Key $p[1] $true
        if ($k) { $keysMade++ } else { $keysFailed++; if ($failKeys.Count -lt 40) { [void]$failKeys.Add($p[1]) } }
    } elseif ($p[0] -eq 'V' -and $p.Count -ge 5) {
        $k = Get-Key $p[1] $true
        if (-not $k) { $valsFailed++; continue }
        $name = $p[2]
        # TSVs built before the reg_merge fix name the default value (default), which as a literal leaves the real default empty
        if ($name -eq '(default)') { $name = '' }
        if ($name -eq '' -and $null -ne $k.GetValue('(default)', $null)) { try { $k.DeleteValue('(default)', $false); $valsRepaired++ } catch {} }
        if ($null -ne $k.GetValue($name, $null)) { $valsSkipped++; continue }
        $raw = [Convert]::FromBase64String($p[4])
        $kind = $kindMap[[int]$p[3]]
        try {
            switch ([int]$p[3]) {
                1  { $k.SetValue($name, [Text.Encoding]::Unicode.GetString($raw).TrimEnd([char]0), $kind) }
                2  { $k.SetValue($name, [Text.Encoding]::Unicode.GetString($raw).TrimEnd([char]0), $kind) }
                4  { $k.SetValue($name, [BitConverter]::ToInt32($raw, 0), $kind) }
                11 { $k.SetValue($name, [BitConverter]::ToInt64($raw, 0), $kind) }
                7  { $k.SetValue($name, ([Text.Encoding]::Unicode.GetString($raw).TrimEnd([char]0) -split "`0"), $kind) }
                default { $k.SetValue($name, $raw, [Microsoft.Win32.RegistryValueKind]::Binary) }
            }
            $valsSet++
        } catch { $valsFailed++ }
    }
}
$sr.Close()
"lines=$n keysCreated=$keysMade keysDenied=$keysFailed valuesSet=$valsSet valuesSkipped(existing)=$valsSkipped valuesFailed=$valsFailed strayDefaultsRemoved=$valsRepaired"
if ($failKeys.Count) { "denied examples:"; $failKeys | Select-Object -First 15 }
