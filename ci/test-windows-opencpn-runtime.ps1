param(
    [Parameter(Mandatory = $true)]
    [string] $Repository,
    [Parameter(Mandatory = $true)]
    [string] $PackageArchive,
    [Parameter(Mandatory = $true)]
    [string] $ArtifactDirectory
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$ProgressPreference = "SilentlyContinue"

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

function Save-Screenshot([string] $path) {
    $bounds = [System.Windows.Forms.SystemInformation]::VirtualScreen
    $bitmap = New-Object System.Drawing.Bitmap -ArgumentList @(
        $bounds.Width, $bounds.Height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty,
            $bounds.Size)
        $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

function Find-ElementByName([string] $name) {
    $condition = [System.Windows.Automation.PropertyCondition]::new(
        [System.Windows.Automation.AutomationElement]::NameProperty, $name)
    return [System.Windows.Automation.AutomationElement]::RootElement.FindFirst(
        [System.Windows.Automation.TreeScope]::Descendants, $condition)
}

function Wait-ElementByName([string] $name, [int] $timeoutSeconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($timeoutSeconds)
    do {
        $element = Find-ElementByName $name
        if ($null -ne $element) { return $element }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}

function Invoke-Element($element, [string] $description) {
    if ($null -eq $element) { throw "UI element not found: $description" }
    $pattern = $element.GetCurrentPattern(
        [System.Windows.Automation.InvokePattern]::Pattern)
    $pattern.Invoke()
}

function Close-WindowElement($element) {
    if ($null -eq $element) { return }
    try {
        $pattern = $element.GetCurrentPattern(
            [System.Windows.Automation.WindowPattern]::Pattern)
        $pattern.Close()
    }
    catch {
        Write-Warning "Could not close UI window cleanly: $($_.Exception.Message)"
    }
}

function Get-VisibleEditValues() {
    $condition = [System.Windows.Automation.PropertyCondition]::new(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::Edit)
    $elements = [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
        [System.Windows.Automation.TreeScope]::Descendants, $condition)
    $values = @()
    foreach ($element in $elements) {
        try {
            $pattern = $element.GetCurrentPattern(
                [System.Windows.Automation.ValuePattern]::Pattern)
            if ($pattern.Current.Value) {
                $values += $pattern.Current.Value
                if ($pattern.Current.Value -eq $script:currentPath) {
                    $element.SetFocus()
                }
            }
        }
        catch {
            # Some native edit-like controls do not expose ValuePattern.
        }
    }
    return $values
}

function Wait-FileContains(
    [string] $path,
    [string] $text,
    [int] $timeoutSeconds
) {
    $deadline = [DateTime]::UtcNow.AddSeconds($timeoutSeconds)
    do {
        if (Test-Path $path) {
            $content = Get-Content $path -Raw -ErrorAction SilentlyContinue
            if ($content -and $content.Contains($text)) { return $true }
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Wait-File([string] $path, [int] $timeoutSeconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($timeoutSeconds)
    do {
        if ((Test-Path $path) -and (Get-Item $path).Length -gt 0) {
            return $true
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

$repositoryPath = (Resolve-Path $Repository).Path
$packageArchivePath = (Resolve-Path $PackageArchive).Path
$artifactPath = (Resolve-Path $ArtifactDirectory).Path
$logDirectory = Join-Path $artifactPath "logs"
$testDirectory = Join-Path $artifactPath "tests"
$screenshotDirectory = Join-Path $artifactPath "screenshots"
$runtimeRoot = Join-Path $repositoryPath "windows-opencpn-runtime"
$opencpnRoot = Join-Path $runtimeRoot "OpenCPN-5.14.0"
$downloadDirectory = Join-Path $runtimeRoot "downloads"
$packageExtract = Join-Path $runtimeRoot "xgrib-package"
$installer = Join-Path $downloadDirectory "opencpn_5.14.0_setup.exe"
$opencpnLog = Join-Path $opencpnRoot "opencpn.log"
$runtimeLog = Join-Path $logDirectory "opencpn.log"
$openCpnExtractLog = Join-Path $logDirectory "opencpn-extract.log"
$runtimeResultPath = Join-Path $testDirectory "windows-opencpn-runtime.json"
New-Item -ItemType Directory -Force $logDirectory,$testDirectory,
    $screenshotDirectory,$runtimeRoot,$downloadDirectory,$packageExtract | Out-Null

$installerUrl = "https://github.com/OpenCPN/OpenCPN/releases/download/Release_5.14.0/opencpn_5.14.0-0+4418.91f3b67_setup.exe"
$installerSha256 = "f049075bd3411dc3d5ba2954229ecebf8510abd36529fd27d961d37f275c1076"
Invoke-WebRequest $installerUrl -OutFile $installer
$actualInstallerHash = (Get-FileHash -Algorithm SHA256 $installer).Hash.ToLowerInvariant()
if ($actualInstallerHash -ne $installerSha256) {
    throw "OpenCPN installer checksum mismatch"
}

# The signed NSIS release is also a valid 7-Zip archive. Extracting it gives
# CI the genuine runtime without UAC, registry writes or a machine-wide
# installation, and makes the isolated test directory fully disposable.
& 7z.exe x -y "-o$opencpnRoot" $installer 2>&1 |
    Set-Content -Encoding utf8 $openCpnExtractLog
if ($LASTEXITCODE -ne 0 -or
    -not (Test-Path (Join-Path $opencpnRoot "opencpn.exe"))) {
    throw "OpenCPN 5.14.0 archive extraction failed"
}

& tar.exe -xf $packageArchivePath -C $packageExtract
if ($LASTEXITCODE -ne 0) { throw "Could not extract the xGRIB package" }
$packagedPlugin = Get-ChildItem $packageExtract -Filter "xgrib_pi.dll" -Recurse |
    Select-Object -First 1
if ($null -eq $packagedPlugin) { throw "xGRIB package has no plugin DLL" }
Copy-Item (Join-Path $packagedPlugin.Directory.FullName "*") `
    (Join-Path $opencpnRoot "plugins") -Recurse -Force
$plugin = Join-Path $opencpnRoot "plugins\xgrib_pi.dll"
$helper = Join-Path $opencpnRoot "plugins\xgrib_pi\bin\environmental-grib.exe"
$pluginData = Join-Path $opencpnRoot "plugins\xgrib_pi"
if (-not (Test-Path $plugin) -or -not (Test-Path $helper) -or
    -not (Test-Path (Join-Path $pluginData "data\sources.json"))) {
    throw "The staged xGRIB installation is incomplete"
}

$fixtureDirectory = Join-Path $runtimeRoot (
    "fixtures with spaces caf" + [char]0x00e9)
$privateData = Join-Path $runtimeRoot "xgrib-private-data"
New-Item -ItemType Directory -Force $fixtureDirectory,$privateData | Out-Null
$weatherPath = Join-Path $fixtureDirectory "wind known.grb2"
$currentPath = Join-Path $fixtureDirectory "current differing.grb"
$outputPath = Join-Path $fixtureDirectory "combined from OpenCPN.grb2"
Copy-Item (Join-Path $repositoryPath "test\fixtures\wind-known.grb2") $weatherPath
Copy-Item (Join-Path $repositoryPath "test\fixtures\current-differing.grb") $currentPath

$config = @"
[Settings]
ConfigVersionString=Version 5.14.0 Build 2026-04-08
NavMessageShown=1
OpenGL=0
DisableOpenGL=1
ShowMenuBar=1
[PlugIns]
[PlugIns/grib_pi.dll]
bEnabled=0
[PlugIns/xgrib_pi.dll]
bEnabled=1
"@
$config | Set-Content -Encoding ascii (Join-Path $opencpnRoot "opencpn.ini")

$env:XGRIB_TEST_OPEN_GENERATOR = "1"
$env:XGRIB_TEST_WEATHER_FILE = $weatherPath
$env:XGRIB_TEST_CURRENT_FILE = $currentPath
$env:XGRIB_TEST_OUTPUT_FILE = $outputPath
$env:XGRIB_TEST_PLUGIN_DATA_DIR = $pluginData
$env:XGRIB_TEST_PRIVATE_DATA_DIR = $privateData
Remove-Item Env:XGRIB_TEST_AUTORUN_MERGE -ErrorAction SilentlyContinue

$sourceIdentifier = "xgrib-helper-start-$PID"
$opencpnProcess = $null
$helperInfo = $null
$cleanShutdown = $false
$pathControlsVerified = $false
$successDialogObserved = $false
Register-WmiEvent -Class Win32_ProcessStartTrace -SourceIdentifier $sourceIdentifier |
    Out-Null
try {
    $opencpnProcess = Start-Process -FilePath (Join-Path $opencpnRoot "opencpn.exe") `
        -ArgumentList @("/p") -WorkingDirectory $opencpnRoot -PassThru

    $mainWindowDeadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $opencpnProcess.Refresh()
        if ($opencpnProcess.MainWindowHandle -ne 0) { break }
        if ($opencpnProcess.HasExited) { throw "OpenCPN exited during startup" }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $mainWindowDeadline)
    if ($opencpnProcess.MainWindowHandle -eq 0) {
        throw "OpenCPN did not create a main window"
    }
    Save-Screenshot (Join-Path $screenshotDirectory "01-opencpn-running.png")

    $agree = Wait-ElementByName "Agree" 5
    if ($null -ne $agree) { Invoke-Element $agree "OpenCPN navigation warning Agree button" }

    $generatorWindow = Wait-ElementByName "Environmental GRIB Generator" 45
    if ($null -eq $generatorWindow) {
        throw "xGRIB did not open its environmental generator window"
    }
    if (-not (Wait-FileContains $opencpnLog "xGRIB smoke test accepted current path:" 10)) {
        throw "xGRIB did not log acceptance of both deterministic paths"
    }

    $editValues = Get-VisibleEditValues
    $pathControlsVerified =
        ($editValues -contains $weatherPath) -and ($editValues -contains $currentPath)
    Start-Sleep -Milliseconds 500
    Save-Screenshot (Join-Path $screenshotDirectory "02-selected-paths-visible.png")

    if (-not (Wait-FileContains $opencpnLog "OnInitTimer...Finalize Canvases" 20)) {
        throw "OpenCPN did not finish its deferred UI initialization"
    }

    $generateButton = Wait-ElementByName "Generate Complete GRIB" 10
    if ($null -eq $generateButton -or -not $generateButton.Current.IsEnabled) {
        throw "The xGRIB Generate Complete GRIB button is not enabled"
    }
    # UI Automation's InvokePattern does not require keyboard focus.  In the
    # CircleCI interactive session wxButton advertises IsKeyboardFocusable as
    # false, so SetFocus() raises before Invoke() can run.
    Invoke-Element $generateButton "xGRIB Generate Complete GRIB button"

    if (-not (Wait-FileContains $opencpnLog `
            "xGRIB environmental generator launched, pid=" 5)) {
        if ($null -ne (Find-ElementByName "Launch failed")) {
            throw "Windows loader rejected the packaged environmental-grib.exe"
        }
        # Retry the control's supported default action once.  Keep this
        # focus-free: keyboard-focus APIs are not valid for this wxMSW button
        # on the CircleCI desktop.  The production PID log below prevents a
        # retry after a successful first invocation.
        Invoke-Element $generateButton `
            "xGRIB Generate Complete GRIB button retry"
    }

    $helperDeadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        if ($null -ne (Find-ElementByName "Launch failed")) {
            throw "Windows loader rejected the packaged environmental-grib.exe"
        }
        $events = @(Get-Event -SourceIdentifier $sourceIdentifier `
            -ErrorAction SilentlyContinue)
        foreach ($event in $events) {
            if ($event.SourceEventArgs.NewEvent.ProcessName -ieq
                "environmental-grib.exe") {
                $helperInfo = [ordered]@{
                    process_name = $event.SourceEventArgs.NewEvent.ProcessName
                    process_id = [int]$event.SourceEventArgs.NewEvent.ProcessID
                    parent_process_id = [int]$event.SourceEventArgs.NewEvent.ParentProcessID
                    observed_after_gui_generate = $true
                    observation_source = "wmi_process_start"
                }
                break
            }
        }
        if ($null -ne $helperInfo) { break }

        if (Test-Path $opencpnLog) {
            $launchLog = Get-Content $opencpnLog -Raw -ErrorAction SilentlyContinue
            $launchMatch = [regex]::Match(
                $launchLog, "xGRIB environmental generator launched, pid=(\d+)")
            if ($launchMatch.Success) {
                $helperInfo = [ordered]@{
                    process_name = "environmental-grib.exe"
                    process_id = [int]$launchMatch.Groups[1].Value
                    parent_process_id = [int]$opencpnProcess.Id
                    observed_after_gui_generate = $true
                    observation_source = "opencpn_plugin_log"
                }
                break
            }
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $helperDeadline)
    if ($null -eq $helperInfo) {
        Save-Screenshot (Join-Path $screenshotDirectory "99-runtime-failure.png")
        throw "The xGRIB GUI did not launch environmental-grib.exe"
    }
    $helperInfo | ConvertTo-Json | Set-Content -Encoding utf8 `
        (Join-Path $testDirectory "helper-launch.json")
    Save-Screenshot (Join-Path $screenshotDirectory "03-helper-launched.png")

    if (-not (Wait-File $outputPath 90)) {
        throw "The helper did not create the deterministic combined GRIB"
    }
    if (-not (Wait-FileContains $opencpnLog "xGRIB: opened generated GRIB:" 30)) {
        throw "xGRIB did not reopen the helper output"
    }

    $successDialog = Wait-ElementByName "Environmental GRIB generated" 10
    if ($null -ne $successDialog) {
        $successDialogObserved = $true
        Save-Screenshot (Join-Path $screenshotDirectory "04-merge-success.png")
        $okCondition = [System.Windows.Automation.PropertyCondition]::new(
            [System.Windows.Automation.AutomationElement]::NameProperty, "OK")
        $ok = $successDialog.FindFirst(
            [System.Windows.Automation.TreeScope]::Descendants, $okCondition)
        if ($null -ne $ok) { Invoke-Element $ok "merge-success OK button" }
    }
    else {
        Save-Screenshot (Join-Path $screenshotDirectory "04-combined-reopened.png")
        throw "xGRIB created and reopened the GRIB but did not show success feedback"
    }

    $inspectionPath = Join-Path $testDirectory "runtime-combined-inspection.json"
    & $helper inspect-grib $outputPath | Set-Content -Encoding utf8 $inspectionPath
    if ($LASTEXITCODE -ne 0) { throw "Packaged helper rejected the GUI output" }
    $inspection = Get-Content $inspectionPath -Raw | ConvertFrom-Json
    if ($inspection.message_count -ne 10 -or
        $inspection.grib2_parameter_counts.'0:2:2' -ne 2 -or
        $inspection.grib2_parameter_counts.'0:2:3' -ne 2 -or
        $inspection.current_component_counts.u_49 -ne 3 -or
        $inspection.current_component_counts.v_50 -ne 3) {
        throw "The GUI-generated GRIB does not contain the expected wind/current fields"
    }

    $logText = Get-Content $opencpnLog -Raw
    foreach ($requiredLogText in @(
        "Checking plugin candidate:",
        "xgrib_pi.dll",
        "PluginLoader: Initializing PlugIn:",
        "xGRIB smoke test opened environmental generator dialog",
        "xGRIB smoke test accepted weather path:",
        "xGRIB smoke test accepted current path:",
        "xGRIB environmental generator launched, pid=",
        "xGRIB: opened generated GRIB:")) {
        if (-not $logText.Contains($requiredLogText)) {
            throw "OpenCPN runtime log is missing: $requiredLogText"
        }
    }

    Close-WindowElement $generatorWindow
    [void]$opencpnProcess.CloseMainWindow()
    if ($opencpnProcess.WaitForExit(3000)) {
        $cleanShutdown = $true
    }
    else {
        $confirmExit = Wait-ElementByName "Yes" 2
        if ($null -ne $confirmExit) {
            Invoke-Element $confirmExit "OpenCPN exit confirmation"
        }
        if ($opencpnProcess.WaitForExit(12000)) { $cleanShutdown = $true }
    }
}
catch {
    try {
        Save-Screenshot (Join-Path $screenshotDirectory "99-runtime-failure.png")
    }
    catch {
        Write-Warning "Could not capture runtime failure screenshot"
    }
    throw
}
finally {
    if ($null -ne $opencpnProcess -and -not $opencpnProcess.HasExited) {
        Stop-Process -Id $opencpnProcess.Id -Force -ErrorAction SilentlyContinue
        $opencpnProcess.WaitForExit(5000) | Out-Null
    }
    if (Test-Path $opencpnLog) { Copy-Item $opencpnLog $runtimeLog -Force }
    Get-Event -SourceIdentifier $sourceIdentifier -ErrorAction SilentlyContinue |
        Remove-Event -ErrorAction SilentlyContinue
    Unregister-Event -SourceIdentifier $sourceIdentifier -ErrorAction SilentlyContinue
}

if (-not $cleanShutdown) { throw "OpenCPN did not shut down cleanly" }
if (-not $pathControlsVerified) {
    throw "Windows UI Automation did not expose both selected path values"
}

$runtimeResult = [ordered]@{
    schema = "xgrib-windows-runtime-v1"
    opencpn_version = "5.14.0"
    installer_sha256 = $actualInstallerHash
    portable_profile = $true
    bundled_grib_disabled = $true
    plugin_discovered = $true
    plugin_loaded = $true
    generator_window_opened = $true
    selected_path_controls_verified = $pathControlsVerified
    helper_launch_observed = ($null -ne $helperInfo)
    helper_process = $helperInfo
    deterministic_merge_passed = $true
    combined_grib_reopened = $true
    success_dialog_observed = $successDialogObserved
    clean_shutdown = $cleanShutdown
    output_path = $outputPath
    output_sha256 = (Get-FileHash -Algorithm SHA256 $outputPath).Hash.ToLowerInvariant()
    screenshots = @(
        "screenshots/01-opencpn-running.png",
        "screenshots/02-selected-paths-visible.png",
        "screenshots/03-helper-launched.png",
        $(if ($successDialogObserved) {
            "screenshots/04-merge-success.png"
        } else {
            "screenshots/04-combined-reopened.png"
        }))
}
$runtimeResult | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 `
    $runtimeResultPath
Write-Host "OpenCPN 5.14.0 loaded xGRIB and launched the packaged x64 helper"
