param([string]$Mode = "resize", [int]$Cols = 220, [int]$Rows = 900, [string]$OutFile = "")

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Con {
    [DllImport("kernel32.dll")] public static extern bool FreeConsole();
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

$proc = Get-Process flycast -ErrorAction Stop | Select-Object -First 1
[Con]::FreeConsole() | Out-Null
if (-not [Con]::AttachConsole([uint32]$proc.Id)) { throw "AttachConsole failed" }
try {
    $h = [Con]::CreateFileW("CONOUT$", [uint32][int64]3221225472, 3, [IntPtr]::Zero, 3, 0, [IntPtr]::Zero)
    if ($h -eq [IntPtr]::New(-1)) { throw "CONOUT$ open failed" }
    if ($Mode -eq "resize") {
        $sz = New-Object Con+COORD
        $sz.X = [int16]$Cols; $sz.Y = [int16]$Rows
        # shrink window rect first is not needed for buffer shrink-to-fit avoidance at startup
        [Con]::SetConsoleScreenBufferSize($h, $sz) | Out-Null
        Write-Output "resized to $Cols x $Rows"
    } else {
        $info = New-Object Con+CONSOLE_SCREEN_BUFFER_INFO
        [Con]::GetConsoleScreenBufferInfo($h, [ref]$info) | Out-Null
        $w = $info.dwSize.X; $ht = $info.dwSize.Y
        $origin = New-Object Con+COORD; $origin.X = 0; $origin.Y = 0
        $sb = New-Object System.Text.StringBuilder ([int]($w * $ht))
        $read = 0
        [Con]::ReadConsoleOutputCharacterW($h, $sb, [uint32]($w * $ht), $origin, [ref]$read) | Out-Null
        $text = $sb.ToString()
        # split into rows of width w, trim trailing spaces, drop empty lines
        $lines = for ($y = 0; $y -lt $ht; $y++) {
            $line = $text.Substring($y * $w, $w).TrimEnd()
            if ($line.Length -gt 0) { $line }
        }
        $lines | Set-Content -Encoding UTF8 $OutFile
        Write-Output ("scraped {0} lines -> {1}" -f $lines.Count, $OutFile)
    }
} finally {
    [Con]::FreeConsole() | Out-Null
}
