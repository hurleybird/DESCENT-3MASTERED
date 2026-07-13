param(
    [string]$Configuration = "Release",
    [string]$Version = "",
    [string]$OpenALDll = "",
    [string]$OpenALLicense = "",
    [string]$ReadmePath = "",
    [switch]$NoSymbols
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildDir = Join-Path $Root "out\build\x86-$Configuration"

if ($Version -eq "") {
    $Version = (git -C $Root describe --tags --dirty=-m).Trim()
}

if ($ReadmePath -eq "") {
    $CandidateReadme = Join-Path $Root "tools\release_readme.md"
    if (Test-Path $CandidateReadme) {
        $ReadmePath = $CandidateReadme
    } else {
        $ReadmePath = Join-Path $Root "README.md"
    }
}

$StageRoot = Join-Path $Root "out\package"
$StageDir = Join-Path $StageRoot "PiccuExperiment-$Version-win32"
$ZipPath = Join-Path $StageRoot "PiccuExperiment-$Version-win32.zip"

if (Test-Path $StageDir) {
    Remove-Item -LiteralPath $StageDir -Recurse -Force
}
New-Item -ItemType Directory -Path $StageDir | Out-Null
New-Item -ItemType Directory -Path (Join-Path $StageDir "online") | Out-Null

$Files = @(
    @{ Source = Join-Path $BuildDir "PiccuEngine.exe"; Destination = "PiccuEngine.exe" },
    @{ Source = Join-Path $BuildDir "piccuengine.hog"; Destination = "piccuengine.hog" },
    @{ Source = Join-Path $BuildDir "dmfc.dll"; Destination = "dmfc.dll" },
    @{ Source = Join-Path $Root "LICENSE"; Destination = "LICENSE" },
    @{ Source = $ReadmePath; Destination = "readme.md" },
    @{ Source = Join-Path $BuildDir "netcon\lanclient\Direct TCP~IP.piccucon"; Destination = "online\Direct TCP~IP.piccucon" },
    @{ Source = Join-Path $BuildDir "netcon\trackclient\Tracker.piccucon"; Destination = "online\Tracker.piccucon" }
)

if (!$NoSymbols) {
    $Files += @(
        @{ Source = Join-Path $BuildDir "PiccuEngine.pdb"; Destination = "PiccuEngine.pdb" },
        @{ Source = Join-Path $BuildDir "dmfc.pdb"; Destination = "dmfc.pdb" },
        @{ Source = Join-Path $BuildDir "netcon\lanclient\Direct TCP~IP.pdb"; Destination = "online\Direct TCP~IP.pdb" },
        @{ Source = Join-Path $BuildDir "netcon\trackclient\Tracker.pdb"; Destination = "online\Tracker.pdb" }
    )
}

if ($OpenALDll -eq "") {
    $DefaultOpenAL = Join-Path $Root "thirdparty\OpenAL\bin\Win32\OpenAL32.dll"
    if (Test-Path $DefaultOpenAL) {
        $OpenALDll = $DefaultOpenAL
    }
}

if ($OpenALDll -ne "") {
    $Files += @{ Source = $OpenALDll; Destination = "OpenAL32.dll" }
    if ($OpenALLicense -eq "") {
        $DefaultOpenALLicense = Join-Path $Root "thirdparty\OpenAL\COPYING"
        if (Test-Path $DefaultOpenALLicense) {
            $OpenALLicense = $DefaultOpenALLicense
        }
    }
    if ($OpenALLicense -ne "") {
        $Files += @{ Source = $OpenALLicense; Destination = "OpenAL-COPYING.txt" }
    }
} else {
    Write-Warning "OpenAL32.dll was not staged. Pass -OpenALDll <path> or place it at thirdparty\OpenAL\bin\Win32\OpenAL32.dll."
}

foreach ($File in $Files) {
    if (!(Test-Path $File.Source)) {
        throw "Missing package input: $($File.Source)"
    }
    Copy-Item -LiteralPath $File.Source -Destination (Join-Path $StageDir $File.Destination)
}

if (Test-Path $ZipPath) {
    Remove-Item -LiteralPath $ZipPath -Force
}
Compress-Archive -Path (Join-Path $StageDir "*") -DestinationPath $ZipPath -CompressionLevel Optimal

Write-Host "Created $ZipPath"
