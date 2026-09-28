param([string]$Executable = "$PSScriptRoot/../build/bin/TuningWizard.exe")
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class WizardWindow {
    public delegate bool Visitor(IntPtr hwnd, IntPtr arg);
    [DllImport("user32.dll")] static extern bool EnumWindows(Visitor v, IntPtr arg);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int capacity);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr w, IntPtr l);
    public static IntPtr Find(uint pid) {
        IntPtr found=IntPtr.Zero;
        EnumWindows((hwnd,arg) => {
            uint owner; GetWindowThreadProcessId(hwnd,out owner);
            var title=new StringBuilder(256); GetWindowText(hwnd,title,256);
            if(owner==pid && title.ToString().Contains("Tuning Wizard")) { found=hwnd; return false; }
            return true;
        },IntPtr.Zero);
        return found;
    }
}
'@
$exePath = (Resolve-Path -LiteralPath $Executable).Path
$wizardProcess = Start-Process -FilePath $exePath -WorkingDirectory (Split-Path $exePath) -WindowStyle Hidden -PassThru
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    $window = [IntPtr]::Zero
    do {
        Start-Sleep -Milliseconds 200
        if ($wizardProcess.HasExited) { throw "Wizard exited during startup: $($wizardProcess.ExitCode)" }
        $window = [WizardWindow]::Find($wizardProcess.Id)
    } while ($window -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
    if ($window -eq [IntPtr]::Zero) { throw 'Wizard window was not initialized' }
    Start-Sleep -Milliseconds 1000
    [void][WizardWindow]::PostMessage($window, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
    if (-not $wizardProcess.WaitForExit(5000)) { throw 'Wizard did not close cleanly' }
    if ($wizardProcess.ExitCode -ne 0) { throw "Wizard shutdown failed: $($wizardProcess.ExitCode)" }
    Write-Output 'PASS hidden desktop startup, neutral window title and clean shutdown'
} finally {
    if (-not $wizardProcess.HasExited) { $wizardProcess.Kill() }
    $wizardProcess.Dispose()
}
