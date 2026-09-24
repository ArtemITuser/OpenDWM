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

    [switch]$Win7LogonUI
)
$ErrorActionPreference = 'Continue'

bcdedit /set testsigning on | Out-Null
bcdedit /set nointegritychecks off | Out-Null
$wl = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon'
Set-ItemProperty $wl AutoAdminLogon '1' -Type String
Set-ItemProperty $wl DefaultUserName 'keel' -Type String
Set-ItemProperty $wl DefaultPassword '1111' -Type String
Set-ItemProperty $wl DefaultDomainName $env:COMPUTERNAME -Type String
Remove-ItemProperty $wl AutoLogonCount -EA SilentlyContinue

$acct = 'keel'
if (-not (Get-LocalUser -Name $acct -ErrorAction SilentlyContinue)) {
    Write-Host "  creating local account '$acct'"
    & net.exe user $acct '1111' /add /expires:never *>&1 | Out-Null
    & net.exe user $acct /active:yes *>&1 | Out-Null
}

foreach ($sid in 'S-1-5-32-544', 'S-1-5-32-545') {
    $grp = Get-LocalGroup -SID $sid -ErrorAction SilentlyContinue
    if (-not $grp) { continue }
    $already = Get-LocalGroupMember -Group $grp.Name -ErrorAction SilentlyContinue |
               Where-Object { $_.Name -like "*\$acct" -or $_.Name -eq $acct }
    if (-not $already) {
        try { Add-LocalGroupMember -Group $grp.Name -Member $acct -ErrorAction Stop; Write-Host "  added '$acct' to $($grp.Name)" }
        catch { Write-Host "  !FAILED! adding '$acct' to $($grp.Name): $($_.Exception.Message)" }
    }
}
$isAdmin = [bool](Get-LocalGroupMember -SID 'S-1-5-32-544' -ErrorAction SilentlyContinue |
                  Where-Object { $_.Name -like "*\$acct" -or $_.Name -eq $acct })
Write-Host "  '$acct' administrator=$isAdmin"
Set-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System' EnableLUA 0 -Type DWord

$layers = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers'
if (Test-Path $layers) {
    foreach ($exe in 'C:\Keel\rtm\keeldwm.exe', 'C:\Windows\System32\dwm.exe') {
        if ($null -ne (Get-ItemProperty $layers -Name $exe -EA SilentlyContinue).$exe) {
            Remove-ItemProperty $layers -Name $exe -Force -EA SilentlyContinue
            "dpi removed the stale '~ DPIUNAWARE' AppCompat layer from $exe"
        }
    }
}
'dpi the compositor is pinned to 96 DPI by keelshim (HookedGetDeviceCaps) while the shell still scales'
New-Item 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\Personalization' -Force | Out-Null
Set-ItemProperty 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\Personalization' NoLockScreen 1 -Type DWord
powercfg /change standby-timeout-ac 0 2>$null; powercfg /change monitor-timeout-ac 0 2>$null; powercfg /hibernate off 2>$null
try { Add-MpPreference -ExclusionPath 'C:\Keel' -EA Stop } catch {}
$wer = 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps'
New-Item $wer -Force | Out-Null
Set-ItemProperty $wer DumpFolder 'C:\Keel\dumps' -Type ExpandString
Set-ItemProperty $wer DumpType 2 -Type DWord; Set-ItemProperty $wer DumpCount 10 -Type DWord
New-Item -ItemType Directory -Force C:\Keel\dumps | Out-Null
Set-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting' DontShowUI 1 -Type DWord
sc.exe config wuauserv start= disabled | Out-Null

foreach ($store in 'Root','TrustedPublisher') {
    $ok = $false
    for ($try = 0; $try -lt 3 -and -not $ok; $try++) {
        try { Import-Certificate -FilePath C:\Keel\bin\keeltest.cer -CertStoreLocation "Cert:\LocalMachine\$store" -EA Stop | Out-Null; $ok = $true }
        catch { certutil.exe -f -addstore $store C:\Keel\bin\keeltest.cer *>$null; if ($LASTEXITCODE -eq 0) { $ok = $true } else { Start-Sleep 2 } }
    }
}

sc.exe stop keeldrv 2>&1 | Out-Null; sc.exe delete keeldrv 2>&1 | Out-Null; Start-Sleep 1
sc.exe create keeldrv type= kernel start= auto binPath= C:\Keel\bin\keeldrv.sys | Out-Null

[Environment]::SetEnvironmentVariable('KEEL_AUTOINIT','1','Machine')
[Environment]::SetEnvironmentVariable('KEEL_LOG','C:\Keel\native-keel.log','Machine')

New-Item -ItemType Directory -Force C:\Keel\fallback | Out-Null
Copy-Item C:\Windows\System32\dwm.exe C:\Keel\fallback\win10dwm.exe -Force -EA SilentlyContinue

$wex = 'C:\Windows\explorer.exe'
if (-not (Test-Path 'C:\Keel\fallback\win10explorer.exe')) { Copy-Item $wex 'C:\Keel\fallback\win10explorer.exe' -Force }
takeown /f $wex /a | Out-Null
icacls $wex /grant 'Administrators:F' | Out-Null
Copy-Item 'C:\Keel\bin\keelexp.exe' $wex -Force

$task = 'KeelUxSmsAlias'
Unregister-ScheduledTask -TaskName $task -Confirm:$false -EA SilentlyContinue
$act = New-ScheduledTaskAction -Execute 'C:\Keel\bin\keeluxsms.exe' -Argument '--alias'
$trg = New-ScheduledTaskTrigger -AtStartup
$pri = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
$set = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit (New-TimeSpan -Minutes 2)
Register-ScheduledTask -TaskName $task -Action $act -Trigger $trg -Principal $pri -Settings $set -Force | Out-Null
Start-ScheduledTask -TaskName $task
if (Test-Path 'C:\Keel\bin32\seam\dwmapi.dll') {
    $f32 = 'C:\Windows\SysWOW64\dwmapi.dll'
    if (-not (Test-Path 'C:\Keel\fallback\win10dwmapi32.dll')) { Copy-Item $f32 'C:\Keel\fallback\win10dwmapi32.dll' -Force }
    takeown /f $f32 /a | Out-Null
    icacls $f32 /grant 'Administrators:F' | Out-Null
    if ((Get-Item $f32).Length -gt 65536) {
        Move-Item $f32 "C:\Keel\fallback\dwmapi32.replaced-$(Get-Date -Format yyyyMMddHHmmss).dll" -Force
    } else { Remove-Item $f32 -Force -EA SilentlyContinue }
    foreach ($n in 'dwmapi7.dll','dwmapi10.dll','keel32.dll','keelstub.dll','dwmapi.dll') {
        $dst = "C:\Windows\SysWOW64\$n"
        try { Copy-Item "C:\Keel\bin32\seam\$n" $dst -Force -EA Stop }
        catch {
            Move-Item $dst "C:\Keel\fallback\$n.inuse-$(Get-Random)" -Force
            Copy-Item "C:\Keel\bin32\seam\$n" $dst -Force
        }
    }
}

if ($Win7LogonUI) {
    if (-not (Test-Path 'C:\Keel\fallback\win10logonui.exe')) {
        Copy-Item C:\Windows\System32\LogonUI.exe 'C:\Keel\fallback\win10logonui.exe' -Force -EA SilentlyContinue
    }
    $lui = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\LogonUI.exe'
    New-Item -Path $lui -Force | Out-Null
    Set-ItemProperty $lui Debugger 'C:\Keel\bin\keelldr.exe --donor C:\Keel\rtm --ifeo-target C:\Keel\rtm\keellogonui.exe --fallback C:\Keel\fallback\win10logonui.exe --wait' -Type String
}

$ifeo = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\dwm.exe'
New-Item -Path $ifeo -Force | Out-Null
Set-ItemProperty $ifeo Debugger 'C:\Keel\bin\keelldr.exe --donor C:\Keel\rtm --ifeo-target C:\Keel\rtm\keeldwm.exe --fallback C:\Keel\fallback\win10dwm.exe --wait' -Type String

$appInit32 = 'HKLM:\SOFTWARE\Wow6432Node\Microsoft\Windows NT\CurrentVersion\Windows'
if (Test-Path $appInit32) {
    Set-ItemProperty $appInit32 AppInit_DLLs 'C:\Keel\bin32\keelshim32.dll' -Type String
    Set-ItemProperty $appInit32 LoadAppInit_DLLs 1 -Type DWord
    Set-ItemProperty $appInit32 RequireSignedAppInit_DLLs 0 -Type DWord
}

$appInit64 = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Windows'
if (Test-Path $appInit64) {
    Set-ItemProperty $appInit64 AppInit_DLLs 'C:\Keel\bin\keeltsf.dll' -Type String
    Set-ItemProperty $appInit64 LoadAppInit_DLLs 1 -Type DWord
    Set-ItemProperty $appInit64 RequireSignedAppInit_DLLs 0 -Type DWord
}
$wl = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon'
Set-ItemProperty $wl Shell 'C:\Keel\bin\keelldr.exe --donor C:\Keel\rtm C:\Keel\rtm\explorer.exe' -Type String
Set-ItemProperty $wl AutoRestartShell 0 -Type DWord
Remove-Item 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\explorer.exe' -Recurse -Force -EA SilentlyContinue

# a Win10 explorer that ran before Keel leaves tray icon streams the Win7 tray cannot read, and then no system icon ever shows
$trayRel = 'Local Settings\Software\Microsoft\Windows\CurrentVersion\TrayNotify'
$trayKeys = @("HKCU:\Software\Classes\$trayRel") + @(Get-ChildItem Registry::HKEY_USERS -EA SilentlyContinue |
    Where-Object { $_.PSChildName -match '^S-1-5-21-[\d-]+_Classes$' } | ForEach-Object { "Registry::HKEY_USERS\$($_.PSChildName)\$trayRel" })
foreach ($tk in ($trayKeys | Select-Object -Unique)) {
    if (-not (Test-Path $tk)) { continue }
    if ((Get-ItemProperty $tk -EA SilentlyContinue).KeelTrayReset) { continue }
    Remove-ItemProperty $tk -Name IconStreams, PastIconsStream -EA SilentlyContinue
    Set-ItemProperty $tk KeelTrayReset 1 -Type DWord
    "tray icon streams reset in $tk"
}

New-Item -ItemType Directory -Force C:\Keel\shell | Out-Null

$reg = @"
Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Software\Classes\Interface\{489E9453-869B-4BCC-A1C7-48B5285FD9D8}]
@="IExplorerHost"
[HKEY_CURRENT_USER\Software\Classes\Interface\{489E9453-869B-4BCC-A1C7-48B5285FD9D8}\NumMethods]
@="4"
[HKEY_CURRENT_USER\Software\Classes\Interface\{489E9453-869B-4BCC-A1C7-48B5285FD9D8}\ProxyStubClsid32]
@="{C90250F3-4D7D-4991-9B69-A5C5BC1C2AE6}"
[HKEY_CURRENT_USER\Software\Classes\Interface\{CC271F08-E1DD-49BF-87CC-CD6DCF3F3D9F}]
@="IHardwareDevices"
[HKEY_CURRENT_USER\Software\Classes\Interface\{CC271F08-E1DD-49BF-87CC-CD6DCF3F3D9F}\NumMethods]
@="8"
[HKEY_CURRENT_USER\Software\Classes\Interface\{CC271F08-E1DD-49BF-87CC-CD6DCF3F3D9F}\ProxyStubClsid32]
@="{C90250F3-4D7D-4991-9B69-A5C5BC1C2AE6}"
[HKEY_CURRENT_USER\Software\Classes\CLSID\{C90250F3-4D7D-4991-9B69-A5C5BC1C2AE6}]
@="PSFactoryBuffer"
[HKEY_CURRENT_USER\Software\Classes\CLSID\{C90250F3-4D7D-4991-9B69-A5C5BC1C2AE6}\InprocServer32]
@="C:\\Keel\\rtm\\actxprxy.dll"
"ThreadingModel"="Both"
"@
Set-Content -Path 'C:\Keel\bin\iexplorerhost.reg' -Value $reg -Encoding ascii
Set-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Run' KeelComReg 'reg.exe import C:\Keel\bin\iexplorerhost.reg' -Type String

# Win10 control.exe cannot host Win7 shell32 so run the manifested Win7 copy, renamed so keelldr does not re-enter this IFEO entry
$w7control = 'C:\Keel\rtm\control.exe'
if (Test-Path $w7control) {
    Copy-Item $w7control 'C:\Keel\rtm\keelcontrol.exe' -Force
    $ifeo = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\control.exe'
    New-Item $ifeo -Force | Out-Null
    Set-ItemProperty $ifeo -Name Debugger -Type String -Value `
        '"C:\Keel\bin\keelldr.exe" --donor C:\Keel\rtm --shim C:\Keel\bin\keelshim.dll --ifeo-target C:\Keel\rtm\keelcontrol.exe'
    'control panel control.exe routed to the Win7 copy under keelldr'
}
else { Write-Warning 'C:\Keel\rtm\control.exe missing so the Start menu Control Panel entry will not open' }

# the desktop shortcut was the workaround for the above, remove it if an older apply left one behind
$oldLnk = Join-Path (Join-Path $env:PUBLIC 'Desktop') 'Control Panel.lnk'
if (Test-Path $oldLnk) { Remove-Item $oldLnk -Force -EA SilentlyContinue }

$dcompExpectVer = '10.0.19041.1266'
$dcompSeam = 'C:\Keel\bin\keeldcomp.dll'
$sys32 = "$env:SystemRoot\System32"
if (-not (Test-Path $dcompSeam)) {
    'dcomp seam keeldcomp.dll not in the payload so skipped'
} elseif (Test-Path "$sys32\dcomp10.dll") {
    'dcomp seam already installed (dcomp10.dll present)'
} else {
    $cur = (Get-Item "$sys32\dcomp.dll" -EA SilentlyContinue).VersionInfo.FileVersion
    if (-not $cur) {
        'dcomp seam no System32\dcomp.dll so skipped'
    } elseif ($cur -notlike "$dcompExpectVer*") {
        "dcomp seam SKIPPED since this machine has dcomp $cur and the seam was generated against $dcompExpectVer, regenerate it rather than installing a mismatched export table."
    } else {
        New-Item -ItemType Directory -Force C:\Keel\backup | Out-Null
        Copy-Item "$sys32\dcomp.dll" 'C:\Keel\backup\dcomp.orig.dll' -Force
        takeown /f "$sys32\dcomp.dll" 2>&1 | Out-Null
        icacls "$sys32\dcomp.dll" /grant "*S-1-5-32-544:(F)" 2>&1 | Out-Null
        Rename-Item "$sys32\dcomp.dll" 'dcomp10.dll' -Force
        Copy-Item $dcompSeam "$sys32\dcomp.dll" -Force
        "dcomp seam installed (original -> dcomp10.dll, backup in C:\Keel\backup). KEEL_NO_DCOMPSEAM=1 disables it."
    }
}

New-Item -ItemType Directory -Force C:\Keel\backup | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
foreach ($nsRoot in @(
    'HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\MyComputer\NameSpace',
    'HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Explorer\MyComputer\NameSpace')) {
    $tag = if ($nsRoot -like '*WOW6432Node*') { 'wow64' } else { 'native' }
    & reg.exe export $nsRoot "C:\Keel\backup\MyComputerNameSpace-$tag-$stamp.reg" /y *>&1 | Out-Null
    foreach ($k in Get-ChildItem "Registry::$nsRoot" -EA SilentlyContinue) {
        if ($k.PSChildName -match '^\{[0-9A-Fa-f-]{36}\}$') { Remove-Item $k.PSPath -Recurse -Force -EA SilentlyContinue }
    }
}
$fdBase = 'SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\FolderDescriptions'
foreach ($fd in @(
    '{754AC886-DF64-4CBA-86B5-F7FBF4FBCEF5}',
    '{f42ee2d3-909f-4907-8871-4c22fc0bf756}',
    '{7d83ee9b-2244-4e70-b1f5-5393042af1e4}',
    '{a0c69a99-21c8-4671-8703-7934162fcf1d}',
    '{0ddd015d-b06c-45d5-8c4c-f59713854639}',
    '{35286a68-3c57-41a1-bbb1-0eae73d76c95}')) {
    if (Test-Path "HKLM:\$fdBase\$fd") {
        & reg.exe export "HKLM\$fdBase\$fd" "C:\Keel\backup\FolderDescription-$($fd.Trim('{}'))-$stamp.reg" /y *>&1 | Out-Null
        Remove-Item "HKLM:\$fdBase\$fd" -Recurse -Force -EA SilentlyContinue
    }
}
"MyComputer namespace GUIDs left=$((Get-ChildItem 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\MyComputer\NameSpace' -EA SilentlyContinue | Where-Object { $_.PSChildName -like '{*' } | Measure-Object).Count) (expect 0)"

"Shell=$((Get-ItemProperty $wl).Shell)"
"testsigning=$((bcdedit /enum '{current}' | Select-String 'testsigning'))"
"keeldrv=$((sc.exe qc keeldrv | Select-String 'START_TYPE'))"
"cert in Root=$((Get-ChildItem Cert:\LocalMachine\Root | Where-Object Subject -match 'Keel' | Measure-Object).Count)"
"IFEO set=$([bool](Get-ItemProperty $ifeo).Debugger)"
