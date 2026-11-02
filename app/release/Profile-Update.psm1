# SPDX-License-Identifier: GPL-2.0-only
Set-StrictMode -Version 2
$ErrorActionPreference='Stop'
$script:DataModule=Import-Module (Join-Path $PSScriptRoot 'Profile-Data.psm1') -Force -PassThru

function Assert-HabumiUpdatePlainPath([string]$Path){
    $current=[IO.Path]::GetFullPath($Path)
    while($current){
        if(Test-Path -LiteralPath $current){
            if(([IO.File]::GetAttributes($current) -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Update paths cannot contain links: $current"}
        }
        $current=[IO.Path]::GetDirectoryName($current)
    }
}

function Get-HabumiUpdateRoot([string]$ProfileRoot){
    if(-not $ProfileRoot){throw 'A Habumi profile directory is required.'}
    $root=[IO.Path]::GetFullPath($ProfileRoot).TrimEnd('\','/')
    Assert-HabumiUpdatePlainPath $root
    if(-not(Test-Path -LiteralPath $root -PathType Container) -or
       -not(Test-Path -LiteralPath (Join-Path $root 'runtime/bin') -PathType Container)){
        throw 'A prepared Habumi profile directory is required.'
    }
    return $root
}

function Test-HabumiUpdatePath([string]$Relative){
    if(-not $Relative -or $Relative.Contains('\') -or $Relative.Contains(':') -or
       $Relative.StartsWith('/') -or $Relative.EndsWith('/')){return $false}
    $parts=$Relative.Split('/')
    foreach($part in $parts){
        foreach($character in $part.ToCharArray()){if([char]::IsControl($character)){return $false}}
        if(-not $part -or $part -in '.','..' -or $part.EndsWith('.') -or $part.EndsWith(' ') -or
           $part.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0 -or
           $part -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)'){return $false}
    }
    $lower=$Relative.ToLowerInvariant()
    $rootFiles=@('habumi.exe','start-habumi.ps1','start.cmd','start-a1.cmd','readme.txt',
        'release-profile.json','package-files.json','qemu-inputs.json','renderer-inputs.json','software-inputs.json',
        'development-build.json','native-profile.json','app-list-build.json','export-worker.ps1','update-habumi.ps1','update-worker.ps1','manage-data.ps1','profile-data.psm1',
        'profile-growth.ps1','profile-systeminfo.ps1','profile-export.psm1','profile-update.psm1','data-worker.ps1')
    if($parts.Count -eq 1 -and $lower -in $rootFiles){return $true}
    if($lower.StartsWith('licenses/')){return $parts.Count -ge 2}
    if($lower.StartsWith('runtime/bin/') -and $parts.Count -eq 3){
        return $lower -ne 'runtime/bin/qemu-system-aarch64.exe'
    }
    if($lower -in @('runtime/tools/habumi-app-list.jar','runtime/tools/app-list-build.json')){return $true}
    if($lower -eq 'runtime/empty-data.zip'){return $true}
    if($lower.StartsWith('runtime/keymaps/') -and $parts.Count -ge 3){return $true}
    if($lower.StartsWith('runtime/share/qemu/') -and $parts.Count -ge 4){return $true}
    if($lower -in @('guest/images/kernel-guest-arm64','guest/images/android/initramfs.img',
        'guest/maintenance/maintenance-build.json','guest/maintenance/maintenance-initramfs.img')){return $true}
    return $false
}

function Test-HabumiUpdateApplyPath([string]$Relative){
    if(-not (Test-HabumiUpdatePath $Relative)){return $false}
    $lower=$Relative.ToLowerInvariant()
    if($lower -eq 'runtime/bin/config.txt' -or $lower.StartsWith('runtime/keymaps/') -or
       $lower -eq 'package-files.json'){return $false}
    return $true
}

function Get-HabumiUpdateFullPath([string]$Root,[string]$Relative){
    if(-not (Test-HabumiUpdatePath $Relative)){throw "Package contains a path outside the software allowlist: $Relative"}
    $full=[IO.Path]::GetFullPath((Join-Path $Root ($Relative.Replace('/','\'))))
    if(-not $full.StartsWith(($Root.TrimEnd('\')+'\'),[StringComparison]::OrdinalIgnoreCase)){
        throw 'Package path escaped the profile.'
    }
    Assert-HabumiUpdatePlainPath $full
    return $full
}

function Get-HabumiUpdateTransactionRoot([string]$Root,[string]$Transaction){
    if($Transaction -notmatch '^[a-f0-9]{32}$'){throw 'Invalid update transaction identifier.'}
    $directory=[IO.Path]::GetFullPath((Join-Path $Root ('.habumi-updates/'+$Transaction)))
    if(-not $directory.StartsWith(($Root.TrimEnd('\')+'\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Update transaction escaped its profile.'}
    Assert-HabumiUpdatePlainPath $directory
    if(-not(Test-Path -LiteralPath $directory -PathType Container)){throw 'Update transaction does not exist.'}
    return $directory
}

function Remove-HabumiUpdateStaging([string]$Root,[string]$Transaction){
    if($Transaction -notmatch '^[a-f0-9]{32}$'){return}
    $directory=[IO.Path]::GetFullPath((Join-Path $Root ('.habumi-updates/'+$Transaction)))
    if(-not $directory.StartsWith(($Root.TrimEnd('\')+'\.habumi-updates\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Staging cleanup escaped the update directory.'}
    if(-not(Test-Path -LiteralPath $directory)){return}
    Assert-HabumiUpdatePlainPath $directory
    foreach($item in Get-ChildItem -LiteralPath $directory -Force -Recurse){Assert-HabumiUpdatePlainPath $item.FullName}
    Remove-Item -LiteralPath $directory -Recurse -Force
}

function Write-HabumiUpdateJson([string]$Path,$Value){
    Assert-HabumiUpdatePlainPath $Path
    & $script:DataModule {param($file,$value) Write-HabumiJson $file $value} $Path $Value
}

function Read-HabumiUpdateJson([string]$Path){
    Assert-HabumiUpdatePlainPath $Path
    return (& $script:DataModule {param($file) Read-HabumiJson $file} $Path)
}

function Open-HabumiUpdateGuard([string]$Root){
    if(Test-Path -LiteralPath (Join-Path $Root 'storage-maintenance.lock')){throw 'Interrupted storage maintenance must be recovered before software updates.'}
    return (& $script:DataModule {param($profile) Open-HabumiDataGuard $profile -Recovery -SoftwareUpdate} $Root)
}

function Get-HabumiUpdateFileRecord([string]$Path){
    Assert-HabumiUpdatePlainPath $Path
    if(-not(Test-Path -LiteralPath $Path -PathType Leaf)){throw "Expected update file is missing: $Path"}
    $file=Get-Item -LiteralPath $Path
    return [pscustomobject]@{Bytes=[long]$file.Length;SHA256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()}
}

function Assert-HabumiUpdateFile([string]$Path,$Record){
    $actual=Get-HabumiUpdateFileRecord $Path
    if($actual.Bytes -ne [long]$Record.Bytes -or $actual.SHA256 -ne [string]$Record.SHA256){
        throw "Update file differs from its recorded identity: $Path"
    }
}

function Read-HabumiPackageInventory([IO.Compression.ZipArchive]$Zip,[string]$ExternalManifest){
    $embedded=@($Zip.Entries|Where-Object {$_ -and $_.FullName -ceq 'package-files.json'})
    if($embedded.Count -gt 1){throw 'Package has duplicate embedded inventories.'}
    if($embedded.Count -eq 1){
        if($embedded[0].Length -gt 2MB){throw 'Embedded package inventory exceeds the supported size.'}
        $stream=$embedded[0].Open()
        try{
            $reader=[IO.StreamReader]::new($stream,[Text.Encoding]::UTF8,$true)
            try{
                $builder=[Text.StringBuilder]::new();$characters=New-Object char[] 4096
                while(($read=$reader.Read($characters,0,$characters.Length)) -gt 0){
                    if($builder.Length+$read -gt 2MB){throw 'Expanded inventory exceeds the supported size.'}
                    [void]$builder.Append($characters,0,$read)
                }
                $json=$builder.ToString()
            }finally{$reader.Dispose()}
        }finally{$stream.Dispose()}
    }else{
        if(-not $ExternalManifest){throw 'Supply the release package-files.json inventory.'}
        Assert-HabumiUpdatePlainPath $ExternalManifest
        if(-not(Test-Path -LiteralPath $ExternalManifest -PathType Leaf) -or (Get-Item -LiteralPath $ExternalManifest).Length -gt 16MB){
            throw 'Package inventory is missing or exceeds the supported size.'
        }
        $json=[IO.File]::ReadAllText($ExternalManifest)
    }
    try{$parsed=ConvertFrom-Json -InputObject $json;$records=@($parsed);if($records.Count -eq 1 -and $records[0] -is [Array]){$records=@($records[0])}}catch{throw 'Package inventory is invalid JSON.'}
    if(-not $records.Count -or $records.Count -gt 10000){throw 'Package inventory has an invalid file count.'}
    $map=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $total=0L
    foreach($record in $records){
        $relative=[string]$record.Path
        if(-not (Test-HabumiUpdatePath -Relative $relative) -or -not $seen.Add($relative)){throw "Inventory contains an unsafe or duplicate path: $relative"}
        if($record.SHA256 -notmatch '^[A-Fa-f0-9]{64}$'){throw "Inventory has an invalid SHA-256: $relative"}
        $bytes=0L
        if(-not [long]::TryParse([string]$record.Bytes,[Globalization.NumberStyles]::None,[Globalization.CultureInfo]::InvariantCulture,[ref]$bytes) -or $bytes -lt 0){
            throw "Inventory has an invalid file size: $relative"
        }
        $record|Add-Member -NotePropertyName NormalizedBytes -NotePropertyValue $bytes -Force
        if($bytes -gt 2GB){throw 'A software file exceeds the supported size.'}
        $total+=$bytes
        if($total -gt 8GB){throw 'Package exceeds the supported uncompressed size.'}
        $map.Add($relative,$record)
    }
    return @{Records=$records;Map=$map;Embedded=($embedded.Count -eq 1);EmbeddedEntry=if($embedded.Count){$embedded[0]}else{$null};TotalBytes=$total}
}

function Get-HabumiZipInventory([IO.Compression.ZipArchive]$Zip,$Inventory){
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $actual=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
    foreach($entry in $Zip.Entries){
        $relative=[string]$entry.FullName
        if($relative.EndsWith('/')){throw 'Directory entries are not allowed in the release archive.'}
        if(-not (Test-HabumiUpdatePath $relative) -or -not $seen.Add($relative)){throw "Archive contains an unsafe, duplicate or unsupported path: $relative"}
        $unixType=($entry.ExternalAttributes -shr 16) -band 0xF000
        if($unixType -notin @(0,0x8000) -or ($entry.ExternalAttributes -band 0x400)){throw "Archive contains a link or unsupported entry type: $relative"}
        if(-not $Inventory.Embedded -or $relative -cne 'package-files.json'){
            if(-not $Inventory.Map.ContainsKey($relative)){throw "Archive file is absent from its inventory: $relative"}
            if([long]$entry.Length -ne [long]$Inventory.Map[$relative].NormalizedBytes){throw "Archive length differs from its inventory: $relative"}
            $actual.Add($relative,$entry)
        }
    }
    if($actual.Count -ne $Inventory.Map.Count){throw 'Archive and package inventory do not contain the same files.'}
    foreach($path in $Inventory.Map.Keys){if(-not $actual.ContainsKey($path)){throw "Inventory file is absent from archive: $path"}}
    return $actual
}

function Expand-HabumiVerifiedPackage([IO.Compression.ZipArchive]$Zip,$Inventory,$ZipInventory,[string]$Stage){
    foreach($entry in $Zip.Entries){
        $relative=[string]$entry.FullName
        $destination=Get-HabumiUpdateFullPath $Stage $relative
        $parent=[IO.Path]::GetDirectoryName($destination)
        [IO.Directory]::CreateDirectory($parent)|Out-Null
        Assert-HabumiUpdatePlainPath $parent
        $input=$entry.Open()
        $output=$null
        try{
            $output=[IO.File]::Open($destination,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
            $buffer=New-Object byte[] 1048576;$copied=0L
            while(($count=$input.Read($buffer,0,$buffer.Length)) -gt 0){
                $copied+=$count
                if($copied -gt $entry.Length){throw 'Archive entry expanded beyond its declared size.'}
                $output.Write($buffer,0,$count)
            }
            if($copied -ne $entry.Length){throw 'Archive entry ended before its declared size.'}
            $output.Flush($true)
        }finally{if($output){$output.Dispose()};$input.Dispose()}
        if($relative -ceq 'package-files.json' -and $Inventory.Embedded){continue}
        Assert-HabumiUpdateFile $destination $Inventory.Map[$relative]
    }
}

function Assert-HabumiReleaseProfile([string]$Stage,$Inventory){
    $path=Join-Path $Stage 'release-profile.json'
    if(-not(Test-Path -LiteralPath $path -PathType Leaf)){throw 'Release profile is missing from package.'}
    try{$profile=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json}catch{throw 'Release profile is invalid JSON.'}
    if($profile.Version -notmatch '^\d+\.\d+\.\d+(-[a-z0-9.]+)?$' -or $profile.UserDataIncluded -ne $false){
        throw 'Release profile version or user-data declaration is invalid.'
    }
    $critical=@($profile.CriticalFiles)
    if(-not $critical.Count -or $critical.Count -gt 256){throw 'Release profile has an invalid critical-file inventory.'}
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($record in $critical){
        $relative=[string]$record.Path
        if(-not (Test-HabumiUpdatePath $relative) -or -not $seen.Add($relative) -or
           $record.SHA256 -notmatch '^[A-Fa-f0-9]{64}$' -or -not $Inventory.Map.ContainsKey($relative)){
            throw "Release profile has an invalid critical file: $relative"
        }
        if($Inventory.Map[$relative].SHA256 -ne $record.SHA256){throw "Critical hash differs from package inventory: $relative"}
        Assert-HabumiUpdateFile (Get-HabumiUpdateFullPath $Stage $relative) @{Bytes=$Inventory.Map[$relative].NormalizedBytes;SHA256=$record.SHA256}
    }
    return $profile
}

function Get-HabumiUpdateJournal([string]$Root,[string]$Transaction){
    $directory=Get-HabumiUpdateTransactionRoot $Root $Transaction
    $path=Join-Path $directory 'journal.json'
    $journal=Read-HabumiUpdateJson $path
    if($journal.Schema -ne 'habumi.update.v1' -or $journal.Transaction -ne $Transaction -or
       $journal.Phase -notin @('Prepared','Applying','Committed','RollingBack','RolledBack','Recovered') -or
       @($journal.Files).Count -eq 0 -or @($journal.Files).Count -gt 10000){throw 'Invalid software update journal.'}
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($file in @($journal.Files)){
        $relative=[string]$file.Path
        if(-not (Test-HabumiUpdateApplyPath $relative) -or -not $seen.Add($relative) -or $file.HadOriginal -isnot [bool]){
            throw 'Update journal contains an unsafe or duplicate file entry.'
        }
        foreach($record in @($file.Original,$file.New)){
            if($record -and ($record.SHA256 -notmatch '^[A-Fa-f0-9]{64}$' -or [long]$record.Bytes -lt 0)){
                throw 'Update journal contains an invalid file identity.'
            }
        }
        if(-not $file.New -or ($file.HadOriginal -and -not $file.Original) -or (-not $file.HadOriginal -and $file.Original)){
            throw 'Update journal has inconsistent original or replacement records.'
        }
    }
    return @{Directory=$directory;Journal=$journal}
}

function Get-HabumiUpdateMarker([string]$Root){
    $path=Join-Path $Root 'software-update.lock'
    if(-not(Test-Path -LiteralPath $path -PathType Leaf)){return $null}
    $marker=Read-HabumiUpdateJson $path
    if($marker.Schema -ne 'habumi.update-lock.v1' -or $marker.Transaction -notmatch '^[a-f0-9]{32}$'){
        throw 'Invalid software update marker; recovery is required.'
    }
    return $marker
}

function Set-HabumiUpdateMarker([string]$Root,[string]$Transaction){
    Write-HabumiUpdateJson (Join-Path $Root 'software-update.lock') @{Schema='habumi.update-lock.v1';Transaction=$Transaction}
}

function Clear-HabumiUpdateMarker([string]$Root,[string]$Transaction){
    $marker=Get-HabumiUpdateMarker $Root
    if($marker -and $marker.Transaction -ne $Transaction){throw 'A different software update owns the recovery marker.'}
    if($marker){[IO.File]::Delete((Join-Path $Root 'software-update.lock'))}
}

function Assert-HabumiUpdateDestinations([string]$Root,$Files){
    foreach($file in $Files){
        $target=Get-HabumiUpdateFullPath $Root $file.Path
        if(Test-Path -LiteralPath $target -PathType Container){throw "Package file conflicts with a profile directory: $($file.Path)"}
        $parent=[IO.Path]::GetDirectoryName($target)
        while($parent -and $parent.StartsWith(($Root+'\'),[StringComparison]::OrdinalIgnoreCase)){
            if(Test-Path -LiteralPath $parent -PathType Leaf){throw "Package directory conflicts with a profile file: $($file.Path)"}
            $parent=[IO.Path]::GetDirectoryName($parent)
        }
    }
}

function Copy-HabumiUpdateOriginals([string]$Root,[string]$Directory,$Files){
    $previous=Join-Path $Directory 'previous'
    $records=@()
    foreach($file in $Files){
        $target=Get-HabumiUpdateFullPath $Root $file.Path
        $present=Test-Path -LiteralPath $target -PathType Leaf
        $original=$null
        if($present){
            $original=Get-HabumiUpdateFileRecord $target
            $saved=Get-HabumiUpdateFullPath $previous $file.Path
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($saved))|Out-Null
            [IO.File]::Copy($target,$saved,$false)
            Assert-HabumiUpdateFile $saved $original
        }
        $records+=@{Path=$file.Path;HadOriginal=[bool]$present;Original=$original;New=@{Bytes=[long]$file.Bytes;SHA256=[string]$file.SHA256}}
    }
    return $records
}

function Get-HabumiUpdateRecordState([string]$Root,[string]$Directory,$File){
    $target=Get-HabumiUpdateFullPath $Root $File.Path
    $exists=Test-Path -LiteralPath $target -PathType Leaf
    if(Test-Path -LiteralPath $target -PathType Container){throw "Update target became a directory: $($File.Path)"}
    if($File.HadOriginal){
        $saved=Get-HabumiUpdateFullPath (Join-Path $Directory 'previous') $File.Path
        Assert-HabumiUpdateFile $saved $File.Original
    }
    if(-not $exists){return @{State='Missing';Target=$target}}
    $actual=Get-HabumiUpdateFileRecord $target
    if($File.HadOriginal -and $actual.Bytes -eq [long]$File.Original.Bytes -and $actual.SHA256 -eq $File.Original.SHA256){return @{State='Original';Target=$target}}
    if($actual.Bytes -eq [long]$File.New.Bytes -and $actual.SHA256 -eq $File.New.SHA256){return @{State='New';Target=$target}}
    throw "Installed file changed outside this update: $($File.Path)"
}

function Restore-HabumiUpdateUnderGuard([string]$Root,[string]$Transaction,[string]$FinalPhase){
    $context=Get-HabumiUpdateJournal $Root $Transaction
    $directory=$context.Directory;$journal=$context.Journal
    $states=@{}
    foreach($file in @($journal.Files)){$states[[string]$file.Path]=Get-HabumiUpdateRecordState $Root $directory $file}
    foreach($file in @($journal.Files)){
        $state=$states[[string]$file.Path];$target=$state.Target
        if($state.State -eq 'New'){
            $rejected=Get-HabumiUpdateFullPath (Join-Path $directory 'rejected') $file.Path
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($rejected))|Out-Null
            if(Test-Path -LiteralPath $rejected){throw 'Rejected update file already exists; recovery stopped.'}
            [IO.File]::Move($target,$rejected)
        }
        if($file.HadOriginal -and $state.State -ne 'Original'){
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))|Out-Null
            $temporary=$target+'.habumi-rollback-'+[guid]::NewGuid().ToString('N')+'.partial'
            try{
                [IO.File]::Copy((Get-HabumiUpdateFullPath (Join-Path $directory 'previous') $file.Path),$temporary,$false)
                Assert-HabumiUpdateFile $temporary $file.Original
                [IO.File]::Move($temporary,$target)
            }finally{if(Test-Path -LiteralPath $temporary -PathType Leaf){[IO.File]::Delete($temporary)}}
        }elseif(-not $file.HadOriginal -and $state.State -eq 'Missing'){
            continue
        }
    }
    foreach($file in @($journal.Files)){
        $target=Get-HabumiUpdateFullPath $Root $file.Path
        if($file.HadOriginal){Assert-HabumiUpdateFile $target $file.Original}
        elseif(Test-Path -LiteralPath $target){throw 'Rollback left an unexpected new file.'}
    }
    $journal.Phase=$FinalPhase
    Write-HabumiUpdateJson (Join-Path $directory 'journal.json') $journal
    Clear-HabumiUpdateMarker $Root $Transaction
    return [pscustomobject]@{Transaction=$Transaction;Phase=$FinalPhase;Previous=$directory}
}

function Assert-HabumiUpdateCommitted([string]$Root,[string]$Transaction){
    $context=Get-HabumiUpdateJournal $Root $Transaction
    foreach($file in @($context.Journal.Files)){
        $target=Get-HabumiUpdateFullPath $Root $file.Path
        Assert-HabumiUpdateFile $target $file.New
    }
    Clear-HabumiUpdateMarker $Root $Transaction
    return [pscustomobject]@{Transaction=$Transaction;Phase='Committed';Version=$context.Journal.NewVersion}
}

function Assert-HabumiNoLauncher([int]$LauncherProcessId){
    if($LauncherProcessId -gt 0){
        $process=Get-Process -Id $LauncherProcessId -ErrorAction SilentlyContinue
        if($process){throw 'Close the Habumi launcher before applying software updates.'}
    }
}

function Compare-HabumiUpdateVersion([string]$Left,[string]$Right){
    foreach($value in @($Left,$Right)){if($value -notmatch '^\d+\.\d+\.\d+(-[a-z0-9.]+)?$'){throw 'Unsupported release version.'}}
    $leftParts=$Left.Split('-');$rightParts=$Right.Split('-')
    $base=([version]$leftParts[0]).CompareTo([version]$rightParts[0])
    if($base){return $base}
    if($leftParts.Count -eq 1){if($rightParts.Count -eq 1){return 0};return 1}
    if($rightParts.Count -eq 1){return -1}
    $a=$leftParts[1].Split('.');$b=$rightParts[1].Split('.')
    for($i=0;$i -lt [Math]::Min($a.Count,$b.Count);$i++){
        if($a[$i] -ceq $b[$i]){continue}
        $an=$a[$i] -match '^\d+$';$bn=$b[$i] -match '^\d+$'
        if($an -and $bn){return ([bigint]::Parse($a[$i])).CompareTo([bigint]::Parse($b[$i]))}
        if($an){return -1};if($bn){return 1}
        return [string]::CompareOrdinal($a[$i],$b[$i])
    }
    return $a.Count.CompareTo($b.Count)
}

function Assert-HabumiUpdateCompatibility($Current,$Next){
    if((Compare-HabumiUpdateVersion ([string]$Next.Version) ([string]$Current.Version)) -lt 0){throw 'Use Rollback for a retained previous version; installing an older release is not supported.'}
    foreach($key in @('DataToolsSchema','HostArchitecture','GuestArchitecture','GuestImageReference')){
        $old=$Current.PSObject.Properties[$key];$new=$Next.PSObject.Properties[$key]
        if($old -and (-not $new -or $old.Value -cne $new.Value)){throw "Update requires an unsupported profile migration: $key"}
    }
    if(-not $Current.PSObject.Properties['GuestImageReference'] -and $Next.PSObject.Properties['GuestImageReference']){
        if($Current.Version -notin @('0.3.0-rc.1','0.3.0-rc.2') -or $Next.GuestImageReference -cne 'waydroid-13-20260927-arm64-only'){
            throw 'Installed Android image compatibility is unknown. Use a separate portable installation.'
        }
    }
}

function Find-HabumiRollbackTransaction([string]$Root){
    $profile=Read-HabumiUpdateJson (Join-Path $Root 'release-profile.json')
    if(-not $profile.Version){throw 'Installed profile version is missing.'}
    $updates=Join-Path $Root '.habumi-updates'
    Assert-HabumiUpdatePlainPath $updates
    if(-not(Test-Path -LiteralPath $updates -PathType Container)){throw 'No retained software version is available.'}
    $matches=@()
    foreach($directory in Get-ChildItem -LiteralPath $updates -Directory -Force){
        if($directory.Name -notmatch '^[a-f0-9]{32}$'){continue}
        Assert-HabumiUpdatePlainPath $directory.FullName
        try{$context=Get-HabumiUpdateJournal $Root $directory.Name}catch{continue}
        if($context.Journal.Phase -ne 'Committed' -or [string]$context.Journal.NewVersion -ne [string]$profile.Version){continue}
        $started=[DateTime]::MinValue
        if(-not [DateTime]::TryParse([string]$context.Journal.StartedUtc,[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::RoundtripKind,[ref]$started)){
            throw 'A committed update has an invalid timestamp; select its transaction explicitly.'
        }
        $matches+=@{Transaction=$directory.Name;Started=$started}
    }
    if(-not $matches.Count){throw 'No committed previous software version matches the installed release.'}
    $ordered=@($matches|Sort-Object Started -Descending)
    if($ordered.Count -gt 1 -and $ordered[0].Started -eq $ordered[1].Started){throw 'More than one retained version is equally recent; choose a transaction explicitly.'}
    return [string]$ordered[0].Transaction
}

function Install-HabumiUpdate {
    [CmdletBinding()]param(
        [Parameter(Mandatory)][string]$ProfileRoot,
        [Parameter(Mandatory)][string]$ArchivePath,
        [Parameter(Mandatory)][string]$ExpectedSHA256,
        [string]$PackageFilesPath,
        [int]$LauncherProcessId=0
    )
    $root=Get-HabumiUpdateRoot $ProfileRoot
    Assert-HabumiNoLauncher $LauncherProcessId
    if($ExpectedSHA256 -notmatch '^[A-Fa-f0-9]{64}$'){throw 'Supply the release ZIP SHA-256.'}
    Assert-HabumiUpdatePlainPath $ArchivePath
    if(-not(Test-Path -LiteralPath $ArchivePath -PathType Leaf)){throw 'Release ZIP does not exist.'}
    if((Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash -ne $ExpectedSHA256.ToUpperInvariant()){
        throw 'Release ZIP SHA-256 does not match; profile was not changed.'
    }
    if(Test-Path -LiteralPath (Join-Path $root 'software-update.lock')){throw 'Recover the pending software update before starting another.'}
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $transaction=[guid]::NewGuid().ToString('N')
    $transactionRoot=Join-Path $root ('.habumi-updates/'+$transaction)
    Assert-HabumiUpdatePlainPath $transactionRoot
    [IO.Directory]::CreateDirectory($transactionRoot)|Out-Null
    $candidate=Join-Path $transactionRoot 'candidate'
    [IO.Directory]::CreateDirectory($candidate)|Out-Null
    $zip=$null;$guard=$null;$markerWritten=$false;$committed=$false
    try{
        $zip=[IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($ArchivePath))
        if($zip.Entries.Count -gt 10000){throw 'Release archive has too many files.'}
        $inventory=Read-HabumiPackageInventory $zip $PackageFilesPath
        $stageDrive=[IO.DriveInfo]::new([IO.Path]::GetPathRoot($root))
        if($stageDrive.AvailableFreeSpace -lt $inventory.TotalBytes+16MB){throw 'Not enough space to verify the staged software package.'}
        $zipInventory=Get-HabumiZipInventory $zip $inventory
        Expand-HabumiVerifiedPackage $zip $inventory $zipInventory $candidate
        $profile=Assert-HabumiReleaseProfile $candidate $inventory
        $software=@(foreach($record in $inventory.Records){if(Test-HabumiUpdateApplyPath ([string]$record.Path)){[pscustomobject]@{Path=[string]$record.Path;Bytes=[long]$record.NormalizedBytes;SHA256=([string]$record.SHA256).ToUpperInvariant()}}})
        if(-not $software.Count){throw 'Release package has no software files to install.'}
        $null=Assert-HabumiUpdateDestinations $root $software

        $guard=Open-HabumiUpdateGuard $root
        if(Get-HabumiUpdateMarker $root){throw 'Recover the pending software update before applying another.'}
        $currentProfile=Read-HabumiUpdateJson (Join-Path $root 'release-profile.json')
        if(-not $currentProfile.Version){throw 'Installed profile version is missing.'}
        Assert-HabumiUpdateCompatibility $currentProfile $profile
        $originalBytes=0L
        foreach($file in $software){
            $target=Get-HabumiUpdateFullPath $root $file.Path
            if(Test-Path -LiteralPath $target -PathType Leaf){$originalBytes+=(Get-Item -LiteralPath $target).Length}
        }
        $drive=[IO.DriveInfo]::new([IO.Path]::GetPathRoot($root))
        if($drive.AvailableFreeSpace -lt $originalBytes+16MB){throw 'Not enough space to retain the previous software version.'}
        $records=Copy-HabumiUpdateOriginals $root $transactionRoot $software
        $journal=[ordered]@{Schema='habumi.update.v1';Transaction=$transaction;Phase='Prepared';StartedUtc=[DateTime]::UtcNow.ToString('o');PreviousVersion=[string]$currentProfile.Version;NewVersion=[string]$profile.Version;Files=$records}
        Write-HabumiUpdateJson (Join-Path $transactionRoot 'journal.json') $journal
        Set-HabumiUpdateMarker $root $transaction;$markerWritten=$true
        $journal.Phase='Applying';Write-HabumiUpdateJson (Join-Path $transactionRoot 'journal.json') $journal
        foreach($file in $records){
            $target=Get-HabumiUpdateFullPath $root $file.Path
            $staged=Get-HabumiUpdateFullPath $candidate $file.Path
            Assert-HabumiUpdateFile $staged $file.New
            if($file.HadOriginal){Assert-HabumiUpdateFile $target $file.Original}
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))|Out-Null
            if(Test-Path -LiteralPath $target -PathType Leaf){[IO.File]::Replace($staged,$target,[NullString]::Value)}
            else{[IO.File]::Move($staged,$target)}
        }
        foreach($file in $records){Assert-HabumiUpdateFile (Get-HabumiUpdateFullPath $root $file.Path) $file.New}
        $journal.Phase='Committed';$journal|ConvertTo-Json -Depth 16|Out-Null
        Write-HabumiUpdateJson (Join-Path $transactionRoot 'journal.json') $journal
        $committed=$true
        Clear-HabumiUpdateMarker $root $transaction;$markerWritten=$false
        return [pscustomobject]@{Transaction=$transaction;PreviousVersion=$currentProfile.Version;Version=$profile.Version;Files=$software.Count;Previous=$transactionRoot}
    }catch{
        $failure=$_
        if($markerWritten -and -not $committed){
            try{$null=Restore-HabumiUpdateUnderGuard $root $transaction 'Recovered';$markerWritten=$false}
            catch{throw ('Update failed and rollback is pending; originals are retained in '+$transactionRoot+'. '+$_.Exception.Message)}
        }
        throw $failure
    }finally{
        if($zip){$zip.Dispose()}
        if($guard){$guard.Dispose()}
        if(-not $markerWritten -and -not $committed){
            try{if(-not (Get-HabumiUpdateMarker $root)){Remove-HabumiUpdateStaging $root $transaction}}catch{}
        }
    }
}

function Recover-HabumiUpdate {
    [CmdletBinding()]param([Parameter(Mandatory)][string]$ProfileRoot)
    $root=Get-HabumiUpdateRoot $ProfileRoot
    $guard=Open-HabumiUpdateGuard $root
    try{
        $marker=Get-HabumiUpdateMarker $root
        if(-not $marker){throw 'No interrupted software update needs recovery.'}
        $context=Get-HabumiUpdateJournal $root $marker.Transaction
        if($context.Journal.Phase -eq 'Committed'){return (Assert-HabumiUpdateCommitted $root $marker.Transaction)}
        return (Restore-HabumiUpdateUnderGuard $root $marker.Transaction 'Recovered')
    }finally{$guard.Dispose()}
}

function Rollback-HabumiUpdate {
    [CmdletBinding()]param(
        [Parameter(Mandatory)][string]$ProfileRoot,
        [string]$Transaction
    )
    $root=Get-HabumiUpdateRoot $ProfileRoot
    $guard=Open-HabumiUpdateGuard $root
    try{
        if(-not $Transaction){$Transaction=Find-HabumiRollbackTransaction $root}
        if($Transaction -notmatch '^[a-f0-9]{32}$'){throw 'Invalid update transaction identifier.'}
        if(Get-HabumiUpdateMarker $root){throw 'Recover the pending software update before rolling back a version.'}
        $context=Get-HabumiUpdateJournal $root $Transaction
        if($context.Journal.Phase -eq 'RolledBack'){return [pscustomobject]@{Transaction=$Transaction;Phase='RolledBack';Previous=$context.Directory}}
        if($context.Journal.Phase -ne 'Committed'){throw 'Only a committed software update can be rolled back.'}
        foreach($file in @($context.Journal.Files)){
            $target=Get-HabumiUpdateFullPath $root $file.Path
            Assert-HabumiUpdateFile $target $file.New
            if($file.HadOriginal){Assert-HabumiUpdateFile (Get-HabumiUpdateFullPath (Join-Path $context.Directory 'previous') $file.Path) $file.Original}
        }
        Set-HabumiUpdateMarker $root $Transaction
        $context.Journal.Phase='RollingBack'
        Write-HabumiUpdateJson (Join-Path $context.Directory 'journal.json') $context.Journal
        return (Restore-HabumiUpdateUnderGuard $root $Transaction 'RolledBack')
    }finally{$guard.Dispose()}
}

Export-ModuleMember -Function Install-HabumiUpdate,Recover-HabumiUpdate,Rollback-HabumiUpdate
