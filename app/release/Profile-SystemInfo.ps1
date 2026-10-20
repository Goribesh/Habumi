# SPDX-License-Identifier: GPL-2.0-only
# Read-only, bounded host diagnostics for support bundles.
function Get-HabumiHostDiagnostics([string]$Root){
    $unavailable='unavailable'
    $system=[ordered]@{WindowsBuild=$unavailable;Architecture=$unavailable;CpuModel=$unavailable;DisplayDrivers=@()}
    try{
        $windows=Get-ItemProperty -Path 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -ErrorAction Stop
        $build=[string]$windows.CurrentBuildNumber
        if(-not $build){$build=[string]$windows.CurrentBuild}
        if($build){$system.WindowsBuild=$build}
    }catch{}
    try{
        $processor=Get-ItemProperty -Path 'HKLM:\HARDWARE\DESCRIPTION\System\CentralProcessor\0' -ErrorAction Stop
        if($processor.ProcessorNameString){$system.CpuModel=[string]$processor.ProcessorNameString}
    }catch{}
    try{
        $os=Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction Stop
        if($system.WindowsBuild -eq $unavailable -and $null -ne $os.BuildNumber){$system.WindowsBuild=[string]$os.BuildNumber}
        $cs=Get-CimInstance -ClassName Win32_ComputerSystem -ErrorAction Stop
        if($cs.SystemType){$system.Architecture=[string]$cs.SystemType}
        $cpu=Get-CimInstance -ClassName Win32_Processor -ErrorAction Stop | Select-Object -First 1
        if($system.CpuModel -eq $unavailable -and $cpu.Name){$system.CpuModel=[string]$cpu.Name}
    }catch{}
    foreach($key in @('WindowsBuild','Architecture','CpuModel')){
        if($system[$key] -is [string] -and $system[$key].Length -gt 256){$system[$key]=$system[$key].Substring(0,256)}
    }
    try{
        $drivers=@(Get-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\*' -ErrorAction Stop |
            Where-Object {$_.DriverDesc} | Select-Object -First 16 | ForEach-Object {
                $name=[string]$_.DriverDesc;$version=[string]$_.DriverVersion
                if($name.Length -gt 256){$name=$name.Substring(0,256)}
                if($version.Length -gt 256){$version=$version.Substring(0,256)}
                [pscustomobject]@{Name=$name;Version=$(if($version){$version}else{$unavailable})}
            })
        if($drivers.Count -gt 0){$system.DisplayDrivers=$drivers}
    }catch{}

    $configured=[ordered]@{};$unknown=New-Object System.Collections.Generic.List[string]
    $known=@('vcpu','memory','width','height','retries','adb_port','clipboard_port','gl','audio','gl_flush_wait','guest_scale','variant','hz','battery_hz','density')
    $config=Join-Path $Root 'runtime/bin/config.txt'
    $configStatus=$unavailable
    try{
        if(Test-Path -LiteralPath $config -PathType Leaf){
            $info=New-Object IO.FileInfo($config)
            if($info.Length -le 65536){
              $configStatus='read'
              foreach($line in [IO.File]::ReadAllLines($config)){
                if($line.Length -gt 4096){$line=$line.Substring(0,4096)}
                if($line -match '^\s*([^#;=\s]+)\s*=\s*(.*?)\s*$'){
                    $name=$matches[1].ToLowerInvariant();$value=$matches[2]
                    if($known -contains $name){if($value.Length -gt 256){$value=$value.Substring(0,256)};$configured[$name]=$value}
                    elseif(-not $unknown.Contains($name) -and $unknown.Count -lt 64){if($name.Length -gt 64){$name=$name.Substring(0,64)};if(-not $unknown.Contains($name)){$unknown.Add($name)}}
                }
              }
            }else{$configStatus='too-large'}
        }
    }catch{}
    foreach($name in $known){if(-not $configured.Contains($name)){$configured[$name]=$unavailable}}
    $unknown.Sort([StringComparer]::Ordinal)
    return [pscustomobject]@{
        Schema='habumi.host-diagnostics.v1'
        System=[pscustomobject]$system
        Configured=[pscustomobject]$configured
        UnknownConfigKeys=@($unknown.ToArray())
        ConfigStatus=$configStatus
        Applied=[pscustomobject]@{Variant=$unavailable;Gl=$unavailable;Width=$unavailable;Height=$unavailable;Vcpu=$unavailable;Memory=$unavailable;AdbPort=$unavailable;ClipboardPort=$unavailable}
    }
}
