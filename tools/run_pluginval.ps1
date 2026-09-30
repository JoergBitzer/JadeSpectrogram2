# run_pluginval.ps1 -- tests a plugin with pluginval (https://github.com/Tracktion/pluginval)
#
# Usage (PowerShell):  tools\run_pluginval.ps1 <path\to\YourPlugin.vst3> [runs]
#
# Runs pluginval at the highest strictness level (10) several times (default 3), because
# some bugs (threading, uninitialised values) only show up now and then.
# pluginval is taken from $env:PLUGINVAL, from the PATH, or downloaded once into
# %LOCALAPPDATA%\pluginval.
#
# Differences to run_pluginval.sh (Linux/macOS): on Windows the plugin uses your real
# AppData folder during the test (JUCE does not follow a changed environment there), and
# JUCE assertions go to the debugger output, not to the log, so they are not counted.
#
# Exit code 0: all runs passed. If PowerShell refuses to run scripts, start it with
#   powershell -ExecutionPolicy Bypass -File tools\run_pluginval.ps1 <plugin> [runs]

param(
    [Parameter(Mandatory = $true)][string]$Plugin,
    [int]$Runs = 3
)

if (-not (Test-Path $Plugin)) { Write-Host "Plugin not found: $Plugin"; exit 2 }
$Plugin = (Resolve-Path $Plugin).Path

# --- find or download pluginval ---
$cache = Join-Path $env:LOCALAPPDATA "pluginval"
if ($env:PLUGINVAL) {
    $pluginval = $env:PLUGINVAL
} elseif (Get-Command pluginval -ErrorAction SilentlyContinue) {
    $pluginval = (Get-Command pluginval).Source
} elseif (Test-Path (Join-Path $cache "pluginval.exe")) {
    $pluginval = Join-Path $cache "pluginval.exe"
} else {
    Write-Host "Downloading pluginval to $cache ..."
    New-Item -ItemType Directory -Force -Path $cache | Out-Null
    $zip = Join-Path $cache "pluginval_Windows.zip"
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri "https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_Windows.zip" -OutFile $zip -UseBasicParsing
        Expand-Archive -Path $zip -DestinationPath $cache -Force
        Remove-Item $zip
    } catch { Write-Host "Download failed: $_"; exit 2 }
    $pluginval = Join-Path $cache "pluginval.exe"
}
Write-Host "pluginval: $pluginval"
Write-Host "plugin:    $Plugin"

# --- test runs ---
$logdir = Join-Path ([IO.Path]::GetTempPath()) ("pluginval_" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $logdir | Out-Null
$failed = 0
for ($i = 1; $i -le $Runs; $i++) {
    $log = Join-Path $logdir "run_$i.log"
    # Start-Process -Wait: pluginval is a GUI application, "&" would not wait for it
    $p = Start-Process -FilePath $pluginval -ArgumentList @("--strictness-level", "10", "--validate", "`"$Plugin`"") `
                       -NoNewWindow -Wait -PassThru -RedirectStandardOutput $log -RedirectStandardError "$log.err"
    if ($p.ExitCode -eq 0) {
        Write-Host "run $i/$Runs`: SUCCESS"
    } else {
        Write-Host "run $i/$Runs`: FAILED (exit code $($p.ExitCode)), log: $log"
        Select-String -Path $log -Pattern "FAILED|\*\*\*" | Select-Object -First 5 | ForEach-Object { Write-Host "    $($_.Line)" }
        $failed++
    }
}

if ($failed -eq 0) {
    Write-Host "All $Runs runs passed."
    Remove-Item -Recurse -Force $logdir
    exit 0
}
Write-Host "$failed of $Runs runs failed. Logs: $logdir"
exit 1
