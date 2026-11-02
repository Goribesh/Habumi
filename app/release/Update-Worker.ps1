# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]param([Parameter(Mandatory)][string]$RequestPath)
Set-StrictMode -Version 2
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$env:PSModulePath=$PSHOME+'\Modules;'+$env:SystemRoot+'\System32\WindowsPowerShell\v1.0\Modules'
$resultPath=Join-Path $PSScriptRoot 'result.json'
function Write-Result($Value){
    $temporary=$resultPath+'.new'
    [IO.File]::WriteAllText($temporary,($Value|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
    [IO.File]::Move($temporary,$resultPath)
}
function Get-Release {
    $headers=@{'User-Agent'='Habumi-manual-updater';Accept='application/vnd.github+json'}
    [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
    $releases=Invoke-RestMethod -Uri 'https://api.github.com/repos/Goribesh/Habumi/releases?per_page=20' -Headers $headers -TimeoutSec 30
    $release=@($releases|Where-Object {-not $_.draft -and ($request.Prerelease -or -not $_.prerelease)})|Select-Object -First 1
    if(-not $release){throw 'No published release was found for this channel.'}
    $version=[string]$release.tag_name
    if($version -notmatch '^\d+\.\d+\.\d+(-[a-z0-9.]+)?$'){throw 'The release version is invalid.'}
    $name="Habumi-$version.zip"
    $assets=@($release.assets|Where-Object name -eq $name)
    $checksums=@($release.assets|Where-Object name -eq 'SHA256SUMS.txt')
    if($assets.Count -ne 1 -or $checksums.Count -ne 1){throw 'The release lacks an unambiguous runtime package or checksum file.'}
    $prefix="https://github.com/Goribesh/Habumi/releases/download/$version/"
    if($assets[0].browser_download_url -cne ($prefix+$name) -or
       $checksums[0].browser_download_url -cne ($prefix+'SHA256SUMS.txt')){throw 'Unexpected release download location.'}
    if([long]$assets[0].size -le 0 -or [long]$assets[0].size -gt 2GB -or [long]$checksums[0].size -gt 65536){throw 'Release asset exceeds the supported size.'}
    $sums=Invoke-WebRequest -UseBasicParsing -Uri $checksums[0].browser_download_url -Headers $headers -TimeoutSec 30
    $sumText=if($sums.Content -is [byte[]]){[Text.Encoding]::UTF8.GetString($sums.Content)}else{[string]$sums.Content}
    if($sumText.Length -gt 65536){throw 'Release checksum text exceeds the supported size.'}
    $matchesFound=@([regex]::Matches($sumText,'(?m)^([a-fA-F0-9]{64})[ \t]+\*?'+[regex]::Escape($name)+'\r?$'))
    if($matchesFound.Count -ne 1){throw 'The runtime checksum is missing or ambiguous.'}
    $hash=$matchesFound[0].Groups[1].Value.ToUpperInvariant()
    if($assets[0].PSObject.Properties['digest'] -and $assets[0].digest -and $assets[0].digest -cne ('sha256:'+$hash.ToLowerInvariant())){throw 'GitHub asset digest differs from the release checksum.'}
    return [ordered]@{Version=$version;Name=$name;URL=$assets[0].browser_download_url;SHA256=$hash;Bytes=[long]$assets[0].size;Page=$release.html_url}
}
function Download-Release($Release){
    Add-Type -AssemblyName System.Net.Http
    $archive=Join-Path $PSScriptRoot $Release.Name
    $client=[Net.Http.HttpClient]::new();$client.Timeout=[TimeSpan]::FromMinutes(20)
    $client.DefaultRequestHeaders.UserAgent.ParseAdd('Habumi-manual-updater')
    $response=$null;$inputStream=$null;$outputStream=$null
    try{
        $response=$client.GetAsync($Release.URL,[Net.Http.HttpCompletionOption]::ResponseHeadersRead).GetAwaiter().GetResult()
        $null=$response.EnsureSuccessStatusCode()
        if($response.Headers.Location -and $response.Headers.Location.Scheme -ne 'https'){throw 'Insecure release redirect.'}
        $inputStream=$response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
        $outputStream=[IO.File]::Open($archive,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        $buffer=New-Object byte[] 1048576;$total=0L
        while($true){
            $read=$inputStream.ReadAsync($buffer,0,$buffer.Length)
            if(-not $read.Wait(60000)){throw 'Release download stalled. The staged file was retained for diagnosis.'}
            $count=$read.GetAwaiter().GetResult();if($count -le 0){break}
            $total+=$count;if($total -gt $Release.Bytes){throw 'Downloaded archive exceeds its declared size.'}
            $outputStream.Write($buffer,0,$count)
        }
        $outputStream.Flush($true);$outputStream.Dispose();$outputStream=$null
        if($total -ne $Release.Bytes -or (Get-FileHash -LiteralPath $archive).Hash -ne $Release.SHA256){throw 'Downloaded archive did not pass size and SHA-256 verification.'}
    }finally{
        if($outputStream){$outputStream.Dispose()};if($inputStream){$inputStream.Dispose()}
        if($response){$response.Dispose()};$client.Dispose()
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip=[IO.Compression.ZipFile]::OpenRead($archive)
    $inventory=Join-Path $PSScriptRoot 'package-files.json'
    try{
        $entries=@($zip.Entries|Where-Object FullName -ceq 'package-files.json')
        if($entries.Count -ne 1 -or $entries[0].Length -gt 2MB){throw 'This release does not contain an updater inventory. Use its portable ZIP manually.'}
        $inputStream=$entries[0].Open();$outputStream=[IO.File]::Open($inventory,[IO.FileMode]::CreateNew)
        try{$inputStream.CopyTo($outputStream);$outputStream.Flush($true)}finally{$inputStream.Dispose();$outputStream.Dispose()}
    }finally{$zip.Dispose()}
    return @{ArchivePath=$archive;PackageFilesPath=$inventory;ExpectedSHA256=$Release.SHA256}
}
try{
    $full=[IO.Path]::GetFullPath($RequestPath)
    if($full -cne (Join-Path $PSScriptRoot 'request.json')){throw 'Invalid updater request location.'}
    if((Get-Item -LiteralPath $full).Length -gt 65536){throw 'Updater request is too large.'}
    $request=[IO.File]::ReadAllText($full)|ConvertFrom-Json
    if($request.Schema -ne 'habumi.update-ui.v1' -or $request.Nonce -notmatch '^[a-f0-9]{32}$' -or
       [IO.Path]::GetFileName($PSScriptRoot) -cne ('Habumi-update-'+$request.Nonce)){throw 'Invalid updater request identity.'}
    if($request.Operation -eq 'Check'){
        $release=Get-Release
        Write-Result @{Nonce=$request.Nonce;OK=$true;Release=$release;Message=('Available: '+$release.Version)}
    }else{
        Import-Module (Join-Path $PSScriptRoot 'Profile-Update.psm1') -Force
        switch($request.Operation){
            'Install'{
                # Re-read release metadata in the worker; UI input cannot select another URL.
                $release=Get-Release
                if($release.Version -cne $request.ExpectedVersion){throw 'Release selection changed. Check again before updating.'}
                $download=Download-Release $release
                $result=Install-HabumiUpdate -ProfileRoot $request.ProfileRoot @download
            }
            'Rollback'{$result=Rollback-HabumiUpdate -ProfileRoot $request.ProfileRoot}
            'Recover'{$result=Recover-HabumiUpdate -ProfileRoot $request.ProfileRoot}
            default{throw 'Unknown updater operation.'}
        }
        Write-Result @{Nonce=$request.Nonce;OK=$true;Message=($request.Operation+' completed. You can start Habumi.');Result=$result}
    }
    exit 0
}catch{
    $nonce=if(Get-Variable request -ErrorAction SilentlyContinue){$request.Nonce}else{''}
    try{Write-Result @{Nonce=$nonce;OK=$false;Message=$_.Exception.Message}}catch{}
    exit 1
}
