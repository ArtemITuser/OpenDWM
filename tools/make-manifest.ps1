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

param([string]$Exe = 'explorer.exe', [string]$Cut3 = '')
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Cut3) { $Cut3 = Join-Path (Split-Path -Parent $here) 'donor\cut3' }

$dlls = Get-ChildItem $Cut3 -File | Where-Object { $_.Extension -ieq '.dll' } | Select-Object -ExpandProperty Name
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>')
[void]$sb.AppendLine('<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">')
[void]$sb.AppendLine("  <assemblyIdentity type=`"win32`" name=`"Keel.$($Exe -replace '\.','_')`" version=`"1.0.0.0`" processorArchitecture=`"amd64`"/>")
foreach ($d in ($dlls | Sort-Object)) { [void]$sb.AppendLine("  <file name=`"$d`"/>") }
[void]$sb.AppendLine('</assembly>')
$out = Join-Path $Cut3 "$Exe.manifest"
[IO.File]::WriteAllText($out, $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))
Write-Host "wrote $out with $($dlls.Count) <file> redirections"
