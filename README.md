# Canon 7D Mark II RAW Pixel Correction

An experimental Magic Lantern recorder modification that captures RAW data
from a later stage of Canon's image-processing pipeline, correcting a repeatable
defective-pixel pattern observed in RAW video on the **Canon EOS 7D Mark II**.

**Tested camera: 7D Mark II, firmware 1.1.2.** This is not an official Magic
Lantern release, a Canon 70D port, or a verified 5× recording implementation.

## The problem

RAW video from the original capture point contained a large number of fixed
defective samples. Dark-frame and lit-scene comparisons identified **689
consistently zero-valued pixels** in the analyzed native frame area. These were
present before the RAW data reached the later processing stage used here.

The project is called “pixel correction” because the measured defect was a
fixed zero/invalid-pixel pattern. It does not claim to eliminate every type of
hot pixel, white dot, or high-ISO noise.

## How it was addressed

The original recorder collected RAW data from Canon's **RSHD** capture point.
We traced Canon's native source-selection path and changed the recording trial
to use **DEFOE**, a later capture point whose output no longer contained the
identified defective samples. The corrected values followed nearby scene data.

On the tested firmware, Canon's `lv_set_raw_wp` setter accepts command argument
`4`, which selects internal source `18` (DEFOE), replacing source `14` (RSHD).
The implementation waits for the expected source and RAW format before enabling
recording, checks them while copying frames, and restores the original source
when recording stops or a returning startup error occurs.

The correction happens **inside the camera's processing pipeline**. No PC
denoising, software bad-pixel repair, or reduced-bit conversion is applied to
the saved RAW frames. Switching capture points adds no further crop to the
existing recorder configuration.

The result is consistent with Canon-side defective-pixel correction. Canon's
exact correction algorithm and all other processing at DEFOE have not been
fully characterized.

## Camera-tested results

Tested recording format: **1832 × 800, uncompressed 14-bit packing, 23.976 fps,
normal 1080 movie Live View at 1×**. The native source buffer remains
1984 × 1068 with a 3472-byte pitch. The saved window at native position
`(144, 144)` predates this capture-point change.

| Test | Result |
| --- | --- |
| Dark-frame and lit-scene A/B snapshots | Same 689 zero-valued sites before; none afterward |
| Initial manual-stop video | 149 frames, 6.21 seconds; correction and restoration verified |
| Initial automatic-stop video | 240 frames, 10.01 seconds; correction and restoration verified |
| One-minute automatic-stop video | 1,439 frames, 60.02 seconds; correction and restoration verified |
| Three manual recordings without rebooting | 116, 1,394 and 176 frames; saved ISO metadata reports ISO 5000 |

The three ISO 5000 clips lasted **4.84, 58.14 and 7.34 seconds**. Their increasing
camera uptime, successful cleanup between recordings and the operator's test
sequence support repeated start/stop within the same boot. Every start selected
DEFOE and every stop restored RSHD, with no recorded restoration fault.

Of the known native defects, **509 lie inside the recorded window**. Those sites
were checked in every frame of each video. None returned to zero. Across the
three ISO 5000 clips alone, this covered **858,174 samples in 1,686 frames**.

File structure, frame counts, payload uniqueness and timestamps passed the
recorded checks. No malformed blocks or frame-timing gaps were detected. The
worst measured whole-frame copy was about **24.8 ms**, within the approximately
41.7 ms frame interval. These measurements do not establish end-to-end latency.

The public evidence contains logs, measurements and hashes, **not personal
photos, footage, RAW pixel payloads or project camera ROM dumps**. See
[validation notes](docs/VALIDATION.md) for methodology and limits.

## Current implementation and limits

The supplied source has an opt-in **Corrected 60s** trial, capped at 1,439 frames.
The capture test defaults to OFF in source. It is restricted to the tested
camera, firmware, format and mode. No ready-to-install camera binary is provided.

- Native **14-bit packing** is verified. True independent sensor precision,
  calibrated color accuracy, highlight behavior and resolution/MTF have not
  been established by these tests.
- Correction of the known defective-sample map is verified. Ordinary ISO 5000
  noise remains, and no matched high-ISO before/after noise comparison is claimed.
- Longer 3–5-minute corrected recordings, indefinite recording, split-file
  behavior under this modification, 5×/10× modes and other cameras are unverified.
- Source settling adds roughly 0.1 seconds before startup. Pipeline latency and
  isolated guard overhead have not been measured.
- Restoration on tested normal/manual/limit stops is verified. Mocked tests
  cover additional failures, but arbitrary mode transitions, native hangs,
  power loss and every hardware error are not covered by those results.
- A top-right timer text overlap has been reported separately. Its cause and a
  visual fix have not yet been verified.

## Repository contents

- [Full source snapshot](source/magiclantern_simplified): upstream base plus the
  relevant working recorder, snapshot diagnostic and existing FPS changes.
- [Capture lifecycle helper](source/magiclantern_simplified/modules/raw_video/mlv_lite/capture_tap_7d2.h)
  and [integrated recorder](source/magiclantern_simplified/modules/raw_video/mlv_lite/mlv_lite.c).
- [Patches](patches): a capture-point patch for the supplied prior recorder
  baseline, and the complete relevant changes against the recorded upstream commit.
- [Technical handover](docs/TECHNICAL-HANDOVER.md): exact firmware-specific
  addresses, DMA/cache context, lifecycle boundaries and porting considerations.
- [Build and tool notes](docs/BUILD.md), [validation](docs/VALIDATION.md), and
  [metadata-only evidence](evidence).

## Credits and license

Based on [Magic Lantern simplified](https://github.com/reticulatedpines/magiclantern_simplified),
upstream commit `142472e95054d73640f50789ac0fd66325499d82`. Magic Lantern and
MLV Lite authors retain their existing copyright and credits.

Special thanks to **`names_are_hard`** and **`Petars23_`** for their help and
feedback during this investigation.

This investigation was prompted by community observations that RAW video might
be collected before Canon's defective-pixel correction. Local reverse engineering,
diagnostics and implementation were developed with LLM assistance and validated
through the camera tests documented here. They are not an upstream endorsement.

Local additions are GPL-2.0-or-later; existing components retain their original
license notices. See [LICENSE](LICENSE) and the source tree's individual notices.
