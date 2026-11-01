# Capture-point implementation

## Exact target and interface

All literal native addresses below are for **7D2 firmware 1.1.2 only**.
No Canon 70D or REC5X/PUNCH5X integration was performed. The command argument
namespace and the internal source namespace are different.

| Item | Value | Use |
| --- | --- | --- |
| Native setter | Thumb code `0xFE2B196A`, callable pointer `0xFE2B196B` | Call with argument **4** |
| Setter signature | `24 49 0E 28 10 B5 2F D2` | Verify eight bytes before use |
| Getter | Thumb code `0xFE2B1960` | Native getter writes state through caller pointer |
| ICU selector RAM | `0x0002D7D0` | Save original selection; not an MMIO register |
| Original source | `14` / `0x0E`, RSHD | Required original in this bounded trial |
| Corrected source | `18` / `0x12`, DEFOE | Setter argument4 maps here; command help calls it DEFCORRE |
| Observed source mux | `0xD0037014 & 63` | Read/verify; Canon updates through masked writes |
| Pack32 configuration | `0xD0008094 = 0x20` | Read/verify expected packed14-bit mode |
| Native RAW write channel | EDMAC2, base `0xD0004200` | Existing source channel, unchanged |
| DMA flags / destination | `0xD0004204` / `0xD0004208` | Flags and dynamic buffer address |
| DMA rows / geometry | `0xD000420C` / `0xD0004210` | Check decoded rows1068, low16 pitch3472 |
| Existing segmented-copy helper | Thumb pointer `0xFE0FF0E9` | API channel0, max8 descriptors |
| Existing control-copy helper | Thumb pointer `0xFE0FF03F` | Earlier single-copy diagnostic |

The fix calls Canon's setter; it does **not** poke18 into the whole mux register,
change packing registers, or select a different physical RAW write DMA channel.
Copy API channel0 is not evidence that sensor RAW uses EDMAC0.

Native setup/update trace: normal x1 getter consumers `FE3DEA02`/`FE3DEF06`,
parameter offsets `0x90`/`0xC0`; controller state `000308E0`. Setup dispatch
`FE542996`, group `0x20002`, goes through `FE6DCE04`/`FE85C654` to Omar
runtime `80003A7A`. RAW root+`0x8C` passes via `80008EBC` to relocated
`01B38124` (ROM `FE722178`). Update root+`0xBC` passes via `80008F34` to
relocated ROM `FE722418`. Frame apply `FE72232C` updates the mux and starts
Pack32. Native emulation used mocked peripheral endpoints; actual camera
format/pixel observations are a separate evidence category.

## Selection and restoration lifecycle

1. Validate camera/firmware, native signature and mode. Require startup mode6,
   1832x800/14-bit/23.976fps, normal1080/1x, no proxy, spanning or triggers.
2. After existing buffer/bit-depth setup, with state RAW_PREPARING and before
   recording can queue frames, require selector/mux14, native1984x1068, pitch3472,
   size3708096, RGGB metadata `0x02010100` and Pack32 `0x20`.
3. Save the original selector and arm restoration **before** calling the native
   setter. Corrected mode calls argument4. Baseline mode leaves14 selected.
4. Wait at most100x10ms for the expected source and at least3 VSYNC callbacks.
   Failure takes returning cleanup; the timeout cannot interrupt a native hang.
5. Enable recording only after settling. Task-context guards check source,
   packing and geometry before/after copies and in the writing loop. Invalid
   copied slots follow the existing failed-copy invalidation path.
6. At manual or frame-limit stop, stop queuing and use the existing drain/close
   path. Restore the saved original RAM selector atomically under cli/sei.
   Early failures after selection also restore it. A copy-task start followed
   by a later startup failure receives its corresponding stop/status cleanup.
7. After ResumeLiveView, check selector/mux14, expected format and2 VSYNCs within
   1s. Failure latches a fault and requests restart before another recording.
8. Module unload refuses recording/preparing/finishing, active or armed state.

The current test rejects an unexpected original source instead of stealing
another owner's selector. Standalone rwtap was disabled for video tests to
avoid conflicting source selection. There is no comprehensive new Live View
transition callback: mode/geometry changes can trigger a guard stop, and
restoration verification may fail outside the supported mode. Arbitrary
transitions, power loss, CPU faults and all existing wait/flush behavior are
not covered by a hardware watchdog.

## DMA, buffers and cache behavior

The recording path retains its previous buffer allocations, RAW DMA destination
redirection through `raw_lv_redirect_edmac`, VSYNC synchronization and native
segmented-copy backend. Dynamic buffer addresses in logs are observations,
not constants to port. Source switching does not add a new DMA redirect.

The saved1832x800 window starts at144,144 in the native1984x1068 frame. Existing
`fast_7d2_frame` copies3206-byte packed rows from3472-byte rows at byte252
(144*14/8). Each batch has8 descriptors, each12bytes: destination, source,
length. The same native copy wrapper/API channel0 and UNCACHEABLE aliases are
retained. Native semaphore/cache behavior is not reimplemented by the fix.

The standalone snapshot diagnostic instead copies full native frames into a
guarded private allocation using synchronous dma_memcpy. It does not freeze
or lease the live sensor buffer; that method is not substituted for the
recorder's normal buffer/synchronization protocol.

Added VSYNC work is a heartbeat plus the cap check in the existing frame path.
No new file IO or image filter is run in VSYNC. Settling took about102-115ms in
observed logs. Frame copy times stayed below24.9ms in these tests. Isolated
overhead and end-to-end sensor latency remain unmeasured.

## Required source and core changes

The current capture-point change is in `mlv_lite.c` plus
`capture_tap_7d2.h`. `active_7d2.h` and `fast_probe_7d2.h` are dependencies of
the earlier working transfer path, not new correction algorithms. The rwtap
module contains the separate revision2 before/after snapshot experiment.

No additional core patch was required for the capture-point switch. The source
snapshot includes earlier `src/fps-engio.c`/`src/fps.h` changes used in the tested
environment: report Canon's normal nominal7D2 rates where timer calibration was
not established, expose the existing FPS getter to the module. These do not
change sensor readout timing or establish5x/override calibration. `src/raw.c`
and `platform/7D2.112` are included from upstream without a local change here.

`patches/capture-point-only.patch` applies to the included prior recorder in
`baseline/`. `patches/upstream-to-working.patch` includes all relevant local
changes against the named upstream commit. Use either appropriate patch;
the full source snapshot already contains both sets of changes. The current
patch includes the60s cap; it differs from the historical10s trial in duration
and menu text only. Tested local binary identities are in `environment.json`;
no prebuilt camera module/core is published here.

## Porting to another recording module or camera

Preserve the destination module's buffer ownership, DMA/cache protocol and
drain/stop/error/unload paths. Integrate selector ownership into those paths
rather than copying the snapshot method into a recorder. Map the target's
native command, state and source format independently. The7D2 addresses,
selectors, row copying and normal1x controller are not a generic70D/5x fix.
5x geometry, source timing, ownership and precision require their own tests.
