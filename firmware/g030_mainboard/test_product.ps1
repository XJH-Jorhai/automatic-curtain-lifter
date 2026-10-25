param(
    [string]$HostGcc = 'gcc'
)
$ErrorActionPreference = 'Stop'
$WorkspaceDir = (Resolve-Path "$PSScriptRoot\..\..").Path
$TestDir = Join-Path $WorkspaceDir 'work\g030-mainboard-tests'
New-Item -ItemType Directory -Path $TestDir -Force | Out-Null
$HostGcc = (Get-Command $HostGcc -ErrorAction Stop).Source
$env:PATH = "$(Split-Path $HostGcc);$env:PATH"
& $HostGcc -std=c11 -Wall -Wextra -Werror -O2 -DLIFT_CONFIRMED_DIRECTION=0 "-I$PSScriptRoot\App\product" `
    "$PSScriptRoot\App\product\motion.c" "$PSScriptRoot\tests\product_motion_test.c" `
    -o "$TestDir\product_motion_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Host motion test build failed' }
& "$TestDir\product_motion_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Host motion tests failed' }
$SdkDir = Join-Path $WorkspaceDir 'work\g030-mainboard-build\Product\tuya_sdk'
if (!(Test-Path -LiteralPath "$SdkDir\wifi.h")) { throw 'Build Product first to prepare the Tuya SDK' }
& $HostGcc -std=c11 -Wall -Wextra -Werror -O2 -DPRODUCT_FIRMWARE=1 -DLIFT_CONFIRMED_DIRECTION=0 `
    "-I$PSScriptRoot\tests\fakes" "-I$PSScriptRoot\App\product" `
    "-I$PSScriptRoot\App\tuya_protocol" "-I$PSScriptRoot\App" "-I$SdkDir" `
    "$PSScriptRoot\App\product\motion.c" "$PSScriptRoot\App\product\product.c" `
    "$PSScriptRoot\App\product\product_motor.c" "$PSScriptRoot\App\product\product_button.c" "$PSScriptRoot\App\product\bridge.c" `
    "$PSScriptRoot\App\music.c" "$PSScriptRoot\App\motor_tune.c" `
    "$SdkDir\mcu_api.c" "$SdkDir\system.c" "$SdkDir\protocol.c" `
    "$PSScriptRoot\tests\product_runtime_test.c" -o "$TestDir\product_runtime_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Host runtime/SDK test build failed' }
& "$TestDir\product_runtime_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Host runtime/SDK tests failed' }
foreach ($StartupCase in @('startup-held', 'startup-blank', 'startup-read-error', 'direction-write-error', 'release-in-adc', 'torque-plateau')) {
    & "$TestDir\product_runtime_test.exe" $StartupCase
    if ($LASTEXITCODE -ne 0) { throw "Startup music case failed: $StartupCase" }
}
& $HostGcc -std=c11 -Wall -Wextra -Werror -O2 `
    "-I$PSScriptRoot\tests\fakes" "-I$PSScriptRoot\App" `
    "$PSScriptRoot\App\eeprom.c" "$PSScriptRoot\tests\product_eeprom_test.c" `
    -o "$TestDir\product_eeprom_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'EEPROM policy test build failed' }
foreach ($EepromCase in @('lift', 'generic', 'torn')) {
    & "$TestDir\product_eeprom_test.exe" $EepromCase
    if ($LASTEXITCODE -ne 0) { throw "EEPROM policy case failed: $EepromCase" }
}

& $HostGcc -std=c11 -Wall -Wextra -Werror -O2 -DPRODUCT_FIRMWARE=1 -DLIFT_TRQ_CAPTURE=1 -DLIFT_CAPTURE_UP_REVERSE=0 `
    "-I$PSScriptRoot\tests\fakes" "-I$PSScriptRoot\App\product" `
    "-I$PSScriptRoot\App\tuya_protocol" "-I$PSScriptRoot\App" "-I$SdkDir" `
    "$PSScriptRoot\App\product\motion.c" "$PSScriptRoot\App\product\product.c" `
    "$PSScriptRoot\App\product\product_motor.c" "$PSScriptRoot\App\product\product_button.c" "$PSScriptRoot\App\product\bridge.c" `
    "$PSScriptRoot\App\music.c" "$PSScriptRoot\App\motor_tune.c" `
    "$SdkDir\mcu_api.c" "$SdkDir\system.c" "$SdkDir\protocol.c" `
    "$PSScriptRoot\tests\product_runtime_test.c" -o "$TestDir\trq_capture_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Constant-current capture test build failed' }
& "$TestDir\trq_capture_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Constant-current capture tests failed' }

& $HostGcc -std=c11 -Wall -Wextra -Werror -O2 `
    "-I$PSScriptRoot\App\product" "$PSScriptRoot\App\product\motion.c" `
    "$PSScriptRoot\tests\trq_capture_motion_test.c" -o "$TestDir\trq_capture_motion_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Capture waveform test build failed' }
& "$TestDir\trq_capture_motion_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Capture waveform tests failed' }

& $HostGcc -std=c11 -Wall -Wextra -Werror -O2 -DPRODUCT_FIRMWARE=1 `
    "-I$PSScriptRoot\tests\fakes" "-I$PSScriptRoot\App\product" `
    "-I$PSScriptRoot\App\tuya_protocol" "-I$PSScriptRoot\App" "-I$SdkDir" `
    "$PSScriptRoot\App\product\motion.c" "$PSScriptRoot\App\product\product.c" `
    "$PSScriptRoot\App\product\product_motor.c" "$PSScriptRoot\App\product\product_button.c" "$PSScriptRoot\App\product\bridge.c" `
    "$PSScriptRoot\App\music.c" "$PSScriptRoot\App\motor_tune.c" `
    "$SdkDir\mcu_api.c" "$SdkDir\system.c" "$SdkDir\protocol.c" `
    "$PSScriptRoot\tests\product_runtime_test.c" -o "$TestDir\confirmed_product_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Confirmed Product test build failed' }
foreach ($ConfirmedCase in @('', 'confirmed-write-error', 'manual-calibration', 'latched-toggle', 'continuous-adjustment', 'cloud-product', 'slider-progress', 'cloud-calibration', 'calibration-reset', 'reference-positioning', 'reference-migration')) {
    if ($ConfirmedCase) { & "$TestDir\confirmed_product_test.exe" $ConfirmedCase }
    else { & "$TestDir\confirmed_product_test.exe" }
    if ($LASTEXITCODE -ne 0) { throw "Confirmed Product test failed: $ConfirmedCase" }
}

& $HostGcc -std=c11 -Wall -Wextra -Werror -O2 "-I$PSScriptRoot\App\product" `
    "$PSScriptRoot\App\product\motion.c" "$PSScriptRoot\App\product\product_button.c" `
    "$PSScriptRoot\tests\manual_calibration_test.c" -o "$TestDir\manual_calibration_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Manual calibration test build failed' }
& "$TestDir\manual_calibration_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Manual calibration tests failed' }

