param([ValidateSet('x86','x64')][string]$Architecture = 'x86')
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path $root 'artifacts/rick-ui-delay'
$runtime = Join-Path $root 'rick-ui-runtime'
New-Item -ItemType Directory -Force $output,$runtime | Out-Null
$inputs = (Get-Content (Join-Path $PSScriptRoot 'rick-ui-delay-inputs.json') -Raw | ConvertFrom-Json).$Architecture

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvarsall.bat'
$vcarch = if ($Architecture -eq 'x86') {'x86'} else {'amd64'}
$probe = Join-Path $runtime 'time-zone-probe.exe'
$probeSource = Join-Path $PSScriptRoot 'rick-time-zone-probe.cpp'
$buildProbe = Join-Path $runtime 'build-probe.cmd'
@"
@call "$vcvars" $vcarch
@if errorlevel 1 exit /b 1
@cl /nologo /EHsc /std:c++20 /O2 /MD "$probeSource" /Fe:"$probe"
@exit /b %errorlevel%
"@ | Set-Content $buildProbe -Encoding ascii
& cmd.exe /d /c $buildProbe |
    Set-Content (Join-Path $output 'time-zone-probe-build.log')
if ($LASTEXITCODE -ne 0) { throw 'Cannot compile native time-zone probe' }
& $probe | Tee-Object -FilePath (Join-Path $output 'time-zone-probe.log')
if ($LASTEXITCODE -ne 0) { throw 'Native time-zone probe failed' }

# This disposable CI host exercises the original published DLLs. No plugin
# rebuild, installer, real user profile or forecast download is involved.
Add-Type @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class DelayWindow {
  public IntPtr Handle, Parent;
  public int Id;
  public string Title;
  public bool Visible;
}
public static class DelayNative {
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr p);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h, EnumProc callback, IntPtr p);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int count);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
  [DllImport("user32.dll")] static extern int GetDlgCtrlID(IntPtr h);
  [DllImport("user32.dll")] static extern IntPtr GetParent(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", SetLastError=true)] static extern IntPtr SendMessageTimeout(IntPtr h, uint m, IntPtr w, IntPtr l, uint flags, uint timeout, out IntPtr result);
  public static bool Responsive(IntPtr h) {
    IntPtr result;
    return SendMessageTimeout(h, 0, IntPtr.Zero, IntPtr.Zero, 2, 50, out result) != IntPtr.Zero;
  }
  public static DelayWindow[] Windows(int processId, bool children) {
    var result = new List<DelayWindow>();
    EnumProc collect = (h, p) => {
      uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid == processId) {
        var text = new StringBuilder(2048); GetWindowText(h, text, text.Capacity);
        result.Add(new DelayWindow {Handle=h, Parent=GetParent(h), Id=GetDlgCtrlID(h), Title=text.ToString(), Visible=IsWindowVisible(h)});
      }
      return true;
    };
    EnumWindows((h,p) => {
      collect(h,p);
      if (children) EnumChildWindows(h, collect, p);
      return true;
    }, IntPtr.Zero);
    return result.ToArray();
  }
}
'@

function Fetch($entry, [string]$destination) {
    Invoke-WebRequest $entry.url -OutFile $destination
    if ((Get-FileHash $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) {
        throw "Checksum mismatch: $destination"
    }
}
function Read-Log([string]$path) {
    if (Test-Path $path) { return [string](Get-Content $path -Raw -ErrorAction SilentlyContinue) }
    return ''
}
function Wait-Until([scriptblock]$test, $process, [int]$seconds = 180) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while ($watch.Elapsed.TotalSeconds -lt $seconds) {
        $process.Refresh()
        if ($process.HasExited) { throw "Test host exited: $($process.ExitCode)" }
        $value = & $test
        if ($value) { return $value }
        Start-Sleep -Milliseconds 25
    }
    throw "Timeout after $seconds seconds"
}

$hostArchive = Join-Path $runtime 'opencpn-setup.exe'
Fetch $inputs.host $hostArchive
foreach ($plugin in @('xgrib','xweather')) {
    $archive = Join-Path $runtime "$plugin.tar.gz"
    Fetch $inputs.$plugin $archive
    $extract = Join-Path $runtime "$plugin-package"
    New-Item -ItemType Directory -Force $extract | Out-Null
    & tar.exe -xf $archive -C $extract
    if ($LASTEXITCODE -ne 0) { throw "Cannot extract $plugin" }
}

$results = @()
try {
    foreach ($mode in @('xgrib','xweather','both')) {
        $hostDir = Join-Path $runtime "$mode-host"
        & 7z.exe x -y "-o$hostDir" $hostArchive | Out-File (Join-Path $output "$mode-extract.log")
        if ($LASTEXITCODE -ne 0) { throw 'Cannot extract OpenCPN' }
        foreach ($plugin in @('xgrib','xweather')) {
            $dllName = if ($plugin -eq 'xgrib') {'xgrib_pi.dll'} else {'xweather_routing_pi.dll'}
            $dll = Get-ChildItem (Join-Path $runtime "$plugin-package") -Recurse -Filter $dllName | Select-Object -First 1
            Copy-Item (Join-Path $dll.Directory.FullName '*') (Join-Path $hostDir 'plugins') -Recurse -Force
        }
        $routingData = Join-Path $hostDir 'routing-profile'
        New-Item -ItemType Directory -Force $routingData | Out-Null
        '<OpenCPNWeatherRoutingConfiguration version="1.10" creator="UI timing test"/>' |
            Set-Content (Join-Path $routingData 'WeatherRoutingConfiguration.xml') -Encoding ascii
        $gribEnabled = if ($mode -ne 'xweather') {1} else {0}
        $wrEnabled = if ($mode -ne 'xgrib') {1} else {0}
        @"
[Settings]
ConfigVersionString=Version 5.14.2-0+de7e706 Build 2026-09-28
NavMessageShown=1
OpenGL=0
DisableOpenGL=1
ShowMenuBar=1
[PlugIns/grib_pi.dll]
bEnabled=0
[PlugIns/xgrib_pi.dll]
bEnabled=$gribEnabled
[PlugIns/xweather_routing_pi.dll]
bEnabled=$wrEnabled
[PlugIns/WeatherRouting]
ConfigVersion=127
"@ | Set-Content (Join-Path $hostDir 'opencpn.ini') -Encoding ascii
        foreach ($run in 1..2) {
            $log = Join-Path $hostDir 'opencpn.log'
            Remove-Item $log -ErrorAction SilentlyContinue
            Remove-Item Env:WR_HEADLESS_ROUTE_TEST,Env:WR_HEADLESS_DATA_DIR,Env:XGRIB_TEST_OPEN_FILE -ErrorAction SilentlyContinue
            if ($gribEnabled) { $env:XGRIB_TEST_OPEN_FILE = Join-Path $root 'test/fixtures/wind-known.grb2' }
            if ($wrEnabled) { $env:WR_HEADLESS_ROUTE_TEST = 'open-only'; $env:WR_HEADLESS_DATA_DIR = $routingData }
            $process = Start-Process (Join-Path $hostDir 'opencpn.exe') -ArgumentList '/p' -WorkingDirectory $hostDir -PassThru
            $started = [DateTime]::UtcNow
            try {
                if ($wrEnabled) {
                    Wait-Until { ([string](Read-Log $log)).Contains('open_only ready') } $process | Out-Null
                    $text = Read-Log $log
                    $a = [regex]::Match($text, '(?m)^(\d\d:\d\d:\d\d\.\d+) .*WR_HEADLESS_ROUTE_TEST timer_fire')
                    $b = [regex]::Match($text, '(?m)^(\d\d:\d\d:\d\d\.\d+) .*WR_HEADLESS_ROUTE_TEST open_only ready')
                    if (-not $a.Success -or -not $b.Success) { throw 'Missing routing timing markers' }
                    $duration = ([TimeSpan]::Parse($b.Groups[1].Value) - [TimeSpan]::Parse($a.Groups[1].Value)).TotalMilliseconds
                    $results += [pscustomobject]@{mode=$mode;run=$run;screen='routing-main';elapsed_ms=$duration;basis='existing plugin log markers'}
                    Write-Host "$mode run $run routing constructor/show: $duration ms"
                }
                Wait-Until { ([string](Read-Log $log)).Contains('OnInitTimer...Finalize Canvases') } $process | Out-Null
                if ($gribEnabled) {
                    $button = Wait-Until { [DelayNative]::Windows($process.Id,$true) | Where-Object { $_.Id -eq 1011 -and $_.Visible -and [DelayNative]::IsWindowEnabled($_.Handle) } | Select-Object -First 1 } $process
                    foreach ($opening in 1..3) {
                        $process.Refresh(); $cpuBefore = $process.TotalProcessorTime.TotalMilliseconds
                        $watch = [Diagnostics.Stopwatch]::StartNew()
                        if (-not [DelayNative]::PostMessage($button.Parent,0x0111,[IntPtr]1011,$button.Handle)) { throw 'Settings command failed' }
                        $settings = Wait-Until { [DelayNative]::Windows($process.Id,$false) | Where-Object { $_.Title -eq 'Settings' -and $_.Visible -and [DelayNative]::Responsive($_.Handle) } | Select-Object -First 1 } $process
                        $watch.Stop(); $process.Refresh()
                        $results += [pscustomobject]@{mode=$mode;run=$run;screen='xgrib-gear-settings';opening=$opening;elapsed_ms=$watch.Elapsed.TotalMilliseconds;process_cpu_ms=($process.TotalProcessorTime.TotalMilliseconds-$cpuBefore);basis='posted normal settings command to visible responsive settings dialog'}
                        Write-Host "$mode run $run Settings opening $opening : $($watch.Elapsed.TotalMilliseconds) ms"
                        [void][DelayNative]::PostMessage($settings.Handle,0x0010,[IntPtr]::Zero,[IntPtr]::Zero)
                        Wait-Until { -not [DelayNative]::IsWindowVisible($settings.Handle) } $process | Out-Null
                    }
                }
                $modules = @($process.Modules | Where-Object { $_.ModuleName -match 'msvcp|vcruntime|wx|icu' } | ForEach-Object {
                    @{name=$_.ModuleName;path=$_.FileName;version=$_.FileVersionInfo.FileVersion}
                })
                $modules | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $output "$mode-$run-modules.json")
                $main = [DelayNative]::Windows($process.Id,$false) | Where-Object { $_.Title -match '^OpenCPN ' } | Select-Object -First 1
                if ($main) { [void][DelayNative]::PostMessage($main.Handle,0x0010,[IntPtr]::Zero,[IntPtr]::Zero) }
                if (-not $process.WaitForExit(5000)) { $process.Kill(); $process.WaitForExit() }
            } finally {
                $process.Refresh()
                if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
                if (Test-Path $log) { Copy-Item $log (Join-Path $output "$mode-$run-opencpn.log") }
                $results | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $output 'timings.json')
            }
        }
    }
} finally {
    @{architecture=$Architecture;os=[Environment]::OSVersion.VersionString;inputs=$inputs;results=$results} |
        ConvertTo-Json -Depth 10 | Set-Content (Join-Path $output 'report.json')
}
