# Persistent-state contract

Genre: contract
Canonical for: what EEPROM keeps and when it is written — USER slot address
immutability and per-slot validity, SAVE-not-SET flush for custom VIA values,
the version-cookie factory-reset blast radius, and the 100 µs write slice
against the 8 kHz budget

The current layout (offsets, sizes, symbols) is source-owned by
`src/ap/modules/qmk/port/port.h`. This file owns why shipped slot addresses
must retain their shape.

## 1. Slot addresses do not move after they have shipped

Retiring a feature does not delete its slot. `EECONFIG_USER_RESERVED_32` is the
retired monitor toggle; nothing reads or writes it. New slots append after the
last occupied offset. The USER block size is `EECONFIG_USER_DATA_SIZE`
(every `<board>/config.h`); current slot declarations are in
`src/ap/modules/qmk/port/port.h`.

> **REFUSED:** moving USER slot offsets after a layout has shipped.
> **WHY:** compacting a hole shifts every later field on devices that already
> hold that layout, and cookie-stable releases exist, so those devices would
> not be factory-reset.
> **REOPENS:** a cookie bump that accepts a full EEPROM blast, with the new
> field appended rather than inserted.

Validity is per slot, and that split is intentional. TAPPING and TAPDANCE treat
a bad signature, version, or out-of-range field as a ruined slot and restore
defaults. MOUSE checks signature and version only; out-of-range fields are
clamped one by one — the knobs in `mousekey_config_storage_t`
(`src/ap/modules/qmk/port/mousekey_config.c`) are independent, so one bad field
is not a reason to drop the rest. The MOUSE slot stores that whole struct even
when the VIA page exposes a subset (`docs/contract_via.md`); opening the rest
later is a definition change, not a migration.

A failed validity check restores defaults in RAM and flushes immediately from
init. A MOUSE signature that is valid but has out-of-range fields only flags
dirty; those clamps persist on the next SAVE.

**Flush point.** `id_custom_set_value` updates runtime and the RAM image.
`id_custom_save` (VIA SAVE) is the flush. A reboot without SAVE rolls back to
the last stored value — VIA's contract, not a defect. BootMode is the
exception: `id_custom_save` on that channel is a no-op; persist is Apply
(`docs/contract_usb.md` §5).

## 2. Raising the version cookie factory-resets every device

`AUTO_FACTORY_RESET_COOKIE` defaults from `_DEF_FIRMWARE_VERSION` in
`src/hw/hw_def.h`. No board overrides it. On boot,
`eepromAutoFactoryResetCheck()` in `src/hw/driver/eeprom_auto_factory_reset.c`
reads `EECONFIG_USER_EEPROM_CLEAR_FLAG` (`AUTO_FACTORY_RESET_FLAG_MAGIC`) and
`EECONFIG_USER_EEPROM_CLEAR_COOKIE`. Flag magic plus a matching cookie skips
the reset. Any other pairing formats the chip (`eepromFormat()`), then
`eeprom_apply_factory_defaults()` rewrites QMK defaults, including dynamic
keymap, macros, and VIA settings (`eeconfig_init_via()`).

A version-string bump is therefore a decision to wipe every
`AUTO_FACTORY_RESET_ENABLE` board on first boot. `src/hw/hw_def.h` defaults
that flag to 0; every `<board>/config.h` sets it to 1.

Factory-default macros such as `RGBLIGHT_DEFAULT_ON` are consumed when
`eeconfig_update_rgblight_default()` runs — virgin EEPROM and post-reset
storage with mode 0 — not on every boot of a device that already stored a
mode. Changing the constant without a cookie bump leaves stored values in
place. The cookie is global, not per board, so each default-changing release
has to decide whether every shipped keyboard eats the reset.

A JSON-only release (channel map, value ids, EEPROM layout, and firmware code
unchanged) must not bump the cookie.

> **REFUSED:** raising `_DEF_FIRMWARE_VERSION` without accepting a full EEPROM
> factory reset on every `AUTO_FACTORY_RESET_ENABLE` board.
> **WHY:** cookie mismatch formats the whole chip — keymap, macros, and VIA
> settings included.
> **REOPENS:** a per-board cookie or a narrower sentinel. Neither exists.

**Failure stops boot.** `src/hw/hw.c` retries three times and blinks
`_DEF_LED1` three times after each failure. Three failures make `hwInit()`
return false and `src/main.c` sit in an LED-toggle loop. Half-initialized
EEPROM must not boot quietly.

The in-product EEPROM reset is the system-channel confirm sequence in
`src/ap/modules/qmk/port/sys_port.c`. Official JSON exposes three toggles
(channel 9, values 2 / 3 / 4). GET returns each bit. SET 1 sets that bit,
SET 0 clears it. The window is `SYS_EEP_RESET_CONFIRM_WINDOW_MS` (10000)
from the first SET 1 and is not extended by later confirms; `via_qmk_system_task()`
and the next GET/SET expire leftover bits so a later third toggle cannot
finish a stale sequence. All three bits inside the window call
`eeprom_req_clean()`, which uses `eepromScheduleDeferredFactoryReset()` to
clear the sentinel and reboot, so the next boot runs the same
`eepromAutoFactoryResetCheck()` path.

## 3. One RAM image, asynchronous persistence, explicit durability

The runtime writer is `src/ap/modules/qmk/port/platforms/eeprom.c`. Its 4096-byte
RAM image holds the latest desired values. A per-byte dirty bitmap records
unacknowledged changes without a finite byte-event queue or direct-write fallback.
Repeated writes to the same address coalesce in the image. Legacy pending/max
getters now count distinct dirty bytes; the compatibility overflow getter is zero.

`eeprom_update()` either services one active page or examines at most eight page
entries and starts one 32-byte snapshot. It never waits for the I2C wire transfer,
the EEPROM write cycle or a retry deadline. The external ZD24C128 backend uses
`src/hw/driver/i2c_async.c` for interrupt-driven memory write and address-only ACK
probes. A NACK schedules a later probe; it does not block the keyboard loop.
Completion is published only after write-cycle ACK, not merely after the last
I2C data byte. The active hardware buffer is immutable until terminal completion.

A completed page clears a dirty bit only if its snapshot still equals the latest
RAM byte. An update made during that transfer therefore remains pending. Failed
start, NACK timeout or lost interrupt retains desired data for a later retry.
A stuck transfer is quiesced before buffer ownership is returned. Channel bounds
and ownership are checked before the synchronous I2C APIs touch the bus, and
readiness checks never enable a caller's masked interrupts.

There is no claimed 100-microsecond wall-clock upper bound and no repeated burst
slice in `qmkUpdate()`. The algorithm has bounded work per call; actual execution
time, I2C interrupt load and scan/HID tail latency require measurement. Whole-page
snapshots may send more wire bytes for an isolated one-byte change, while repeated
updates collapse into fewer writes and do not stall the input loop.

`eeprom_flush_pending()` is an explicit durability barrier for initialization,
factory reset and maintenance. It requires thread context with interrupts enabled.
A no-progress timeout returns false while preserving dirty data and any active
transaction. Normal VIA SAVE schedules persistence; its response is not a power-loss
commit record. User-requested USB reset waits for persistence and response completion
without spinning in the normal loop. If storage or the host cannot complete them,
the reset stays pending and keyboard processing continues.

Initial image-read failure stops hardware initialization instead of booting from a
partly filled image. Invalid accesses fail within the image bounds. CLI writes
inside the QMK image use the same writer, preventing a later page snapshot from
undoing an out-of-band byte write. Such maintenance commands may explicitly flush.

The driver in use is external I2C EEPROM (`src/hw/driver/eeprom/zd24c128.c`,
`EEPROM_PAGE_SIZE` 32). Internal flash emulation (`src/hw/driver/eeprom/emul.c`)
retains a synchronous fallback and is not hardware-verified (`docs/state_open.md`).
Page ACK is not atomicity across multiple pages. Power-loss-safe transactions,
journaling and persistence of in-flight RAM updates are not provided by this layout.

USB diagnostics do not write EEPROM (`docs/contract_usb.md` §4).
RGB SLEEP writes only its four-byte slot on VIA SAVE / CLEAN / invalid-slot
init (`src/ap/modules/qmk/port/rgb_sleep.c`). The slot is signature, a version
byte, and uint16 seconds. The version byte's high bit is an inverted
RGB-Sleep-disable flag; the low seven bits remain storage version 1. Therefore
old version-1 records load as RGB Sleep enabled. A stored timeout 0 is treated as
600 (10 min). Disabling RGB Sleep preserves the timeout and does not resize or
move the slot; the flag gates idle, USB Suspend, and host-loss RGB sleep alike.
Sleep/wake itself is RAM and `*_noeeprom`; it does not add flash wear.
