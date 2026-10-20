# SPDX-License-Identifier: GPL-2.0-only
$ErrorActionPreference='Stop'
$scriptPath=Join-Path (Split-Path -Parent $PSScriptRoot) 'release/Profile-SystemInfo.ps1'
. $scriptPath
$root=Join-Path ([Environment]::GetFolderPath('Desktop')) ('Habumi-Workspace/Builds/Checks/system-info-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force | Out-Null
$checks=0
try{
    $runtime=Join-Path $root 'runtime/bin'
    New-Item -ItemType Directory -Path $runtime -Force | Out-Null
    $configPath=Join-Path $runtime 'config.txt'
    $original="variant=stable`ngl=angle`nwidth=1920`nheight=1080`nvcpu=4`nmemory=4096`nadb_port=5555`nclipboard_port=7000`nretries=3`nhz=60`nbattery_hz=30`ndensity=240`naudio=on`ngl_flush_wait=yes`nguest_scale=no`nsecret-token=do-not-export`n"
    [IO.File]::WriteAllText($configPath,$original,[Text.Encoding]::ASCII)
    $result=Get-HabumiHostDiagnostics $root;$checks++
    foreach($key in @('variant','gl','width','height','vcpu','memory','adb_port','clipboard_port','retries','hz','battery_hz','density','audio','gl_flush_wait','guest_scale')){
        if($result.Configured.$key -eq 'unavailable'){throw "Configured key missing: $key"};$checks++
    }
    if($result.Configured.variant -ne 'stable' -or $result.Configured.width -ne '1920' -or $result.Configured.gl -ne 'angle'){throw 'Configured values were not returned.'};$checks++
    if($result.UnknownConfigKeys -notcontains 'secret-token'){throw 'Unknown key name was not reported.'};$checks++
    $serialized=ConvertTo-Json $result -Depth 8
    if($serialized.Contains('do-not-export')){throw 'Unknown key value leaked.'};$checks++
    foreach($value in $result.Configured.PSObject.Properties.Value){if($value.Length -gt 256){throw 'Configured value exceeded bound.'}};$checks++
    foreach($value in @($result.System.WindowsBuild,$result.System.Architecture,$result.System.CpuModel)){
        if($value.Length -gt 256){throw 'System value exceeded bound.'}
    };$checks++
    foreach($property in $result.Applied.PSObject.Properties){if($property.Value -ne 'unavailable'){throw 'Applied setting was inferred.'}};$checks++
    if([IO.File]::ReadAllText($configPath) -ne $original){throw 'Fixture config was modified.'};$checks++
    if($result.UnknownConfigKeys.Count -gt 64){throw 'Too many unknown key names.'};$checks++
    $many=New-Object Text.StringBuilder
    for($i=0;$i -lt 100;$i++){[void]$many.Append(('unknown-key-{0:D3}-{1}=private-{0}' -f $i,('x'*70))).Append("`n")}
    [IO.File]::WriteAllText($configPath,$many.ToString(),[Text.Encoding]::ASCII)
    $bounded=Get-HabumiHostDiagnostics $root
    if($bounded.UnknownConfigKeys.Count -ne 64){throw 'Unknown key count limit was not enforced.'};$checks++
    foreach($name in $bounded.UnknownConfigKeys){if($name.Length -gt 64){throw 'Unknown key name exceeded bound.'}};$checks++
    if((ConvertTo-Json $bounded -Depth 8).Contains('private-')){throw 'Unknown values leaked.'};$checks++
    $large=New-Object Text.StringBuilder
    [void]$large.Append(('x'*70000))
    [IO.File]::WriteAllText($configPath,$large.ToString(),[Text.Encoding]::ASCII)
    $tooLarge=Get-HabumiHostDiagnostics $root
    if($tooLarge.ConfigStatus -ne 'too-large' -or $tooLarge.Configured.variant -ne 'unavailable'){throw 'Oversized config was not skipped.'};$checks++
    Write-Output "PASS: $checks checks"
}finally{Write-Output "Fixture retained: $root"}
