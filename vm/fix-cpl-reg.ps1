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
    [string]$Tsv    = 'C:\Keel\vm\cpl-reg-win7.tsv',
    [string]$Backup = 'C:\Keel\cpl-reg-backup.tsv',
    [switch]$WhatIfOnly,
    [switch]$Undo
)
$ErrorActionPreference = 'Stop'

if (-not (New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
        ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run elevated since these keys are TrustedInstaller-owned.'
}

Add-Type -Namespace KeelPriv2 -Name Tok -MemberDefinition @'
[DllImport("ntdll.dll")]
public static extern int RtlAdjustPrivilege(int Privilege, bool Enable, bool CurrentThread, out bool Enabled);
'@
foreach ($p in 8, 9, 17, 18) { $was = $false; [void][KeelPriv2.Tok]::RtlAdjustPrivilege($p, $true, $false, [ref]$was) }

$AdminsSid = New-Object System.Security.Principal.SecurityIdentifier 'S-1-5-32-544'
function Grant-KeyControl([string]$Rel) {
    try {
        $k = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($Rel,
                [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree,
                [System.Security.AccessControl.RegistryRights]::TakeOwnership)
        if (-not $k) { return $false }
        $acl = $k.GetAccessControl([System.Security.AccessControl.AccessControlSections]::None)
        $acl.SetOwner($AdminsSid); $k.SetAccessControl($acl); $k.Close()
    } catch { }
    try {
        $k2 = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($Rel,
                [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree,
                [System.Security.AccessControl.RegistryRights]::ChangePermissions)
        if (-not $k2) { return $false }
        $acl2 = $k2.GetAccessControl()
        $acl2.SetAccessRule((New-Object System.Security.AccessControl.RegistryAccessRule(
            $AdminsSid, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow')))
        $k2.SetAccessControl($acl2); $k2.Close(); return $true
    } catch { return $false }
}

$SysDrive = $env:SystemDrive
# the donor hive came from a Win7 install on drive D, so a foreign drive prefix is rewritten onto this machine
function Normalize-Path([string]$v) {
    if (-not $v) { return $v }
    $v = [regex]::Replace($v, '(?i)\b([A-Z]):\\Windows\b',       ($SysDrive + '\Windows'))
    $v = [regex]::Replace($v, '(?i)\b([A-Z]):\\Program Files\b', ($SysDrive + '\Program Files'))
    return $v
}

function Parse-Row($line) {
    $p = $line -split "`t", 5
    [pscustomobject]@{ Clsid = $p[0]; SubKey = $p[1]; Name = $p[2]; Type = $p[3]; Value = (Normalize-Path $p[4]) }
}
function To-Typed($type, $raw) {
    switch ($type) {
        'DWord'        { return [int]$raw }
        'QWord'        { return [int64]$raw }
        'Binary'       { $b = @(); for ($i = 0; $i -lt $raw.Length; $i += 2) { $b += [Convert]::ToByte($raw.Substring($i,2),16) }; return ,([byte[]]$b) }
        'MultiString'  { return ,([string[]]($raw -split ' ')) }
        default        { return [string]$raw }
    }
}

$src = if ($Undo) { $Backup } else { $Tsv }
if (-not (Test-Path $src)) { throw "input not found $src" }
$rows = Get-Content $src | Select-Object -Skip 1 | Where-Object { $_ } | ForEach-Object { Parse-Row $_ }
Write-Host "$($rows.Count) values from $src"

$backupRows = New-Object System.Collections.Generic.List[string]
$backupRows.Add("CLSID`tSubKey`tName`tType`tValue")
$changed = 0; $skipped = 0; $failed = 0
$touchedKeys = @{}

foreach ($r in $rows) {
    $rel  = "SOFTWARE\Classes\CLSID\$($r.Clsid)"
    if ($r.SubKey) { $rel = "$rel\$($r.SubKey)" }
    $ps   = "HKLM:\$rel"

    if (-not (Test-Path "HKLM:\SOFTWARE\Classes\CLSID\$($r.Clsid)")) { $skipped++; continue }

    # a Win7 @file,-id string resolves against the Win10 binary and the shell then drops the item
    if (-not $Undo -and "$($r.Value)" -match '^@.+,-\d+') { $skipped++; continue }

    $want = To-Typed $r.Type $r.Value
    $have = $null; $exists = $false
    if (Test-Path $ps) {
        $k = Get-Item $ps
        if ($k.GetValueNames() -contains $r.Name) {
            $exists = $true
            $have = if ($k.GetValueKind($r.Name) -eq 'ExpandString') { $k.GetValue($r.Name, $null, 'DoNotExpandEnvironmentNames') } else { $k.GetValue($r.Name) }
        }
    }
    $same = $exists -and ("$have" -eq "$($r.Value)")
    if ($same) { $skipped++; continue }

    $shown = if ($r.Name) { $r.Name } else { '(default)' }
    Write-Host ("  {0}\{1} :: {2} = {3}" -f $r.Clsid, $r.SubKey, $shown, $r.Value)
    if ($WhatIfOnly) { $changed++; continue }

    if (-not $touchedKeys.ContainsKey($rel)) { [void](Grant-KeyControl $rel); $touchedKeys[$rel] = $true }
    try {
        if (-not (Test-Path $ps)) { New-Item -Path $ps -Force | Out-Null }
        if ($exists) { $backupRows.Add(("{0}`t{1}`t{2}`t{3}`t{4}" -f $r.Clsid, $r.SubKey, $r.Name, $r.Type, $have)) }

        $kind = [Microsoft.Win32.RegistryValueKind]::$($r.Type)
        $rk = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($rel, $true)
        if (-not $rk) { throw "cannot open $rel for write" }
        # A key default value has an empty name and New-ItemProperty cannot write it
        $rk.SetValue($r.Name, $want, $kind)
        $rk.Close()
        $changed++
    } catch {
        Write-Host ("    FAILED {0}" -f $_.Exception.Message)
        $failed++
    }
}

if (-not $WhatIfOnly -and -not $Undo) { Set-Content -Path $Backup -Value $backupRows -Encoding UTF8 }
Write-Host ""
Write-Host ("changed={0}  already-correct/skipped={1}  failed={2}" -f $changed, $skipped, $failed)
if (-not $Undo) { Write-Host "backup of replaced values -> $Backup  (re-run with -Undo to revert)" }
