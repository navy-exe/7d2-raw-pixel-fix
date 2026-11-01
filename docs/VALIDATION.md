# Evidence and verification scope

Camera observations and host simulations are different evidence categories.
JSON/log records under `evidence/` retain actual measurements and payload hashes.
Personal image data is excluded, so independent reanalysis of the original
pixels is not possible from the public repository alone.

## Actual camera results

| Run | Frames | Duration | Stop | Exposure metadata |
| --- | ---: | ---: | --- | --- |
| RD0021 /M02-0118 |149|6.21455s|Manual|See inspection record |
| RD0022 /M02-0119 |240|10.01001s|Automatic limit|See inspection record |
| RD0023 /M02-0153 |1439|60.01835s|Automatic limit|ISO100 |
| RD0024 /M02-0209 |116|4.83817s|Manual|ISO5000 |
| RD0025 /M02-0210 |1394|58.14147s|Manual|ISO5000 |
| RD0026 /M02-0211 |176|7.34067s|Manual|ISO5000 |

All clips are1832x800, uncompressed14-bit packing, nominal23.976fps. Every VIDF
was decoded, checked for expected payload length, hashed, checked for flatness
and zero samples, and compared with physical frame numbering/header counts and
timestamps. Every known defective site inside the saved window was checked
again against the frame's decoded values and original payload hash. First,
middle and last previews were generated; sample scene previews were inspected.
No PC pixel repair or denoising was used on those payloads.

Full native dark and lit tests found the same689 persistent zero-valued sites
before source switching, none after.509 sites fall inside the existing recorder
window. In the ISO5000 sequence, all858174 corresponding samples across1686
frames stayed nonzero. Other bright/hot defects and general noise were not
proven eliminated. Scene edges can be miscounted as bright outliers, so such
counts are not treated as confirmed hot pixels.

RD0024/25/26 entry uptime79819/93335/155614ms; cleanup85902/153014/164322ms.
Gaps after completed cleanup7433ms and2600ms, no reset in observed progression,
supporting the user's same-boot sequence. All have MANUAL1/stop0, selection18,
restoration14/mux14/pack20 and fault0/cleanup. No log error or timing gap was
detected; that is not proof against every possible sensor/delivery failure.

The ISO5000 EXPO records also carry iso_mode1. Upstream code derives this flag
from the cached lens_info.iso field, which reads zero on this port in these
tests. The records confirm the saved ISO value; they do not independently
establish manual/auto control or frame-by-frame unchanging exposure.

## RAW integrity limits

Pack32 `0x20`, metadata bpp14, native geometry/pitch and decoded payload sizes
agree. All low-two-bit combinations occur, but this is not proof of14 independent
bits. Black metadata2047 and dark medians2048 agree before/after. White metadata
16200 is retained, but clipping/linearity is uncalibrated. RGGB metadata and
normal-looking approximate renderings support Bayer interpretation, not absolute
color accuracy. Lit median-mosaic correlation0.998397 and median absolute
difference4ADU excluding bad sites are descriptive, not an MTF/noise study.

## Host checks

The selector lifecycle helper passed31 mocked host cases. The10s and60s builds
each passed22 tests against compiled ARM recorder code, with external camera
APIs mocked and task buffer/file/metadata helpers stubbed in cleanup cases.
They cover baseline/corrected readiness, bounds, setter failure, no VSYNC,
format drift, restore failure latch, reuse, frame cap and returning task cleanup.
They do not simulate the complete camera or prove physical DMA throughput.

Snapshot revision2 passed41 compiled-module mock cases. Native selector mapping
passed16 emulated cases and native pipeline setup/update/frame-apply3 cases,
with physical endpoints mocked. These native checks originally required a
private matching ROM; the ROM is not published here. The initialTP0000 run lost
its RAW payloads to filename collisions and contributes no pixel evidence.
Only later revision2TP0001/TP0002 data underpin the snapshot pixel findings.

## Remaining work

Longer3-5min tests and split-file behavior, calibrated highlights/color/detail,
sensor latency, true precision, arbitrary LV/mode/error transitions,5x/10x,
70D and REC5X/PUNCH5X remain unverified. The existing reported timer overlay
issue needs actual screen evidence before a fix can be assessed.
