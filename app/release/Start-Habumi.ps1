# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]
param([switch]$Validate)
$ErrorActionPreference='Stop'
$taskRoot=$PSScriptRoot
function Get-ReleaseHash([string]$Path) {
    $taskStream=[IO.File]::OpenRead($Path)
    $taskHasher=[Security.Cryptography.SHA256]::Create()
    try{return ([BitConverter]::ToString($taskHasher.ComputeHash($taskStream))).Replace('-','')}
    finally{$taskStream.Dispose();$taskHasher.Dispose()}
}
function Find-QualcommIcd {
    $taskClass='HKLM:/SYSTEM/CurrentControlSet/Control/Class/{4d36e968-e325-11ce-bfc1-08002be10318}'
    foreach($taskIndex in 0..31){
        try{$taskDriver=Get-ItemProperty ($taskClass+'/'+('{0:d4}' -f $taskIndex)) -ErrorAction Stop}catch{continue}
        foreach($taskCandidate in @($taskDriver.VulkanDriverName)){
            if(-not $taskCandidate -or $taskCandidate -notmatch 'qcvk.*\.json$'){continue}
            if(-not (Test-Path -LiteralPath $taskCandidate -PathType Leaf)){continue}
            $taskManifest=Get-Content -LiteralPath $taskCandidate -Raw|ConvertFrom-Json
            $taskLibrary=Join-Path (Split-Path $taskCandidate -Parent) $taskManifest.ICD.library_path
            if(Test-Path -LiteralPath $taskLibrary -PathType Leaf){return $taskCandidate}
        }
    }
    throw 'A native Qualcomm Vulkan driver was not found. Install the graphics driver supplied for your Windows ARM64 device.'
}
try{
    if(Test-Path -LiteralPath (Join-Path $taskRoot 'storage-maintenance.lock')){throw 'Storage maintenance is in progress.'}
    $taskProfile=Get-Content -LiteralPath (Join-Path $taskRoot 'release-profile.json') -Raw|ConvertFrom-Json
    foreach($taskFile in $taskProfile.CriticalFiles){
        $taskPath=Join-Path $taskRoot $taskFile.Path
        if((Get-ReleaseHash $taskPath) -ne $taskFile.SHA256){throw "Release file changed: $($taskFile.Path)"}
    }
    $taskIcd=Find-QualcommIcd
    # Solo variabili del processo; nessuna impostazione del driver di sistema.
    $taskEnvironment=@{
        VK_DRIVER_FILES=$taskIcd;VK_ICD_FILENAMES=$taskIcd;GALLIUM_DRIVER='zink';
        WINQ_GL='wgl';WINQ_GL_FLUSH_WAIT='no';WINQ_APPEND_EXTRA='winq.prop.ro.hardware.vulkan=virtio';
        WINQ_ANDROID_WSI='1';WINQ_NATIVE_WSI_ALLOC='1';WINQ_DIAG='0';VIRGL_LOG_LEVEL='info';
        VIRGL_LOG_FILE=(Join-Path $taskRoot 'guest/logs/native-venus.log');
        WINQ_HOST_POWER_TEST=$(if($taskProfile.HostPowerExperimental){'1'}else{$null})
    }
    foreach($taskName in @('WINQ_VENUS_DIRECT','WINQ_VENUS_DIRECT_FILE','WINQ_VENUS_TRACE','WINQ_VENUS_TRACE_FILE',
        'WINQ_VENUS_BATCH_SYNC','WINQ_GPU_DIAG','WINQ_VENUS_FENCE_TRACE','WINQ_WHPX_STAT','WINQ_PRESENT_DIAG',
        'WINQ_HZ','MESA_WGL_NO_FLUSH_WAIT')){[Environment]::SetEnvironmentVariable($taskName,$null,'Process')}
    foreach($taskName in $taskEnvironment.Keys){[Environment]::SetEnvironmentVariable($taskName,$taskEnvironment[$taskName],'Process')}
    New-Item -ItemType Directory -Path (Join-Path $taskRoot 'guest/logs') -Force|Out-Null
    if($Validate){
        foreach($taskMode in @('config','arguments')){
            $taskCheck=Start-Process (Join-Path $taskRoot 'runtime/bin/Habumi.exe') -ArgumentList "--$taskMode" -WorkingDirectory $taskRoot -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $taskRoot "guest/logs/validate-$taskMode.txt") -RedirectStandardError (Join-Path $taskRoot "guest/logs/validate-$taskMode-error.txt")
            if($taskCheck.ExitCode){throw "Habumi $taskMode validation failed."}
        }
        Write-Output 'PASS: release files, native ICD, configuration and arguments validated; Android was not started.'
        exit 0
    }
    $taskHabumi=Start-Process (Join-Path $taskRoot 'runtime/bin/Habumi.exe') -WorkingDirectory $taskRoot -WindowStyle Normal -PassThru
    $taskHabumi.WaitForExit()
    exit $taskHabumi.ExitCode
}catch{
    $taskError=$_.Exception.Message
    try{
        [IO.Directory]::CreateDirectory((Join-Path $taskRoot 'guest/logs'))|Out-Null
        [IO.File]::WriteAllText((Join-Path $taskRoot 'guest/logs/launcher-error.txt'),$taskError,[Text.UTF8Encoding]::new($false))
    }catch{}
    if(-not $Validate){
        Add-Type -AssemblyName System.Windows.Forms
        [Windows.Forms.MessageBox]::Show($taskError,'Habumi startup')|Out-Null
    }
    throw $taskError
}
