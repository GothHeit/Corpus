<#
.SYNOPSIS
Puts the LGPL libmpv-2.dll, which Corpus plays videos with, into build-gui, where the build puts it inside Corpus.exe.

.DESCRIPTION
The build puts libmpv-2.dll inside Corpus.exe when it finds it in build-gui as it configures, so run this
before configuring (or configure again after). Without it videos play only with the DLL next to the .exe, and
otherwise open in their default program.

Corpus uses its own build of libmpv, with the GPL parts of mpv and FFmpeg left out so it is LGPL 2.1 or later.
It is made with mpv-winbuild-cmake plus tools/libmpv-lgpl/recipe-changes.patch (see the README for the steps),
which gives a libmpv-2.dll. This script copies that file (a path, or an https address you host it at) to the
build folder, checking its SHA-256 when one is given.

From WSL:
    powershell.exe -NoProfile -File "$(wslpath -w tools/get-libmpv.ps1)" -Source <libmpv-2.dll or its address>
Or from a shell with the folder of the built DLL: cp <build>/libmpv-2.dll build-gui/

.PARAMETER Source
The libmpv-2.dll to use: a file, or an https address. Defaults to the CORPUS_LIBMPV_DLL environment variable.

.PARAMETER Sha256
The hash the file must have; leave out to skip the check.

.PARAMETER Destination
The folder to put libmpv-2.dll in. Defaults to build-gui next to this folder.
#>
param(
    [string]$Source = $env:CORPUS_LIBMPV_DLL,
    [string]$Sha256 = '',
    [string]$Destination = (Join-Path $PSScriptRoot '..\build-gui')
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'  # the progress bar makes Invoke-WebRequest several times slower

if (-not $Source) {
    Write-Host "FAIL: no libmpv-2.dll given. Pass -Source <file or https address> or set CORPUS_LIBMPV_DLL."
    exit 1
}

$Destination = [IO.Path]::GetFullPath($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Destination))
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$target = Join-Path $Destination 'libmpv-2.dll'

$scratch = Join-Path $env:TEMP ('corpus-libmpv-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
try {
    $file = Join-Path $scratch 'libmpv-2.dll'
    if ($Source -match '^https://') {
        Write-Host "Downloading $Source"
        Invoke-WebRequest -Uri $Source -OutFile $file -UserAgent 'Wget' -UseBasicParsing
    }
    else {
        Copy-Item -LiteralPath $Source -Destination $file
    }

    if ($Sha256) {
        $hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
        if ($hash -ne $Sha256) {
            Write-Host "FAIL: $Source has SHA-256 $hash, expected $Sha256."
            exit 1
        }
    }
    Copy-Item -LiteralPath $file -Destination $target -Force
    Write-Host "OK: $target"
}
finally {
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
}
