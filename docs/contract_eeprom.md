# Persistent-state contract

Genre: contract
Canonical for: what EEPROM keeps and when it is written — USER slot address
immutability and per-slot validity, SAVE-not-SET flush for custom VIA values,
the version-cookie factory-reset blast radius, and the 100 µs write slice
against the 8 kHz budget

Layout (offsets, sizes, symbols) is generated in `docs/MAP.md` §5 from
`src/ap/modules/qmk/port/port.h`. This file is why that layout must stay that
shape.

## 1. Slot addresses do not move after they have shipped

Retiring a feature does not delete its slot. `EECONFIG_USER_RESERVED_32` is the
retired monitor toggle; nothing reads or writes it. New slots append after the
last occupied offset. The USER block size is `EECONFIG_USER_DATA_SIZE`
(`docs/MAP.md` §5; every `<board>/config.h`).

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
`src/ap/modules/qmk/port/sys_port.c`. It calls `eeprom_req_clean()`, which
uses `eepromScheduleDeferredFactoryReset()` to clear the sentinel and reboot,
so the next boot runs the same `eepromAutoFactoryResetCheck()` path.

## 3. The write path stays inside the 8 kHz budget

VIA/QMK writes enqueue in RAM. Each `eeprom_update()` call in
`src/ap/modules/qmk/port/platforms/eeprom.c` spends at most
`EEPROM_WRITE_SLICE_MAX_US` (100) microseconds in the lower driver. If the
queue is at or above `EEPROM_WRITE_BURST_THRESHOLD` (512 entries),
`src/ap/modules/qmk/qmk.c` makes `EEPROM_WRITE_BURST_EXTRA_CALLS` (2) extra
calls of the same function — extra slices, not a longer slice.

> **REFUSED:** synchronous EEPROM writes on the QMK path, or raising
> `EEPROM_WRITE_SLICE_MAX_US` without a new 8 kHz budget.
> **WHY:** `eeprom_update()` runs in the same loop as USB polling; a longer
> slice delays HID IN.
> **REOPENS:** a measured budget that still meets the 8 kHz SOF deadline, or a
> path that never runs in that loop.

A full queue retries for `EEPROM_WRITE_QUEUE_WAIT_MS` (2) milliseconds, then
falls back to a direct write and logs. The byte is not dropped silently.

The driver in use is external I2C EEPROM (`src/hw/driver/eeprom/zd24c128.c`,
`EEPROM_PAGE_SIZE` 32). Internal flash emulation
(`src/hw/driver/eeprom/emul.c`) implements the same API and is not
hardware-verified (`docs/state_open.md`).

USB diagnostics do not write EEPROM (`docs/contract_usb.md` §4).
RGB SLEEP writes only the timeout slot on VIA SAVE / CLEAN / invalid-slot
init (`src/ap/modules/qmk/port/rgb_sleep.c`). The 4 B slot is signature,
version, and uint16 seconds. A stored 0 is treated as 600 (10 min).
Sleep/wake itself is RAM and `*_noeeprom`; it does not add flash wear.
