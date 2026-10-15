# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]
param(
    [string]$ProfileRoot='C:/Users/GODZILLATAB/Desktop/Habumi-Startup-Test',
    [string]$Serial='127.0.0.1:17555',
    [string]$OutputDirectory,
    [switch]$SelfTest
)
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$requiredRoot=[IO.Path]::GetFullPath('C:/Users/GODZILLATAB/Desktop/Habumi-Startup-Test').TrimEnd('\')
$captureScript=Join-Path $PSScriptRoot 'capture-guest-health.ps1'

function Assert-StartupTarget{
    $resolved=[IO.Path]::GetFullPath($ProfileRoot).TrimEnd('\')
    if(-not [string]::Equals($resolved,$requiredRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Refusing any profile except Habumi-Startup-Test.'}
    if($Serial -cne '127.0.0.1:17555'){throw 'Refusing any serial except 127.0.0.1:17555.'}
}

function Test-Pair($Before,$After){
    if(-not $Before.ready -or -not $After.ready){return 'not-ready'}
    if($Before.bootId -cne $After.bootId){return 'reboot'}
    foreach($name in @('surfaceflinger','systemui','system_server')){
        if((@($Before.pids.$name) -join ',') -cne (@($After.pids.$name) -join ',')){return "service-restart:$name"}
    }
    $a=0.0;$b=0.0
    if(-not [double]::TryParse([string]$Before.uptimeSeconds,[Globalization.NumberStyles]::Float,[Globalization.CultureInfo]::InvariantCulture,[ref]$a) -or
       -not [double]::TryParse([string]$After.uptimeSeconds,[Globalization.NumberStyles]::Float,[Globalization.CultureInfo]::InvariantCulture,[ref]$b) -or
       [double]::IsNaN($a) -or [double]::IsInfinity($a) -or [double]::IsNaN($b) -or [double]::IsInfinity($b)){return 'not-ready'}
    if($b -lt $a){return 'uptime-regressed'}
    return 'continuity-observed'
}

function Send-PowerCommand([string]$Text){
    $pipe=[IO.Pipes.NamedPipeClientStream]::new('.','AndroidRuntimeGuscio.comandi',[IO.Pipes.PipeDirection]::Out)
    try{
        $pipe.Connect(1000)
        $bytes=[Text.Encoding]::ASCII.GetBytes($Text+"`n")
        $write=$pipe.WriteAsync($bytes,0,$bytes.Length)
        if(-not $write.Wait(1000)){throw 'Named-pipe command write exceeded one second.'}
    }finally{$pipe.Dispose()}
}

function Wait-Log([int]$Offset,[string]$Pattern,[string]$Path,[int]$MaxAttempts=40){
    for($i=0;$i -lt $MaxAttempts;$i++){
        $text=Read-SharedLog $Path
        if($text.Length -gt $Offset -and $text.Substring($Offset) -match $Pattern){return}
        Start-Sleep -Milliseconds 100
    }
    throw "Expected power log entry missing: $Pattern"
}

function Read-SharedLog([string]$Path){
    $share=[IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
    $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,$share)
    try{
        $reader=[IO.StreamReader]::new($stream)
        try{return $reader.ReadToEnd()}finally{$reader.Dispose()}
    }finally{$stream.Dispose()}
}

function Capture([string]$Label,[string]$ComparePath){
    $dir=Join-Path $OutputDirectory $Label
    $captureArgs=@{RuntimeRoot=$ProfileRoot;Serial=$Serial;OutputDirectory=$dir;Label=$Label}
    if($ComparePath){$captureArgs.CompareBefore=$ComparePath}
    $null=& $captureScript @captureArgs
    $json=Join-Path $dir ($Label+'.json')
    return [pscustomobject]@{path=$json;data=(Get-Content -LiteralPath $json -Raw|ConvertFrom-Json)}
}

function Invoke-SelfTest{
    $before=[pscustomobject]@{ready=$true;bootId='boot-a';uptimeSeconds='10';pids=[pscustomobject]@{surfaceflinger=@('1');systemui=@('2');system_server=@('3')}}
    $cases=@(
        @{name='same-guest';after=([pscustomobject]@{ready=$true;bootId='boot-a';uptimeSeconds='11';pids=$before.pids});expected='continuity-observed'},
        @{name='reboot';after=([pscustomobject]@{ready=$true;bootId='boot-b';uptimeSeconds='1';pids=$before.pids});expected='reboot'},
        @{name='service-restart';after=([pscustomobject]@{ready=$true;bootId='boot-a';uptimeSeconds='11';pids=[pscustomobject]@{surfaceflinger=@('4');systemui=@('2');system_server=@('3')}});expected='service-restart:surfaceflinger'},
        @{name='not-ready';after=([pscustomobject]@{ready=$false});expected='not-ready'},
        @{name='uptime-regressed';after=([pscustomobject]@{ready=$true;bootId='boot-a';uptimeSeconds='9';pids=$before.pids});expected='uptime-regressed'}
    )
    $report=@();$failed=$false
    foreach($case in $cases){$actual=Test-Pair $before $case.after;$ok=$actual -ceq $case.expected;if(-not $ok){$failed=$true};$report+= [pscustomobject]@{name=$case.name;expected=$case.expected;actual=$actual;passed=$ok}}
    $testPath=Join-Path $OutputDirectory 'self-test.json'
    $stream=[IO.File]::Open($testPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$bytes=[Text.UTF8Encoding]::new($false).GetBytes((([pscustomobject]@{passed=(-not $failed);cases=$report}|ConvertTo-Json -Depth 6)+"`n"));$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
    $report|ForEach-Object {Write-Output ("{0}: {1}" -f $_.name,$_.actual)}
    if($failed){throw 'Pause/resume helper self-test failed.'}
}

Assert-StartupTarget
if(-not $OutputDirectory){$OutputDirectory=Join-Path $repoRoot ('research/results/development-vm-pause-20261015/run-'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $OutputDirectory){throw 'OutputDirectory must be new; refusing to overwrite existing output.'}
if($SelfTest){New-Item -ItemType Directory -Path $OutputDirectory|Out-Null;Invoke-SelfTest;exit 0}

$profile=[IO.Path]::GetFullPath($ProfileRoot)
$manifestPath=Join-Path $profile 'release-profile.json'
$qemu=Join-Path $profile 'runtime/bin/qemu-nostro.exe'
$shell=Join-Path $profile 'runtime/bin/Habumi.exe'
$log=Join-Path $profile 'guest/logs/registro-guscio.log'
foreach($path in @($manifestPath,$qemu,$shell,$log,$captureScript)){if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw "Required Startup-Test file is missing: $path"}}
$manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
$qemuEntry=@($manifest.CriticalFiles|Where-Object {$_.Path -ceq 'runtime/bin/qemu-nostro.exe'})
if($qemuEntry.Count -ne 1 -or $qemuEntry[0].SHA256 -notmatch '^[A-Fa-f0-9]{64}$'){throw 'release-profile.json has no unique QEMU CriticalFiles hash.'}
$actualHash=(Get-FileHash -LiteralPath $qemu -Algorithm SHA256).Hash
if($actualHash -ne $qemuEntry[0].SHA256){throw 'Startup-Test QEMU does not match its release-profile.json hash.'}

$expectedShell=[IO.Path]::GetFullPath($shell);$expectedQemu=[IO.Path]::GetFullPath($qemu)
$processes=@(Get-CimInstance -ClassName Win32_Process)
$vmNames=@($processes|Where-Object {$_.Name -ieq 'Habumi.exe' -or $_.Name -match '^qemu.*\.exe$'})
if($vmNames.Count -ne 2){throw 'Expected exactly one Startup-Test Habumi.exe and one qemu-nostro.exe; another Habumi/QEMU process is present.'}
$shellProc=@($vmNames|Where-Object {$_.Name -ieq 'Habumi.exe' -and [string]::Equals([IO.Path]::GetFullPath($_.ExecutablePath),$expectedShell,[StringComparison]::OrdinalIgnoreCase)})
$qemuProc=@($vmNames|Where-Object {$_.Name -ieq 'qemu-nostro.exe' -and [string]::Equals([IO.Path]::GetFullPath($_.ExecutablePath),$expectedQemu,[StringComparison]::OrdinalIgnoreCase)})
if($shellProc.Count -ne 1 -or $qemuProc.Count -ne 1){throw 'The only Habumi/QEMU processes must both belong to Startup-Test.'}

New-Item -ItemType Directory -Path $OutputDirectory|Out-Null
$before=Capture 'before' $null
if(-not $before.data.ready){throw 'Startup-Test Android is not boot-ready before the first cycle.'}
$cycles=@()
for($cycle=1;$cycle -le 3;$cycle++){
    $prior=Capture ("cycle-{0}-before" -f $cycle) $null
    if(-not $prior.data.ready){throw "Cycle $cycle began with Android not ready."}
    $offset=(Read-SharedLog $log).Length
    $suspendAttempted=$false
    $watch=[Diagnostics.Stopwatch]::StartNew()
    try{
        $suspendAttempted=$true
        Send-PowerCommand 'host-power suspend'
        Wait-Log $offset 'VM paused before host suspend; partition time suspended' $log 6
        Start-Sleep -Milliseconds 750
    }finally{
        try{if($suspendAttempted){Send-PowerCommand 'host-power resume'}}finally{$watch.Stop()}
    }
    Wait-Log $offset 'host resume restarted the VM paused by this handler' $log
    if($watch.Elapsed.TotalSeconds -gt 3){throw "Cycle $cycle exceeded the three-second pause/resume command window."}
    $post=Capture ("cycle-{0}-after" -f $cycle) $prior.path
    $pair=Test-Pair $prior.data $post.data
    $captured=if($post.data.comparison){$post.data.comparison.status}else{'missing-comparison'}
    if($pair -cne 'continuity-observed' -or $captured -cne 'continuity-observed'){
        throw "Cycle $cycle failed continuity check: $pair (capture: $captured)."
    }
    $cycles+= [pscustomobject]@{cycle=$cycle;classification='simulated-pause-resume';requestedHoldMilliseconds=750;pauseCommandWindowSeconds=$watch.Elapsed.TotalSeconds;bootIdPreserved=$true;pidsPreserved=$true;uptimeNondecreasing=$true;continuity='continuity observed; does not prove Windows standby delivery'}
    Write-Output "PASS: simulated pause/resume $cycle/3"
}
$result=[pscustomobject]@{schema='habumi.development-vm-pause.v1';createdUtc=[DateTime]::UtcNow.ToString('o');profileRoot=$profile;serial=$Serial;qemuSHA256=$actualHash;classification='simulated-pause-resume';windowsStandbyProven=$false;cycles=$cycles;limit='Simulated VM pause/resume only; does not exercise Windows standby or hibernation delivery.'}
$resultPath=Join-Path $OutputDirectory 'result.json'
$stream=[IO.File]::Open($resultPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{$bytes=[Text.UTF8Encoding]::new($false).GetBytes(($result|ConvertTo-Json -Depth 7)+"`n");$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
Write-Output "Results: $resultPath"
