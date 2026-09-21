# Persistent-state contract

Genre: contract
Canonical for: shipped USER-slot address compatibility and per-slot validity,
SAVE-not-SET persistence, the version-cookie factory-reset blast radius, and
RAM-image/asynchronous durability and partial-failure semantics

The current USER layout (offsets, sizes, symbols) is source-owned by
`src/ap/modules/qmk/port/port.h`; each board `config.h` owns its USER block
size. This file owns why shipped addresses retain their shape, when writes
become durable, and what reset or partial failure is allowed to discard.

## 1. Slot addresses do not move after they have shipped

Retiring a feature does not delete its shipped slot. `EECONFIG_USER_RESERVED_32`
is the retired monitor toggle; nothing reads or writes it. New persistent slots
append after the last occupied slot. Current offsets and sizes stay in
`src/ap/modules/qmk/port/port.h` rather than being copied here.

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
`src/hw/hw_def.h`. The boot entry point is `eepromAutoFactoryResetCheck()` in
`src/hw/driver/eeprom_auto_factory_reset.c`: matching sentinel magic and cookie
preserve storage; any other pairing runs the full format/default path, including
dynamic keymap, macros, and VIA settings.

A version-string bump is therefore a decision to wipe every board that enables
`AUTO_FACTORY_RESET_ENABLE` on first boot. Which boards enable it is owned by
their current `config.h`, not by this contract. Stored defaults are not rewritten
on every ordinary boot, so changing a default without a cookie bump leaves an
already-stored value in place. The cookie is global rather than per board; each
default-changing release must decide whether that global reset blast is intended.

A JSON-only release (channel map, value ids, EEPROM layout, and firmware code
unchanged) must not bump the cookie.

> **REFUSED:** raising `_DEF_FIRMWARE_VERSION` without accepting a full EEPROM
> factory reset on every `AUTO_FACTORY_RESET_ENABLE` board.
> **WHY:** cookie mismatch formats the whole chip — keymap, macros, and VIA
> settings included.
> **REOPENS:** a per-board cookie or a narrower sentinel. Neither exists.

**Failure stops boot.** EEPROM initialization/reset failure is fail-closed:
`hwInit()` must return false and `src/main.c` must not continue with a
half-initialized store. Retry count and LED indication are implementation detail
owned by `src/hw/hw.c` and `src/main.c`.

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

The runtime writer is `src/ap/modules/qmk/port/platforms/eeprom.c`. One RAM image
holds the latest desired EEPROM state; dirty state marks bytes not yet durably
acknowledged. Repeated writes to the same address coalesce there instead of
forming a finite write-event queue, and QMK-image writes do not bypass that owner.

Normal service performs bounded work per call and returns before the physical
write cycle completes. Current page/scan sizes and retry timing are source-owned.
The external EEPROM path is asynchronous; completion means the backend has
confirmed write-cycle readiness, not merely that the last I2C data byte was sent.

Completion clears pending state only for bytes whose submitted snapshot still
matches the latest RAM value. A newer write during the transfer therefore remains
pending. Failed starts, NACK/timeout paths and lost interrupts retain desired data
for retry, and a stuck transfer must be quiesced before its buffer is reused.
Actual wall-clock cost, interrupt load and scan/HID tail latency require
measurement; this contract claims no fixed microsecond upper bound.

`eeprom_flush_pending()` is the explicit durability barrier for initialization,
factory reset and maintenance. It requires thread context with interrupts enabled.
Failure to make progress returns false without discarding dirty state or ownership
of an active transaction. Normal VIA SAVE schedules persistence; its response is
not a power-loss commit record. A user-requested USB reset waits for persistence
and response completion; if either cannot complete, the reset remains pending
while keyboard processing continues.

Initial image-read failure stops hardware initialization rather than exposing a
partly filled image. Invalid accesses fail within the image bounds. Maintenance
writes inside the QMK image share the same writer so a later snapshot cannot undo
an out-of-band byte write; maintenance code may use the durability barrier.

The shipped external I2C backend is the asynchronous path. Internal flash
emulation (`src/hw/driver/eeprom/emul.c`) remains a synchronous fallback and is
hardware-unverified (`docs/state_open.md`). Backend page completion is not a
multi-page transaction: this layout provides no journaling, power-loss-safe
transaction boundary, or persistence of in-flight RAM updates across power loss.

USB diagnostics do not write EEPROM (`docs/contract_usb.md` §4).
RGB SLEEP writes only its four-byte slot on VIA SAVE / CLEAN / invalid-slot
init (`src/ap/modules/qmk/port/rgb_sleep.c`). The slot is signature, a version
byte, and uint16 seconds. The version byte's high bit is an inverted
RGB-Sleep-disable flag; the low seven bits remain storage version 1. Therefore
old version-1 records load as RGB Sleep enabled. A stored timeout 0 is treated as
600 (10 min). Disabling RGB Sleep preserves the timeout and does not resize or
move the slot; the flag gates idle, USB Suspend, and host-loss RGB sleep alike.
Sleep/wake itself is RAM and `*_noeeprom`; it does not add flash wear.
