# Verifica che l'ambiente sia quello previsto dalla spec e fornisce
# Get-BinaryArch, usata dai task successivi per scartare binari emulati.

function Get-BinaryArch {
    param([Parameter(Mandatory)][string]$Path)
    if (-not (Test-Path $Path)) { return "unknown" }

    # Altre due verifiche chiamano questa funzione su binari appena scaricati, che
    # possono essere troncati o corrotti. Senza finally un'eccezione durante
    # la lettura lascerebbe l'handle aperto: da qui il try/finally.
    $fs = $null
    $br = $null
    try {
        $fs = [System.IO.File]::OpenRead($Path)
        $br = New-Object System.IO.BinaryReader($fs)
        $fs.Seek(0x3C, 'Begin') | Out-Null
        $peOffset = $br.ReadInt32()
        if ($peOffset -le 0 -or $peOffset -ge $fs.Length) { return "unknown" }
        $fs.Seek($peOffset, 'Begin') | Out-Null
        $sig = $br.ReadUInt32()          # 'PE\0\0' = 0x00004550
        if ($sig -ne 0x00004550) { return "unknown" }
        $machine = $br.ReadUInt16()
        switch ($machine) {
            0xAA64 { return "ARM64" }
            0xA641 { return "ARM64EC" }
            0x8664 { return "x64" }
            0x014C { return "x86" }
            default { return "unknown" }
        }
    } catch {
        return "unknown"
    } finally {
        if ($br) { $br.Dispose() }
        if ($fs) { $fs.Dispose() }
    }
}

function Invoke-EnvCheck {
    param([string]$OutFile)

    $cv  = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    $cpu = (Get-CimInstance Win32_Processor).Name
    $whp = Test-Path (Join-Path $env:SystemRoot 'System32\WinHvPlatform.dll')

    $env = [ordered]@{
        build       = $cv.CurrentBuild
        ubr         = [int]$cv.UBR
        displayVer  = $cv.DisplayVersion
        cpu         = $cpu
        whpPresent  = [bool]$whp
        psArch      = $env:PROCESSOR_ARCHITECTURE
        timestamp   = (Get-Date -Format 'o')
    }

    $env | ConvertTo-Json | Out-File -FilePath $OutFile -Encoding utf8
    return $env
}

if ($MyInvocation.InvocationName -ne '.') {
    $outDir = Join-Path $PSScriptRoot '..\results'
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    $r = Invoke-EnvCheck -OutFile (Join-Path $outDir 'env.json')

    "Build      : {0}.{1} ({2})" -f $r.build, $r.ubr, $r.displayVer
    "CPU        : {0}" -f $r.cpu
    "PS arch    : {0}" -f $r.psArch
    "WHP        : {0}" -f $r.whpPresent

    $ok = $true
    if ([int]$r.build -lt 26100 -or ([int]$r.build -eq 26100 -and $r.ubr -lt 3915)) {
        "FAIL: build sotto il minimo per WHP ARM64 (26100.3915)"; $ok = $false
    }
    if ($r.psArch -ne 'ARM64') {
        "FAIL: PowerShell non e' ARM64 ($($r.psArch)) - le misure sarebbero falsate"; $ok = $false
    }
    if (-not $r.whpPresent) { "FAIL: WinHvPlatform.dll assente"; $ok = $false }

    if ($ok) { "ESITO: ambiente idoneo" } else { "ESITO: ambiente NON idoneo"; exit 1 }
}
