# Raw IQ CSV schema

CSV files produced by [`benchmarking/scripts/get_iq_main.py`](../scripts/get_iq_main.py) when collecting from Teensy firmware [`raw_iq_xm125`](../../src_teensy/raw_iq_xm125/raw_iq_xm125.ino) (START/STOP) and [`RawI2CIqProtocol.h`](../../src_teensy/raw_iq_xm125/RawI2CIqProtocol.h) serial framing.

## File format

| Property | Value |
|----------|--------|
| **Encoding** | UTF-8 |
| **Delimiter** | Comma (`,`) |
| **Header row** | Yes — first row is the column header |
| **Row grain** | One row per **range bin** (one `bin_index I Q` line from serial) |

## Column definitions

Columns appear in this **fixed order**:

| # | Column | Type | Description |
|---|--------|------|-------------|
| 1 | `trial` | integer | 0-based collection trial index (`--trials`). |
| 2 | `host_frame_id` | string | Teensy `frameCounter` from the `FRAME` serial line (host-side frame counter). |
| 3 | `sensor` | string | Sensor label from `FRAME`, e.g. `BACK`, `FRONT`. |
| 4 | `i2c_addr_hex` | string | Value after `addr=` on `FRAME` (hex digits without `0x` prefix in the cell, matching firmware print). |
| 5 | `bins` | string | Value after `bins=` on `FRAME`: number of bins in that IQ frame. |
| 6 | `firmware_frame_id` | string | Value after `fid=` on `FRAME`: STM firmware IQ cache frame id. |
| 7 | `t_ms` | string | Value after `t_ms=` on `FRAME`: Teensy `millis()` when the frame header was printed. |
| 8 | `bin_index` | string or numeric | First field of the bin line — global register index (back often `0..N-1`; front continues from `N` when both sensors are enabled). |
| 9 | `I` | float | In-phase component (Teensy sends floats already scaled from register milli-units). |
| 10 | `Q` | float | Quadrature component (same as `I`). |
| 11 | `expected_distance_mm` | float or empty | Optional experiment metadata from `--expected-distance-mm` for that trial; **empty** if not supplied. |

## Serial mapping

Teensy prints:

1. **Frame header:** `FRAME <host_frame_id> <SENSOR> addr=0xXX bins=<N> fid=<firmware_fid> t_ms=<millis>`
2. **Bin rows:** `<bin_index> <I> <Q>` (repeated `bins` times)
3. **Frame footer:** `ENDFRAME <host_frame_id> <SENSOR>`

Each bin row becomes one CSV row. Metadata columns (`host_frame_id` through `t_ms`) are **duplicated** from the most recent `FRAME` line so analysis does not need to join separate metadata files.

## Collection modes

| Mode | CLI | Row selection |
|------|-----|----------------|
| By IQ frames | `--num-iq-frames N` | Only bin rows **between** a `FRAME` and matching `ENDFRAME`; stop after `N` complete frame blocks. |
| By bin count | `--num-bin-rows N` | Stop after `N` bin rows. If no `FRAME` has been seen yet, frame metadata columns may be **empty** until the first `FRAME` is parsed. |

## Grouping for analysis

Treat rows as belonging to one **logical IQ frame** when they share the same:

- `trial`
- `host_frame_id`
- `sensor`
- `firmware_frame_id` (and typically the same `bins` and `t_ms` from that `FRAME`)

Across bins within that frame, `firmware_frame_id` should stay constant for a coherent cached capture on the STM side.

## Example header row

```csv
trial,host_frame_id,sensor,i2c_addr_hex,bins,firmware_frame_id,t_ms,bin_index,I,Q,expected_distance_mm
```

## Related

- I2C / firmware IQ protocol: [I2C_RAW_IQ_DISTANCE_API_GUIDE.md](I2C_RAW_IQ_DISTANCE_API_GUIDE.md)
- Collector CLI: `python benchmarking/scripts/get_iq_main.py --help`
