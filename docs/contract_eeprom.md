# Persistent-state contract

Genre: contract
Canonical for: shipped USER-slot address compatibility and per-slot validity,
SAVE-not-SET persistence, the EEPROM reset key and its factory-reset blast radius, and
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
> hold that layout, and releases keep the reset key unless the stored format
> changes, so those devices would not be factory-reset.
> **REOPENS:** an `ERA_EEPROM_RESET_KEY` bump that accepts a full EEPROM wipe,
> with the new field appended rather than inserted.

Validity is per slot, and that split is intentional. TAPPING and TAPDANCE treat
a bad signature, version, or out-of-range field as a ruined slot and restore
defaults. MOUSE checks signature and version only; out-of-range fields are
clamped one by one — the knobs in `mousekey_config_storage_t`
(`src/ap/modules/qmk/port/mousekey_config.c`) are independent, so one bad field
is not a reason to drop the rest. The MOUSE slot stores that whole struct even
with version 2 and unchanged slot size/offset. V261004R1 changes both the
meaning of stored MOUSE bytes and the global reset key; the first boot resets
all settings. No v1 migration is performed (`docs/contract_via.md` §7).

A failed validity check restores defaults in RAM and flushes immediately from
init. A MOUSE signature that is valid but has out-of-range fields only flags
dirty; those clamps persist on the next SAVE.

**Flush point.** `id_custom_set_value` updates runtime and the RAM image.
`id_custom_save` (VIA SAVE) is the flush. A reboot without SAVE rolls back to
the last stored value — VIA's contract, not a defect. BootMode is the
exception: `id_custom_save` on that channel is a no-op; persist is Apply
(`docs/contract_usb.md` §5).

## 2. Only the reset key wipes storage, and only for a format change

`ERA_EEPROM_RESET_KEY` (`src/hw/hw_def.h`) is the storage identity, with
EERRAA's name and meaning; `_DEF_FIRMWARE_VERSION` is identity only, so a
release that keeps the stored format keeps every device's keymap, macros, and
VIA settings. The boot reset guard `eepromResetGuardCheck()`
(`src/hw/driver/eeprom_reset_guard.c`) has no off switch: a matching guard
magic and key preserve storage, anything else runs the full format/default
path, and the guard is written after the defaults, so a reset cut short by
power loss runs again.

Raise the key when a stored byte would mean something else in the new build: a
moved or resized USER slot; a changed persisted type, signature, or slot
version; a changed QMK eeconfig, VIA, or dynamic-keymap address or geometry; or
a renumbered RGB mode. The key is global, so raising it wipes every board on
first boot. A release that changes no stored format — JSON, code, or a default
— keeps the key, and an already-stored value then stays over the new default.

`QMK_BUILDDATE` (`src/ap/modules/qmk/port/version.h`) seeds VIA's own magic. It
is pinned, equal to EERRAA's, and left alone: changing it resets keymaps and
macros whatever the key says.

The `storage` check in `tools/era_doc_refs.py`, also run by the pre-commit
hook, fingerprints those sources against `tools/eeprom_reset_key.json` and
fails until the key is raised or the record is rewritten with the reason the
stored bytes stay valid; the record's diff is the review evidence of that
decision.

> **REFUSED:** deriving the reset key from `_DEF_FIRMWARE_VERSION` or any
> other per-release value, or making the boot reset guard optional per board.
> **WHY:** every update then wipes keymaps and macros whether or not the format
> changed, and users cannot tell a routine update from a breaking one. A board
> without the guard would silently ignore both a raised key and EEPROM CLEAN.
> **REOPENS:** a per-board key or a narrower guard, if one board's format change
> must stop wiping the others. Neither exists.

**Failure stops boot.** EEPROM initialization/reset failure is fail-closed:
`hwInit()` must return false and `src/main.c` must not continue with a
half-initialized store. Retry count and LED indication are implementation detail
owned by `src/hw/hw.c` and `src/main.c`.

The in-product EEPROM reset is the system-channel confirm sequence in
`src/ap/modules/qmk/port/sys_port.c`. Official JSON exposes three toggles
(channel 9, values 2 / 3 / 4). GET returns each bit. SET 1 sets that bit,
SET 0 clears it. These RAM confirmation bits have no time limit and clear on
reboot. GET and SAVE do not trigger CLEAN. Setting all three bits consumes the
confirmations and calls
`eeprom_req_clean()`, which shares the reset-guard invalidation owner and
reboots only after the writer verifies the cleared guard, so the next boot runs
the same `eepromResetGuardCheck()` path. A known persistence or reset-scheduling
failure returns the existing VIA unhandled response. Once recorded, CLEAN intent
survives such failures; normal service retries without a blocking durability wait.

Tap Dance retains the 88-byte version-1 record, slot addresses, signature and
all action/term offsets. Reserved byte 1 tags the input-mode extension with
`0xD2`; bytes 0 and 2 hold two bits per TD0..TD3 and TD4..TD7 respectively,
lowest slot first. Values 0/1/2 mean legacy, after-decision and on-press.
All-zero reserved bytes remain valid mode 0 of the same format. An unknown
tag, nonzero untagged bits, mode 3, or on-press with a non-transparent first
hold is invalid. No old-layout converter or reset-key change is needed.
Defaults clear the reserved bytes. SAVE persists modes with the existing
actions; SET alone does not promise durability. Older firmware ignores these
mode bits and therefore cannot reproduce the new explicit-silence semantics.

Tap Dance advanced timing appends a separate 24-byte record at USER offset 172
(`EECONFIG_USER_TAPDANCE_TIMING`). Existing USER owners, the 88-byte Tap Dance
record and keymap/macro addresses do not move, so the reset key stays unchanged.
The packed record contains eight uint16 LE hold times, one bit-mask byte for
hold-on-other-key, version 1, a zero uint16 reserved field and signature
`0x4D544454`. Invalid/uninitialized records default independently to all-zero
hold times and flags. A zero hold time follows the slot term. Defaults and
SAVE cover both records, through the existing EEPROM persistence owner.

Factory defaults have one checked owner chain: reset guard, QMK defaults, VIA
defaults and keyboard/user defaults. Keyboard/user initialization is not repeated
by the outer factory function. The old guard is durably invalidated first; VIA
validity, QMK validity and the reset guard are published only after their preceding
payload barriers succeed. Matching reset guard with corrupt QMK validity still
uses this checked path; qmkInit/apInit must not continue after failure. Per-slot
validity rules in §1 remain independent. EEPROM CLEAN uses the same next-boot path.
This changes no byte addresses, reset key, VIA build date or firmware version.

## 3. One RAM image, asynchronous persistence, explicit durability

The runtime writer is `src/ap/modules/qmk/port/platforms/eeprom.c`. One RAM image
holds the latest desired EEPROM state; dirty state marks bytes not yet durably
acknowledged. Repeated writes to the same address coalesce there instead of
forming a finite write-event queue, and QMK-image writes do not bypass that owner.

Normal service performs bounded work per call and returns before the physical
write cycle completes. Current page/scan sizes and retry timing are source-owned.
The external EEPROM path is asynchronous; completion means the backend has
confirmed write-cycle readiness and read back the submitted page unchanged.
A bus ACK alone is not a durability receipt.

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
partly filled image. Reinitialization returns failure instead of discarding
retained dirty intent. Invalid accesses fail within the image bounds. Maintenance
writes inside the QMK image share the same writer so a later snapshot cannot undo
an out-of-band byte write; maintenance code may use the durability barrier.

A generic completion-byte fence retires an old validity marker before payload
mutation and schedules the final marker only after all preceding dirty pages
have verified receipts. Reads keep the marker invalid until final readback.
Failed invalidation rejects the payload and latches CLOSE rejection until a fresh
opener/reset; it must never silently drop a chunk and later publish success.
The macro caller uses this fence for upload and RESET. A failed command uses the
existing VIA unhandled response. A generic image observer reports changes to
GET-visible bytes; the dynamic-keymap owner classifies KEYMAP and MACRO ranges.
This runtime invalidation is separate from physical completion. The final macro
marker's hidden staging is not visible mutation: fence release after verified
readback reports its visible transition once. Core/custom CONFIG runtime owners
publish their own semantic changes. `docs/contract_via.md` §4 owns token meaning.

The shipped external I2C backend is the asynchronous path. Internal flash
emulation (`src/hw/driver/eeprom/emul.c`) remains an unselected synchronous fallback, with readback required by the image
writer, and is hardware-unverified (`docs/state_open.md`). Backend page completion is not a
multi-page transaction: this layout provides no journaling, power-loss-safe
transaction boundary, or persistence of in-flight RAM updates across power loss.

The read-only USB polling setting does not write EEPROM (`docs/contract_usb.md` §4).
RGB SLEEP writes only its four-byte slot on VIA SAVE / CLEAN / invalid-slot
init (`src/ap/modules/qmk/port/rgb_sleep.c`). The slot is signature, a version
byte, and uint16 seconds. The version byte's high bit is an inverted
RGB-Sleep-disable flag; the low seven bits remain storage version 1. Therefore
old version-1 records load as RGB Sleep enabled. A stored timeout 0 is treated as
600 (10 min). Disabling RGB Sleep preserves the timeout and does not resize or
move the slot; the flag gates idle, USB Suspend, and host-loss RGB sleep alike.
Sleep/wake itself is RAM and `*_noeeprom`; it does not add flash wear.
