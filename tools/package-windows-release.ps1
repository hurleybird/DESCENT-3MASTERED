[CmdletBinding()]
param(
    [string]$OutputDirectory = "",
    [switch]$KeepBuildDirectories
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$engineBrand = Join-Path $root "lib\enginebrand.h"
$versionMatch = Select-String -LiteralPath $engineBrand `
    -Pattern '^\s*#define\s+ENGINE_VERSION_STRING\s+"([^"]+)"\s*$'
if (-not $versionMatch) {
    throw "Unable to read ENGINE_VERSION_STRING from $engineBrand"
}

$version = $versionMatch.Matches[0].Groups[1].Value
$archiveVersion = $version -replace '[^A-Za-z0-9._-]+', '-'
$packageName = "Descent-3MASTERED-$archiveVersion-Windows-x64"

if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $root "out\release"
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)

$workRoot = Join-Path $root "out\release-work"
$x64Build = Join-Path $workRoot "x64"
$x86Build = Join-Path $workRoot "x86"
$stageParent = Join-Path $workRoot "stage"
$stageRoot = Join-Path $stageParent $packageName
$archivePath = Join-Path $OutputDirectory "$packageName.zip"
$hashPath = "$archivePath.sha256"

function Assert-PathBelow {
    param([string]$Path, [string]$Parent)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullParent = [System.IO.Path]::GetFullPath($Parent).TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($fullParent, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify path outside $Parent`: $Path"
    }
}

function Remove-ReleasePath {
    param([string]$Path)

    if (Test-Path -LiteralPath $Path) {
        Assert-PathBelow -Path $Path -Parent (Join-Path $root "out")
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
}

function Remove-ReleaseFile {
    param([string]$Path)

    if (Test-Path -LiteralPath $Path) {
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
            throw "Refusing to remove non-file release output: $Path"
        }
        Remove-Item -LiteralPath $Path -Force
    }
}

function Find-VsDevCmd {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} `
        "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $installPath = (& $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath | Select-Object -First 1)
        if ($installPath) {
            $candidate = Join-Path $installPath "Common7\Tools\VsDevCmd.bat"
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }

    $fallbacks = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"
    )
    foreach ($candidate in $fallbacks) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }
    throw "Visual Studio 2022 with the x86/x64 C++ tools was not found."
}

function Invoke-VsCommand {
    param(
        [ValidateSet("x64", "x86")]
        [string]$Architecture,
        [string]$Command
    )

    $cmdLine = 'call "{0}" -no_logo -arch={1} -host_arch=x64 && {2}' -f `
        $script:vsDevCmd, $Architecture, $Command
    & $env:ComSpec /d /s /c $cmdLine
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed for $Architecture with exit code $LASTEXITCODE`: $Command"
    }
}

function Get-PeMachine {
    param([string]$Path)

    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $reader = [System.IO.BinaryReader]::new($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) {
            throw "$Path is not a PE file."
        }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadUInt32()
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw "$Path has an invalid PE header."
        }
        return $reader.ReadUInt16()
    }
    finally {
        $stream.Dispose()
    }
}

$vsDevCmd = Find-VsDevCmd

if (-not $KeepBuildDirectories) {
    Remove-ReleasePath $workRoot
}
else {
    Remove-ReleasePath $stageParent
}
New-Item -ItemType Directory -Force -Path $x64Build, $x86Build, `
    $stageRoot, $OutputDirectory | Out-Null

$quotedRoot = '"' + $root + '"'
$quotedX64 = '"' + $x64Build + '"'
$quotedX86 = '"' + $x86Build + '"'
$quotedStage = '"' + $stageRoot + '"'

Invoke-VsCommand x64 (
    "cmake -S $quotedRoot -B $quotedX64 -G Ninja " +
    "-DCMAKE_BUILD_TYPE=Release -DD3_GAMEDIR=./"
)
Invoke-VsCommand x64 "cmake --build $quotedX64 --config Release --parallel"
Invoke-VsCommand x64 "cmake --install $quotedX64 --config Release --prefix $quotedStage"

Invoke-VsCommand x86 (
    "cmake -S $quotedRoot -B $quotedX86 -G Ninja " +
    "-DCMAKE_BUILD_TYPE=Release -DD3_GAMEDIR=./"
)
Invoke-VsCommand x86 (
    "cmake --build $quotedX86 --target OsirisHost32 --config Release --parallel"
)
Copy-Item -LiteralPath (Join-Path $x86Build "osiris_bridge\OsirisHost32.exe") `
    -Destination $stageRoot

$requiredFiles = @(
    "Descent 3MASTERED.exe",
    "OsirisHost32.exe",
    "OpenAL32.dll",
    "wooting_analog_sdk_dist.dll",
    "dmfc.dll",
    "descent3mastered.hog",
    "descent3mastered-win.hog",
    "README.md",
    "INSTALL-WINDOWS.txt",
    "LICENSE.txt",
    "THIRD-PARTY-LICENSES.txt",
    "online\Direct TCP~IP.piccucon",
    "online\Tracker.piccucon",
    "netgames\anarchy.d3m",
    "netgames\coop.d3m",
    "netgames\ctf.d3m",
    "netgames\entropy.d3m",
    "netgames\hoard.d3m",
    "netgames\hyperanarchy.d3m",
    "netgames\monsterball.d3m",
    "netgames\roboanarchy.d3m",
    "netgames\tanarchy.d3m"
)
foreach ($relativePath in $requiredFiles) {
    $candidate = Join-Path $stageRoot $relativePath
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "Required release file is missing: $relativePath"
    }
}

$gameMachine = Get-PeMachine (Join-Path $stageRoot "Descent 3MASTERED.exe")
$bridgeMachine = Get-PeMachine (Join-Path $stageRoot "OsirisHost32.exe")
if ($gameMachine -ne 0x8664) {
    throw "Descent 3MASTERED.exe is not x64 (PE machine 0x$($gameMachine.ToString('X4')))."
}
if ($bridgeMachine -ne 0x014C) {
    throw "OsirisHost32.exe is not x86 (PE machine 0x$($bridgeMachine.ToString('X4')))."
}

Remove-ReleaseFile $archivePath
Remove-ReleaseFile $hashPath
Push-Location $stageRoot
try {
    $archiveEntries = Get-ChildItem -LiteralPath $stageRoot -Force |
        Sort-Object Name |
        ForEach-Object { $_.Name }
    & cmake -E tar cf $archivePath --format=zip @archiveEntries
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to create $archivePath"
    }
}
finally {
    Pop-Location
}

$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $archivePath
"$($hash.Hash)  $([System.IO.Path]::GetFileName($archivePath))" |
    Set-Content -LiteralPath $hashPath -Encoding ascii

Write-Host ""
Write-Host "Release candidate:"
Write-Host "  $archivePath"
Write-Host "SHA-256:"
Write-Host "  $($hash.Hash)"
