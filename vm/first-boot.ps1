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

    [switch]$NoReboot
)
$ErrorActionPreference = 'Continue'
Start-Transcript -Path 'C:\Keel\first-boot.log' -Append -ErrorAction SilentlyContinue | Out-Null
Write-Host "Keel first boot $(Get-Date -Format s)"

if (Test-Path 'C:\Keel\.keel-applied') { Write-Host 'already applied so nothing to do'; Stop-Transcript -EA SilentlyContinue; return }
if (-not (Test-Path 'C:\Keel\vm\apply-keel-guest.ps1')) {
    Write-Host '!FATAL! C:\Keel was not populated by Setup ($OEM$ copy missing). Keel cannot be applied.'
    Stop-Transcript -EA SilentlyContinue; return
}

Set-ExecutionPolicy Bypass -Scope Process -Force -ErrorAction SilentlyContinue

$merges = @(
    @{ Tsv = 'C:\Keel\regmerge\software.tsv'; Root = 'SOFTWARE'; Hive = 'LocalMachine' },
    @{ Tsv = 'C:\Keel\regmerge\default.tsv';  Root = '.DEFAULT'; Hive = 'Users' },
    @{ Tsv = 'C:\Keel\regmerge\hkcu.tsv';     Root = '';         Hive = 'CurrentUser' }
)

$defaultHive = 'C:\Users\Default\NTUSER.DAT'
if ([Security.Principal.WindowsIdentity]::GetCurrent().IsSystem) {
    $hkcu = $merges | Where-Object { $_.Hive -eq 'CurrentUser' }
    $merges = $merges | Where-Object { $_.Hive -ne 'CurrentUser' }
    if ($hkcu -and (Test-Path $hkcu.Tsv) -and (Test-Path $defaultHive)) {
        Write-Host '  running as SYSTEM applying the HKCU delta to the DEFAULT USER PROFILE'
        & reg.exe load HKLM\KEELDEF $defaultHive *>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) {
            try {
                & C:\Keel\vm\apply-reg-merge.ps1 -Tsv $hkcu.Tsv -Root 'KEELDEF' -Hive 'LocalMachine' | ForEach-Object { "    $_" }
            } finally {

                [gc]::Collect(); [gc]::WaitForPendingFinalizers(); Start-Sleep 1
                & reg.exe unload HKLM\KEELDEF *>&1 | Out-Null
                if ($LASTEXITCODE -ne 0) { Write-Host '    !WARNING! could not unload the default hive' }
            }
        } else { Write-Host "    !WARNING! could not load $defaultHive" }
    }
}
foreach ($m in $merges) {
    if (-not (Test-Path $m.Tsv)) { Write-Host "  registry merge $($m.Tsv) not on the media so skipped"; continue }
    Write-Host "  registry merge applying $($m.Tsv)"
    & C:\Keel\vm\apply-reg-merge.ps1 -Tsv $m.Tsv -Root $m.Root -Hive $m.Hive | ForEach-Object { "    $_" }
}

Write-Host '  applying the Keel stack (apply-keel-guest.ps1)'
# order matters, add-only merge first, then the stack, then the value level repairs the merge cannot reach
& C:\Keel\vm\apply-keel-guest.ps1 | ForEach-Object { "    $_" }

if (Test-Path 'C:\Keel\vm\fix-settings-verbs.ps1') {
    Write-Host '  restoring the Win7 desktop verbs (fix-settings-verbs.ps1)'
    & C:\Keel\vm\fix-settings-verbs.ps1 | ForEach-Object { "    $_" }
} else {
    Write-Host '  !WARNING! fix-settings-verbs.ps1 missing so Personalize and Display will open the Settings app and fail'
}

if (Test-Path 'C:\Keel\vm\fix-cpl-reg.ps1') {
    Write-Host '  restoring the Win7 Control Panel item registrations (fix-cpl-reg.ps1)'
    & C:\Keel\vm\fix-cpl-reg.ps1 | Select-Object -Last 3 | ForEach-Object { "    $_" }
} else {
    Write-Host '  !WARNING! fix-cpl-reg.ps1 missing so several Control Panel items will open nothing'
}

try {
    $d = Get-MpComputerStatus -ErrorAction Stop
    Write-Host "  Defender running=$($d.AMRunningMode) rtp=$($d.RealTimeProtectionEnabled) tamper=$($d.IsTamperProtected)"
    if ($d.RealTimeProtectionEnabled) { Write-Host '  !WARNING! Defender is still active and will remediate the shell swap' }
} catch { Write-Host '  Defender service not running, good' }

try {
    $sb = Confirm-SecureBootUEFI -ErrorAction Stop
    if ($sb) {
        Write-Host ''
        Write-Host '  ****************************************************************************'
        Write-Host '  * SECURE BOOT IS ENABLED. Keel will NOT work until it is turned off in the  *'
        Write-Host '  * firmware setup. Test signing is ignored, so keeldrv cannot load, and      *'
        Write-Host '  * AppInit_DLLs is ignored, so keelshim32/keeltsf are never injected.        *'
        Write-Host '  ****************************************************************************'
        Write-Host ''
    } else { Write-Host '  Secure Boot off, good' }
} catch {

    Write-Host '  Secure Boot not a UEFI boot (BIOS/CSM), fine'
}

Set-Content 'C:\Keel\.keel-applied' -Value (Get-Date -Format s) -Encoding ascii
if ($NoReboot) {
    Write-Host 'Keel applied but not rebooting since Setup reboots into OOBE by itself, and the first'
    Write-Host 'logon lands straight on the Windows 7 shell (Winlogon Shell and the dwm IFEO are set).'
    Stop-Transcript -ErrorAction SilentlyContinue | Out-Null
} else {
    Write-Host 'Keel applied so rebooting into the Windows 7 desktop.'
    Stop-Transcript -ErrorAction SilentlyContinue | Out-Null
    shutdown /r /t 5 /f
}
