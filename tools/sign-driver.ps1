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

param([string]$Sys = '', [string]$CertOut = '')
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here
if (-not $Sys) { $Sys = Join-Path $root 'out\debug\keeldrv\keeldrv.sys' }
if (-not $CertOut) { $CertOut = Join-Path $root 'out\debug\keeldrv\keeltest.cer' }
if (-not (Test-Path $Sys)) { throw "driver not found $Sys (build it first)" }

$subject = 'CN=Keel Test Signing'
$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $subject } | Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject -CertStoreLocation 'Cert:\CurrentUser\My' -KeyUsage DigitalSignature -KeyExportPolicy Exportable -NotAfter (Get-Date).AddYears(5) -HashAlgorithm SHA256
    Write-Host "created test cert $($cert.Thumbprint)" -ForegroundColor Green
}
New-Item -ItemType Directory -Force (Split-Path $CertOut) | Out-Null
Export-Certificate -Cert $cert -FilePath $CertOut | Out-Null

$toolsRoot = if ($env:KEEL_TOOLS) { $env:KEEL_TOOLS } else { 'C:\Keel-tools' }
$signtool = Get-ChildItem (Join-Path $toolsRoot 'msvc\Windows Kits\10\bin') -Recurse -Filter signtool.exe -EA SilentlyContinue | Where-Object { $_.FullName -match '\\x64\\' } | Select-Object -First 1
if (-not $signtool) { throw 'signtool.exe not found in WDK' }
& $signtool.FullName sign /v /fd SHA256 /sha1 $cert.Thumbprint $Sys 2>&1 | Select-String 'Successfully|error|Done' | ForEach-Object { $_.Line }
$sig = Get-AuthenticodeSignature $Sys
Write-Host "keeldrv.sys signature $($sig.Status) by $($sig.SignerCertificate.Subject)" -ForegroundColor Green
Write-Host "public cert $CertOut (import to guest Root + TrustedPublisher)"
