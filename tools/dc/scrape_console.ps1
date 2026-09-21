param([ValidateSet('resize','read')][string]$Mode = "resize",
      [ValidateRange(1,32767)][int]$Cols = 220,
      [ValidateRange(1,32767)][int]$Rows = 900,
      [string]$OutFile = "", [ValidateRange(0,2147483647)][int]$ProcessId = 0)

if ($Mode -eq 'read' -and [string]::IsNullOrWhiteSpace($OutFile)) {
    throw 'Reading the console requires -OutFile.'
}

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Con {
    [DllImport("kernel32.dll")] public static extern bool FreeConsole();
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll")] public static extern bool AttachConsole(uint pid);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)]
    public static extern IntPtr CreateFileW(string name, uint access, uint share, IntPtr sa, uint disp, uint flags, IntPtr template);
    [StructLayout(LayoutKind.Sequential)]
    public struct COORD { public short X, Y; }
    [StructLayout(LayoutKind.Sequential)]
    public struct CONSOLE_SCREEN_BUFFER_INFO {
        public COORD dwSize, dwCursorPosition;
        public short wAttributes; public _SMALL_RECT srWindow;
        public COORD dwMaximumWindowSize;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct _SMALL_RECT { public short Left, Top, Right, Bottom; }
    [DllImport("kernel32.dll")] public static extern bool GetConsoleScreenBufferInfo(IntPtr h, out CONSOLE_SCREEN_BUFFER_INFO info);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)]
    public static extern bool ReadConsoleOutputCharacterW(IntPtr h, [Out] System.Text.StringBuilder buf, uint len, COORD origin, out uint read);
    [DllImport("kernel32.dll")] public static extern bool SetConsoleScreenBufferSize(IntPtr h, COORD size);
}
"@

$candidates = @(if ($ProcessId) { Get-Process -Id $ProcessId -ErrorAction Stop } else { Get-Process flycast -ErrorAction Stop })
if ($candidates.Count -ne 1 -or $candidates[0].ProcessName -ne 'flycast') {
    throw 'Select exactly one Flycast instance with -ProcessId.'
}
$proc = $candidates[0]
[Con]::FreeConsole() | Out-Null
if (-not [Con]::AttachConsole([uint32]$proc.Id)) { throw "AttachConsole failed" }
$h = [IntPtr]::New(-1)
try {
    $h = [Con]::CreateFileW("CONOUT$", [uint32][int64]3221225472, 3, [IntPtr]::Zero, 3, 0, [IntPtr]::Zero)
    if ($h -eq [IntPtr]::New(-1)) { throw "CONOUT$ open failed" }
    if ($Mode -eq "resize") {
        $sz = New-Object Con+COORD
        $sz.X = [int16]$Cols; $sz.Y = [int16]$Rows
        # shrink window rect first is not needed for buffer shrink-to-fit avoidance at startup
        if (-not [Con]::SetConsoleScreenBufferSize($h, $sz)) { throw 'Console resize failed.' }
        Write-Output "resized to $Cols x $Rows"
    } else {
        $info = New-Object Con+CONSOLE_SCREEN_BUFFER_INFO
        if (-not [Con]::GetConsoleScreenBufferInfo($h, [ref]$info)) { throw 'Console buffer query failed.' }
        $w = $info.dwSize.X; $ht = $info.dwSize.Y
        $origin = New-Object Con+COORD; $origin.X = 0; $origin.Y = 0
        $sb = New-Object System.Text.StringBuilder ([int]($w * $ht))
        $read = 0
        if (-not [Con]::ReadConsoleOutputCharacterW($h, $sb, [uint32]($w * $ht), $origin, [ref]$read) -or
            $read -ne $w * $ht) { throw 'Console read failed or was incomplete.' }
        $text = $sb.ToString()
        # split into rows of width w, trim trailing spaces, drop empty lines
        $lines = for ($y = 0; $y -lt $ht; $y++) {
            $line = $text.Substring($y * $w, $w).TrimEnd()
            if ($line.Length -gt 0) { $line }
        }
        $lines | Set-Content -Encoding UTF8 $OutFile -ErrorAction Stop
        Write-Output ("scraped {0} lines -> {1}" -f $lines.Count, $OutFile)
    }
} finally {
    if ($h -ne [IntPtr]::New(-1)) { [Con]::CloseHandle($h) | Out-Null }
    [Con]::FreeConsole() | Out-Null
}
