# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]
param(
    [string]$RuntimeRoot,
    [string]$Serial='127.0.0.1:16555',
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$Label='guest-health',
    [string]$CompareBefore,
    [switch]$SelfTest
)
$ErrorActionPreference='Stop'

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

function Test-ReadySnapshot($Snapshot){
    if($null -eq $Snapshot -or $Snapshot.schema -ne 'habumi.guest-health.v1' -or
       $Snapshot.marker -cne 'HABUMI_GUEST_HEALTH_V1' -or
       [string]::IsNullOrWhiteSpace([string]$Snapshot.bootId) -or
       $Snapshot.bootCompleted -ne '1') {return $false}
    $uptime=0.0
    if(-not [double]::TryParse([string]$Snapshot.uptimeSeconds,
        [Globalization.NumberStyles]::Float,[Globalization.CultureInfo]::InvariantCulture,[ref]$uptime) -or
        [double]::IsNaN($uptime) -or [double]::IsInfinity($uptime) -or $uptime -lt 0){return $false}
    foreach($name in @('surfaceflinger','systemui','system_server')){
        $values=@($Snapshot.pids.$name)
        if($values.Count -eq 0){return $false}
        foreach($value in $values){$pidValue=0;if(-not [int]::TryParse([string]$value,[ref]$pidValue) -or $pidValue -le 0){return $false}}
    }
    return $true
}

function Compare-Snapshots($Before,$After){
    $status='continuity-observed';$reason='continuity observed; does not prove Windows standby delivery'
    if(-not (Test-ReadySnapshot $Before) -or -not (Test-ReadySnapshot $After)){
        $status='not-ready';$reason='one or both snapshots are missing required fields or Android is not boot-ready'
    }elseif([string]$Before.bootId -cne [string]$After.bootId){
        $status='reboot';$reason='Android boot_id changed'
    }else{
        foreach($name in @('surfaceflinger','systemui','system_server')){
            if((@($Before.pids.$name) -join ',') -cne (@($After.pids.$name) -join ',')){
                $status='service-restart';$reason="$name PID changed";break
            }
        }
        if($status -eq 'continuity-observed'){
            $b=[double]::Parse([string]$Before.uptimeSeconds,[Globalization.CultureInfo]::InvariantCulture)
            $a=[double]::Parse([string]$After.uptimeSeconds,[Globalization.CultureInfo]::InvariantCulture)
            if($a -lt $b){$status='uptime-regressed';$reason='Android uptime decreased'}
        }
    }
    return [pscustomobject]@{status=$status;reason=$reason}
}

function New-Snapshot([string]$Boot,[string]$Uptime,[string]$Complete,[int[]]$Sf,[int[]]$Ui,[int[]]$Server){
    return [pscustomobject]@{schema='habumi.guest-health.v1';marker='HABUMI_GUEST_HEALTH_V1';bootId=$Boot;uptimeSeconds=$Uptime;bootCompleted=$Complete;pids=[pscustomobject]@{surfaceflinger=$Sf;systemui=$Ui;system_server=$Server}}
}

function Invoke-SelfTest{
    $base=New-Snapshot 'boot-a' '101.0' '1' @(10) @(11) @(12)
    $cases=@(
        @{name='same-guest';before=$base;after=(New-Snapshot 'boot-a' '105.0' '1' @(10) @(11) @(12));expected='continuity-observed'},
        @{name='reboot';before=$base;after=(New-Snapshot 'boot-b' '3.0' '1' @(20) @(21) @(22));expected='reboot'},
        @{name='pid-changed';before=$base;after=(New-Snapshot 'boot-a' '105.0' '1' @(13) @(11) @(12));expected='service-restart'},
        @{name='missing-fields';before=$base;after=([pscustomobject]@{schema='habumi.guest-health.v1';marker='HABUMI_GUEST_HEALTH_V1';bootCompleted='1';pids=@{}});expected='not-ready'},
        @{name='uptime-nan';before=$base;after=(New-Snapshot 'boot-a' 'NaN' '1' @(10) @(11) @(12));expected='not-ready'},
        @{name='uptime-infinity';before=$base;after=(New-Snapshot 'boot-a' 'Infinity' '1' @(10) @(11) @(12));expected='not-ready'},
        @{name='uptime-regressed';before=$base;after=(New-Snapshot 'boot-a' '99.0' '1' @(10) @(11) @(12));expected='uptime-regressed'}
    )
    $out=@();$failed=$false
    foreach($case in $cases){$actual=Compare-Snapshots $case.before $case.after;$ok=$actual.status -eq $case.expected;if(-not $ok){$failed=$true};$out+= [pscustomobject]@{name=$case.name;expected=$case.expected;actual=$actual.status;passed=$ok;reason=$actual.reason}}
    $report=[pscustomobject]@{schema='habumi.guest-health-selftest.v1';createdUtc=[DateTime]::UtcNow.ToString('o');passed=(-not $failed);cases=$out}
    $path=Join-Path $OutputDirectory 'self-test.json'
    $stream=[IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$bytes=[Text.UTF8Encoding]::new($false).GetBytes(($report|ConvertTo-Json -Depth 8)+"`n");$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
    $out|ForEach-Object {Write-Output ("{0}: {1}" -f $_.name,$_.actual)}
    if($failed){throw 'Guest health comparison self-test failed.'}
}

if($Serial -notmatch '^127\.0\.0\.1:(\d{1,5})$' -or [int]$Matches[1] -lt 1 -or [int]$Matches[1] -gt 65535){throw 'Serial must be 127.0.0.1 with a valid TCP port.'}
if($Label -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$'){throw 'Label must be filename-safe (1-64 ASCII characters).'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $OutputDirectory){throw 'OutputDirectory must be new; refusing to overwrite existing output.'}
New-Item -ItemType Directory -Path $OutputDirectory|Out-Null
if($SelfTest){Invoke-SelfTest;exit 0}
if([string]::IsNullOrWhiteSpace($RuntimeRoot)){throw 'RuntimeRoot is required unless -SelfTest is used.'}
$adb=[IO.Path]::GetFullPath((Join-Path $RuntimeRoot 'runtime/bin/adb.exe'))
if(-not (Test-Path -LiteralPath $adb -PathType Leaf)){throw 'RuntimeRoot must contain runtime/bin/adb.exe.'}

$guestCommand="printf 'HABUMI_GUEST_HEALTH_V1\n'; printf 'boot_id='; cat /proc/sys/kernel/random/boot_id; printf 'uptime='; awk '{print `$1}' /proc/uptime; printf 'boot_completed='; getprop sys.boot_completed; printf 'surfaceflinger='; pidof surfaceflinger; printf 'systemui='; pidof com.android.systemui; printf 'system_server='; pidof system_server"
$adbArgs=@('-s',$Serial,'shell',$guestCommand)
$psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=$adb;$psi.Arguments=(($adbArgs|ForEach-Object {Quote-WinArg ([string]$_)}) -join ' ');$psi.UseShellExecute=$false;$psi.CreateNoWindow=$true;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
$proc=[Diagnostics.Process]::new();$proc.StartInfo=$psi
try{
    if(-not $proc.Start()){throw 'Could not start the owned ADB diagnostic.'}
    $stdoutReader=$proc.StandardOutput;$stderrReader=$proc.StandardError
    $stdoutTask=$stdoutReader.ReadToEndAsync();$stderrTask=$stderrReader.ReadToEndAsync()
    if(-not $proc.WaitForExit(20000)){
        try{$proc.Kill()}catch{}
        [void]$proc.WaitForExit(3000);throw 'Owned ADB diagnostic exceeded 20 seconds and was stopped.'
    }
    $stdoutDone=$stdoutTask.Wait(1000);$stderrDone=$stderrTask.Wait(1000)
    if(-not $stdoutDone -or -not $stderrDone){
        throw 'Owned ADB output streams did not close within one second.'
    }
    $raw=$stdoutTask.Result;$stderr=$stderrTask.Result;$code=$proc.ExitCode
}finally{
    if($stdoutReader){$stdoutReader.Dispose()}
    if($stderrReader){$stderrReader.Dispose()}
    $proc.Dispose()
}
$rawPath=Join-Path $OutputDirectory ($Label+'.raw.txt');$jsonPath=Join-Path $OutputDirectory ($Label+'.json')
foreach($path in @($rawPath,$jsonPath)){if(Test-Path -LiteralPath $path){throw 'Output file already exists; refusing to overwrite.'}}
$rawStream=[IO.File]::Open($rawPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{$rawBytes=[Text.UTF8Encoding]::new($false).GetBytes($raw);$rawStream.Write($rawBytes,0,$rawBytes.Length)}finally{$rawStream.Dispose()}
if($code -ne 0){throw "ADB diagnostic failed with exit code $code. $stderr"}
$values=@{}
foreach($line in ($raw -split "`r?`n")){
    if($line -ceq 'HABUMI_GUEST_HEALTH_V1'){$values.marker=$line;continue}
    if($line -match '^(boot_id|uptime|boot_completed|surfaceflinger|systemui|system_server)=(.*)$'){$values[$Matches[1]]=$Matches[2].Trim()}
}
$snap=[pscustomobject]@{schema='habumi.guest-health.v1';capturedUtc=[DateTime]::UtcNow.ToString('o');label=$Label;serial=$Serial;marker=$values.marker;bootId=$values.boot_id;uptimeSeconds=$values.uptime;bootCompleted=$values.boot_completed;pids=[pscustomobject]@{surfaceflinger=@(($values.surfaceflinger -split '\s+')|Where-Object {$_});systemui=@(($values.systemui -split '\s+')|Where-Object {$_});system_server=@(($values.system_server -split '\s+')|Where-Object {$_})};rawFile=[IO.Path]::GetFileName($rawPath);ready=$false}
$snap.ready=Test-ReadySnapshot $snap
if($CompareBefore){
    try{$before=Get-Content -LiteralPath $CompareBefore -Raw|ConvertFrom-Json}catch{$before=$null}
    $snap|Add-Member -NotePropertyName comparison -NotePropertyValue (Compare-Snapshots $before $snap)
}
$jsonStream=[IO.File]::Open($jsonPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{$bytes=[Text.UTF8Encoding]::new($false).GetBytes(($snap|ConvertTo-Json -Depth 8)+"`n");$jsonStream.Write($bytes,0,$bytes.Length)}finally{$jsonStream.Dispose()}
if($snap.comparison){Write-Output ("{0}: {1}" -f $snap.comparison.status,$snap.comparison.reason)}
elseif($snap.ready){Write-Output ("Snapshot written (ready): {0}" -f $jsonPath)}
else{Write-Output ("not-ready: required health fields are missing or Android has not completed boot; snapshot: {0}" -f $jsonPath)}
