<#
.SYNOPSIS
Downloads libmpv-2.dll, which corpus-gui loads to play videos, and puts it next to corpus-gui.exe.

.DESCRIPTION
Corpus doesn't ship libmpv: it loads libmpv-2.dll from its own folder the first time a video opens, and without
it videos open in their default program. This fetches a pinned prebuilt build for Windows x64 (shinchiro's
mpv-winbuild-cmake, as mirrored on SourceForge by the mpv-player-windows project), checks its SHA-256 and
extracts the DLL with the tar.exe that comes with Windows 10 and later.

From WSL:
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w tools/get-libmpv.ps1)"

.PARAMETER Destination
The folder to put libmpv-2.dll in. Defaults to build-gui next to this folder.
#>
param(
    [string]$Destination = (Join-Path $PSScriptRoot '..\build-gui')
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'  # the progress bar makes Invoke-WebRequest several times slower

$name = 'mpv-dev-x86_64-20261018-git-01e2533f31.7z'
$url = "https://sourceforge.net/projects/mpv-player-windows/files/libmpv/$name/download"
$sha256 = 'D7F5621F42B6E161FE032DF914A10BDECE28A89DF3E57C6C3E16A5A250929B5E'

$Destination = [IO.Path]::GetFullPath($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Destination))
New-Item -ItemType Directory -Force -Path $Destination | Out-Null

$scratch = Join-Path $env:TEMP ('corpus-libmpv-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
try {
    $archive = Join-Path $scratch $name
    Write-Host "Downloading $name"
    # SourceForge answers browsers with a page; a plain client gets the file.
    Invoke-WebRequest -Uri $url -OutFile $archive -UserAgent 'Wget' -UseBasicParsing

    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    if ($hash -ne $sha256) {
        Write-Host "FAIL: $name has SHA-256 $hash, expected $sha256."
        exit 1
    }

    & "$env:SystemRoot\System32\tar.exe" -x -f $archive -C $scratch libmpv-2.dll
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAIL: tar.exe could not extract libmpv-2.dll."
        exit 1
    }
    Copy-Item -LiteralPath (Join-Path $scratch 'libmpv-2.dll') -Destination $Destination -Force
    Write-Host "OK: $(Join-Path $Destination 'libmpv-2.dll')"
}
finally {
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
}
