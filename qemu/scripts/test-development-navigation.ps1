# SPDX-License-Identifier: GPL-2.0-only
# Bounded navigation smoke test for the dedicated Habumi startup-test runtime.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'

# Il runtime e il seriale sono fissati al profilo di prova dedicato.
$RuntimeRoot='C:\Users\GODZILLATAB\Desktop\Habumi-Startup-Test'
$Serial='127.0.0.1:17555'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$healthScript=Join-Path $PSScriptRoot 'capture-guest-health.ps1'
$adb=[IO.Path]::GetFullPath((Join-Path $RuntimeRoot 'runtime\bin\adb.exe'))
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $OutputDirectory){throw 'OutputDirectory must be new; refusing to overwrite existing output.'}
if(-not (Test-Path -LiteralPath $adb -PathType Leaf)){throw 'The fixed test RuntimeRoot does not contain runtime/bin/adb.exe.'}
if(-not (Test-Path -LiteralPath $healthScript -PathType Leaf)){throw 'capture-guest-health.ps1 is required.'}

function Quote-WinArg([string]$Value){
    if($Value -notmatch '[\s"]'){return $Value}
    $builder=[Text.StringBuilder]::new();[void]$builder.Append('"')
    $slashes=0
    foreach($ch in $Value.ToCharArray()){
        if($ch -eq '\'){$slashes++;continue}
        if($ch -eq '"'){
            [void]$builder.Append(('\' * (2*$slashes+1)));[void]$builder.Append('"');$slashes=0;continue
        }
        if($slashes){[void]$builder.Append(('\' * $slashes));$slashes=0}
        [void]$builder.Append($ch)
    }
    if($slashes){[void]$builder.Append(('\' * (2*$slashes)))}
    [void]$builder.Append('"');return $builder.ToString()
}

# Ogni client ADB appartiene a questo script, usa pipe separate e ha un limite temporale.
$script:commandNumber=0
function Invoke-Guest([string[]]$Arguments){
    $script:commandNumber++
    $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=$adb
    $allArgs=@('-s',$Serial)+$Arguments
    $psi.Arguments=(($allArgs|ForEach-Object {Quote-WinArg ([string]$_)}) -join ' ')
    $psi.UseShellExecute=$false;$psi.CreateNoWindow=$true;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
    $proc=[Diagnostics.Process]::new();$proc.StartInfo=$psi;$stdoutReader=$null;$stderrReader=$null;$started=$false
    try{
        if(-not $proc.Start()){throw 'Could not start the owned ADB client.'}
        $started=$true
        $stdoutReader=$proc.StandardOutput;$stderrReader=$proc.StandardError
        $stdoutTask=$stdoutReader.ReadToEndAsync();$stderrTask=$stderrReader.ReadToEndAsync()
        if(-not $proc.WaitForExit(20000)){
            try{$proc.Kill()}catch{}
            [void]$proc.WaitForExit(3000)
            throw 'Owned ADB client exceeded 20 seconds and was stopped.'
        }
        $exitCode=$proc.ExitCode
        $stdoutDone=$stdoutTask.Wait(1000);$stderrDone=$stderrTask.Wait(1000)
        if(-not $stdoutDone -or -not $stderrDone){throw 'Owned ADB output streams did not close within one second.'}
        $stdout=$stdoutTask.Result;$stderr=$stderrTask.Result
        $record=[pscustomobject]@{number=$script:commandNumber;arguments=$Arguments;exitCode=$exitCode;stdout=$stdout;stderr=$stderr}
        $record|ConvertTo-Json -Depth 5|Add-Content -LiteralPath (Join-Path $OutputDirectory 'adb-commands.jsonl') -Encoding utf8
        if($exitCode -ne 0){throw "ADB command $script:commandNumber failed with exit code $exitCode. $stderr"}
        return $stdout.Trim()
    }finally{
        if($stdoutReader){$stdoutReader.Dispose()};if($stderrReader){$stderrReader.Dispose()}
        if($started -and -not $proc.HasExited){try{$proc.Kill()}catch{}}
        $proc.Dispose()
    }
}
function Save-Text([string]$Name,[string]$Content){
    $path=Join-Path $OutputDirectory $Name
    $stream=[IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$bytes=[Text.UTF8Encoding]::new($false).GetBytes($Content);$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
}
function Capture-Screenshot([string]$Name){
    $remote='/data/local/tmp/habumi-nav-'+[guid]::NewGuid().ToString('N')+'.png'
    [void](Invoke-Guest @('shell','screencap','-p',$remote))
    [void](Invoke-Guest @('pull',$remote,(Join-Path $OutputDirectory ($Name+'.png'))))
    [void](Invoke-Guest @('shell','rm','-f',$remote))
}
function Invoke-Health([string]$Label){
    $subdir=Join-Path $OutputDirectory $Label
    $pwsh=(Get-Process -Id $PID).Path
    $args=@('-NoProfile','-File',$healthScript,'-RuntimeRoot',$RuntimeRoot,'-Serial',$Serial,'-OutputDirectory',$subdir,'-Label',$Label)
    $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=$pwsh
    $psi.Arguments=(($args|ForEach-Object {Quote-WinArg ([string]$_)}) -join ' ')
    $psi.UseShellExecute=$false;$psi.CreateNoWindow=$true;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
    $proc=[Diagnostics.Process]::new();$proc.StartInfo=$psi;$outReader=$null;$errReader=$null;$started=$false
    try{
        if(-not $proc.Start()){throw 'Could not start guest-health capture.'}
        $started=$true
        $outReader=$proc.StandardOutput;$errReader=$proc.StandardError
        $outTask=$outReader.ReadToEndAsync();$errTask=$errReader.ReadToEndAsync()
        if(-not $proc.WaitForExit(25000)){try{$proc.Kill()}catch{};[void]$proc.WaitForExit(3000);throw 'Guest-health capture exceeded 25 seconds.'}
        $code=$proc.ExitCode
        if(-not $outTask.Wait(1000) -or -not $errTask.Wait(1000)){throw 'Guest-health output streams did not close within one second.'}
        Save-Text ($Label+'-launcher.stdout.txt') $outTask.Result
        Save-Text ($Label+'-launcher.stderr.txt') $errTask.Result
        if($code -ne 0){throw "Guest-health capture failed with exit code $code."}
        $snapshot=Get-Content -LiteralPath (Join-Path $subdir ($Label+'.json')) -Raw|ConvertFrom-Json
        if(-not $snapshot.ready){throw "Guest-health snapshot '$Label' is not ready."}
        return $snapshot
    }finally{
        if($outReader){$outReader.Dispose()};if($errReader){$errReader.Dispose()}
        if($started -and -not $proc.HasExited){try{$proc.Kill()}catch{}}
        $proc.Dispose()
    }
}

# Controlla i processi Windows prima di avviare client diagnostici.
New-Item -ItemType Directory -Path $OutputDirectory|Out-Null
$processes=@(Get-CimInstance Win32_Process)
$suspicious=@($processes|Where-Object {
    $_.Name -ieq 'Habumi.exe' -or $_.Name -ieq 'qemu-nostro.exe' -or $_.Name -like 'qemu*.exe'
})
$inventory=@($suspicious|ForEach-Object {[pscustomobject]@{processId=$_.ProcessId;name=$_.Name;executablePath=$_.ExecutablePath;commandLine=$_.CommandLine}})
Save-Text 'host-process-check.json' (($inventory|ConvertTo-Json -Depth 5)+"`n")
$expectedHabumi=[IO.Path]::GetFullPath((Join-Path $RuntimeRoot 'runtime\bin\Habumi.exe'))
$expectedQemu=[IO.Path]::GetFullPath((Join-Path $RuntimeRoot 'runtime\bin\qemu-nostro.exe'))
$rootPrefix=$RuntimeRoot.TrimEnd('\')+'\'
$wrongPath=@($suspicious|Where-Object {
    $path=[string]$_.ExecutablePath
    -not $path.StartsWith($rootPrefix,[StringComparison]::OrdinalIgnoreCase) -or
    ($_.Name -ieq 'Habumi.exe' -and -not $path.Equals($expectedHabumi,[StringComparison]::OrdinalIgnoreCase)) -or
    ($_.Name -ieq 'qemu-nostro.exe' -and -not $path.Equals($expectedQemu,[StringComparison]::OrdinalIgnoreCase))
})
$habumiProcesses=@($suspicious|Where-Object {$_.Name -ieq 'Habumi.exe'})
$qemuProcesses=@($suspicious|Where-Object {$_.Name -ieq 'qemu-nostro.exe'})
if($suspicious.Count -ne 2 -or $habumiProcesses.Count -ne 1 -or $qemuProcesses.Count -ne 1 -or $wrongPath.Count -gt 0){
    throw 'Expected exactly one Habumi.exe and one qemu-nostro.exe from the fixed RuntimeRoot; refusing to continue.'
}

$before=Invoke-Health 'before'
if((Invoke-Guest @('get-state')) -ne 'device'){throw 'Android ADB state is not device.'}
if((Invoke-Guest @('shell','getprop','sys.boot_completed')) -ne '1'){throw 'Android boot is not complete.'}
$size=Invoke-Guest @('shell','wm','size')
if($size -notmatch '(?:Override|Physical) size: (\d+)x(\d+)'){throw 'Display size unavailable.'}
$width=[int]$Matches[1];$height=[int]$Matches[2]
$workloadId=[guid]::NewGuid().ToString('N')
$guestTime=Invoke-Guest @('shell','cat','/proc/uptime')
$guestUptime=($guestTime -split '\s+')[0]
$marker="navigation-$workloadId-guest-uptime-$guestUptime"
[void](Invoke-Guest @('shell','log','-t','HABUMI_NAV_TEST',$marker))
Save-Text 'workload-marker.txt' ("marker=$marker`nserial=$Serial`n")

# I log vengono conservati senza svuotare i buffer guest; i dati grezzi possono precedere il marker.
$crashBefore=Invoke-Guest @('logcat','-b','crash','-d','-v','threadtime')
$eventsBefore=Invoke-Guest @('logcat','-b','events','-d','-v','threadtime')
Save-Text 'crash-log-before.raw.txt' $crashBefore
Save-Text 'events-log-before.raw.txt' $eventsBefore
[void](Invoke-Guest @('shell','input','keyevent','3'));Start-Sleep -Milliseconds 500
Capture-Screenshot 'home'
for($cycle=1;$cycle -le 3;$cycle++){
    $launch=Invoke-Guest @('shell','am','start','-W','-a','android.settings.SETTINGS')
    Save-Text ("settings-launch-$cycle.txt") $launch
    if($launch -notmatch '(?m)^Status:\s*ok\s*$'){throw "Settings launch $cycle did not report Status: ok."}
    Start-Sleep -Milliseconds 500
    if($cycle -eq 1){Capture-Screenshot 'settings'}
    $launch=Invoke-Guest @('shell','am','start','-W','-a','android.settings.DISPLAY_SETTINGS')
    Save-Text ("display-launch-$cycle.txt") $launch
    if($launch -notmatch '(?m)^Status:\s*ok\s*$'){throw "Display settings launch $cycle did not report Status: ok."}
    Start-Sleep -Milliseconds 500
    [void](Invoke-Guest @('shell','input','keyevent','187'));Start-Sleep -Milliseconds 500
    [void](Invoke-Guest @('shell','input','keyevent','3'));Start-Sleep -Milliseconds 500
    Write-Output "PASS: navigation cycle $cycle/3"
}
$activities=Invoke-Guest @('shell','dumpsys','activity','activities')
Save-Text 'final-activities.txt' $activities
$resumed=@($activities -split '\r?\n'|Where-Object {$_ -match 'topResumedActivity|mResumedActivity'})
if(-not ($resumed -match 'com\.android\.launcher3')){throw 'Navigation did not return to the launcher.'}
Capture-Screenshot 'home-final'
$crashAfter=Invoke-Guest @('logcat','-b','crash','-d','-v','threadtime')
$eventsAfter=Invoke-Guest @('logcat','-b','events','-d','-v','threadtime')
Save-Text 'crash-log-after.raw.txt' $crashAfter
Save-Text 'events-log-after.raw.txt' $eventsAfter
$after=Invoke-Health 'after'
$continuity=($before.bootId -ceq $after.bootId)
foreach($name in @('surfaceflinger','systemui','system_server')){
    if((@($before.pids.$name) -join ',') -cne (@($after.pids.$name) -join ',')){$continuity=$false}
}
$beforeUptime=[double]::Parse([string]$before.uptimeSeconds,[Globalization.CultureInfo]::InvariantCulture)
$afterUptime=[double]::Parse([string]$after.uptimeSeconds,[Globalization.CultureInfo]::InvariantCulture)
if($afterUptime -lt $beforeUptime){$continuity=$false}
if(-not $continuity){throw 'Guest boot identity, required PIDs, or uptime did not remain continuous.'}

$result=[pscustomobject]@{
    schema='habumi.navigation-probe.v1';createdUtc=[DateTime]::UtcNow.ToString('o');runtimeRoot=$RuntimeRoot;serial=$Serial
    cycles=3;display=[pscustomobject]@{width=$width;height=$height};workloadMarker=$marker;guestUptimeAtMarkerSeconds=$guestUptime
    beforeHealth='before/before.json';afterHealth='after/after.json';bootIdContinuity=$true;requiredPidContinuity=$true;uptimeNondecreasing=$true
    finalResumedActivities=$resumed;launcherResumed=[bool]($resumed -match 'com\.android\.launcher3')
    screenshots=@('home.png','settings.png','home-final.png');screenshotsRequireVisualReview=$true
    logBuffers=@{crash='raw before/after dumps retained; buffers were not cleared';events='raw before/after dumps retained; pre-existing entries may be present'}
    scope='Observed three Settings, Display, Recents, and Home navigation cycles with guest boot/PID continuity and a final launcher-resumed activity. This does not assess screenshot pixels, long-duration stability, or all UI behavior.'
}
Save-Text 'result.json' (($result|ConvertTo-Json -Depth 8)+"`n")
Write-Output "Results: $OutputDirectory"
