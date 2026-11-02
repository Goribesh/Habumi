# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$OutputPath,
    [string]$SdkRoot=(Join-Path $env:LOCALAPPDATA 'Android\Sdk'),
    [string]$JdkRoot='C:\Program Files\Java\jdk-25.0.2'
)
$ErrorActionPreference='Stop'

$outputPath=[IO.Path]::GetFullPath($OutputPath)
$outputDirectory=[IO.Path]::GetDirectoryName($outputPath)
$provenancePath=$outputPath+'.json'
$workDirectory=$outputPath+'.work'
$sdkRoot=[IO.Path]::GetFullPath($SdkRoot)
$jdkRoot=[IO.Path]::GetFullPath($JdkRoot)
$androidJar=Join-Path $sdkRoot 'platforms\android-36.1\android.jar'
$d8Jar=Join-Path $sdkRoot 'build-tools\36.1.0\lib\d8.jar'
$java=Join-Path $jdkRoot 'bin\java.exe'
$javac=Join-Path $jdkRoot 'bin\javac.exe'
$jar=Join-Path $jdkRoot 'bin\jar.exe'
$source=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\app-list\AppList.java'))

if(-not (Test-Path -LiteralPath $outputDirectory -PathType Container)){
    throw "Output directory does not exist: $outputDirectory"
}
foreach($path in @($outputPath,$provenancePath,$workDirectory)){
    if(Test-Path -LiteralPath $path){throw "Refusing to overwrite existing output: $path"}
}
foreach($path in @($androidJar,$d8Jar,$java,$javac,$jar,$source)){
    if(-not (Test-Path -LiteralPath $path -PathType Leaf)){
        throw "Required build input is missing: $path"
    }
}

$classes=Join-Path $workDirectory 'classes'
$dex=Join-Path $workDirectory 'dex'
New-Item -ItemType Directory -Path $classes,$dex -Force|Out-Null
& $javac '--release' '11' '-Xlint:all' '-classpath' $androidJar '-d' $classes $source
if($LASTEXITCODE -ne 0){throw "javac failed with exit code $LASTEXITCODE"}
$classFiles=@(Get-ChildItem -LiteralPath $classes -Filter '*.class' -Recurse|ForEach-Object {$_.FullName})
if($classFiles.Count -eq 0){throw 'javac produced no class files.'}
& $java '-cp' $d8Jar 'com.android.tools.r8.D8' '--release' '--min-api' '26' '--lib' $androidJar '--output' $dex @classFiles
if($LASTEXITCODE -ne 0){throw "D8 failed with exit code $LASTEXITCODE"}
$dexFile=Join-Path $dex 'classes.dex'
if(-not (Test-Path -LiteralPath $dexFile -PathType Leaf)){throw 'D8 did not produce classes.dex.'}
& $jar "-J-Djava.io.tmpdir=$workDirectory" '--create' '--file' $outputPath '-C' $dex 'classes.dex'
if($LASTEXITCODE -ne 0){throw "jar failed with exit code $LASTEXITCODE"}

$javaVersion=(& $java '-version' 2>&1|Out-String).Trim()
$javacVersion=(& $javac '-version' 2>&1|Out-String).Trim()
$d8Version=(& $java '-cp' $d8Jar 'com.android.tools.r8.D8' '--version' 2>&1|Out-String).Trim()
$report=[ordered]@{
    recipe='habumi-app-list-dexjar-v1'
    javaSourceSha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    androidJarSha256=(Get-FileHash -LiteralPath $androidJar -Algorithm SHA256).Hash
    outputJarSha256=(Get-FileHash -LiteralPath $outputPath -Algorithm SHA256).Hash
    outputJar=$outputPath
    javaPath=$java
    javaVersion=$javaVersion
    javacPath=$javac
    javacVersion=$javacVersion
    d8Jar=$d8Jar
    d8Version=$d8Version
    d8JarSha256=(Get-FileHash -LiteralPath $d8Jar -Algorithm SHA256).Hash
    apiMinimum=26
    installedOrRun=$false
}
[IO.File]::WriteAllText($provenancePath,(($report|ConvertTo-Json -Depth 4)+"`n"),[Text.UTF8Encoding]::new($false))
Write-Output "Runtime tools jar: $outputPath"
Write-Output "Build provenance: $provenancePath"
