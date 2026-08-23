$ErrorActionPreference = "Stop"
$Here = $PSScriptRoot
$Root = (Resolve-Path (Join-Path $Here "..\..")).Path
$Gcc = "D:\baram-fw-tools_exe\arm_toolchain\mingw_gcc\bin\gcc.exe"
$Out = Join-Path $Here "test_era_via_exact_ms.exe"
$Inc = Join-Path $Here "include"
$Qmk = Join-Path $Root "src\ap\modules\qmk"
$Sandbox = Join-Path $Here "sandbox"

if (Test-Path $Sandbox) {
    Remove-Item -Recurse -Force $Sandbox
}
New-Item -ItemType Directory -Path (Join-Path $Sandbox "process_keycode") | Out-Null
New-Item -ItemType Directory -Path (Join-Path $Sandbox "platforms") | Out-Null

Copy-Item (Join-Path $Qmk "port\tapping_term.c") $Sandbox
Copy-Item (Join-Path $Qmk "port\tapping_term.h") $Sandbox
Copy-Item (Join-Path $Qmk "port\tapdance.c") $Sandbox
Copy-Item (Join-Path $Qmk "port\tapdance.h") $Sandbox
Copy-Item (Join-Path $Qmk "port\era_state_sync.c") $Sandbox
Copy-Item (Join-Path $Qmk "port\era_state_sync.h") $Sandbox
Copy-Item (Join-Path $Qmk "port\mousekey_config.c") $Sandbox
Copy-Item (Join-Path $Qmk "port\mousekey_config.h") $Sandbox
Copy-Item (Join-Path $Inc "port.h") $Sandbox
Copy-Item (Join-Path $Inc "quantum.h") $Sandbox
Copy-Item (Join-Path $Inc "wait.h") $Sandbox
Copy-Item (Join-Path $Inc "timer.h") $Sandbox
Copy-Item (Join-Path $Inc "platforms\eeprom.h") (Join-Path $Sandbox "platforms\eeprom.h")
Copy-Item (Join-Path $Inc "process_keycode\process_tap_dance.h") (Join-Path $Sandbox "process_keycode\process_tap_dance.h")

$argsList = @(
    "-std=gnu11",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wno-unused-parameter",
    "-Wno-unused-function",
    "-include", (Join-Path $Inc "force_host.h"),
    "-DEECONFIG_USER_DATA_SIZE=512",
    "-DMOUSEKEY_ENABLE",
    "-DERA_MOUSEKEY_RUNTIME_DELTA",
    "-I$Sandbox",
    "-I$Inc",
    "-I$Qmk\quantum",
    "-I$Qmk\quantum\keymap_extras",
    "-I$Qmk\quantum\process_keycode",
    (Join-Path $Here "test_era_via_exact_ms.c"),
    (Join-Path $Here "host_stubs.c"),
    (Join-Path $Sandbox "era_state_sync.c"),
    (Join-Path $Sandbox "tapping_term.c"),
    (Join-Path $Sandbox "tapdance.c"),
    (Join-Path $Sandbox "mousekey_config.c"),
    "-o", $Out
)

& $Gcc @argsList
if ($LASTEXITCODE -ne 0) {
    throw "host test compile failed"
}
& $Out
if ($LASTEXITCODE -ne 0) {
    throw "host tests failed"
}

python (Join-Path $Here "check_single_producer.py")
if ($LASTEXITCODE -ne 0) {
    throw "single-producer check failed"
}
