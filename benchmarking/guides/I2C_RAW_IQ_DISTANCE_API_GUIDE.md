# I2C Raw IQ Distance API Guide (Cached Robust Variant)

This guide targets firmware built with:

- `Src/applications/i2c/i2c_iq_custom_cache_robust.c`

It explains the best host/master flow to fetch raw IQ over a constrained I2C link while preserving single-frame coherency.

## Overview

The robust cached IQ variant behaves as follows:

- IQ command namespace is `op = 6` in the high 16 bits of the command register value.
- On first IQ request when cache is invalid, the module captures one frame and caches per-bin averaged I/Q.
- Further IQ requests only move the selected bin index and read from that same cached frame.
- Cache is invalidated when detector state-changing commands run (`APPLY_CONFIGURATION`, `CALIBRATE`, `RECALIBRATE`, `APPLY_CONFIG_AND_CALIBRATE`, `MEASURE_DISTANCE`).
- Cache can also be explicitly invalidated by host with IQ index `0xFFFF`.

## Register Usage

Use the existing distance register map:

- `DISTANCE_REG_COMMAND_ADDRESS` (`256`): write command words.
- `DISTANCE_REG_DETECTOR_STATUS_ADDRESS` (`3`): read detector and IQ status bits.
- `DISTANCE_REG_MEASURE_COUNTER_ADDRESS` (`2`): read IQ metadata when cache is valid.
- `DISTANCE_REG_PEAK0_DISTANCE_ADDRESS` (`17`): current selected bin I (real), scaled x1000 in transport.
- `DISTANCE_REG_PEAK0_STRENGTH_ADDRESS` (`27`): current selected bin Q (imag), scaled x1000 in transport.

## Command Encoding

Command payload is 32-bit:

- `op` = upper 16 bits
- `index` = lower 16 bits

For IQ:

- `command = (6 << 16) | bin_index`
- Explicit invalidate:
  - `command = (6 << 16) | 0xFFFF`

## Status Bits (Robust Variant)

Read `DETECTOR_STATUS` and inspect:

- `IQ_READY` = bit 30 (`1 << 30`)
- `IQ_CAPTURE_ERROR` = bit 29 (`1 << 29`)

These are chosen to avoid overlap with protocol-defined detector error/status fields.

## MEASURE_COUNTER Interpretation (Robust Variant)

When IQ cache is valid, `MEASURE_COUNTER` returns:

- low 16 bits: `iq_num_points` (available bins)
- high 16 bits: `iq_frame_id` (increments on successful fresh capture)

When IQ cache is not valid, behavior falls back to default distance measure counter.

## Recommended Host Flow

1. Ensure detector is configured and calibrated.
2. Trigger IQ capture/select start bin:
   - write `COMMAND = (6 << 16) | 0`
3. Poll `DETECTOR_STATUS` until:
   - `IQ_READY == 1`
   - `IQ_CAPTURE_ERROR == 0`
4. Read `MEASURE_COUNTER`:
   - `frame_id_start = (counter >> 16) & 0xFFFF`
   - `num_bins = counter & 0xFFFF`
5. For each `bin` in `[0, num_bins-1]`:
   - write `COMMAND = (6 << 16) | bin`
   - read `PEAK0_DISTANCE` => `I_milli`
   - read `PEAK0_STRENGTH` => `Q_milli`
   - convert to float:
     - `I = I_milli / 1000.0`
     - `Q = Q_milli / 1000.0`
6. Re-read `MEASURE_COUNTER` and compare frame id:
   - `frame_id_end = (counter2 >> 16) & 0xFFFF`
   - if `frame_id_end != frame_id_start`, discard and retry capture sequence.

## Throughput Tips for Slow I2C

- Keep one capture and stream all bins from cache; do not force recapture between bins.
- Avoid unnecessary status polls during steady bin reads once cache is ready.
- Use sequential register reads where practical to reduce transaction overhead.
- Use fixed retry windows and bounded poll intervals for deterministic benchmark timing.

## Error Handling

If `IQ_CAPTURE_ERROR` is set:

1. Optionally send explicit invalidate command: `(6 << 16) | 0xFFFF`
2. Retry IQ capture command `(6 << 16) | desired_index`
3. If failures persist:
   - verify detector ready/configured/calibrated state
   - verify sensor IRQ timing and host timeout budget

## Notes

- Exported IQ length may be capped by firmware (`IQ_MAX_POINTS`), even if raw frame has more bins.
- Robust indexing in firmware uses full frame stride internally, so truncated export still preserves correct bin mapping.
