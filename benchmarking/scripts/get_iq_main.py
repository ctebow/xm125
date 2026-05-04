"""
Collect raw IQ CSV from Teensy `raw_iq_xm125` firmware over USB serial.

Firmware (see ``src_teensy/raw_iq_xm125/raw_iq_xm125.ino``):
  - Send ``START\\n`` to begin streaming; ``STOP\\n`` to end.
  - Each IQ capture prints a header line, one line per range bin, then a footer.

Serial framing (from ``RawI2CIqProtocol.h``)::

    FRAME <host_frame_id> <SENSOR> addr=0x52 bins=<N> fid=<firmware_fid> t_ms=<millis>
    <bin_index> <I> <Q>
    ...
    ENDFRAME <host_frame_id> <SENSOR>

``host_frame_id`` is the Teensy ``frameCounter``; ``firmware_frame_id`` (``fid``) is
the STM IQ cache frame id. Bin rows use floats for I and Q (already scaled by 1/1000
on the Teensy).

Example (PowerShell, repo root)::

    python benchmarking/scripts/get_iq_main.py -o benchmarking/data/iq_smoke.csv \\
        --num-iq-frames 5 --read-timeout 5 --trial-delay-seconds 0

Object / phase analysis should consume the CSV from this script in a separate notebook
or module (not implemented here).
"""

from __future__ import annotations

import csv
import logging
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

import click
import serial
import serial.tools.list_ports

DEFAULT_BAUDRATE = 115200
DEFAULT_READ_TIMEOUT_S = 5.0
DEFAULT_TRIALS = 1
DEFAULT_TRIAL_DELAY_S = 0


@dataclass
class FrameMeta:
    """Metadata from a single ``FRAME ...`` line."""

    host_frame_id: str = ""
    sensor: str = ""
    i2c_addr_hex: str = ""
    bins: str = ""
    firmware_frame_id: str = ""
    t_ms: str = ""

    def clear(self) -> None:
        self.host_frame_id = ""
        self.sensor = ""
        self.i2c_addr_hex = ""
        self.bins = ""
        self.firmware_frame_id = ""
        self.t_ms = ""


def parse_frame_line(line: str) -> Optional[FrameMeta]:
    """Parse ``FRAME <id> <SENSOR> addr=... bins=... fid=... t_ms=...``."""
    if not line.startswith("FRAME "):
        return None
    parts = line.split()
    if len(parts) < 3:
        return None
    meta = FrameMeta(host_frame_id=parts[1], sensor=parts[2])
    for tok in parts[3:]:
        if tok.startswith("addr="):
            meta.i2c_addr_hex = tok[5:]
        elif tok.startswith("bins="):
            meta.bins = tok[5:]
        elif tok.startswith("fid="):
            meta.firmware_frame_id = tok[4:]
        elif tok.startswith("t_ms="):
            meta.t_ms = tok[5:]
    return meta


CSV_HEADER = [
    "trial",
    "host_frame_id",
    "sensor",
    "i2c_addr_hex",
    "bins",
    "firmware_frame_id",
    "t_ms",
    "bin_index",
    "I",
    "Q",
    "expected_distance_mm",
]


def open_serial(
    baud_rate: int,
    timeout: float,
    port_name: Optional[str] = None,
) -> serial.Serial:
    if port_name:
        chosen = port_name
        print(f"Connecting to port: {chosen}")
    else:
        ports = serial.tools.list_ports.comports()
        if not ports:
            raise OSError("No serial ports found. Connect the Teensy or pass --serial-port.")
        chosen = ports[0].device
        print(f"Connecting to first available port: {chosen}")

    ser = serial.Serial(chosen, baud_rate, timeout=timeout)
    ser.setDTR(False)
    time.sleep(1)
    ser.reset_input_buffer()
    ser.setDTR(True)
    return ser


def _write_bin_row(
    writer: csv.writer,
    trial: int,
    meta: FrameMeta,
    bin_index: str,
    i_val: str,
    q_val: str,
    expected_distance_mm: Optional[float],
) -> None:
    exp = "" if expected_distance_mm is None else expected_distance_mm
    writer.writerow(
        [
            trial,
            meta.host_frame_id,
            meta.sensor,
            meta.i2c_addr_hex,
            meta.bins,
            meta.firmware_frame_id,
            meta.t_ms,
            bin_index,
            i_val,
            q_val,
            exp,
        ]
    )


def collect_iq_session(
    writer: csv.writer,
    serial_com: serial.Serial,
    trial: int,
    num_bin_rows: Optional[int],
    num_iq_frames: Optional[int],
    expected_distance_mm: Optional[float],
) -> None:
    """Send START, read until row or frame quota, send STOP."""
    if serial_com.in_waiting:
        serial_com.reset_input_buffer()
    serial_com.write(b"START\n")

    rows_recorded = 0
    frames_recorded = 0
    inside_frame = False
    meta = FrameMeta()

    while True:
        if num_bin_rows is not None and rows_recorded >= num_bin_rows:
            break
        if num_iq_frames is not None and frames_recorded >= num_iq_frames:
            break

        try:
            s_bytes = serial_com.readline()
            decoded = s_bytes.decode("utf-8", errors="replace").strip("\r\n")
            if not decoded:
                continue

            if decoded.startswith("FRAME "):
                parsed = parse_frame_line(decoded)
                if parsed is not None:
                    meta = parsed
                inside_frame = True
                continue

            if decoded.startswith("ENDFRAME "):
                if inside_frame:
                    frames_recorded += 1
                inside_frame = False
                continue

            if num_iq_frames is not None and not inside_frame:
                continue

            parts = decoded.split()
            if len(parts) < 3:
                continue

            try:
                float(parts[0])
                float(parts[1])
                float(parts[2])
            except ValueError:
                continue

            bin_index, i_val, q_val = parts[0], parts[1], parts[2]
            _write_bin_row(writer, trial, meta, bin_index, i_val, q_val, expected_distance_mm)
            rows_recorded += 1
            logging.debug("bin row: %s", parts[:3])

        except Exception:
            logging.exception("Serial read failed; stopping session early.")
            break

    serial_com.write(b"STOP\n")


def default_output_path() -> Path:
    repo_scripts = Path(__file__).resolve().parent
    data_dir = repo_scripts.parent / "data"
    data_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
    return data_dir / f"iq_capture_{stamp}.csv"


@click.command(context_settings={"help_option_names": ["-h", "--help"]})
@click.option(
    "--output",
    "-o",
    "output_path",
    type=click.Path(path_type=Path, dir_okay=False, writable=True),
    default=None,
    help="CSV output file. If omitted, writes under benchmarking/data/ with a UTC timestamp.",
)
@click.option("--baudrate", "-b", default=DEFAULT_BAUDRATE, show_default=True, help="Serial baud rate (Teensy default 115200).")
@click.option(
    "--read-timeout",
    "-t",
    type=float,
    default=DEFAULT_READ_TIMEOUT_S,
    show_default=True,
    help="Per-read timeout in seconds (pyserial readline). Use a larger value for long IQ frames.",
)
@click.option(
    "--serial-port",
    "-p",
    "serial_port",
    type=str,
    default=None,
    help="COM port (e.g. COM7). If omitted, uses the first port reported by the OS.",
)
@click.option(
    "--num-bin-rows",
    type=int,
    default=None,
    help="Stop after this many bin lines (three floats per line). Mutually exclusive with --num-iq-frames.",
)
@click.option(
    "--num-iq-frames",
    type=int,
    default=None,
    help="Stop after this many complete FRAME..ENDFRAME blocks. Only bin lines inside frames are recorded. Mutually exclusive with --num-bin-rows.",
)
@click.option("--trials", type=int, default=DEFAULT_TRIALS, show_default=True, help="Number of START/STOP collection trials.")
@click.option(
    "--trial-delay-seconds",
    type=int,
    default=DEFAULT_TRIAL_DELAY_S,
    show_default=True,
    help="Seconds to wait between trials (simple countdown + sleep).",
)
@click.option(
    "--expected-distance-mm",
    type=str,
    default=None,
    help="Optional comma-separated expected distance (mm) per trial for experiment metadata in the CSV.",
)
@click.option("--verbose", "-v", count=True, help="Logging verbosity (-v INFO, -vv DEBUG).")
@click.option("--dry-run", is_flag=True, help="Print resolved options and exit without opening serial.")
def main(
    output_path: Optional[Path],
    baudrate: int,
    read_timeout: float,
    serial_port: Optional[str],
    num_bin_rows: Optional[int],
    num_iq_frames: Optional[int],
    trials: int,
    trial_delay_seconds: int,
    expected_distance_mm: Optional[str],
    verbose: int,
    dry_run: bool,
) -> None:
    """Collect raw IQ from Teensy ``raw_iq_xm125`` into a CSV for offline analysis."""

    log_level = logging.WARNING
    if verbose >= 2:
        log_level = logging.DEBUG
    elif verbose == 1:
        log_level = logging.INFO
    logging.basicConfig(level=log_level, format="%(asctime)s %(levelname)s: %(message)s")

    if (num_bin_rows is None) == (num_iq_frames is None):
        raise click.UsageError("Specify exactly one of --num-bin-rows or --num-iq-frames.")
    if num_bin_rows is not None and num_bin_rows <= 0:
        raise click.UsageError("--num-bin-rows must be greater than 0.")
    if num_iq_frames is not None and num_iq_frames <= 0:
        raise click.UsageError("--num-iq-frames must be greater than 0.")
    if trials <= 0:
        raise click.UsageError("--trials must be greater than 0.")

    out_path = output_path if output_path is not None else default_output_path()

    edist_list: Optional[list[float]] = None
    if expected_distance_mm:
        edist_list = [float(x.strip()) for x in expected_distance_mm.split(",")]

    if dry_run:
        click.echo("Dry run — resolved options:")
        click.echo(f"  output:            {out_path}")
        click.echo(f"  baudrate:          {baudrate}")
        click.echo(f"  read_timeout:      {read_timeout}")
        click.echo(f"  serial_port:       {serial_port!r}")
        click.echo(f"  num_bin_rows:      {num_bin_rows}")
        click.echo(f"  num_iq_frames:     {num_iq_frames}")
        click.echo(f"  trials:            {trials}")
        click.echo(f"  trial_delay_s:     {trial_delay_seconds}")
        click.echo(f"  expected_dist_mm:  {expected_distance_mm!r}")
        return

    file_empty = True
    if out_path.exists() and out_path.stat().st_size > 0:
        file_empty = False

    out_stream = None
    try:
        out_stream = open(out_path, "a", encoding="utf-8", newline="")
        writer = csv.writer(out_stream)
        if file_empty:
            writer.writerow(CSV_HEADER)

        ser = open_serial(baudrate, read_timeout, port_name=serial_port)
        logging.info("Writing IQ CSV to %s", out_path)

        for trial in range(trials):
            exp_dist: Optional[float] = None
            if edist_list and trial < len(edist_list):
                exp_dist = edist_list[trial]

            collect_iq_session(
                writer,
                ser,
                trial,
                num_bin_rows=num_bin_rows,
                num_iq_frames=num_iq_frames,
                expected_distance_mm=exp_dist,
            )

            if trial < trials - 1 and trial_delay_seconds > 0:
                for n in range(trial_delay_seconds, 0, -1):
                    print(f"Next trial in {n} s…")
                    time.sleep(1)

    finally:
        try:
            if out_stream is not None:
                out_stream.close()
        except OSError:
            pass
        try:
            if "ser" in locals() and getattr(ser, "is_open", False):
                ser.close()
                logging.info("Serial port closed")
        except OSError:
            pass


if __name__ == "__main__":
    main(prog_name="get_iq_main")
