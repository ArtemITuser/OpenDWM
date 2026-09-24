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
param([switch]$WhatIfOnly)

$ErrorActionPreference = 'Stop'

if (-not (New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
        ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run elevated since these keys are TrustedInstaller-owned.'
}

Add-Type -Namespace KeelPriv -Name Tok -MemberDefinition @'
[DllImport("ntdll.dll")]
public static extern int RtlAdjustPrivilege(int Privilege, bool Enable, bool CurrentThread, out bool Enabled);
'@
foreach ($p in 8, 9, 17, 18) {
    $was = $false
    [void][KeelPriv.Tok]::RtlAdjustPrivilege($p, $true, $false, [ref]$was)
}

$AdminsSid = New-Object System.Security.Principal.SecurityIdentifier 'S-1-5-32-544'
function Grant-KeyControl([string]$Rel) {
    try {
        $k = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey(
                $Rel, [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree,
                [System.Security.AccessControl.RegistryRights]::TakeOwnership)
        if (-not $k) { return $false }
        $acl = $k.GetAccessControl([System.Security.AccessControl.AccessControlSections]::None)
        $acl.SetOwner($AdminsSid)
        $k.SetAccessControl($acl)
        $k.Close()
    } catch { return $false }
    try {
        $k2 = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey(
                $Rel, [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree,
                [System.Security.AccessControl.RegistryRights]::ChangePermissions)
        $acl2 = $k2.GetAccessControl()
        $acl2.SetAccessRule((New-Object System.Security.AccessControl.RegistryAccessRule(
            $AdminsSid, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow')))
        $k2.SetAccessControl($acl2)
        $k2.Close()
        return $true
    } catch { return $false }
}

# CLSID_LaunchSettingsPageHandler is Win10 only, so the Win7 shell cannot supply it and the verb fails
$OpenControlPanel = '{06622D85-6856-4460-8DE1-A81921B41C4B}'
$verbs = @(
    @{ Verb = 'Personalize'; CplName = 'Microsoft.Personalization'; CplPage = $null },
    @{ Verb = 'Display';     CplName = 'Microsoft.Display';         CplPage = 'Settings' }
)

foreach ($v in $verbs) {
    $rel    = "SOFTWARE\Classes\DesktopBackground\Shell\$($v.Verb)"
    $relCmd = "$rel\command"
    $psBase = "HKLM:\$rel"
    $psCmd  = "HKLM:\$relCmd"

    if (-not (Test-Path $psCmd)) { Write-Host "skip (absent) $relCmd"; continue }

    $current = (Get-ItemProperty $psCmd -Name DelegateExecute -ErrorAction SilentlyContinue).DelegateExecute
    if ($current -eq $OpenControlPanel) { Write-Host "already Win7 $($v.Verb)"; continue }

    Write-Host ("{0}: DelegateExecute {1} -> {2}" -f $v.Verb, $current, $OpenControlPanel)
    if ($WhatIfOnly) { continue }

    [void](Grant-KeyControl $rel)
    [void](Grant-KeyControl $relCmd)

    Set-ItemProperty $psCmd  -Name DelegateExecute  -Value $OpenControlPanel
    Set-ItemProperty $psBase -Name ControlPanelName -Value $v.CplName
    if ($v.CplPage) { Set-ItemProperty $psBase -Name ControlPanelPage -Value $v.CplPage }

    Remove-ItemProperty $psBase -Name SettingsURI -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host '! result !'
foreach ($v in $verbs) {
    $psCmd = "HKLM:\SOFTWARE\Classes\DesktopBackground\Shell\$($v.Verb)\command"
    if (Test-Path $psCmd) {
        $d = (Get-ItemProperty $psCmd -Name DelegateExecute -ErrorAction SilentlyContinue).DelegateExecute
        $u = (Get-ItemProperty "HKLM:\SOFTWARE\Classes\DesktopBackground\Shell\$($v.Verb)" -Name SettingsURI -ErrorAction SilentlyContinue).SettingsURI
        "{0,-12} DelegateExecute={1}  SettingsURI={2}" -f $v.Verb, $d, $(if ($u) { $u } else { '<removed>' })
    }
}
