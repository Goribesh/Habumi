# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]
param([Parameter(Mandatory)][ValidateSet('Backup','Restore','Recover','Diagnostics')][string]$Operation,
      [string]$ProfileRoot='',
      [string]$Path)
$ErrorActionPreference='Stop'
if(-not $ProfileRoot){$ProfileRoot=$PSScriptRoot}
Import-Module (Join-Path $PSScriptRoot 'Profile-Data.psm1') -Force
switch($Operation){
    'Backup' { Backup-HabumiProfile -ProfileRoot $ProfileRoot -Destination $Path }
    'Restore' { Restore-HabumiProfile -ProfileRoot $ProfileRoot -BackupPath $Path }
    'Recover' { Repair-HabumiProfile -ProfileRoot $ProfileRoot }
    'Diagnostics' { Export-HabumiDiagnostics -ProfileRoot $ProfileRoot -Destination $Path }
}
