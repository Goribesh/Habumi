# SPDX-License-Identifier: GPL-2.0-only
# Offline profile operations. All paths are confined and restores retain originals.
Set-StrictMode -Version 2
$ErrorActionPreference='Stop'

function Assert-HabumiPlainPath([string]$Path){
    $current=[IO.Path]::GetFullPath($Path)
    while($current){
        if(Test-Path -LiteralPath $current){
            if(([IO.File]::GetAttributes($current) -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Links are not supported for data maintenance: $current"}
        }
        $current=[IO.Path]::GetDirectoryName($current)
    }
}
function Get-HabumiRoot([string]$Path){
    if(-not $Path){throw 'A profile directory is required.'}
    $root=[IO.Path]::GetFullPath($Path).TrimEnd('\','/')
    Assert-HabumiPlainPath $root
    if(-not(Test-Path -LiteralPath $root -PathType Container)){throw 'Profile directory does not exist.'}
    return $root
}
function Get-HabumiDataPath([string]$Root,[string]$Relative){
    if(-not $Relative -or $Relative.Contains('\') -or
       ($Relative -notin @('runtime/bin/config.txt','guest/images/android/data.img','guest/images/android/data-gapps.img') -and
        -not $Relative.StartsWith('runtime/keymaps/',[StringComparison]::OrdinalIgnoreCase))){throw "Unsupported data path: $Relative"}
    foreach($part in $Relative.Split('/')){
        if(-not $part -or $part -in '.','..' -or $part.EndsWith('.') -or $part.EndsWith(' ') -or
           $part.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0 -or
           $part -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)'){throw "Unsafe data path: $Relative"}
    }
    $path=[IO.Path]::GetFullPath((Join-Path $Root $Relative))
    if(-not $path.StartsWith(($Root+'\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Data path escaped its directory.'}
    Assert-HabumiPlainPath $path
    return $path
}
function Get-HabumiDataFiles([string]$Root){
    foreach($path in @('runtime/bin/config.txt','guest/images/android/data.img','guest/images/android/data-gapps.img')){
        $full=Get-HabumiDataPath $Root $path
        if(Test-Path -LiteralPath $full -PathType Leaf){$path}
    }
    $keys=Join-Path $Root 'runtime/keymaps'
    if(Test-Path -LiteralPath $keys){
        Assert-HabumiPlainPath $keys
        $queue=[Collections.Generic.Queue[string]]::new();$queue.Enqueue($keys)
        while($queue.Count){
            foreach($item in @(Get-ChildItem -LiteralPath $queue.Dequeue() -Force)){
                Assert-HabumiPlainPath $item.FullName
                if($item.PSIsContainer){$queue.Enqueue($item.FullName)}
                else{$item.FullName.Substring($Root.Length+1).Replace('\','/')}
            }
        }
    }
}
function Open-HabumiDataGuard([string]$Root,[switch]$Recovery){
    if(-not(Test-Path -LiteralPath (Join-Path $Root 'runtime/bin') -PathType Container)){throw 'This is not a prepared Habumi profile.'}
    Assert-HabumiPlainPath (Join-Path $Root '.habumi-profile.lock')
    try{$guard=[IO.File]::Open((Join-Path $Root '.habumi-profile.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}
    catch{throw 'This profile is running or another data operation is active.'}
    if(-not $Recovery -and (Test-Path -LiteralPath (Join-Path $Root 'storage-maintenance.lock'))){$guard.Dispose();throw 'Interrupted maintenance requires Recover before other operations.'}
    return $guard
}
function Write-HabumiJson([string]$Path,$Value){
    $temporary=$Path+'.new-'+[guid]::NewGuid().ToString('N')
    $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value|ConvertTo-Json -Depth 16)+"`n")
    $stream=[IO.File]::Open($temporary,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
    if(Test-Path -LiteralPath $Path){[IO.File]::Replace($temporary,$Path,[NullString]::Value)}else{[IO.File]::Move($temporary,$Path)}
}
function Read-HabumiJson([string]$Path){
    Assert-HabumiPlainPath $Path
    if((Get-Item -LiteralPath $Path).Length -gt 2MB){throw 'Metadata file exceeds the supported limit.'}
    return ([IO.File]::ReadAllText($Path)|ConvertFrom-Json)
}
function Assert-HabumiFile([string]$Path,$Record){
    if(-not(Test-Path -LiteralPath $Path -PathType Leaf) -or (Get-Item -LiteralPath $Path).Length -ne [long]$Record.Bytes -or
       (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $Record.SHA256){throw "File is missing, incomplete or corrupt: $Path"}
}
function Get-HabumiOriginalRecord([string]$Path){
    $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
    $sha=[Security.Cryptography.SHA256]::Create()
    try{return @{Bytes=$stream.Length;SHA256=([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','')}}
    finally{$stream.Dispose();$sha.Dispose()}
}
function Copy-HabumiFile([string]$Source,[string]$Target){
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($Target)) -Force|Out-Null
    $inputStream=[IO.File]::Open($Source,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
    $outputStream=$null;$sha=[Security.Cryptography.SHA256]::Create()
    try{
        $hash=([BitConverter]::ToString($sha.ComputeHash($inputStream))).Replace('-','')
        $length=$inputStream.Length;$inputStream.Position=0
        $outputStream=[IO.File]::Open($Target,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        $inputStream.CopyTo($outputStream,1MB);$outputStream.Flush($true)
    }finally{if($outputStream){$outputStream.Dispose()};$inputStream.Dispose();$sha.Dispose()}
    $record=[pscustomobject]@{Bytes=$length;SHA256=$hash}
    Assert-HabumiFile $Target $record
    return $record
}
function Assert-HabumiSpace([string]$Path,[long]$Bytes){
    $drive=[IO.DriveInfo]::new([IO.Path]::GetPathRoot($Path))
    if($drive.AvailableFreeSpace -lt $Bytes+16MB){throw 'Insufficient free space; existing profile data was not replaced.'}
}
function Get-HabumiNewDestination([string]$Path,[string]$Root){
    if(-not $Path){throw 'A destination path is required.'}
    $full=[IO.Path]::GetFullPath($Path).TrimEnd('\','/')
    if($full -eq $Root -or $full.StartsWith(($Root+'\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Choose a destination outside the profile.'}
    Assert-HabumiPlainPath $full
    if(Test-Path -LiteralPath $full){throw 'Destination already exists; nothing will be overwritten.'}
    if(-not(Test-Path -LiteralPath ([IO.Path]::GetDirectoryName($full)) -PathType Container)){throw 'Destination parent directory must exist.'}
    return $full
}
function Get-HabumiMetadata([string]$Root){
    $images=@(foreach($relative in @('release-profile.json','development-build.json','native-profile.json',
        'guest/images/kernel-guest-arm64','guest/images/android/initramfs.img',
        'guest/images/android/system.img','guest/images/android/system-gapps.img','guest/images/android/vendor.img')){
        $file=Join-Path $Root $relative
        if(Test-Path -LiteralPath $file -PathType Leaf){Assert-HabumiPlainPath $file;[ordered]@{Path=$relative;Bytes=(Get-Item -LiteralPath $file).Length;SHA256=(Get-FileHash -LiteralPath $file).Hash}}
    })
    $release=$null
    if(Test-Path -LiteralPath (Join-Path $Root 'release-profile.json')){$release=Read-HabumiJson (Join-Path $Root 'release-profile.json')}
    return [ordered]@{ProfileSchema=1;CapturedUtc=[DateTime]::UtcNow.ToString('o');Release=$release;ReferenceFiles=$images}
}
function Read-HabumiBackup([string]$BackupPath){
    $root=Get-HabumiRoot $BackupPath
    $manifest=Read-HabumiJson (Join-Path $root 'backup.json')
    if($manifest.Schema -ne 'habumi.backup.v1' -or $manifest.State -ne 'Complete' -or
       $manifest.Metadata.ProfileSchema -ne 1 -or
       @($manifest.Files).Count -eq 0 -or @($manifest.Files).Count -gt 4096){throw 'Incomplete or unsupported backup manifest.'}
    $seen=@{}
    foreach($entry in @($manifest.Files)){
        if($entry.SHA256 -notmatch '^[0-9A-Fa-f]{64}$' -or [long]$entry.Bytes -lt 0 -or [long]$entry.Bytes -gt 1TB -or $seen.ContainsKey([string]$entry.Path)){throw 'Invalid or duplicate backup entry.'}
        $seen[[string]$entry.Path]=$true
        $file=Get-HabumiDataPath (Join-Path $root 'payload') $entry.Path
        Assert-HabumiFile $file $entry
    }
    if(-not $seen.ContainsKey('runtime/bin/config.txt')){throw 'Backup configuration is missing.'}
    return $manifest
}
function Assert-HabumiReferenceImages([string]$Root,$Manifest){
    foreach($reference in @($Manifest.Metadata.ReferenceFiles)){
        if($reference.Path -in @('guest/images/android/system.img','guest/images/android/system-gapps.img','guest/images/android/vendor.img')){
            $path=Join-Path $Root $reference.Path
            Assert-HabumiPlainPath $path
            if($reference.SHA256 -notmatch '^[A-Fa-f0-9]{64}$'){throw 'Invalid reference image identity.'}
            Assert-HabumiFile $path $reference
        }
    }
}
function Backup-HabumiProfile {
    [CmdletBinding()]param([Parameter(Mandatory)][string]$ProfileRoot,[Parameter(Mandatory)][string]$Destination)
    $root=Get-HabumiRoot $ProfileRoot;$guard=Open-HabumiDataGuard $root
    try{
        $destination=Get-HabumiNewDestination $Destination $root
        $files=@(Get-HabumiDataFiles $root)
        if($files -notcontains 'runtime/bin/config.txt'){throw 'Profile configuration is missing.'}
        $bytes=0L;foreach($relative in $files){$bytes+=(Get-Item -LiteralPath (Get-HabumiDataPath $root $relative)).Length}
        Assert-HabumiSpace $destination $bytes
        $stage=$destination+'.partial-'+[guid]::NewGuid().ToString('N')
        New-Item -ItemType Directory -Path $stage|Out-Null
        $manifest=[ordered]@{Schema='habumi.backup.v1';State='Incomplete';Files=@();Metadata=(Get-HabumiMetadata $root)}
        Write-HabumiJson (Join-Path $stage 'backup.json') $manifest
        foreach($relative in $files){
            Write-Progress -Activity 'Backing up Habumi' -Status $relative
            $record=Copy-HabumiFile (Get-HabumiDataPath $root $relative) (Get-HabumiDataPath (Join-Path $stage 'payload') $relative)
            $manifest.Files+=@{Path=$relative;Bytes=$record.Bytes;SHA256=$record.SHA256}
        }
        $manifest.State='Complete';Write-HabumiJson (Join-Path $stage 'backup.json') $manifest
        $null=Read-HabumiBackup $stage
        [IO.Directory]::Move($stage,$destination)
        Write-Progress -Activity 'Backing up Habumi' -Completed
        return [pscustomobject]@{Backup=$destination;Files=$files.Count;Bytes=$bytes}
    }finally{$guard.Dispose()}
}
function Get-HabumiRecovery([string]$Root){
    $marker=Read-HabumiJson (Join-Path $Root 'storage-maintenance.lock')
    if($marker.Schema -ne 'habumi.maintenance.v1' -or $marker.Transaction -notmatch '^[a-f0-9]{32}$'){throw 'Invalid maintenance marker; originals must be inspected before recovery.'}
    $directory=Join-Path $Root ('maintenance/'+$marker.Transaction)
    Assert-HabumiPlainPath $directory
    $journal=Read-HabumiJson (Join-Path $directory 'journal.json')
    if($journal.Schema -ne 'habumi.restore.v1' -or $journal.Transaction -ne $marker.Transaction -or
       $journal.Phase -notin 'Replacing','Recovered','Committed' -or @($journal.Files).Count -eq 0 -or
       @($journal.Files).Count -gt 4096){throw 'Invalid recovery journal.'}
    $seen=@{}
    foreach($file in @($journal.Files)){
        $null=Get-HabumiDataPath $Root $file.Path
        if($seen.ContainsKey([string]$file.Path)){throw 'Duplicate recovery entry.'};$seen[[string]$file.Path]=$true
        if($file.HadOriginal -isnot [bool] -or $file.HasNew -isnot [bool] -or (-not $file.HadOriginal -and -not $file.HasNew)){throw 'Invalid recovery entry flags.'}
        if($file.HadOriginal -and ($file.Original.SHA256 -notmatch '^[A-Fa-f0-9]{64}$' -or [long]$file.Original.Bytes -lt 0)){throw 'Invalid original file record.'}
        if($file.HasNew -and ($file.New.SHA256 -notmatch '^[A-Fa-f0-9]{64}$' -or [long]$file.New.Bytes -lt 0)){throw 'Invalid replacement file record.'}
    }
    return @{Directory=$directory;Journal=$journal}
}
function Repair-HabumiUnderGuard([string]$Root){
    $recovery=Get-HabumiRecovery $Root;$directory=$recovery.Directory;$journal=$recovery.Journal
    if($journal.Phase -eq 'Committed'){
        foreach($file in @($journal.Files)){
            $target=Get-HabumiDataPath $Root $file.Path
            if($file.HasNew){Assert-HabumiFile $target $file.New}
            elseif(Test-Path -LiteralPath $target){throw 'Unexpected file after committed restore.'}
        }
    }else{
        # Verify every original before moving any file during rollback.
        foreach($file in @($journal.Files)){
            if($file.HadOriginal){
                $saved=Get-HabumiDataPath (Join-Path $directory 'original') $file.Path
                if(-not(Test-Path -LiteralPath $saved)){$saved=Get-HabumiDataPath $Root $file.Path}
                Assert-HabumiFile $saved $file.Original
            }
        }
        foreach($file in @($journal.Files)){
            $target=Get-HabumiDataPath $Root $file.Path
            $saved=Get-HabumiDataPath (Join-Path $directory 'original') $file.Path
            if((Test-Path -LiteralPath $saved) -or -not $file.HadOriginal){
                if(Test-Path -LiteralPath $target){
                    $rejected=Get-HabumiDataPath (Join-Path $directory 'interrupted') $file.Path
                    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($rejected)) -Force|Out-Null
                    [IO.File]::Move($target,$rejected)
                }
                if(Test-Path -LiteralPath $saved){
                    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force|Out-Null
                    [IO.File]::Move($saved,$target)
                }
            }
        }
        $journal.Phase='Recovered';Write-HabumiJson (Join-Path $directory 'journal.json') $journal
    }
    [IO.File]::Delete((Join-Path $Root 'storage-maintenance.lock'))
    return [pscustomobject]@{Profile=$Root;Phase=$journal.Phase;RecoveryDirectory=$directory}
}
function Repair-HabumiProfile {
    [CmdletBinding()]param([Parameter(Mandatory)][string]$ProfileRoot)
    $root=Get-HabumiRoot $ProfileRoot;$guard=Open-HabumiDataGuard $root -Recovery
    try{return (Repair-HabumiUnderGuard $root)}finally{$guard.Dispose()}
}
function Restore-HabumiProfile {
    [CmdletBinding()]param([Parameter(Mandatory)][string]$ProfileRoot,[Parameter(Mandatory)][string]$BackupPath)
    $root=Get-HabumiRoot $ProfileRoot;$guard=Open-HabumiDataGuard $root
    $marked=$false
    try{
        $backup=Get-HabumiRoot $BackupPath
        if($backup -eq $root -or $backup.StartsWith(($root+'\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Backup must be outside the profile.'}
        $manifest=Read-HabumiBackup $backup
        Assert-HabumiReferenceImages $root $manifest
        foreach($entry in @($manifest.Files)){
            $target=Get-HabumiDataPath $root $entry.Path
            if(Test-Path -LiteralPath $target -PathType Container){throw 'Restore would replace a directory with a file; profile not replaced.'}
            $parent=[IO.Path]::GetDirectoryName($target)
            while($parent -ne $root){
                if(Test-Path -LiteralPath $parent -PathType Leaf){throw 'Restore would replace a file with a directory; profile not replaced.'}
                $parent=[IO.Path]::GetDirectoryName($parent)
            }
        }
        $bytes=0L;foreach($file in @($manifest.Files)){$bytes+=[long]$file.Bytes};Assert-HabumiSpace $root $bytes
        $id=[guid]::NewGuid().ToString('N');$directory=Join-Path $root ('maintenance/'+$id)
        Assert-HabumiPlainPath $directory
        New-Item -ItemType Directory -Path $directory|Out-Null
        $new=@{}
        foreach($entry in @($manifest.Files)){
            $record=Copy-HabumiFile (Get-HabumiDataPath (Join-Path $backup 'payload') $entry.Path) (Get-HabumiDataPath (Join-Path $directory 'new') $entry.Path)
            if($record.Bytes -ne $entry.Bytes -or $record.SHA256 -ne $entry.SHA256){throw 'Backup changed while staging; profile not replaced.'}
            $new[[string]$entry.Path]=$entry
        }
        $original=@(Get-HabumiDataFiles $root);$all=@($original)+@($new.Keys)|Sort-Object -Unique
        $records=@(foreach($relative in $all){
            $exists=$original -contains $relative;$record=$null
            if($exists){$record=Get-HabumiOriginalRecord (Get-HabumiDataPath $root $relative)}
            @{Path=$relative;HadOriginal=$exists;Original=$record;HasNew=$new.ContainsKey($relative);New=$new[$relative]}
        })
        $journal=[ordered]@{Schema='habumi.restore.v1';Transaction=$id;Phase='Replacing';StartedUtc=[DateTime]::UtcNow.ToString('o');BackupManifestSHA256=(Get-FileHash -LiteralPath (Join-Path $backup 'backup.json')).Hash;Files=$records}
        Write-HabumiJson (Join-Path $directory 'journal.json') $journal
        Write-HabumiJson (Join-Path $root 'storage-maintenance.lock') @{Schema='habumi.maintenance.v1';Transaction=$id}
        $marked=$true
        foreach($relative in $original){
            $saved=Get-HabumiDataPath (Join-Path $directory 'original') $relative
            New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($saved)) -Force|Out-Null
            [IO.File]::Move((Get-HabumiDataPath $root $relative),$saved)
        }
        foreach($entry in @($manifest.Files)){
            $target=Get-HabumiDataPath $root $entry.Path
            New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force|Out-Null
            [IO.File]::Move((Get-HabumiDataPath (Join-Path $directory 'new') $entry.Path),$target)
        }
        $journal.Phase='Committed';Write-HabumiJson (Join-Path $directory 'journal.json') $journal
        [IO.File]::Delete((Join-Path $root 'storage-maintenance.lock'));$marked=$false
        return [pscustomobject]@{Profile=$root;Phase='Committed';PreviousFiles=(Join-Path $directory 'original');Transaction=$id}
    }catch{
        $failure=$_
        if($marked){try{$null=Repair-HabumiUnderGuard $root}catch{throw ('Recovery required; originals retained in '+$directory+'. '+$_.Exception.Message)}}
        throw $failure
    }finally{$guard.Dispose()}
}
function Export-HabumiDiagnostics {
    [CmdletBinding()]param([Parameter(Mandatory)][string]$ProfileRoot,[Parameter(Mandatory)][string]$Destination)
    $root=Get-HabumiRoot $ProfileRoot;$guard=Open-HabumiDataGuard $root
    try{
        $destination=Get-HabumiNewDestination $Destination $root
        Assert-HabumiSpace $destination 64MB
        $stage=$destination+'.partial-'+[guid]::NewGuid().ToString('N')
        New-Item -ItemType Directory -Path $stage|Out-Null
        Write-HabumiJson (Join-Path $stage 'diagnostics.json') @{Schema='habumi.diagnostics.v1';Metadata=(Get-HabumiMetadata $root);Data=@(foreach($relative in @(Get-HabumiDataFiles $root)){@{Path=$relative;Bytes=(Get-Item -LiteralPath (Get-HabumiDataPath $root $relative)).Length}})}
        $logs=Join-Path $root 'guest/logs'
        if(Test-Path -LiteralPath $logs){
            Assert-HabumiPlainPath $logs
            New-Item -ItemType Directory -Path (Join-Path $stage 'logs')|Out-Null
            foreach($file in @(Get-ChildItem -LiteralPath $logs -File|Sort-Object LastWriteTime -Descending|Select-Object -First 16)){
                Assert-HabumiPlainPath $file.FullName
                $inputStream=[IO.File]::Open($file.FullName,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
                try{$inputStream.Position=[Math]::Max(0,$inputStream.Length-4MB);$outputStream=[IO.File]::Create((Join-Path (Join-Path $stage 'logs') $file.Name));try{$inputStream.CopyTo($outputStream);$outputStream.Flush($true)}finally{$outputStream.Dispose()}}finally{$inputStream.Dispose()}
            }
        }
        [IO.Directory]::Move($stage,$destination)
        return [pscustomobject]@{Diagnostics=$destination;ContainsDataImages=$false}
    }finally{$guard.Dispose()}
}
Export-ModuleMember -Function Backup-HabumiProfile,Restore-HabumiProfile,Repair-HabumiProfile,Export-HabumiDiagnostics
