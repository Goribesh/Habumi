# SPDX-License-Identifier: GPL-2.0-only
Set-StrictMode -Version 2
$ErrorActionPreference='Stop'

function Assert-HabumiExportPlainPath([string]$Path){
    $current=[IO.Path]::GetFullPath($Path)
    while($current){
        if(Test-Path -LiteralPath $current){
            if(([IO.File]::GetAttributes($current) -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Export paths cannot contain links: $current"}
        }
        $current=[IO.Path]::GetDirectoryName($current)
    }
}

function Get-HabumiExportRoot([string]$ProfileRoot){
    if(-not $ProfileRoot){throw 'A Habumi profile directory is required.'}
    $root=[IO.Path]::GetFullPath($ProfileRoot).TrimEnd('\','/')
    Assert-HabumiExportPlainPath $root
    if(-not(Test-Path -LiteralPath $root -PathType Container)){throw 'Habumi profile directory does not exist.'}
    return $root
}

function Get-HabumiExportPort([string]$Root,[int]$RequestedPort){
    $port=15555
    if($RequestedPort){$port=$RequestedPort}else{
        $config=Join-Path $Root 'runtime/bin/config.txt'
        if(Test-Path -LiteralPath $config -PathType Leaf){
            Assert-HabumiExportPlainPath $config
            $text=[IO.File]::ReadAllText($config)
            foreach($line in ($text -split "`r?`n")){
                if($line -match '^\s*adb_port\s*=\s*([0-9]+)\s*(?:#.*)?$'){
                    $candidate=0
                    if([int]::TryParse($matches[1],[Globalization.NumberStyles]::None,[Globalization.CultureInfo]::InvariantCulture,[ref]$candidate) -and
                       $candidate -ge 1024 -and $candidate -le 65535 -and ($candidate -lt 5554 -or $candidate -gt 5585)){$port=$candidate}
                }
            }
        }
    }
    if($port -lt 1024 -or $port -gt 65535 -or ($port -ge 5554 -and $port -le 5585)){
        throw 'The configured ADB port is invalid or reserved.'
    }
    return $port
}

function Assert-HabumiGuestExportPath([string]$GuestPath){
    if(-not $GuestPath -or $GuestPath.IndexOf([char]0) -ge 0 -or $GuestPath.Contains('\')){throw 'Choose a file under /sdcard/ or /storage/emulated/0/.'}
    foreach($character in $GuestPath.ToCharArray()){
        if([char]::IsControl($character)){throw 'The Android path contains a control character.'}
    }
    $prefix=if($GuestPath.StartsWith('/sdcard/',[StringComparison]::Ordinal)){'/sdcard/'}
        elseif($GuestPath.StartsWith('/storage/emulated/0/',[StringComparison]::Ordinal)){'/storage/emulated/0/'}
        else{throw 'Export is limited to /sdcard/ and /storage/emulated/0/.'}
    $relative=$GuestPath.Substring($prefix.Length)
    if(-not $relative){throw 'Choose a file below the Android storage root.'}
    foreach($segment in $relative.Split('/')){
        if(-not $segment -or $segment -in '.','..'){throw 'The Android path contains an empty or unsafe segment.'}
    }
}

function Quote-HabumiWindowsArgument([string]$Argument){
    if($null -eq $Argument){$Argument=''}
    if($Argument.Length -gt 0 -and $Argument -notmatch '[\s"]'){return $Argument}
    $builder=[Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $slashes=0
    foreach($character in $Argument.ToCharArray()){
        if($character -eq [char]92){$slashes++;continue}
        if($character -eq [char]34){
            if($slashes){[void]$builder.Append(('\' * (2*$slashes+1) -join ''))}
            else{[void]$builder.Append('\')}
            [void]$builder.Append('"');$slashes=0;continue
        }
        if($slashes){[void]$builder.Append(('\' * $slashes -join ''));$slashes=0}
        [void]$builder.Append($character)
    }
    if($slashes){[void]$builder.Append(('\' * (2*$slashes) -join ''))}
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Quote-HabumiPosixArgument([string]$Argument){
    return "'"+$Argument.Replace("'","'\''")+"'"
}

function Invoke-HabumiExportAdb([string]$Adb,[string[]]$Arguments,[int]$TimeoutSeconds){
    $start=[Diagnostics.ProcessStartInfo]::new()
    $start.FileName=$Adb
    $start.Arguments=(($Arguments|ForEach-Object {Quote-HabumiWindowsArgument $_}) -join ' ')
    $start.WorkingDirectory=[IO.Path]::GetDirectoryName($Adb)
    $start.UseShellExecute=$false
    $start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true
    $start.RedirectStandardError=$true
    $start.StandardOutputEncoding=[Text.Encoding]::UTF8
    $start.StandardErrorEncoding=[Text.Encoding]::UTF8
    $process=[Diagnostics.Process]::new()
    $process.StartInfo=$start
    try{
        if(-not $process.Start()){throw 'Could not start the profile ADB client.'}
        $stdoutTask=$process.StandardOutput.ReadToEndAsync()
        $stderrTask=$process.StandardError.ReadToEndAsync()
        if(-not $process.WaitForExit($TimeoutSeconds*1000)){
            try{$process.Kill();$null=$process.WaitForExit(5000)}catch{}
            throw 'ADB export operation timed out; only its client process was stopped.'
        }
        $process.WaitForExit()
        $stdout=$stdoutTask.GetAwaiter().GetResult()
        $stderr=$stderrTask.GetAwaiter().GetResult()
        if($process.ExitCode -ne 0){
            $detail=($stderr -replace '[\r\n]+',' ').Trim()
            if($detail.Length -gt 500){$detail=$detail.Substring(0,500)}
            if($detail){throw "ADB export operation failed: $detail"}
            throw "ADB export operation failed with exit code $($process.ExitCode)."
        }
        return $stdout
    }finally{$process.Dispose()}
}

function Get-HabumiRemoteFileRecord([string]$Adb,[int]$Port,[string]$GuestPath){
    $quoted=Quote-HabumiPosixArgument $GuestPath
    $command='sha256sum '+$quoted+' && stat -c ''%s'' '+$quoted
    $output=Invoke-HabumiExportAdb $Adb @('-s',"127.0.0.1:$Port",'shell',$command) 20
    $lines=@($output -split "`r?`n"|Where-Object {$_ -ne ''})
    if($lines.Count -ne 2 -or $lines[0] -notmatch '^([A-Fa-f0-9]{64})\s+'){
        throw 'Android returned invalid file hash or size metadata.'
    }
    $hash=$matches[1].ToUpperInvariant()
    if($lines[1] -notmatch '^[0-9]+$'){throw 'Android returned invalid file hash or size metadata.'}
    $bytes=0L
    if(-not [long]::TryParse($lines[1],[Globalization.NumberStyles]::None,[Globalization.CultureInfo]::InvariantCulture,[ref]$bytes)){
        throw 'Android returned an invalid file size.'
    }
    if($bytes -lt 0 -or $bytes -gt 2GB){throw 'Export is limited to files no larger than 2 GiB.'}
    return [pscustomobject]@{Bytes=$bytes;SHA256=$hash}
}

function Export-HabumiFile {
    [CmdletBinding()]param(
        [Parameter(Mandatory)][string]$ProfileRoot,
        [Parameter(Mandatory)][string]$GuestPath,
        [Parameter(Mandatory)][string]$Destination,
        [int]$AdbPort=0
    )
    $root=Get-HabumiExportRoot $ProfileRoot
    Assert-HabumiGuestExportPath $GuestPath
    $port=Get-HabumiExportPort $root $AdbPort
    $adb=Join-Path $root 'runtime/bin/adb.exe'
    Assert-HabumiExportPlainPath $adb
    if(-not(Test-Path -LiteralPath $adb -PathType Leaf)){throw 'The profile ADB client is missing.'}
    if(-not $Destination -or $Destination.IndexOf([char]0) -ge 0){throw 'Choose a Windows destination file.'}
    $destinationFull=[IO.Path]::GetFullPath($Destination)
    Assert-HabumiExportPlainPath $destinationFull
    if(Test-Path -LiteralPath $destinationFull){throw 'Destination already exists; nothing was overwritten.'}
    $parent=[IO.Path]::GetDirectoryName($destinationFull)
    if(-not(Test-Path -LiteralPath $parent -PathType Container)){throw 'Destination folder must already exist.'}
    $temporary=$destinationFull+'.habumi-export-'+[guid]::NewGuid().ToString('N')+'.partial'
    $owned=$false
    try{
        $reservation=[IO.File]::Open($temporary,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        $reservation.Dispose();$owned=$true
        $before=Get-HabumiRemoteFileRecord $adb $port $GuestPath
        $timeout=[Math]::Min(300,[Math]::Ceiling(20+($before.Bytes/10MB)))
        $null=Invoke-HabumiExportAdb $adb @('-s',"127.0.0.1:$port",'pull',$GuestPath,$temporary) $timeout
        if(-not(Test-Path -LiteralPath $temporary -PathType Leaf)){throw 'ADB did not create the staged export.'}
        Assert-HabumiExportPlainPath $temporary
        $local=Get-Item -LiteralPath $temporary
        if($local.Length -ne $before.Bytes -or $local.Length -gt 2GB){throw 'Downloaded file size differs from Android; export was discarded.'}
        $localHash=(Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash.ToUpperInvariant()
        if($localHash -ne $before.SHA256){throw 'Downloaded file hash differs from Android; export was discarded.'}
        $after=Get-HabumiRemoteFileRecord $adb $port $GuestPath
        if($after.Bytes -ne $before.Bytes -or $after.SHA256 -ne $before.SHA256){throw 'Android file changed during export; export was discarded.'}
        Assert-HabumiExportPlainPath $destinationFull
        if(Test-Path -LiteralPath $destinationFull){throw 'Destination already exists; nothing was overwritten.'}
        [IO.File]::Move($temporary,$destinationFull)
        $owned=$false
        return [pscustomobject]@{Destination=$destinationFull;Bytes=$before.Bytes;SHA256=$before.SHA256;GuestPath=$GuestPath}
    }finally{
        if($owned -and (Test-Path -LiteralPath $temporary -PathType Leaf)){[IO.File]::Delete($temporary)}
    }
}

Export-ModuleMember -Function Export-HabumiFile
