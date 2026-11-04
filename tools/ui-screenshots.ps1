<#
.SYNOPSIS
Takes screenshots of Corpus's music and layout states on a scratch library, for UI review.

.DESCRIPTION
Walks the state manifest (tools\ui-states.psd1; docs\ui-states.md lists it) and saves one PNG per state, then one
contact sheet (contact-sheet.png, grouped like the manifest, uncaptured states as grey gaps) and a diff against the
previous run (the old PNGs are kept in OutDir\previous; OutDir\diff holds a red overlay of what changed and
diff-report.txt the percentages). -NoDiff skips the diff. -WriteDocs only regenerates docs\ui-states.md, without touching the app.

Builds a test library under %TEMP% (generated WAV songs and drawn pictures, never the real catalog), imports it
through the app's own Add folder dialog (Ctrl+O), starts Corpus.exe on it and walks through the states, saving one PNG each into -OutDir:


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
    [string]$OutDir = (Join-Path $env:USERPROFILE 'corpus-ui-shots'),
    [switch]$WriteDocs,
    [switch]$NoDiff
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

# --- The state manifest: every screen and state, from tools\ui-states.psd1 ---
$manifest = (Import-PowerShellDataFile -LiteralPath (Join-Path $PSScriptRoot 'ui-states.psd1')).States
if ($WriteDocs) {
    $md = New-Object System.Text.StringBuilder
    [void]$md.AppendLine('# UI states')
    [void]$md.AppendLine('')
    [void]$md.AppendLine('Every screen and state of the window and how to reach it. Generated from `tools/ui-states.psd1` by')
    [void]$md.AppendLine('`tools/ui-screenshots.ps1 -WriteDocs`; edit the manifest, not this file. "Shot" says whether `tools/ui-screenshots.ps1`')
    [void]$md.AppendLine('captures the state (yes) or not yet (the reason is in the last column).')
    foreach ($g in ($manifest | ForEach-Object { $_.Group } | Select-Object -Unique)) {
        [void]$md.AppendLine('')
        [void]$md.AppendLine("## $g")
        [void]$md.AppendLine('')
        [void]$md.AppendLine('| Id | State | How to reach it | Shot |')
        [void]$md.AppendLine('|---|---|---|---|')
        foreach ($st in ($manifest | Where-Object { $_.Group -eq $g } | Sort-Object { $_.Id })) {
            $shot = if ($st.Auto) { 'yes' } else { 'no: ' + $st.Why }
            [void]$md.AppendLine("| ``$($st.Id)`` | $($st.Title) | $($st.Reach) | $shot |")
        }
    }
    $docPath = Join-Path $PSScriptRoot '..\docs\ui-states.md'
    [IO.File]::WriteAllText($docPath, $md.ToString().Replace("`r`n", "`n"), (New-Object Text.UTF8Encoding $false))
    Write-Host "Wrote $docPath ($($manifest.Count) states)"
    return
}
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
 [DllImport("user32.dll")] static extern void mouse_event(uint f,int dx,int dy,uint d,IntPtr e);
 [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr h);
 public static double Scale(IntPtr h){return GetDpiForWindow(h)/96.0;}
 public static void RightClick(){mouse_event(8,0,0,0,IntPtr.Zero);System.Threading.Thread.Sleep(120);mouse_event(16,0,0,0,IntPtr.Zero);System.Threading.Thread.Sleep(150);}
 public static void Wheel(int notches){mouse_event(0x800,0,0,(uint)(notches*120),IntPtr.Zero);System.Threading.Thread.Sleep(150);}
 // Fraction of pixels that differ between two PNGs (some channel by more than 16); writes b with the changed pixels red to 'outPng'. -1 when the sizes differ.
 public static double Diff(string a,string b,string outPng){
  using(var x=new Bitmap(a))using(var y=new Bitmap(b)){
   if(x.Width!=y.Width||x.Height!=y.Height)return -1;
   int w=x.Width,h=x.Height;var r=new Rectangle(0,0,w,h);var f=System.Drawing.Imaging.PixelFormat.Format32bppArgb;
   var dx=x.LockBits(r,System.Drawing.Imaging.ImageLockMode.ReadOnly,f);var dy=y.LockBits(r,System.Drawing.Imaging.ImageLockMode.ReadOnly,f);
   byte[] px=new byte[dx.Stride*h],py=new byte[dy.Stride*h];Marshal.Copy(dx.Scan0,px,0,px.Length);Marshal.Copy(dy.Scan0,py,0,py.Length);
   x.UnlockBits(dx);y.UnlockBits(dy);
   int n=0;for(int i=0;i<px.Length;i+=4){
    if(Math.Abs(px[i]-py[i])>16||Math.Abs(px[i+1]-py[i+1])>16||Math.Abs(px[i+2]-py[i+2])>16){n++;py[i]=0;py[i+1]=0;py[i+2]=255;}}
   if(n>0){using(var o=new Bitmap(w,h,f)){var d=o.LockBits(r,System.Drawing.Imaging.ImageLockMode.WriteOnly,f);Marshal.Copy(py,0,d.Scan0,py.Length);o.UnlockBits(d);o.Save(outPng,System.Drawing.Imaging.ImageFormat.Png);}}
   return (double)n/((double)w*h);}}
 public static void Down(){mouse_event(2,0,0,0,IntPtr.Zero);System.Threading.Thread.Sleep(120);}
 public static void Up(){mouse_event(4,0,0,0,IntPtr.Zero);System.Threading.Thread.Sleep(120);}
 public static void Click(){Down();Up();}
 public static void ShiftClick(){keybd_event(0x10,0,0,IntPtr.Zero);Down();Up();keybd_event(0x10,0,2,IntPtr.Zero);}
 // Shift plus a key of the extended block (Delete): flagged, or Windows reads it as the numpad's and Shift turns it into a point.
 public static void ShiftExt(byte vk){keybd_event(0x10,0,0,IntPtr.Zero);keybd_event(vk,0,1,IntPtr.Zero);keybd_event(vk,0,3,IntPtr.Zero);keybd_event(0x10,0,2,IntPtr.Zero);System.Threading.Thread.Sleep(150);}
 public static void ShiftKey(byte vk){keybd_event(0x10,0,0,IntPtr.Zero);keybd_event(vk,0,0,IntPtr.Zero);keybd_event(vk,0,2,IntPtr.Zero);keybd_event(0x10,0,2,IntPtr.Zero);System.Threading.Thread.Sleep(150);}
 // Bounding box (window coordinates) of the pixels close to one color in a capture of the window; Width 0 when none.
 public static int[] Find(IntPtr h,int cr,int cg,int cb,int tol){
  RECT r=Rect(h);int w=r.R-r.L,hh=r.B-r.T;int x0=w,y0=hh,x1=-1,y1=-1;
  using(var b=new Bitmap(w,hh)){using(var g=Graphics.FromImage(b)){IntPtr dc=g.GetHdc();PrintWindow(h,dc,2);g.ReleaseHdc(dc);}
   var d=b.LockBits(new Rectangle(0,0,w,hh),System.Drawing.Imaging.ImageLockMode.ReadOnly,System.Drawing.Imaging.PixelFormat.Format32bppArgb);
   byte[] px=new byte[d.Stride*hh];Marshal.Copy(d.Scan0,px,0,px.Length);b.UnlockBits(d);
   for(int y=0;y<hh;y++)for(int x=0;x<w;x++){int i=y*d.Stride+x*4;
    if(Math.Abs(px[i+2]-cr)<=tol&&Math.Abs(px[i+1]-cg)<=tol&&Math.Abs(px[i]-cb)<=tol){if(x<x0)x0=x;if(x>x1)x1=x;if(y<y0)y0=y;if(y>y1)y1=y;}}}
  return new int[]{x0,y0,x1,y1};}
 public static void CropBottom(string src,string dst,double fraction){Bitmap c;using(var b=new Bitmap(src)){int top=(int)(b.Height*(1-fraction));c=b.Clone(new Rectangle(0,top,b.Width,b.Height-top),System.Drawing.Imaging.PixelFormat.Format32bppArgb);}c.Save(dst,System.Drawing.Imaging.ImageFormat.Png);c.Dispose();}
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

# The run shares HKCU\Software\Corpus and takes over the keyboard: never start while a Corpus is open (its settings would be overwritten).
if (Get-Process -Name Corpus -ErrorAction SilentlyContinue) { throw 'Corpus is running. Close it first: this script changes its settings for the run and takes over the mouse and keyboard.' }

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
    # The sidebar open and "play on" off, whatever the real settings say (everything is restored at the end).
    if (-not (Test-Path -LiteralPath $regPath)) { New-Item -Path $regPath -Force | Out-Null }
    New-ItemProperty -LiteralPath $regPath -Name SidebarHidden -PropertyType DWord -Value 0 -Force | Out-Null
    $hadClip = $false
    try { $clip = Get-Clipboard -Raw; $hadClip = $true } catch {}

    $exe = (Resolve-Path -LiteralPath $Exe).Path
    $scratch = Join-Path $env:TEMP 'corpus-ui-shots-lib'
    $files = Join-Path $scratch 'files'
    if (Test-Path $scratch) { Remove-Item -Recurse -Force $scratch }
    New-Item -ItemType Directory -Force -Path "$files\Songs", "$files\Albums", "$files\Pictures", "$files\Books", $OutDir | Out-Null
    # The last run's pictures become "previous" (for the diff); only top-level PNGs are states.
    $prevDir = Join-Path $OutDir 'previous'
    $oldShots = @(Get-ChildItem -LiteralPath $OutDir -Filter '*.png' -File | Where-Object { $_.Name -ne 'contact-sheet.png' })
    if ($oldShots.Count -gt 0) {
        if (Test-Path $prevDir) { Remove-Item -Recurse -Force $prevDir }
        New-Item -ItemType Directory -Force -Path $prevDir | Out-Null
        $oldShots | Move-Item -Destination $prevDir
    }

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

    # A seventh song with an embedded cover: a WAV with an "id3 " chunk holding an ID3v2.3 tag with a JPEG picture
    # (magenta, with a yellow frame so the script can find where the cover is drawn).
    $bmp = New-Object System.Drawing.Bitmap 600, 600
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::FromArgb(255, 255, 255, 0))
    $g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 170, 30, 140))), 30, 30, 540, 540)
    $g.FillEllipse((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 240, 200, 230))), 180, 180, 240, 240)
    $g.Dispose()
    $jm = New-Object System.IO.MemoryStream
    $bmp.Save($jm, [System.Drawing.Imaging.ImageFormat]::Jpeg); $bmp.Dispose()
    $jpeg = $jm.ToArray()
    $mime = [Text.Encoding]::ASCII.GetBytes('image/jpeg')
    $apic = [byte[]](@(0) + $mime + @(0, 3, 0) + $jpeg)
    $tm = New-Object System.IO.MemoryStream
    $tm.Write([Text.Encoding]::ASCII.GetBytes('APIC'), 0, 4)
    $tm.Write([byte[]]@((($apic.Length -shr 24) -band 255), (($apic.Length -shr 16) -band 255), (($apic.Length -shr 8) -band 255), ($apic.Length -band 255), 0, 0), 0, 6)
    $tm.Write($apic, 0, $apic.Length)
    $frames = $tm.ToArray()
    $sz = $frames.Length
    $tag = [byte[]](@(0x49, 0x44, 0x33, 3, 0, 0, (($sz -shr 21) -band 127), (($sz -shr 14) -band 127), (($sz -shr 7) -band 127), ($sz -band 127)) + $frames)
    if ($tag.Length % 2) { $tag = [byte[]]($tag + @(0)) }
    $rate = 8000; $n = $rate * 40
    $ms = New-Object System.IO.MemoryStream
    $w = New-Object System.IO.BinaryWriter($ms)
    $w.Write([Text.Encoding]::ASCII.GetBytes('RIFF')); $w.Write([int](36 + 8 + $tag.Length + $n * 2))
    $w.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt ')); $w.Write([int]16); $w.Write([int16]1); $w.Write([int16]1)
    $w.Write([int]$rate); $w.Write([int]($rate * 2)); $w.Write([int16]2); $w.Write([int16]16)
    $w.Write([Text.Encoding]::ASCII.GetBytes('id3 ')); $w.Write([int]$tag.Length); $w.Write($tag)
    $w.Write([Text.Encoding]::ASCII.GetBytes('data')); $w.Write([int]($n * 2))
    for ($i = 0; $i -lt $n; $i++) { $w.Write([int16](6000 * [Math]::Sin(2 * [Math]::PI * 523 * $i / $rate))) }
    $w.Flush()
    [IO.File]::WriteAllBytes("$files\Albums\zebra.wav", $ms.ToArray())

    # A comic book: a .cbz (a zip of pictures) of three pages.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::Open("$files\Books\comic.cbz", 'Create')
    for ($p = 1; $p -le 3; $p++) {
        $bmp = New-Object System.Drawing.Bitmap 800, 1200
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.Clear([System.Drawing.Color]::FromArgb(255, (60 * $p), 80, (240 - 60 * $p)))
        $g.DrawString("Page $p", (New-Object System.Drawing.Font 'Segoe UI', 80), [System.Drawing.Brushes]::White, 120, 500)
        $g.Dispose()
        $tmp = Join-Path $scratch "page$p.jpg"
        $bmp.Save($tmp, [System.Drawing.Imaging.ImageFormat]::Jpeg); $bmp.Dispose()
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $tmp, "page-0$p.jpg")
    }
    $zip.Dispose()

    # An EPUB (a zip with a container, a package and one chapter) whose text holds a word found nowhere else.
    $zip = [IO.Compression.ZipFile]::Open("$files\Books\airship.epub", 'Create')
    function AddEntry($name, $text, $level) {
        $e = $zip.CreateEntry($name, $level)
        $sw = New-Object IO.StreamWriter($e.Open(), (New-Object Text.UTF8Encoding $false)); $sw.Write($text); $sw.Dispose()
    }
    AddEntry 'mimetype' 'application/epub+zip' ([IO.Compression.CompressionLevel]::NoCompression)
    $L = [IO.Compression.CompressionLevel]::Optimal
    AddEntry 'META-INF/container.xml' '<?xml version="1.0"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>' $L
    AddEntry 'content.opf' '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>The Airship</dc:title><dc:identifier id="id">airship-1</dc:identifier><dc:language>en</dc:language></metadata><manifest><item id="c1" href="ch1.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="c1"/></spine></package>' $L
    AddEntry 'ch1.xhtml' '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>One</title></head><body><h1>Chapter One</h1><p>The zeppelin rose slowly over the harbour while the crew watched the gulls wheel below. Nobody spoke of the storm.</p><p>By evening the zeppelin had crossed the bay.</p></body></html>' $L
    $zip.Dispose()

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

    $captured = New-Object System.Collections.Generic.List[string]
    $known = @($manifest | ForEach-Object { $_.Id })
    function Shot($name, $delay = 700) {
        if ($known -notcontains $name) { Write-Warning "$name is not in tools\ui-states.psd1" }
        Start-Sleep -Milliseconds $delay; [UiShots]::Shot($h, (Join-Path $OutDir "$name.png")); $captured.Add($name); Write-Host "saved $name"
    }
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
        Shot '21-empty'
        # Import: Ctrl+O opens the folder picker; paste the path, Enter goes into it, Enter picks it.
        Set-Clipboard -Value $files
        # The picker is its own window and has the focus, so these keys go out without bringing the main window forward.
        Focus; [UiKeys]::Combo(0x11, 0x4F); Start-Sleep -Seconds 2
        [UiKeys]::Combo(0x11, 0x56); Start-Sleep -Milliseconds 500
        [UiShots]::Key(0x0D); Start-Sleep -Seconds 1; [UiShots]::Key(0x0D)
        Start-Sleep -Seconds 4
        Shot '42-sheet-folder-tags'
        Press 0x0D          # accept the folder tags sheet
        Start-Sleep -Seconds 2
        Shot '01-grid'

        # --- Library tour: the grid, search, selection, menus, sheets and settings, before any music plays ---
        $r = [UiShots]::Rect($h); $ww = $r.R - $r.L; $wh = $r.B - $r.T; $scale = [UiShots]::Scale($h)
        function Away { [UiShots]::Move($h, [int]($ww * 0.6), [int]($wh * 0.97)) }
        function Dismiss { Press 0x1B; Start-Sleep -Milliseconds 300 }
        function Tile($col) { [UiShots]::Move($h, [int]($ww * (0.26 + 0.126 * $col)), [int]($wh * 0.235)) }
        Away; Shot '18-grid-sidebar'
        Tile 0; Shot '30-tile-hover'; Away
        Combo 0x42; Shot '22-grid-sidebar-hidden'; Combo 0x42
        1..4 | ForEach-Object { Combo 0xBB }; Away; Shot '23-grid-tiles-large'
        1..10 | ForEach-Object { Combo 0xBD }; Shot '24-grid-tiles-small'
        1..6 | ForEach-Object { Combo 0xBB }
        Size 480 560; Shot '28-grid-small-window'
        Size 1600 300; Shot '62-window-short'
        Size 2200 1200

        Search 'qqqq'; Shot '25-search-no-results'
        [UiShots]::Move($h, [int]($ww * 0.6), [int]($wh * 0.5)); Focus; [UiShots]::RightClick(); Shot '35-menu-grid'; Dismiss
        Combo 0x46; Press 0x1B; Combo 0x46; Set-Clipboard -Value '{files}'; Combo 0x56   # pasted: the braces are on different keys in each keyboard layout
        Start-Sleep -Milliseconds 500; Shot '26-search-tag-query'

        Search 'pic'; Press 0x0D
        Focus
        Tile 0; [UiShots]::Click()
        Tile 1; [UiShots]::ShiftClick()   # two tiles over; the third is two columns away
        Tile 2; [UiShots]::ShiftClick()
        Away; Shot '19-grid-three-selected'
        Press 0x1B
        Search 'pic'; Press 0x0D; Focus; Tile 0; [UiShots]::Click()
        Combo 0x49; Away; Shot '27-details-picture'; Combo 0x49
        Press 0x5D; Shot '33-menu-file'; Dismiss      # Menu key
        Combo 0x54; Shot '38-sheet-tag'; Dismiss
        Press 0x71; Shot '39-sheet-rename'; Dismiss
        Press 0x2E; Shot '40-sheet-remove'; Dismiss
        [UiShots]::ShiftExt(0x2E); Shot '41-sheet-delete'; Dismiss   # Esc cancels: nothing is deleted
        Press 0x70; Shot '63-sheet-shortcuts'; Dismiss
        Search 'song'; Press 0x0D; Focus; Tile 0; [UiShots]::Click()
        Combo 0x49; Away; Shot '20-details-song'; Combo 0x49
        Press 0x5D; Shot '34-menu-song'; Dismiss

        # The sort button: the left half of it is always the button, whether or not it shows its label.
        [UiShots]::Move($h, [int]($ww - (16 + 28 + 6 + 14) * $scale), [int](56 * $scale)); Focus; [UiShots]::Click()
        Shot '36-menu-sort'; Dismiss

        Combo 0xBC; Shot '47-settings-top'
        [UiShots]::Move($h, [int]($ww * 0.5), [int]($wh * 0.5)); [UiShots]::Wheel(-30); Shot '48-settings-bottom'
        Size 480 560; Shot '49-settings-small'
        Size 2200 1200
        Combo 0xBC

        # Pictures in the viewer, nothing playing.
        Search 'pic'; Press 0x0D; Press 0x0D
        Start-Sleep -Seconds 2
        [UiShots]::Move($h, [int]($ww / 2), [int]($wh / 2)); Shot '51-viewer-picture'
        1..3 | ForEach-Object { Press 0xBB }; Shot '52-viewer-picture-zoom'
        Press 0x30
        Combo 0x54; Shot '53-viewer-picture-tags'; Dismiss
        Press 0x46; Start-Sleep -Seconds 1; Shot '54-viewer-fullscreen'; Press 0x46; Start-Sleep -Seconds 1
        Size 480 560; Shot '55-viewer-small'
        Size 2200 1200
        Dismiss; Start-Sleep -Seconds 1

        # A comic book.
        Search 'comic'; Press 0x0D; Press 0x0D
        Start-Sleep -Seconds 3
        Shot '58-viewer-book'
        Press 0x27; Start-Sleep -Seconds 1; Shot '59-viewer-book-next'
        Dismiss; Start-Sleep -Seconds 1

        # A word that is only inside the EPUB: the "in text" tile, the info pane's snippet, then the book itself.
        Search 'zeppelin'; Start-Sleep -Seconds 2; Shot '64-search-in-text'
        Press 0x0D; Focus; Tile 0; [UiShots]::Click()
        Combo 0x49; Away; Shot '65-details-in-text'; Combo 0x49
        Press 0x0D; Start-Sleep -Seconds 3; Shot '60-viewer-book-epub'
        Dismiss; Start-Sleep -Seconds 1

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

        # Rows of the rail: the first at 22% of the height, each 56 DIP below the last.
        $scale = [UiShots]::Scale($h)
        function RailY($i) { [int](0.22 * $wh + $i * 56 * $scale) }
        function Park { [UiShots]::Move($h, [int]($ww * 0.08), [int]($wh * 0.5)) }   # blank backdrop, no overlay
        # A second queued song (the first Q went to row 0; row 1 is now the divider, so the listed songs start at row 2; row 3 is the second).
        [UiShots]::Move($h, $x, (RailY 3)); Press 0x51
        Park; Shot '12-stage-no-cover'
        Press 0x20; Park; Shot '11-stage-paused'
        Press 0x20                                  # play again

        # "Play on": a click on the divider (the row after the queue); a second click puts it back.
        [UiShots]::Move($h, $x, (RailY 2)); [UiShots]::Click(); Park; Shot '13-stage-play-on'
        [UiShots]::Move($h, $x, (RailY 2)); [UiShots]::Click()

        # Drag: pick up the first queued row, hold it over the second.
        [UiShots]::Move($h, $x, (RailY 0)); Start-Sleep -Milliseconds 300
        [UiShots]::Down()
        [UiShots]::Move($h, $x, (RailY 0) + 12); Start-Sleep -Milliseconds 150
        [UiShots]::Move($h, $x, (RailY 0) + 40); Start-Sleep -Milliseconds 150
        [UiShots]::Move($h, $x, (RailY 1)); Shot '14-rail-drag'
        [UiShots]::Up()

        Size 1000 800; Shot '05-stage-narrow'
        Size 480 560; Shot '06-stage-small'
        Size ([int](900 * $scale)) ([int](800 * $scale)); Shot '16-stage-900dip'
        Size 2200 1200

        # Close the viewer: the music goes on in the strip.
        Press 0x1B; Start-Sleep -Seconds 1
        [UiShots]::Move($h, 5, 5)
        Shot '07-grid-strip'

        # A picture over the parked music: the strip retracts; the pointer at the bottom edge pulls it out.
        Search 'pic'; Press 0x0D; Press 0x0D
        Start-Sleep -Seconds 2
        $r = [UiShots]::Rect($h); $ww = $r.R - $r.L; $wh = $r.B - $r.T
        [UiShots]::Move($h, [int]($ww / 2) - 30, [int]($wh / 2)); Start-Sleep -Milliseconds 200
        [UiShots]::Move($h, [int]($ww / 2), [int]($wh / 2))
        # Just after the pointer moved, the line is still up: a crop of the bottom of the window.
        Shot '17-retracted-line'
        [UiShots]::CropBottom((Join-Path $OutDir '17-retracted-line.png'), (Join-Path $OutDir '17-retracted-line.png'), 0.2)
        Start-Sleep -Seconds 4   # let the overlays fade
        Shot '08-picture-retracted'
        [UiShots]::Move($h, [int]($ww / 2), $wh - 60); Start-Sleep -Milliseconds 300
        [UiShots]::Move($h, [int]($ww / 2), $wh - 14); Start-Sleep -Milliseconds 300
        [UiShots]::Move($h, [int]($ww / 2) + 3, $wh - 12); Start-Sleep -Milliseconds 600
        Shot '09-picture-strip-out'

        Press 0x1B; Start-Sleep -Seconds 1
        Size 480 560; Shot '10-grid-small-strip'

        # The cover filling the screen: open the song with a cover, find the cover by its yellow frame, press the
        # button in its bottom right corner (it shows while the pointer is over the cover).
        Size 2200 1200
        Search 'zebra'; Press 0x0D; Press 0x0D
        Start-Sleep -Seconds 3
        $box = [UiShots]::Find($h, 255, 255, 0, 12)
        if ($box[2] -lt 0) { Write-Warning 'Could not find the cover; skipped 15-cover-full.' }
        else {
            $bx = [int]($box[2] - 26 * $scale); $by = [int]($box[3] - 26 * $scale)
            [UiShots]::Move($h, $bx - 40, $by - 40); Start-Sleep -Milliseconds 400
            [UiShots]::Move($h, $bx, $by); Start-Sleep -Milliseconds 500
            [UiShots]::Click()
            [UiShots]::Move($h, 5, [int]($wh * 0.5)); Start-Sleep -Seconds 3
            Shot '15-cover-full'
            [UiShots]::Click()                      # any click gives the stage back
        }
        Press 0x1B; Start-Sleep -Seconds 1

        # Notices (after the music, because Q fills the queue).
        Search 'song'; Press 0x0D; Focus; Tile 0; [UiShots]::Click(); Press 0x51; Shot '45-notice-queued' 150
        Search 'pic'; Press 0x0D; Focus; Tile 0; [UiShots]::Click(); Press 0x51; Shot '46-notice-error' 150
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

# --- Report: states the manifest promises but the run did not save ---
$missing = @($manifest | Where-Object { $_.Auto -and -not (Test-Path -LiteralPath (Join-Path $OutDir "$($_.Id).png")) })
foreach ($m in $missing) { Write-Warning "Not captured: $($m.Id) ($($m.Title))" }

# --- Contact sheet: every state of the manifest in its group, uncaptured ones as gaps ---
$tileW = 360; $tileH = 230; $gap = 12; $label = 34; $cols = 5; $head = 40
$groups = @($manifest | ForEach-Object { $_.Group } | Select-Object -Unique)
$rows = 0; foreach ($g in $groups) { $n = @($manifest | Where-Object { $_.Group -eq $g }).Count; $rows += [Math]::Ceiling($n / $cols) }
$sheetW = $cols * ($tileW + $gap) + $gap
$sheetH = $groups.Count * $head + $rows * ($tileH + $label + $gap) + $gap
$sheet = New-Object System.Drawing.Bitmap $sheetW, $sheetH
$gfx = [System.Drawing.Graphics]::FromImage($sheet)
$gfx.Clear([System.Drawing.Color]::FromArgb(27, 25, 30))
$gfx.InterpolationMode = 'HighQualityBicubic'; $gfx.TextRenderingHint = 'ClearTypeGridFit'
$fontH = New-Object System.Drawing.Font 'Segoe UI Semibold', 14
$fontS = New-Object System.Drawing.Font 'Segoe UI', 9
$white = [System.Drawing.Brushes]::White; $dim = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(141, 134, 152))
$gapBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(42, 39, 48))
$y = $gap
foreach ($g in $groups) {
    $gfx.DrawString($g, $fontH, $white, $gap, $y + 6); $y += $head
    $items = @($manifest | Where-Object { $_.Group -eq $g })
    for ($i = 0; $i -lt $items.Count; $i++) {
        $x = $gap + ($i % $cols) * ($tileW + $gap); $ty = $y + [Math]::Floor($i / $cols) * ($tileH + $label + $gap)
        $png = Join-Path $OutDir "$($items[$i].Id).png"
        if (Test-Path -LiteralPath $png) {
            $img = [System.Drawing.Image]::FromFile($png)
            $k = [Math]::Min($tileW / $img.Width, $tileH / $img.Height)
            $dw = [int]($img.Width * $k); $dh = [int]($img.Height * $k)
            $gfx.FillRectangle($gapBrush, $x, $ty, $tileW, $tileH)
            $gfx.DrawImage($img, $x + [int](($tileW - $dw) / 2), $ty + [int](($tileH - $dh) / 2), $dw, $dh)
            $img.Dispose()
        } else {
            $gfx.FillRectangle($gapBrush, $x, $ty, $tileW, $tileH)
            $why = if ($items[$i].Auto) { 'not captured this run' } else { $items[$i].Why }
            $gfx.DrawString($why, $fontS, $dim, (New-Object System.Drawing.RectangleF ($x + 10), ($ty + 10), ($tileW - 20), ($tileH - 20)))
        }
        $gfx.DrawString("$($items[$i].Id)`n$($items[$i].Title)", $fontS, $white, (New-Object System.Drawing.RectangleF $x, ($ty + $tileH + 2), $tileW, $label))
    }
    $y += [Math]::Ceiling($items.Count / $cols) * ($tileH + $label + $gap)
}
$gfx.Dispose()
$sheet.Save((Join-Path $OutDir 'contact-sheet.png'), [System.Drawing.Imaging.ImageFormat]::Png); $sheet.Dispose()
Write-Host "Contact sheet: $(Join-Path $OutDir 'contact-sheet.png')"

# --- Diff against the previous run: a red overlay of the pixels that changed, and a report ---
if (-not $NoDiff -and (Test-Path -LiteralPath (Join-Path $OutDir 'previous'))) {
    $diffDir = Join-Path $OutDir 'diff'
    if (Test-Path $diffDir) { Remove-Item -Recurse -Force $diffDir }
    New-Item -ItemType Directory -Force -Path $diffDir | Out-Null
    $report = New-Object System.Collections.Generic.List[string]
    foreach ($png in (Get-ChildItem -LiteralPath $OutDir -Filter '*.png' -File | Where-Object { $_.Name -ne 'contact-sheet.png' })) {
        $old = Join-Path $OutDir "previous\$($png.Name)"
        if (-not (Test-Path -LiteralPath $old)) { $report.Add("new        $($png.BaseName)"); continue }
        $f = [UiShots]::Diff($old, $png.FullName, (Join-Path $diffDir $png.Name))
        if ($f -lt 0) { $report.Add("resized    $($png.BaseName)") }
        elseif ($f -gt 0) { $report.Add(('changed    {0}  {1:P2} of the pixels' -f $png.BaseName, $f)) }
    }
    foreach ($old in (Get-ChildItem -LiteralPath (Join-Path $OutDir 'previous') -Filter '*.png' -File)) {
        if (-not (Test-Path -LiteralPath (Join-Path $OutDir $old.Name))) { $report.Add("gone       $($old.BaseName)") }
    }
    if ($report.Count -eq 0) { $report.Add('No state changed since the previous run.') }
    [IO.File]::WriteAllLines((Join-Path $OutDir 'diff-report.txt'), $report)
    Write-Host "Diff against the previous run ($($report.Count) lines), overlays in ${diffDir}:"
    $report | ForEach-Object { Write-Host "  $_" }
}
Write-Host "Screenshots in $OutDir"
