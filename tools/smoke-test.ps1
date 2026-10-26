<#
.SYNOPSIS
Starts corpus-gui.exe on a scratch library, checks that its window opens without an error dialog, saves a
screenshot of it and closes it.

.DESCRIPTION
The library is a new database in a folder under %TEMP%, deleted afterwards, so the real catalog in
%LOCALAPPDATA%\Corpus is never touched. It has to be a local path: SQLite's file locks don't work on
\\wsl.localhost, where a relative path would land when the script is started from WSL. The .exe is copied
into that folder and started from there, since Windows can hold a program started straight from
\\wsl.localhost behind a security prompt.

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

.PARAMETER OpenFile
Tests opening a file from the command line, as Explorer's "Open with" does: a picture to start corpus-gui with, or
'sample' to draw one. The window must open on the viewer (the screenshot shows it). Then corpus-gui is started a
second time with the same file, which must hand it to the first window and end on its own with code 0; a second
screenshot (smoke-test-second.png next to the first) shows the first window again.

.PARAMETER Library
A corpus.db to start from instead of an empty library, to see the grid with files in it. It is copied into the
scratch folder first, so the original is never changed. With files in it, the typing checks run in the tag sheet and the rename
sheet.

.NOTES
With -Library, after the first screenshot the script types into the tag sheet (Ctrl+A, Ctrl+T) and the rename sheet
(F2) with real key presses: "ab", then "c" and Backspace, which must bring the field back to how it looked with
"ab". Then Ctrl+A and Del must ask in the window's own sheet, not a Windows dialog (smoke-test-confirm.png); Enter
removes the files (smoke-test-removed.png) and Ctrl+Z must bring them back (smoke-test-undone.png). Last, Ctrl+F
must put the keyboard in the search, typed into the same way. It brings the window to the front for that, so don't
type while it runs.
#>
param(
    [string]$Exe = (Join-Path $PSScriptRoot '..\build-gui\corpus-gui.exe'),
    [string]$Screenshot = '',
    [int]$Timeout = 15,
    [string]$Library = '',
    [string]$OpenFile = ''
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
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
    [DllImport("user32.dll")] static extern uint SendInput(uint count, INPUT[] inputs, int size);

    [StructLayout(LayoutKind.Sequential)]
    struct KEYBDINPUT { public ushort Vk, Scan; public uint Flags, Time; public IntPtr Extra; }
    // INPUT is a union with MOUSEINPUT, the largest member; the padding keeps its size right.
    [StructLayout(LayoutKind.Sequential)]
    struct INPUT { public uint Type; public KEYBDINPUT Ki; public long Padding; }

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

    // A tap of Alt first: Windows only lets a process that just had input bring another window to the front.
    public static bool BringToFront(IntPtr hWnd)
    {
        keybd_event(0x12, 0, 0, IntPtr.Zero);
        keybd_event(0x12, 0, 2, IntPtr.Zero);
        SetForegroundWindow(hWnd);
        System.Threading.Thread.Sleep(200);
        return GetForegroundWindow() == hWnd;
    }

    // Presses and releases a key, with the modifier held if it isn't 0, as real key presses: they go through
    // TranslateMessage like typing does.
    public static void Key(ushort vk, ushort modifier)
    {
        if (modifier != 0)
            Send(modifier, false);
        Send(vk, false);
        Send(vk, true);
        if (modifier != 0)
            Send(modifier, true);
        System.Threading.Thread.Sleep(100);
    }

    static void Send(ushort vk, bool up)
    {
        var input = new INPUT[1];
        input[0].Type = 1;  // INPUT_KEYBOARD
        input[0].Ki.Vk = vk;
        // Page Up to Delete are extended keys; without the flag they would be the number pad's.
        input[0].Ki.Flags = (up ? 2u : 0u) | (vk >= 0x21 && vk <= 0x2E ? 1u : 0u);
        SendInput(1, input, Marshal.SizeOf(typeof(INPUT)));
    }

    // The window's pixels, compared by Same.
    public static byte[] Pixels(IntPtr hWnd)
    {
        using (var bitmap = Capture(hWnd))
        {
            var data = bitmap.LockBits(new Rectangle(0, 0, bitmap.Width, bitmap.Height),
                System.Drawing.Imaging.ImageLockMode.ReadOnly, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
            var bytes = new byte[data.Stride * data.Height];
            Marshal.Copy(data.Scan0, bytes, 0, bytes.Length);
            bitmap.UnlockBits(data);
            return bytes;
        }
    }

    public static bool Same(byte[] a, byte[] b)
    {
        if (a.Length != b.Length)
            return false;
        for (int i = 0; i < a.Length; i++)
            if (a[i] != b[i])
                return false;
        return true;
    }

    public static void SavePng(IntPtr hWnd, string path)
    {
        using (var bitmap = Capture(hWnd))
            bitmap.Save(path, System.Drawing.Imaging.ImageFormat.Png);
    }

    // PrintWindow asks the window to draw itself, so the capture is right even if another window covers it.
    static Bitmap Capture(IntPtr hWnd)
    {
        // Per-monitor aware, like corpus-gui, so the rectangle is in real pixels on a scaled display.
        SetThreadDpiAwarenessContext(new IntPtr(-4));
        RECT rect;
        if (!GetWindowRect(hWnd, out rect) || rect.Right <= rect.Left || rect.Bottom <= rect.Top)
            throw new Exception("Could not get the window's size.");
        var bitmap = new Bitmap(rect.Right - rect.Left, rect.Bottom - rect.Top);
        using (var graphics = Graphics.FromImage(bitmap))
        {
            IntPtr hdc = graphics.GetHdc();
            bool ok = PrintWindow(hWnd, hdc, PW_RENDERFULLCONTENT);
            graphics.ReleaseHdc(hdc);
            if (!ok)
            {
                bitmap.Dispose();
                throw new Exception("PrintWindow failed.");
            }
        }
        return bitmap;
    }

    public static void Close(IntPtr hWnd)
    {
        PostMessage(hWnd, WM_CLOSE, IntPtr.Zero, IntPtr.Zero);
    }
}
'@

# Types "ab" into the focused field, then "c" and Backspace: the field must look as it did with "ab" again. The caret
# blinks, so the second look is retried until it matches. Returns why it failed, or $null.
function Test-Typing([IntPtr]$hWnd, [string]$field) {
    $empty = [SmokeTest]::Pixels($hWnd)
    [SmokeTest]::Key(0x41, 0)
    [SmokeTest]::Key(0x42, 0)
    Start-Sleep -Milliseconds 200
    $typed = [SmokeTest]::Pixels($hWnd)
    if ([SmokeTest]::Same($empty, $typed)) {
        return "Typing changed nothing in the $field."
    }
    [SmokeTest]::Key(0x43, 0)
    [SmokeTest]::Key(0x08, 0)
    for ($i = 0; $i -lt 20; $i++) {
        if ([SmokeTest]::Same($typed, [SmokeTest]::Pixels($hWnd))) {
            return $null
        }
        Start-Sleep -Milliseconds 100
    }
    return "Backspace didn't undo a typed letter in the $field."
}

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
# Relative to the PowerShell location, not the process's current directory, which differs when started from WSL.
$Screenshot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Screenshot)

$scratch = Join-Path $env:TEMP ('corpus-smoke-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
$scratchLibrary = Join-Path $scratch 'corpus.db'
$localExe = Join-Path $scratch (Split-Path $Exe -Leaf)

$failure = $null
$process = $null
try {
    Copy-Item -LiteralPath $Exe -Destination $localExe
    if ($Library) {
        Copy-Item -LiteralPath $Library -Destination $scratchLibrary
    }
    $arguments = @('--library', "`"$scratchLibrary`"")
    if ($OpenFile -eq 'sample') {
        $OpenFile = Join-Path $scratch 'sample.png'
        $bitmap = New-Object System.Drawing.Bitmap 400, 300
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $graphics.Clear([System.Drawing.Color]::SteelBlue)
        $graphics.FillEllipse([System.Drawing.Brushes]::Gold, 120, 70, 160, 160)
        $graphics.Dispose()
        $bitmap.Save($OpenFile, [System.Drawing.Imaging.ImageFormat]::Png)
        $bitmap.Dispose()
    }
    elseif ($OpenFile) {
        $OpenFile = (Resolve-Path -LiteralPath $OpenFile).ProviderPath
    }
    if ($OpenFile) {
        $arguments += "`"$OpenFile`""
    }
    $process = Start-Process -FilePath $localExe -ArgumentList $arguments -WorkingDirectory $scratch -PassThru
    # Opens the process handle now: without it ExitCode stays empty once the process has exited.
    $null = $process.Handle

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

        if ($OpenFile) {
            # A second start with the same library must hand its file to this window and end by itself.
            $second = Start-Process -FilePath $localExe -ArgumentList $arguments -WorkingDirectory $scratch -PassThru
            $null = $second.Handle
            if (-not $second.WaitForExit(15000)) {
                $second.Kill()
                $failure = 'A second corpus-gui did not hand its file to the first and end.'
            }
            elseif ($second.ExitCode -ne 0) {
                $failure = "The second corpus-gui exited with code $($second.ExitCode)."
            }
            else {
                Start-Sleep -Milliseconds 500
                $secondShot = Join-Path (Split-Path $Screenshot) 'smoke-test-second.png'
                [SmokeTest]::SavePng($window, $secondShot)
                Write-Host "Screenshot after the second start: $secondShot"
            }
        }

        if (-not $OpenFile -and -not $failure) {
            if (-not [SmokeTest]::BringToFront($window)) {
                $failure = 'Could not bring the window to the front to type into it.'
            }
            else {
                if ($Library) {
                    # Ctrl+A selects every file; Ctrl+T opens the tag sheet, F2 the rename sheet on the first file.
                    [SmokeTest]::Key(0x41, 0x11)
                    [SmokeTest]::Key(0x54, 0x11)
                    Start-Sleep -Milliseconds 200
                    $failure = Test-Typing $window 'tag sheet'
                    [SmokeTest]::Key(0x1B, 0)
                    if (-not $failure) {
                        [SmokeTest]::Key(0x71, 0)
                        Start-Sleep -Milliseconds 200
                        $failure = Test-Typing $window 'rename sheet'
                        [SmokeTest]::Key(0x1B, 0)
                    }
                    if (-not $failure) {
                        # Del asks in a sheet of the window, with no Windows dialog; Enter removes, Ctrl+Z puts back.
                        $folder = Split-Path $Screenshot
                        [SmokeTest]::Key(0x41, 0x11)
                        [SmokeTest]::Key(0x2E, 0)
                        Start-Sleep -Milliseconds 300
                        foreach ($hWnd in [SmokeTest]::VisibleWindows($process.Id)) {
                            if ([SmokeTest]::ClassName($hWnd) -eq $dialogClass) { $failure = 'Del asked with a Windows dialog.' }
                        }
                        [SmokeTest]::SavePng($window, (Join-Path $folder 'smoke-test-confirm.png'))
                        if (-not $failure) {
                            [SmokeTest]::Key(0x0D, 0)
                            Start-Sleep -Milliseconds 300
                            $removed = [SmokeTest]::Pixels($window)
                            [SmokeTest]::SavePng($window, (Join-Path $folder 'smoke-test-removed.png'))
                            [SmokeTest]::Key(0x5A, 0x11)
                            Start-Sleep -Milliseconds 500
                            [SmokeTest]::SavePng($window, (Join-Path $folder 'smoke-test-undone.png'))
                            if ([SmokeTest]::Same($removed, [SmokeTest]::Pixels($window))) {
                                $failure = 'Ctrl+Z did not bring the removed files back.'
                            }
                            Write-Host "Screenshots of remove and undo: smoke-test-confirm.png, smoke-test-removed.png, smoke-test-undone.png"
                        }
                    }
                    if (-not $failure) {
                        [SmokeTest]::Key(0x46, 0x11)
                        Start-Sleep -Milliseconds 200
                        $failure = Test-Typing $window 'search'
                        [SmokeTest]::Key(0x1B, 0)
                        [SmokeTest]::Key(0x1B, 0)
                    }
                }
                if ($failure) {
                    $typingShot = Join-Path (Split-Path $Screenshot) 'smoke-test-typing.png'
                    [SmokeTest]::SavePng($window, $typingShot)
                    Write-Host "Screenshot of the failed typing check: $typingShot"
                }
            }
        }

        [SmokeTest]::Close($window)
        if (-not $process.WaitForExit(5000)) {
            if (-not $failure) { $failure = 'corpus-gui did not close within 5 seconds of WM_CLOSE.' }
        }
        elseif ($process.ExitCode -ne 0 -and -not $failure) {
            $failure = "corpus-gui exited with code $($process.ExitCode)."
        }
    }
}
catch {
    $failure = $_.Exception.Message
}
finally {
    # The process can exit between HasExited and Kill; that must not hide the failure or skip the cleanup.
    try {
        if ($process -and -not $process.HasExited) {
            $process.Kill()
            $process.WaitForExit()
        }
    }
    catch {}
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
}

if ($failure) {
    Write-Host "FAIL: $failure"
    exit 1
}
Write-Host 'OK: the window opened with no error dialog, passed its checks and closed cleanly.'
exit 0
