# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]
param([Parameter(Mandatory)][ValidateSet('Backup','Restore','Recover','Diagnostics','Grow')][string]$Operation,
      [string]$ProfileRoot='',
      [string]$Path,
      [ValidateSet('data.img','data-gapps.img')][string]$Image,
      [ValidateRange(4,16)][int]$SizeGiB)
$ErrorActionPreference='Stop'
if(-not $ProfileRoot){$ProfileRoot=$PSScriptRoot}
Import-Module (Join-Path $PSScriptRoot 'Profile-Data.psm1') -Force
switch($Operation){
    'Backup' { Backup-HabumiProfile -ProfileRoot $ProfileRoot -Destination $Path }
    'Restore' { Restore-HabumiProfile -ProfileRoot $ProfileRoot -BackupPath $Path }
    'Recover' { Repair-HabumiProfile -ProfileRoot $ProfileRoot }
    'Diagnostics' { Export-HabumiDiagnostics -ProfileRoot $ProfileRoot -Destination $Path }
    'Grow' {
        if(-not $Image -or -not $SizeGiB){throw 'Grow requires explicit -Image data.img or data-gapps.img and -SizeGiB.'}
        Expand-HabumiData -ProfileRoot $ProfileRoot -Image $Image -SizeGiB $SizeGiB
    }
}
