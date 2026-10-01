# VLCBlib_PIC fork — b47 (KeithB)

Base: b46. Six opt-in options, all **off unless the application defines them in
`module.h`**. With none defined, every existing module compiles as it did on b46.
Tracked in `CR_VLCBlib_fork_enhancements_log.md` (LCR numbers below).

| LCR | Define | Files | What it does |
| --- | ------ | ----- | ------------ |
| 001 | `VLCB_DEFAULT_ISR_RESET` | `vlcb.c` | Q83 default ISR executes `RESET()` instead of returning (an unflagged source otherwise re-enters for ever) |
| 001 | `VLCB_DEFAULT_ISR_HOOK` (needs the above) | `vlcb.c`, `vlcb.h` | calls `APP_unhandledInterrupt()` before the reset |
| 002 | `VLCB_APP_CONFIG` | `vlcb.c` | skips the library's Q83 `#pragma config` block; the application supplies every configuration word |
| 003 | `VLCB_POLL_DIVIDER n` (2..255) | `vlcb.c`, `vlcb.h` | `poll()` every nth main-loop pass; `loop()` and the per-pass tick unchanged |
| 004 | `VLCB_EEPROM_ASYNC n` (2..255, Q83 only) | `nvm.c`, `nvm.h`, `vlcb.c` | background EEPROM writer: queue of n, coalescing, read-your-write, verify with 3 attempts, VDD-guard hold (refused after 1 s), full queue drained then written in line (counted), `nvmPoll()` from `poll()`, `flushNVM()`, counters `nvmAsyncWrites/Failures/Refused/Fallbacks/HighWater`, `nvmAsyncPending()` |
| 005 | (active with 004) | `vlcb.c`, `boot.c`, `mns.c` | `flushNVM()` before each of the four library `RESET()` calls |
| 007 | `VLCB_PB_NO_BOOTLOADER_OFFSET` | `vlcb.c` | power-up button bands timed from power-up when no bootloader holds the button first |

Not in b47: **LCR-006** (`VLCB_CAN_APP_HW`, CAN service without the hardware
driver) — its own build, after CanCan's hooks are designed against it.

## Verification done on the host

1. **Undefined = unchanged (rule R2, proxy).** `vlcb.c`, `nvm.c`, `boot.c` and
   `mns.c` preprocessed (`gcc -E -P`) before and after, with each real
   application `module.h` and none of the new defines: **token-identical** for
   CANCMDB (Q83 and K80 family macros) and for CANPAN3 with `HARDWARE` = 1, 2, 3
   on both families. Identical preprocessor output means identical input to XC8.
   The on-target samecode (hex compare in MPLAB) is still to be run: it is the
   acceptance check for the fork.
2. **Async writer behaviour.** `test/harness_nvm_async.c` runs the literal
   writer code sliced from `nvm.c` against a model of the Q83 NVM controller:
   13 scenarios, all pass (background landing, read-your-write, coalescing,
   in-flight head, no-wear skip, verify-retry, abandon after 3, full-queue
   fallback, low-rail hold and 1 s refusal, recovery, flush including in-flight
   and on a low rail, 100 ms stuck-write timeout, ring wrap).

Not yet done: an XC8 build with each option enabled, and bench tests. Neither
compiler nor board is available where b47 was written.

## Notes for applications

- `VLCB_EEPROM_ASYNC`: call `flushNVM()` before any reset the application makes
  itself. A read of an EEPROM cell that is not queued waits for an in-flight write
  (one NVM controller); reads are rare in VLCBlib (NVs are cached with `NV_CACHE`).
  A write stuck with GO set past 100 ms is abandoned as a failed attempt but
  still waits for GO to clear before the next NVM operation, as `EEPROM_Read`
  always has.
- `VLCB_APP_CONFIG`: the application owns the configuration audit; the
  bootloader's `hwsettings.c` must carry the same words.
