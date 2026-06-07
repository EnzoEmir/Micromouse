<#
.SYNOPSIS
    Builds and runs the Micromouse host-based unit/integration test suite.

.DESCRIPTION
    Compiles each test target with MSVC (cl.exe) against the real firmware
    sources plus host mocks of the ESP-IDF APIs, then runs the resulting
    executables. No ESP32 hardware, QEMU, or ESP-IDF environment is required.

.PARAMETER Filter
    Optional substring; only targets whose name contains it are built/run.
    Example:  .\run_tests.ps1 maze

.EXAMPLE
    .\run_tests.ps1
    .\run_tests.ps1 battery
#>
param([string]$Filter = "")

$ErrorActionPreference = "Stop"

# --- Locate things ----------------------------------------------------------
$here = Split-Path -Parent $MyInvocation.MyCommand.Path        # test/host
$root = Resolve-Path (Join-Path $here "..\..")                 # Micromouse
$mocks = Join-Path $here "mocks"
$main = Join-Path $root "main"
$cjsonDir = Join-Path $root "managed_components\espressif__cjson\cJSON"
$cjsonC = Join-Path $cjsonDir "cJSON.c"
$build = Join-Path $here "build"
New-Item -ItemType Directory -Force -Path $build | Out-Null

# --- Find and import the MSVC environment -----------------------------------
$vcCandidates = @(
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
)
$vcvars = $vcCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vcvars) { throw "Could not find vcvars64.bat (Visual Studio Build Tools)." }

if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^(.*?)=(.*)$') { Set-Item -Path ("Env:" + $matches[1]) -Value $matches[2] }
    }
}
if (-not (Get-Command cl -ErrorAction SilentlyContinue)) { throw "cl.exe not available after importing vcvars." }

# --- Target definitions -----------------------------------------------------
# Each target: name, source files, extra include dirs (test/host is always added).
$targets = @(
    @{ name = "test_maze";
       srcs = @("$here\test_maze.cpp", "$main\maze\maze.cpp");
       incs = @($main) },

    @{ name = "test_envio_dados";
       srcs = @("$here\test_envio_dados.cpp", "$main\envio_dados\envio_dados.cpp",
                "$mocks\esp_http_client_mock.cpp", "$cjsonC");
       incs = @($mocks, $main, "$main\maze", "$main\envio_dados", $cjsonDir) },

    @{ name = "test_battery";
       srcs = @("$here\test_battery.cpp", "$main\battery\battery.cpp",
                "$mocks\i2c_manager_mock.cpp", "$mocks\ina226_mock.cpp", "$mocks\esp_timer_mock.cpp");
       incs = @($mocks, $main) },

    @{ name = "test_motor";
       srcs = @("$here\test_motor.cpp", "$main\motor\motor.cpp", "$mocks\driver_mock.cpp");
       incs = @($mocks, $main) },

    @{ name = "test_telemetria";
       srcs = @("$here\test_telemetria.cpp", "$main\telemetria\telemetria.cpp", "$main\maze\maze.cpp",
                "$mocks\mock_envio_dados.cpp", "$mocks\wifi_mock.cpp", "$mocks\esp_timer_mock.cpp");
       incs = @($mocks, $main, "$main\maze", "$main\telemetria", "$main\envio_dados") },

    # System (end-to-end) target: real maze + battery + telemetria + envio_dados
    # wired together; only the hardware/network edges are mocked (Wi-Fi, HTTP,
    # clock, INA226/I2C). Exercises a full mission through the JSON on the wire.
    @{ name = "test_system";
       srcs = @("$here\test_system.cpp", "$main\maze\maze.cpp", "$main\telemetria\telemetria.cpp",
                "$main\envio_dados\envio_dados.cpp", "$main\battery\battery.cpp",
                "$mocks\esp_http_client_mock.cpp", "$mocks\wifi_mock.cpp", "$mocks\esp_timer_mock.cpp",
                "$mocks\ina226_mock.cpp", "$mocks\i2c_manager_mock.cpp", "$cjsonC");
       incs = @($mocks, $main, "$main\maze", "$main\envio_dados", "$main\telemetria", "$main\battery", $cjsonDir) }
)

# --- Build + run loop -------------------------------------------------------
$built = 0; $passed = 0; $failed = @()
foreach ($t in $targets) {
    if ($Filter -and ($t.name -notlike "*$Filter*")) { continue }
    $built++
    $name = $t.name
    $objdir = Join-Path $build $name
    New-Item -ItemType Directory -Force -Path $objdir | Out-Null
    $exe = Join-Path $build "$name.exe"

    Write-Host "============================================================"
    Write-Host " BUILD  $name" -ForegroundColor Cyan

    $incArgs = @("/I", $here)
    foreach ($i in $t.incs) { $incArgs += @("/I", $i) }

    $clArgs = @("/nologo", "/std:c++17", "/EHsc", "/W3", "/D_CRT_SECURE_NO_WARNINGS") +
              $incArgs + @("/Fo$objdir\", "/Fe$exe") + $t.srcs

    & cl @clArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host " COMPILE FAILED: $name" -ForegroundColor Red
        $failed += $name
        continue
    }

    Write-Host " RUN    $name" -ForegroundColor Cyan
    & $exe
    if ($LASTEXITCODE -eq 0) { $passed++ } else { $failed += $name }
}

# --- Summary ----------------------------------------------------------------
Write-Host "============================================================"
Write-Host " SUITE SUMMARY: $passed/$built target(s) passed" -ForegroundColor Yellow
if ($failed.Count -gt 0) {
    Write-Host " FAILED: $($failed -join ', ')" -ForegroundColor Red
    exit 1
}
Write-Host " ALL GREEN" -ForegroundColor Green
exit 0
