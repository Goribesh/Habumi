# SPDX-License-Identifier: GPL-2.0-only
# Loaded inside Profile-Data.psm1 to share its guard and recovery contract.
function Get-HabumiQemuProcesses {
    return @(Get-CimInstance -ClassName Win32_Process -Filter "Name='qemu-nostro.exe' OR Name='qemu-system-aarch64.exe'" -ErrorAction Stop)
}
function Open-HabumiMaintenanceGuard {
    # Share the existing shell singleton name. Object creation, rather than a
    # later ownership wait, is the handshake used by both old and new shells.
    # Keep the handle throughout growth so a normal Habumi startup cannot race.
    $created=$false;$hostGuard=$null
    try{
        $hostGuard=[Threading.Mutex]::new($false,'Local\AndroidRuntimeGuscio.istanza',[ref]$created)
        if(-not $created){throw 'Close all Habumi windows before growing storage. Another runtime or maintenance operation is active.'}
        try{$running=@(Get-HabumiQemuProcesses)}catch{throw 'Cannot verify that QEMU is stopped. Storage growth was not started.'}
        if($running.Count){throw 'Close all Habumi/QEMU instances before growing storage, including other profiles and leftover processes.'}
        return $hostGuard
    }catch{if($hostGuard){$hostGuard.Dispose()};throw}
}
function Get-HabumiExt4([string]$Path){
    $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
    $super=New-Object byte[] 2048
    try{
        if($stream.Read($super,0,$super.Length) -ne $super.Length -or [BitConverter]::ToUInt16($super,1080) -ne 0xef53){throw 'Data must be a raw ext4 filesystem, without a partition table.'}
        $length=$stream.Length
    }finally{$stream.Dispose()}
    $shift=[BitConverter]::ToUInt32($super,1048)
    if($shift -gt 6){throw 'Unsupported ext4 block size.'}
    $blockSize=1024L -shl $shift
    $incompat=[BitConverter]::ToUInt32($super,1120)
    $readOnlyCompat=[BitConverter]::ToUInt32($super,1124)
    $blocks=[long][BitConverter]::ToUInt32($super,1028)
    if(($incompat -band 128) -ne 0){$blocks+=4294967296L*[BitConverter]::ToUInt32($super,1360)}
    if($blocks -le 0 -or $blocks -gt [long]::MaxValue/$blockSize){throw 'Invalid ext4 geometry.'}
    return [pscustomobject]@{Bytes=$length;FilesystemBytes=$blocks*$blockSize;BlockSize=$blockSize;
        UUID=[BitConverter]::ToString($super,1128,16);
        Compat=[BitConverter]::ToUInt32($super,1116);
        Incompat=([long]$incompat -band (-bnot 4L));ReadOnlyCompat=([long]$readOnlyCompat -band (-bnot 65536L));
        NeedsRecovery=(($incompat -band 4) -ne 0);OrphansPresent=(($readOnlyCompat -band 65536) -ne 0)}
}
function Assert-HabumiGrowthGeometry($Before,$After,[long]$Bytes){
    if($After.Bytes -ne $Bytes -or $After.FilesystemBytes -ne $Bytes -or
       $After.UUID -ne $Before.UUID -or $After.BlockSize -ne $Before.BlockSize -or
       $After.Compat -ne $Before.Compat -or $After.Incompat -ne $Before.Incompat -or
       $After.ReadOnlyCompat -ne $Before.ReadOnlyCompat -or $After.NeedsRecovery -or $After.OrphansPresent){throw 'Growth changed filesystem identity/features or produced incorrect geometry; original retained.'}
}
function Get-HabumiMaintenanceTools([string]$Root){
    $receiptPath=Join-Path $Root 'guest/maintenance/maintenance-build.json'
    $receipt=Read-HabumiJson $receiptPath
    if($receipt.Schema -ne 'habumi.maintenance-build.v1' -or $receipt.Protocol -ne 1 -or
       $receipt.Architecture -ne 'aarch64' -or $receipt.E2fsprogs -ne '1.47.2' -or
       $receipt.ImageSHA256 -notmatch '^[a-fA-F0-9]{64}$'){throw 'Unsupported maintenance tool receipt.'}
    $image=Join-Path $Root 'guest/maintenance/maintenance-initramfs.img'
    Assert-HabumiPlainPath $image
    if((Get-FileHash -LiteralPath $image).Hash -ne $receipt.ImageSHA256){throw 'Maintenance image differs from its build receipt.'}
    $release=Read-HabumiJson (Join-Path $Root 'release-profile.json')
    foreach($relative in @('runtime/bin/qemu-nostro.exe','guest/images/kernel-guest-arm64')){
        $records=@($release.CriticalFiles|Where-Object Path -eq $relative)
        if($records.Count -ne 1 -or $records[0].SHA256 -notmatch '^[A-Fa-f0-9]{64}$'){throw 'Maintenance requires a verified packaged QEMU and kernel.'}
        $path=Join-Path $Root $relative;Assert-HabumiPlainPath $path
        if((Get-FileHash -LiteralPath $path).Hash -ne $records[0].SHA256){throw "Installed tool differs from release manifest: $relative"}
    }
    return $receipt
}
function Invoke-HabumiMaintenance([string]$Root,[string]$Transaction,[string]$Relative,[int]$TimeoutSeconds){
    # Expand-HabumiData holds the session runtime gate for this entire call.
    # All QEMU arguments are fixed or validated profile-relative paths. This also
    # avoids drive-option comma parsing and Windows argument quoting ambiguity.
    if($Transaction -notmatch '^[a-f0-9]{32}$' -or $Relative -notin @('guest/images/android/data.img','guest/images/android/data-gapps.img')){throw 'Invalid maintenance target.'}
    $relativeImage='maintenance/'+$Transaction+'/new/'+$Relative
    $relativeLog='maintenance/'+$Transaction+'/serial.log'
    $start=[Diagnostics.ProcessStartInfo]::new()
    $start.FileName=Join-Path $Root 'runtime/bin/qemu-nostro.exe'
    $start.WorkingDirectory=$Root;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    # Offline maintenance needs no hardware acceleration. Keep it outside WHPX
    # after the unexplained host watchdog reset; the Android runtime is separate.
    $start.Arguments='-M virt -accel tcg,thread=single -cpu max -m 512 -smp 1 -nodefaults -no-reboot '+
        '-kernel guest/images/kernel-guest-arm64 -initrd guest/maintenance/maintenance-initramfs.img '+
        '-append "console=ttyAMA0 panic=-1 habumi.maintenance='+$Transaction+'" '+
        '-drive file='+$relativeImage+',format=raw,if=virtio,cache=writeback,aio=threads '+
        '-display none -monitor none -serial file:'+$relativeLog
    $process=[Diagnostics.Process]::Start($start)
    try{
        $out=$process.StandardOutput.ReadToEndAsync();$err=$process.StandardError.ReadToEndAsync()
        if(-not $process.WaitForExit($TimeoutSeconds*1000)){
            $process.Kill();$process.WaitForExit()
            throw 'Maintenance timed out; only the staged disk was used. Original retained.'
        }
        $text=$out.Result+"`n"+$err.Result
        [IO.File]::WriteAllText((Join-Path $Root ('maintenance/'+$Transaction+'/qemu.log')),$text)
        $log=Join-Path $Root $relativeLog
        if($process.ExitCode -ne 0 -or -not(Test-Path -LiteralPath $log) -or
           -not(@([IO.File]::ReadAllLines($log)) -contains ('HABUMI-MAINT v1 '+$Transaction+' OK'))){throw 'Maintenance failed; inspect serial.log and qemu.log. Original retained.'}
    }finally{if(-not $process.HasExited){$process.Kill();$process.WaitForExit()};$process.Dispose()}
}
function Expand-HabumiData {
    [CmdletBinding()]param([Parameter(Mandatory)][string]$ProfileRoot,
        [Parameter(Mandatory)][ValidateSet('data.img','data-gapps.img')][string]$Image,
        [Parameter(Mandatory)][ValidateRange(4,16)][int]$SizeGiB,
        [ValidateRange(10,3600)][int]$TimeoutSeconds=600)
    $root=Get-HabumiRoot $ProfileRoot;$guard=Open-HabumiDataGuard $root;$marked=$false;$hostGuard=$null
    try{
        Report-HabumiDataProgress 'Checking runtime' 'All Habumi and QEMU instances must be stopped before storage growth'
        $hostGuard=Open-HabumiMaintenanceGuard
        $relative='guest/images/android/'+$Image;$target=Get-HabumiDataPath $root $relative
        $before=Get-HabumiExt4 $target;$bytes=[long]$SizeGiB*1GB
        if($bytes -le $before.Bytes -or $before.FilesystemBytes -ne $before.Bytes){throw 'Growth requires a larger target and a filesystem occupying the current raw image.'}
        Assert-HabumiSpace $root $bytes
        $tools=Get-HabumiMaintenanceTools $root
        $id=[guid]::NewGuid().ToString('N');$directory=Join-Path $root ('maintenance/'+$id)
        Assert-HabumiPlainPath $directory
        $stage=Get-HabumiDataPath (Join-Path $directory 'new') $relative
        $original=Copy-HabumiFile $target $stage
        $stream=[IO.File]::Open($stage,[IO.FileMode]::Open,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try{$stream.SetLength($bytes);$stream.Flush($true)}finally{$stream.Dispose()}
        Report-HabumiDataProgress 'Growing filesystem' 'Offline ext4 checks and resize; this may take several minutes'
        Invoke-HabumiMaintenance $root $id $relative $TimeoutSeconds
        $after=Get-HabumiExt4 $stage;Assert-HabumiGrowthGeometry $before $after $bytes
        Report-HabumiDataProgress 'Verifying enlarged disk' 'Checking identity and hashing the complete image'
        $new=Get-HabumiOriginalRecord $stage
        Assert-HabumiFile $target $original
        # Also check exclusive access before publishing the durable recovery marker.
        $current=Get-HabumiOriginalRecord $target
        if($current.SHA256 -ne $original.SHA256 -or $current.Bytes -ne $original.Bytes){throw 'Original data changed during maintenance; not replaced.'}
        Write-HabumiJson (Join-Path $directory 'growth.json') @{Schema='habumi.growth.v1';Image=$relative;Before=$before;After=$after;Tools=$tools;Execution=@{Accelerator='tcg';Thread='single';Cpu='max';MemoryMiB=512;Vcpu=1}}
        $journal=[ordered]@{Schema='habumi.restore.v1';Operation='Grow';Transaction=$id;Phase='Replacing';StartedUtc=[DateTime]::UtcNow.ToString('o');
            Files=@(@{Path=$relative;HadOriginal=$true;Original=$original;HasNew=$true;New=$new})}
        Write-HabumiJson (Join-Path $directory 'journal.json') $journal
        Write-HabumiJson (Join-Path $root 'storage-maintenance.lock') @{Schema='habumi.maintenance.v1';Transaction=$id}
        $marked=$true
        Report-HabumiDataProgress 'Replacing disk' 'The original image is retained for recovery'
        $saved=Get-HabumiDataPath (Join-Path $directory 'original') $relative
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($saved)) -Force|Out-Null
        [IO.File]::Move($target,$saved);[IO.File]::Move($stage,$target)
        $journal.Phase='Committed';Write-HabumiJson (Join-Path $directory 'journal.json') $journal
        [IO.File]::Delete((Join-Path $root 'storage-maintenance.lock'));$marked=$false
        return [pscustomobject]@{Profile=$root;Image=$Image;Bytes=$bytes;Phase='Committed';PreviousImage=$saved;Transaction=$id}
    }catch{
        $failure=$_
        if($marked){try{$null=Repair-HabumiUnderGuard $root}catch{throw ('Recovery required; originals retained in '+$directory+'. '+$_.Exception.Message)}}
        throw $failure
    }finally{if($hostGuard){$hostGuard.Dispose()};$guard.Dispose()}
}
