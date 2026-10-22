<#
.SYNOPSIS
Starts corpus-gui.exe on a scratch library, checks that its window opens without an error dialog, saves a
screenshot of it and closes it.

.DESCRIPTION
The library is a new database in a folder under %TEMP%, deleted afterwards, so the real catalog in
%LOCALAPPDATA%\Corpus is never touched. It has to be a local path: SQLite's file locks don't work on
\\wsl.localhost, where a relative path would land when the script is started from WSL.

Exits with 0 if the window opened, showed no error dialog and closed cleanly; otherwise prints why and exits
with 1. If an error dialog appears, the screenshot shows the dialog instead of the window.

From WSL:
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w tools/smoke-test.ps1)"

.PARAMETER Exe
The corpus-gui.exe to test. Defaults to build-gui\corpus-gui.exe next to this folder.

.PARAMETER Screenshot
Where to save the PNG. Defaults to smoke-test.png next to the .exe.

.PARAMETER Timeout
How many seconds to wait for the window to appear.
#>
param(
    [string]$Exe = (Join-Path $PSScriptRoot '..\build-gui\corpus-gui.exe'),
    [string]$Screenshot = '',
    [int]$Timeout = 15
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Text;

public static class SmokeTest
{
    delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    struct RECT { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll")] static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr hWnd, StringBuilder name, int size);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int size);
    [DllImport("user32.dll")] static extern IntPtr GetDlgItem(IntPtr hDlg, int id);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);

    const uint PW_RENDERFULLCONTENT = 2;
    const uint WM_CLOSE = 0x0010;
    const int MessageBoxTextId = 0xFFFF;

    // The visible top-level windows of a process.
    public static IntPtr[] VisibleWindows(int processId)
    {
        var windows = new List<IntPtr>();
        EnumWindows((hWnd, lParam) =>
        {
            uint owner;
            GetWindowThreadProcessId(hWnd, out owner);
            if (owner == (uint)processId && IsWindowVisible(hWnd))
                windows.Add(hWnd);
            return true;
        }, IntPtr.Zero);
        return windows.ToArray();
    }

    public static string ClassName(IntPtr hWnd)
    {
        var name = new StringBuilder(256);
        GetClassName(hWnd, name, name.Capacity);
        return name.ToString();
    }

    public static string Text(IntPtr hWnd)
    {
        var text = new StringBuilder(4096);
        GetWindowText(hWnd, text, text.Capacity);
        return text.ToString();
    }

    // A MessageBox keeps its message in a static control with this id.
    public static string MessageBoxText(IntPtr hDlg)
    {
        IntPtr label = GetDlgItem(hDlg, MessageBoxTextId);
        return label == IntPtr.Zero ? "" : Text(label);
    }

    // PrintWindow asks the window to draw itself, so the capture is right even if another window covers it.
    public static void SavePng(IntPtr hWnd, string path)
    {
        // Per-monitor aware, like corpus-gui, so the rectangle is in real pixels on a scaled display.
        SetThreadDpiAwarenessContext(new IntPtr(-4));
        RECT rect;
        if (!GetWindowRect(hWnd, out rect) || rect.Right <= rect.Left || rect.Bottom <= rect.Top)
            throw new Exception("Could not get the window's size.");
        using (var bitmap = new Bitmap(rect.Right - rect.Left, rect.Bottom - rect.Top))
        {
            using (var graphics = Graphics.FromImage(bitmap))
            {
                IntPtr hdc = graphics.GetHdc();
                bool ok = PrintWindow(hWnd, hdc, PW_RENDERFULLCONTENT);
                graphics.ReleaseHdc(hdc);
                if (!ok)
                    throw new Exception("PrintWindow failed.");
            }
            bitmap.Save(path, System.Drawing.Imaging.ImageFormat.Png);
        }
    }

    public static void Close(IntPtr hWnd)
    {
        PostMessage(hWnd, WM_CLOSE, IntPtr.Zero, IntPtr.Zero);
    }
}
'@

$mainWindowClass = 'Kafkas Window Class'
$dialogClass = '#32770'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    Write-Host "FAIL: $Exe not found. Build it first."
    exit 1
}
$Exe = (Resolve-Path -LiteralPath $Exe).ProviderPath
if (-not $Screenshot) {
    $Screenshot = Join-Path (Split-Path $Exe) 'smoke-test.png'
}
$Screenshot = [System.IO.Path]::GetFullPath($Screenshot)

$scratch = Join-Path $env:TEMP ('corpus-smoke-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
$library = Join-Path $scratch 'corpus.db'

$failure = $null
$process = $null
try {
    $process = Start-Process -FilePath $Exe -ArgumentList @('--library', "`"$library`"") `
        -WorkingDirectory $scratch -PassThru

    # Waits for either the main window or a dialog: on a failed start main() shows a MessageBox with no
    # main window at all.
    $window = [IntPtr]::Zero
    $dialog = [IntPtr]::Zero
    $deadline = (Get-Date).AddSeconds($Timeout)
    while ($window -eq [IntPtr]::Zero -and $dialog -eq [IntPtr]::Zero) {
        if ($process.HasExited) {
            $failure = "corpus-gui exited with code $($process.ExitCode) before showing its window."
            break
        }
        if ((Get-Date) -gt $deadline) {
            $failure = "No window appeared within $Timeout seconds."
            break
        }
        foreach ($hWnd in [SmokeTest]::VisibleWindows($process.Id)) {
            $class = [SmokeTest]::ClassName($hWnd)
            if ($class -eq $dialogClass) { $dialog = $hWnd }
            elseif ($class -eq $mainWindowClass) { $window = $hWnd }
        }
        Start-Sleep -Milliseconds 100
    }

    # Lets the window finish its first paint, and anything it shows right after starting appear.
    if (-not $failure -and $dialog -eq [IntPtr]::Zero) {
        Start-Sleep -Milliseconds 1000
        foreach ($hWnd in [SmokeTest]::VisibleWindows($process.Id)) {
            if ([SmokeTest]::ClassName($hWnd) -eq $dialogClass) { $dialog = $hWnd }
        }
    }

    if ($dialog -ne [IntPtr]::Zero) {
        $text = [SmokeTest]::MessageBoxText($dialog)
        $failure = "A dialog appeared: " + ($text -replace '\s+', ' ')
        [SmokeTest]::SavePng($dialog, $Screenshot)
        Write-Host "Screenshot of the dialog: $Screenshot"
    }
    elseif (-not $failure) {
        [SmokeTest]::SavePng($window, $Screenshot)
        Write-Host "Screenshot: $Screenshot"

        [SmokeTest]::Close($window)
        if (-not $process.WaitForExit(5000)) {
            $failure = 'corpus-gui did not close within 5 seconds of WM_CLOSE.'
        }
        elseif ($process.ExitCode -ne 0) {
            $failure = "corpus-gui exited with code $($process.ExitCode)."
        }
    }
}
catch {
    $failure = $_.Exception.Message
}
finally {
    if ($process -and -not $process.HasExited) {
        $process.Kill()
        $process.WaitForExit()
    }
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
}

if ($failure) {
    Write-Host "FAIL: $failure"
    exit 1
}
Write-Host 'OK: the window opened with no error dialog and closed cleanly.'
exit 0
