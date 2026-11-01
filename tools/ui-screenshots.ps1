<#
.SYNOPSIS
Takes screenshots of Corpus's music and layout states on a scratch library, for UI review.

.DESCRIPTION
Builds a test library under %TEMP% (generated WAV songs and drawn pictures, never the real catalog), imports it
through the app's own Add folder dialog (Ctrl+O), starts Corpus.exe on it and walks through the states, saving one PNG each into -OutDir:

  01-grid                  the main grid, nothing playing
  02-stage-wide            the now playing stage with the Up next rail
  03-rail-hover            the pointer on a rail row (its "+" shows)
  04-rail-queued           after Q on that row: the queue and its divider
  05-stage-narrow          the stage in a window too narrow for the rail
  06-stage-small           the stage in a small window
  07-grid-strip            the grid with the music strip under it, viewer closed
  08-picture-retracted     a picture over the parked music: retracted strip
  09-picture-strip-out     the pointer at the bottom edge: strip slid over the picture
  10-grid-small-strip      a small window with the strip

The app shares HKCU\Software\Corpus with the real Corpus (queue, volume, tile size...). The script saves that key
to %LOCALAPPDATA%\corpus-ui-shots-settings.json first, empties the queue for the run, and puts every value back
exactly at the end (removing what the run added, deleting the key if it did not exist), keeping the file until the
restore is verified. If a run died before restoring, the next run restores that file first. It also saves and restores the clipboard.

It uses real key presses and moves the mouse pointer a few times (rail rows, bottom edge), and brings the window
to the front, so don't touch the mouse or keyboard while it runs.

From WSL:
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w tools/ui-screenshots.ps1)"

.PARAMETER Exe
Corpus.exe to run. Defaults to build-gui\Corpus.exe.
.PARAMETER OutDir
Where the PNGs go. Defaults to corpus-ui-shots in the user's profile folder.
#>
param(
    [string]$Exe = (Join-Path $PSScriptRoot '..\build-gui\Corpus.exe'),
    [string]$OutDir = (Join-Path $env:USERPROFILE 'corpus-ui-shots')
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;using System.Drawing;using System.Runtime.InteropServices;
public static class UiShots{
 [StructLayout(LayoutKind.Sequential)] public struct RECT{public int L,T,R,B;}
 [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h,IntPtr dc,uint f);
 [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] static extern bool SetProcessDPIAware();
 [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int w,int hh,uint f);
 [DllImport("user32.dll")] static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] static extern void keybd_event(byte vk,byte sc,uint f,IntPtr e);
 public static void Init(){SetProcessDPIAware();}
 public static RECT Rect(IntPtr h){RECT r;GetWindowRect(h,out r);return r;}
 [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
 public static bool Front(IntPtr h){keybd_event(0x12,0,0,IntPtr.Zero);keybd_event(0x12,0,2,IntPtr.Zero);SetForegroundWindow(h);System.Threading.Thread.Sleep(150);return GetForegroundWindow()==h;}
 public static void Place(IntPtr h,int x,int y,int w,int hh){SetWindowPos(h,IntPtr.Zero,x,y,w,hh,0x44);}
 public static void Move(IntPtr h,int x,int y){RECT r=Rect(h);SetCursorPos(r.L+x,r.T+y);}
 public static void Key(byte vk){keybd_event(vk,0,0,IntPtr.Zero);keybd_event(vk,0,2,IntPtr.Zero);System.Threading.Thread.Sleep(120);}
 public static void Shot(IntPtr h,string path){RECT r=Rect(h);using(var b=new Bitmap(r.R-r.L,r.B-r.T)){using(var g=Graphics.FromImage(b)){IntPtr dc=g.GetHdc();PrintWindow(h,dc,2);g.ReleaseHdc(dc);}b.Save(path,System.Drawing.Imaging.ImageFormat.Png);}}
}
'@
[UiShots]::Init()
Add-Type -TypeDefinition @'
using System;using System.Runtime.InteropServices;
public static class UiKeys{
 [DllImport("user32.dll")] static extern void keybd_event(byte vk,byte sc,uint f,IntPtr e);
 public static void Combo(byte mod,byte vk){keybd_event(mod,0,0,IntPtr.Zero);keybd_event(vk,0,0,IntPtr.Zero);keybd_event(vk,0,2,IntPtr.Zero);keybd_event(mod,0,2,IntPtr.Zero);System.Threading.Thread.Sleep(150);}
}
'@

# --- Settings: HKCU\Software\Corpus is shared with the real app. Read every value with its type, write them back exactly. ---
$regPath = 'HKCU:\Software\Corpus'
$backupFile = Join-Path $env:LOCALAPPDATA 'corpus-ui-shots-settings.json'

function Get-Settings {
    if (-not (Test-Path -LiteralPath $regPath)) { return [pscustomobject]@{ existed = $false; values = @() } }
    $k = Get-Item -LiteralPath $regPath
    $vals = foreach ($n in ($k.GetValueNames() | Sort-Object)) {
        $kind = $k.GetValueKind($n).ToString()
        $v = $k.GetValue($n, $null, 'DoNotExpandEnvironmentNames')
        if ($kind -eq 'Binary') { $v = [Convert]::ToBase64String([byte[]]$v) }
        elseif ($kind -eq 'MultiString') { $v = @($v) }
        [pscustomobject]@{ name = $n; kind = $kind; value = $v }
    }
    [pscustomobject]@{ existed = $true; values = @($vals) }
}
function Settings-Json($state) { ConvertTo-Json -InputObject $state -Depth 5 -Compress }
function Restore-Settings($state) {
    if (-not $state.existed) {
        if (Test-Path -LiteralPath $regPath) { Remove-Item -LiteralPath $regPath -Recurse -Force }
        return
    }
    if (-not (Test-Path -LiteralPath $regPath)) { New-Item -Path $regPath -Force | Out-Null }
    $k = Get-Item -LiteralPath $regPath
    foreach ($n in $k.GetValueNames()) { Remove-ItemProperty -LiteralPath $regPath -Name $n -Force }
    foreach ($e in $state.values) {
        $v = $e.value
        if ($e.kind -eq 'Binary') { $v = [Convert]::FromBase64String($v) }
        elseif ($e.kind -eq 'MultiString') { $v = [string[]]@($v) }
        New-ItemProperty -LiteralPath $regPath -Name $e.name -PropertyType $e.kind -Value $v -Force | Out-Null
    }
}
# Restores $state and checks it by reading the key back; returns $true when identical.
function Restore-Verified($state) {
    Restore-Settings $state
    (Settings-Json (Get-Settings)) -eq (Settings-Json $state)
}

# A previous run that died before restoring left its backup: put that back first, and never overwrite it.
if (Test-Path -LiteralPath $backupFile) {
    Write-Host "Found settings backup from an interrupted run: restoring it first."
    $old = Get-Content -LiteralPath $backupFile -Raw | ConvertFrom-Json
    if (-not (Restore-Verified $old)) { throw "Could not verify the restore of $backupFile; it was kept. Fix HKCU\Software\Corpus by hand." }
    Remove-Item -LiteralPath $backupFile
}

# Keys go to whichever window has the focus: make sure it is ours before every press.
function Focus { if (-not [UiShots]::Front($h)) { throw 'Corpus window did not get the keyboard focus; stopped before sending keys.' } }

$proc = $null; $savedState = $null; $scratch = $null; $hadClip = $false
try {
    # Save the settings (the backup file is written before anything is changed), then start with an empty queue.
    $savedState = Get-Settings
    [IO.File]::WriteAllText($backupFile, (Settings-Json $savedState))
    Remove-ItemProperty -LiteralPath $regPath -Name Queue -ErrorAction SilentlyContinue
    $hadClip = $false
    try { $clip = Get-Clipboard -Raw; $hadClip = $true } catch {}

    $exe = (Resolve-Path -LiteralPath $Exe).Path
    $scratch = Join-Path $env:TEMP 'corpus-ui-shots-lib'
    $files = Join-Path $scratch 'files'
    if (Test-Path $scratch) { Remove-Item -Recurse -Force $scratch }
    New-Item -ItemType Directory -Force -Path "$files\Songs", "$files\Pictures", $OutDir | Out-Null

    # --- Test files: six 40 s sine-melody WAVs (8 kHz, 16 bit) and eight drawn pictures. ---
    $notes = @(220, 262, 294, 330, 392, 440)
    for ($s = 1; $s -le 6; $s++) {
        $rate = 8000; $n = $rate * 40
        $ms = New-Object System.IO.MemoryStream
        $w = New-Object System.IO.BinaryWriter($ms)
        $w.Write([Text.Encoding]::ASCII.GetBytes('RIFF')); $w.Write([int](36 + $n * 2))
        $w.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt ')); $w.Write([int]16); $w.Write([int16]1); $w.Write([int16]1)
        $w.Write([int]$rate); $w.Write([int]($rate * 2)); $w.Write([int16]2); $w.Write([int16]16)
        $w.Write([Text.Encoding]::ASCII.GetBytes('data')); $w.Write([int]($n * 2))
        $f = $notes[$s - 1]
        for ($i = 0; $i -lt $n; $i++) {
            $w.Write([int16](6000 * [Math]::Sin(2 * [Math]::PI * $f * (1 + 0.25 * [Math]::Floor($i / 8000 % 4)) * $i / $rate)))
        }
        $w.Flush()
        [IO.File]::WriteAllBytes("$files\Songs\song-0$s.wav", $ms.ToArray())
    }
    for ($p = 1; $p -le 8; $p++) {
        $bmp = New-Object System.Drawing.Bitmap 1200, 800
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $c1 = [System.Drawing.Color]::FromArgb(255, (40 + 25 * $p), 90, (230 - 20 * $p))
        $c2 = [System.Drawing.Color]::FromArgb(255, 20, (200 - 15 * $p), 120)
        $br = New-Object System.Drawing.Drawing2D.LinearGradientBrush((New-Object System.Drawing.Rectangle 0, 0, 1200, 800), $c1, $c2, 35.0)
        $g.FillRectangle($br, 0, 0, 1200, 800)
        $g.Dispose(); $bmp.Save("$files\Pictures\pic-0$p.jpg", [System.Drawing.Imaging.ImageFormat]::Jpeg); $bmp.Dispose()
    }

    Copy-Item -LiteralPath $exe -Destination "$scratch\Corpus.exe"
    $db = Join-Path $scratch 'corpus.db'


    # --- Run ---
    $proc = Start-Process "$scratch\Corpus.exe" -ArgumentList '--library', "`"$db`"" -PassThru
    $h = [IntPtr]::Zero
    for ($i = 0; $i -lt 40 -and $h -eq [IntPtr]::Zero; $i++) {
        Start-Sleep -Milliseconds 400
        $proc.Refresh(); $h = $proc.MainWindowHandle
    }
    if ($h -eq [IntPtr]::Zero) { throw 'Corpus did not open a window.' }

    function Shot($name) { Start-Sleep -Milliseconds 700; [UiShots]::Shot($h, (Join-Path $OutDir "$name.png")); Write-Host "saved $name" }
    function Size($w, $hh) { [UiShots]::Place($h, 20, 20, $w, $hh); Start-Sleep -Milliseconds 700 }
    function Press($vk) { Focus; [UiShots]::Key([byte]$vk) }
    function Combo($vk) { Focus; [UiKeys]::Combo(0x11, [byte]$vk) }
    function TypeText($t) { foreach ($c in $t.ToCharArray()) { Press ([int][char]::ToUpper($c)); } }
    function Search($text) {
        Combo 0x46            # Ctrl+F
        Press 0x1B            # Esc: empties a leftover query
        Combo 0x46
        TypeText $text
        Start-Sleep -Milliseconds 500
    }


        Size 2200 1200
        Start-Sleep -Seconds 2
        # Import: Ctrl+O opens the folder picker; paste the path, Enter goes into it, Enter picks it.
        Set-Clipboard -Value $files
        # The picker is its own window and has the focus, so these keys go out without bringing the main window forward.
        Focus; [UiKeys]::Combo(0x11, 0x4F); Start-Sleep -Seconds 2
        [UiKeys]::Combo(0x11, 0x56); Start-Sleep -Milliseconds 500
        [UiShots]::Key(0x0D); Start-Sleep -Seconds 1; [UiShots]::Key(0x0D)
        Start-Sleep -Seconds 4
        Press 0x0D          # accept the folder tags sheet
        Start-Sleep -Seconds 2
        Shot '01-grid'

        # Open the first song: search "song", Enter (to the grid), Enter (open).
        Search 'song'; Press 0x0D; Press 0x0D
        Start-Sleep -Seconds 2
        Shot '02-stage-wide'

        # Pointer on the first rail rows. The rail is the right 24% of the window; rows start a little below the top.
        $r = [UiShots]::Rect($h); $ww = $r.R - $r.L; $wh = $r.B - $r.T
        $x = [int]($ww - $ww * 0.12)
        [UiShots]::Move($h, $x, [int]($wh * 0.22)); Shot '03-rail-hover'
        Press 0x51                                  # Q: queue the song under the pointer
        [UiShots]::Move($h, $x, [int]($wh * 0.29)); Press 0x51
        [UiShots]::Move($h, $x, [int]($wh * 0.40)); Shot '04-rail-queued'

        Size 1000 800; Shot '05-stage-narrow'
        Size 480 560; Shot '06-stage-small'
        Size 2200 1200

        # Close the viewer: the music goes on in the strip.
        Press 0x1B; Start-Sleep -Seconds 1
        [UiShots]::Move($h, 5, 5)
        Shot '07-grid-strip'

        # A picture over the parked music: the strip retracts; the pointer at the bottom edge pulls it out.
        Search 'pic'; Press 0x0D; Press 0x0D
        Start-Sleep -Seconds 2
        $r = [UiShots]::Rect($h); $ww = $r.R - $r.L; $wh = $r.B - $r.T
        [UiShots]::Move($h, [int]($ww / 2), [int]($wh / 2)); Start-Sleep -Seconds 4   # let the overlays fade
        Shot '08-picture-retracted'
        [UiShots]::Move($h, [int]($ww / 2), $wh - 60); Start-Sleep -Milliseconds 300
        [UiShots]::Move($h, [int]($ww / 2), $wh - 14); Start-Sleep -Milliseconds 300
        [UiShots]::Move($h, [int]($ww / 2) + 3, $wh - 12); Start-Sleep -Milliseconds 600
        Shot '09-picture-strip-out'

        Press 0x1B; Start-Sleep -Seconds 1
        Size 480 560; Shot '10-grid-small-strip'
}
finally {
    if ($proc -and -not $proc.HasExited) {
        $proc.CloseMainWindow() | Out-Null; Start-Sleep 2
        if (-not $proc.HasExited) { $proc.Kill() }
        $proc.WaitForExit(5000) | Out-Null
    }
    Start-Sleep -Milliseconds 500
    if ($savedState) {
        if (Restore-Verified $savedState) { Remove-Item -LiteralPath $backupFile -ErrorAction SilentlyContinue }
        else { Write-Warning "Settings restore did not verify; the backup was kept in $backupFile (run the script again to retry)." }
    }
    if ($hadClip) { Set-Clipboard -Value $clip }
    if ($scratch) { Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue }
}
Write-Host "Screenshots in $OutDir"
