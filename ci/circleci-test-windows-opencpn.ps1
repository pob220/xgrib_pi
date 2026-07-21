$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$artifact = (Resolve-Path (Join-Path $repo "artifacts\windows-x86")).Path
$packageDir = Join-Path $artifact "package"
$logDir = Join-Path $artifact "logs"
$testDir = Join-Path $artifact "tests"
$resultPath = Join-Path $artifact "result.json"
$runtimeScript = Join-Path $repo "ci\test-windows-opencpn-runtime.ps1"
$archives = @(Get-ChildItem $packageDir -File -Filter "xgrib_pi-*.tar.gz")
if ($archives.Count -ne 1) {
    $names = ($archives | ForEach-Object { $_.Name }) -join ", "
    throw "Expected exactly one Windows workspace package; found $($archives.Count): $names"
}
$archive = $archives[0]
$metadataPath = $archive.FullName.Substring(
    0, $archive.FullName.Length - ".tar.gz".Length) + ".xml"
if (-not (Test-Path $metadataPath -PathType Leaf)) {
    throw "The Windows workspace package has no matching metadata: $metadataPath"
}
if (-not (Test-Path $resultPath)) {
    throw "The Windows build workspace has no result manifest"
}

$parseTokens = $null
$parseErrors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile(
    $runtimeScript, [ref]$parseTokens, [ref]$parseErrors)
if ($parseErrors.Count -ne 0) {
    $details = ($parseErrors | ForEach-Object { $_.Message }) -join "; "
    throw "PowerShell syntax check failed for runtime test: $details"
}

$runtimeLog = Join-Path $logDir "opencpn-runtime-test.log"
$savedPreference = $ErrorActionPreference
try {
    $ErrorActionPreference = "Continue"
    & powershell.exe -ExecutionPolicy Bypass -File $runtimeScript `
        -Repository $repo -PackageArchive $archive.FullName `
        -ArtifactDirectory $artifact 2>&1 | Tee-Object -FilePath $runtimeLog
    $runtimeExit = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $savedPreference
}

$result = Get-Content $resultPath -Raw | ConvertFrom-Json
if ($runtimeExit -ne 0) {
    $result.blocker_or_failure_details =
        "OpenCPN runtime validation failed; see logs/opencpn-runtime-test.log"
    $result | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 $resultPath
    throw "OpenCPN Windows runtime test failed with exit code $runtimeExit"
}

$runtimeResult = Get-Content `
    (Join-Path $testDir "windows-opencpn-runtime.json") -Raw | ConvertFrom-Json
$result.opencpn_version = $runtimeResult.opencpn_version
$result.installation_status = "passed"
$result.plugin_discovery_status = "passed"
$result.plugin_load_status = "passed"
$result.graphical_test_status = "passed"
$result.file_path_display_status = "passed"
$result.merge_status = "passed"
$result.output_validation_status = "passed"
$result.screenshot_paths = $runtimeResult.screenshots
$result.log_paths = @($result.log_paths) + @(
    "logs/opencpn-extract.log",
    "logs/opencpn-runtime-test.log",
    "logs/opencpn.log",
    "tests/helper-launch.json",
    "tests/windows-opencpn-runtime.json",
    "tests/runtime-combined-inspection.json")
$result.result_classification = $(if ($runtimeResult.clean_shutdown) {
    "fully-tested"
} else {
    "runtime-tested"
})
$result.blocker_or_failure_details = $(if ($runtimeResult.clean_shutdown) {
    ""
} else {
    "All Windows functional/UI checks passed; the CircleCI desktop could not gracefully close the OpenCPN host frame and cleanup required forced process termination."
})
$result | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 $resultPath

Write-Host "OpenCPN Windows runtime validation completed: $($result.result_classification)"
