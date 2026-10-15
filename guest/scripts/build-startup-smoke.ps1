# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$SdkRoot=(Join-Path $env:LOCALAPPDATA 'Android\Sdk'),
    [string]$JdkRoot='C:\Program Files\Java\jdk-25.0.2'
)
$ErrorActionPreference='Stop'

# Toolchain locale esplicita: nessun download o ricerca nella PATH.
$sdkRoot=[IO.Path]::GetFullPath($SdkRoot)
$buildTools=Join-Path $sdkRoot 'build-tools\36.1.0'
$androidJar=Join-Path $sdkRoot 'platforms\android-36.1\android.jar'
$jdkRoot=[IO.Path]::GetFullPath($JdkRoot)
$java=Join-Path $jdkRoot 'bin\java.exe'
$javac=Join-Path $jdkRoot 'bin\javac.exe'
$keytool=Join-Path $jdkRoot 'bin\keytool.exe'
$aapt2=Join-Path $buildTools 'aapt2.exe'
$aapt=Join-Path $buildTools 'aapt.exe'
$d8Jar=Join-Path $buildTools 'lib\d8.jar'
$zipalign=Join-Path $buildTools 'zipalign.exe'
$apksignerJar=Join-Path $buildTools 'lib\apksigner.jar'
$sourceRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\probe\startup-smoke'))
$manifest=Join-Path $sourceRoot 'AndroidManifest.xml'
$source=Join-Path $sourceRoot 'StartupSmokeActivity.java'
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $OutputDirectory){throw 'OutputDirectory must be new; refusing to overwrite existing output.'}
foreach($path in @($java,$javac,$keytool,$aapt2,$aapt,$d8Jar,$zipalign,$apksignerJar,$androidJar,$manifest,$source)){
    if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw "Required build input is missing: $path"}
}
New-Item -ItemType Directory -Path $OutputDirectory|Out-Null
$classes=Join-Path $OutputDirectory 'classes';$dex=Join-Path $OutputDirectory 'dex';$keys=Join-Path $OutputDirectory 'keys'
New-Item -ItemType Directory -Path $classes,$dex,$keys|Out-Null

function Quote-WinArg([string]$Value){
    if($Value -notmatch '[\s"]'){return $Value}
    $builder=[Text.StringBuilder]::new();[void]$builder.Append('"');$slashes=0
    foreach($ch in $Value.ToCharArray()){
        if($ch -eq '\'){$slashes++;continue}
        if($ch -eq '"'){[void]$builder.Append(('\'*(2*$slashes+1)));[void]$builder.Append('"');$slashes=0;continue}
        if($slashes){[void]$builder.Append(('\'*$slashes));$slashes=0};[void]$builder.Append($ch)
    }
    if($slashes){[void]$builder.Append(('\'*(2*$slashes)))}
    [void]$builder.Append('"');return $builder.ToString()
}
function Invoke-BuildTool([string]$Name,[string]$File,[string[]]$Arguments,[string]$WorkingDirectory=$OutputDirectory){
    $stdoutPath=Join-Path $OutputDirectory ($Name+'.stdout.txt');$stderrPath=Join-Path $OutputDirectory ($Name+'.stderr.txt')
    foreach($path in @($stdoutPath,$stderrPath)){if(Test-Path -LiteralPath $path){throw "Refusing to overwrite build log: $path"}}
    $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=$File;$psi.Arguments=(($Arguments|ForEach-Object {Quote-WinArg ([string]$_)}) -join ' ')
    $psi.WorkingDirectory=$WorkingDirectory;$psi.UseShellExecute=$false;$psi.CreateNoWindow=$true;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
    $proc=[Diagnostics.Process]::new();$proc.StartInfo=$psi;$outReader=$null;$errReader=$null;$started=$false
    try{
        if(-not $proc.Start()){throw "Could not start build step '$Name'."};$started=$true
        $outReader=$proc.StandardOutput;$errReader=$proc.StandardError
        $outTask=$outReader.ReadToEndAsync();$errTask=$errReader.ReadToEndAsync()
        if(-not $proc.WaitForExit(60000)){try{$proc.Kill()}catch{};[void]$proc.WaitForExit(3000);throw "Build step '$Name' exceeded 60 seconds and was stopped."}
        $exitCode=$proc.ExitCode
        $outDone=$outTask.Wait(1000);$errDone=$errTask.Wait(1000)
        if(-not $outDone -or -not $errDone){throw "Build step '$Name' output streams did not close within one second."}
        [IO.File]::WriteAllText($stdoutPath,$outTask.Result,[Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText($stderrPath,$errTask.Result,[Text.UTF8Encoding]::new($false))
        if($exitCode -ne 0){throw "Build step '$Name' failed with exit code $exitCode; see its captured output in $OutputDirectory."}
        Write-Output "PASS: $Name"
    }finally{
        if($outReader){$outReader.Dispose()};if($errReader){$errReader.Dispose()}
        if($started -and -not $proc.HasExited){try{$proc.Kill()}catch{}}
        $proc.Dispose()
    }
}

# Password fisso, pubblico e riservato esclusivamente alla chiave di prova temporanea.
$testPassword='habumi-test-only'
$keystore=Join-Path $keys 'startup-smoke-test.keystore'
$baseApk=Join-Path $OutputDirectory 'base.apk';$unalignedApk=Join-Path $OutputDirectory 'unaligned.apk'
$alignedApk=Join-Path $OutputDirectory 'aligned.apk';$apk=Join-Path $OutputDirectory 'HabumiStartupSmoke.apk'
Invoke-BuildTool 'keygen' $keytool @('-genkeypair','-noprompt','-keystore',$keystore,'-storetype','PKCS12','-storepass',$testPassword,'-keypass',$testPassword,'-alias','habumi-smoke','-dname','CN=Habumi Startup Smoke Test','-keyalg','RSA','-keysize','2048','-validity','30')
Invoke-BuildTool 'manifest-link' $aapt2 @('link','--manifest',$manifest,'--min-sdk-version','29','--target-sdk-version','35','-I',$androidJar,'-o',$baseApk)
Invoke-BuildTool 'java-compile' $javac @('--release','11','-Xlint:all','-classpath',$androidJar,'-d',$classes,$source)
$classFiles=@(Get-ChildItem -LiteralPath $classes -Filter '*.class' -Recurse|ForEach-Object {$_.FullName})
if($classFiles.Count -eq 0){throw 'javac produced no class files.'}
$d8Args=@('--release','--min-api','29','--lib',$androidJar,'--output',$dex)+$classFiles
Invoke-BuildTool 'dex-convert' $java (@('-cp',$d8Jar,'com.android.tools.r8.D8')+$d8Args)
$dexFile=Join-Path $dex 'classes.dex'
if(-not (Test-Path -LiteralPath $dexFile -PathType Leaf)){throw 'D8 did not produce classes.dex.'}
Invoke-BuildTool 'apk-add-dex' $aapt @('add','-f',$baseApk,'classes.dex') $dex
Move-Item -LiteralPath $baseApk -Destination $unalignedApk
Invoke-BuildTool 'zip-align' $zipalign @('-p','-f','4',$unalignedApk,$alignedApk)
Invoke-BuildTool 'apk-sign' $java @('-jar',$apksignerJar,'sign','--v4-signing-enabled','false','--ks',$keystore,'--ks-type','PKCS12','--ks-key-alias','habumi-smoke','--ks-pass',('pass:'+$testPassword),'--key-pass',('pass:'+$testPassword),'--min-sdk-version','29','--out',$apk,$alignedApk)
Invoke-BuildTool 'signature-verify' $java @('-jar',$apksignerJar,'verify','--verbose',$apk)
Invoke-BuildTool 'alignment-verify' $zipalign @('-c','-p','4',$apk)
$manifestText=Get-Content -LiteralPath $manifest -Raw
if($manifestText -match '<uses-permission\b'){throw 'Manifest contains an unexpected permission declaration.'}
$report=[pscustomobject]@{schema='habumi.startup-smoke-build.v1';createdUtc=[DateTime]::UtcNow.ToString('o');applicationId='org.habumi.probe.startup';apk=[IO.Path]::GetFileName($apk);apkSha256=(Get-FileHash -LiteralPath $apk -Algorithm SHA256).Hash;manifestSha256=(Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash;sourceSha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash;signing='ephemeral test keystore stored only in this output directory';v4SigningEnabled=$false;sdk='Android SDK platform 36.1 / build-tools 36.1.0';jdkRoot=$jdkRoot;installedOrRun=$false}
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'build-result.json'),(($report|ConvertTo-Json -Depth 5)+"`n"),[Text.UTF8Encoding]::new($false))
Write-Output "APK: $apk"
Write-Output "Build report: $(Join-Path $OutputDirectory 'build-result.json')"
Write-Output 'The APK was compiled and signed only; it was not installed or run.'
