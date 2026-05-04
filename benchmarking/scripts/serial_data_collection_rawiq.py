from __future__ import annotations

import csv
import logging
import os
import re
import sys
import time
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

import click
import serial
import serial.tools.list_ports


DEFAULT_BAUDRATE = 115200
DEFAULT_NUM_SAMPLES = 200
DEFAULT_NUM_TRIALS = 1
DEFAULT_DELAY = 10

FRAME_RE = re.compile(
    r"^FRAME\s+(\d+)\s+([A-Z?]+)\s+addr=0x([0-9A-Fa-f]+)\s+bins=(\d+)\s+t_ms=(\d+)$"
)
ENDFRAME_RE = re.compile(r"^ENDFRAME\s+(\d+)\s+([A-Z?]+)$")
IQ_RE = re.compile(r"^(-?\d+)\s+(-?\d+)\s+(-?\d+)$")


@dataclass
class CaptureStats:
    accepted_rows: int = 0
    rejected_lines_total: int = 0
    rejected_by_reason: Counter = None

    def __post_init__(self) -> None:
        if self.rejected_by_reason is None:
            self.rejected_by_reason = Counter()

    def reject(self, reason: str) -> None:
        self.rejected_lines_total += 1
        self.rejected_by_reason[reason] += 1


@dataclass
class FrameContext:
    frame_id: int
    sensor_label: str
    sensor_addr_hex: str
    frame_bins: int
    frame_t_ms: int
    bin_index_sensor: int = 0


def open_serial(baud_rate: int, timeout: float = 1.0):
    ports = serial.tools.list_ports.comports()
    if not ports:
        raise IOError("No serial ports found. Make sure your device is connected.")

    port_name = ports[0].device
    print(f"Connecting to port: {port_name}")

    serial_com = serial.Serial(port_name, baud_rate, timeout=timeout)
    serial_com.setDTR(False)
    time.sleep(1)
    serial_com.flushInput()
    serial_com.setDTR(True)
    return serial_com


def _is_known_status_or_error(line: str) -> bool:
    lower = line.lower()
    status_tokens = (
        "raw_iq_xm125:",
        "protocol:",
        "ready. send start",
        "connected:",
        "resetting sensor",
        "[i2c scan]",
        "[i2c precheck]",
        "[i2c fail]",
        "could not connect over i2c",
        "detector status error",
        " status error",
        "busy wait error",
        "start detector error",
        "measure distance error",
        "calibration needed",
        "recalibration complete",
        "apply_recalibration",
        "configuration application error",
        "[detector_status raw]",
        "error:",
    )
    return any(token in lower for token in status_tokens)


def _log_reject(reason: str, line: str, stats: CaptureStats, verbose: bool) -> None:
    stats.reject(reason)
    if verbose:
        logging.info("Rejected [%s]: %s", reason, line)


def get_reading_raw_iq(
    num_samples: int,
    writer: csv.writer,
    serial_com,
    trial: int,
    stats: CaptureStats,
    verbose_rejects: bool = False,
) -> None:
    if serial_com.in_waiting > 0:
        serial_com.reset_input_buffer()

    current_frame: Optional[FrameContext] = None
    serial_com.write(b"START\n")
    try:
        while stats.accepted_rows < num_samples:
            raw_bytes = serial_com.readline()
            decoded_line = raw_bytes.decode("utf-8", errors="replace").strip("\r\n")

            if not decoded_line:
                _log_reject("empty", decoded_line, stats, verbose_rejects)
                continue

            frame_match = FRAME_RE.match(decoded_line)
            if frame_match:
                try:
                    current_frame = FrameContext(
                        frame_id=int(frame_match.group(1)),
                        sensor_label=frame_match.group(2),
                        sensor_addr_hex=frame_match.group(3).upper(),
                        frame_bins=int(frame_match.group(4)),
                        frame_t_ms=int(frame_match.group(5)),
                        bin_index_sensor=0,
                    )
                except ValueError:
                    _log_reject("bad_frame_header", decoded_line, stats, verbose_rejects)
                continue

            endframe_match = ENDFRAME_RE.match(decoded_line)
            if endframe_match:
                if current_frame is None:
                    _log_reject("bad_endframe", decoded_line, stats, verbose_rejects)
                    continue

                try:
                    end_frame_id = int(endframe_match.group(1))
                except ValueError:
                    _log_reject("bad_endframe", decoded_line, stats, verbose_rejects)
                    continue

                end_label = endframe_match.group(2)
                if (
                    end_frame_id != current_frame.frame_id
                    or end_label != current_frame.sensor_label
                ):
                    _log_reject("bad_endframe", decoded_line, stats, verbose_rejects)

                current_frame = None
                continue

            iq_match = IQ_RE.match(decoded_line)
            if iq_match:
                if current_frame is None:
                    _log_reject("out_of_frame_iq", decoded_line, stats, verbose_rejects)
                    continue

                try:
                    register_index_global = int(iq_match.group(1))
                    i_raw_milli = int(iq_match.group(2))
                    q_raw_milli = int(iq_match.group(3))
                except ValueError:
                    _log_reject("bad_iq_row", decoded_line, stats, verbose_rejects)
                    continue

                row = [
                    trial,
                    current_frame.frame_id,
                    current_frame.sensor_label,
                    current_frame.sensor_addr_hex,
                    current_frame.frame_bins,
                    current_frame.frame_t_ms,
                    register_index_global,
                    current_frame.bin_index_sensor,
                    i_raw_milli,
                    q_raw_milli,
                ]
                writer.writerow(row)

                current_frame.bin_index_sensor += 1
                stats.accepted_rows += 1
                continue

            if _is_known_status_or_error(decoded_line):
                _log_reject("non_data_status", decoded_line, stats, verbose_rejects)
            else:
                _log_reject("bad_iq_row", decoded_line, stats, verbose_rejects)
    finally:
        serial_com.write(b"STOP\n")


@click.command()
@click.option("--baudrate", "-b", default=DEFAULT_BAUDRATE, show_default=True, help="Baud rate.")
@click.option("--timeout", "-t", type=float, default=1.0, show_default=True, help="Read timeout (seconds).")
@click.option("--outfile", "-o", type=click.Path(path_type=Path, writable=True), default=None, help="File to write data to. If omitted, prints to stdout.")
@click.option("--duration", "-d", type=float, default=None, help="Total time to collect data (seconds). Mutually exclusive with --num-samples.")
@click.option("--num-samples", "-n", type=int, default=DEFAULT_NUM_SAMPLES, help="Number of IQ rows to write per trial.")
@click.option("--delimiter", "-D", default="\\n", show_default=True, help="Line delimiter/terminator used by device.")
@click.option("--dry-run", is_flag=True, help="Print the resolved options and exit without opening serial port.")
@click.option("--verbose", "-v", count=True, help="Increase verbosity (use -v, -vv).")
@click.option("--num-trials", type=int, default=DEFAULT_NUM_TRIALS, help="Number of trials to take, each with num_samples IQ rows.")
@click.option("--delay", type=int, default=DEFAULT_DELAY, help="Delay in seconds between each trial.")
@click.option("--edistance", "-e", type=str, default=None, help="Reserved for compatibility with serial_data_collection.py.")
def main(
    baudrate: int,
    timeout: float,
    outfile: Optional[Path],
    duration: Optional[float],
    num_samples: Optional[int],
    delimiter: str,
    dry_run: bool,
    verbose: int,
    num_trials: int,
    delay: int,
    edistance: Optional[str],
) -> None:
    log_level = logging.WARNING
    if verbose >= 2:
        log_level = logging.DEBUG
    elif verbose == 1:
        log_level = logging.INFO
    logging.basicConfig(level=log_level, format="%(asctime)s %(levelname)s: %(message)s")

    if duration is not None and num_samples is not None:
        raise click.UsageError("Options --duration and --num-samples are mutually exclusive.")

    logging.debug(
        "Resolved options: baud=%d timeout=%.3f outfile=%s duration=%s num_samples=%s delimiter=%r dry_run=%s",
        baudrate,
        timeout,
        outfile,
        duration,
        num_samples,
        delimiter,
        dry_run,
    )

    if dry_run:
        click.echo("Dry run -- no serial port will be opened. Options:")
        click.echo(f"  baudrate={baudrate}")
        click.echo(f"  timeout={timeout}")
        click.echo(f"  outfile={outfile}")
        click.echo(f"  duration={duration}")
        click.echo(f"  num samples={num_samples}")
        click.echo(f"  delimiter={repr(delimiter)}")
        click.echo(f"  num_trials={num_trials}")
        click.echo(f"  delay={delay}")
        click.echo(f"  edistance={edistance}")
        return

    file_empty = True
    if outfile and os.path.exists(outfile) and os.path.getsize(outfile) > 0:
        file_empty = False

    out_stream = None
    all_stats = CaptureStats()
    try:
        out_stream = open(outfile, "a", encoding="utf-8", newline="") if outfile else sys.stdout
        writer = csv.writer(out_stream, delimiter=",")
        if file_empty:
            writer.writerow(
                [
                    "trial",
                    "frame_id",
                    "sensor_label",
                    "sensor_addr_hex",
                    "frame_bins",
                    "frame_t_ms",
                    "register_index_global",
                    "bin_index_sensor",
                    "I_raw_milli",
                    "Q_raw_milli",
                ]
            )

        ser = open_serial(baudrate, timeout=timeout)
        logging.info("Starting read loop")

        for trial in range(num_trials):
            per_trial_stats = CaptureStats()
            get_reading_raw_iq(
                num_samples=num_samples,
                writer=writer,
                serial_com=ser,
                trial=trial,
                stats=per_trial_stats,
                verbose_rejects=verbose >= 1,
            )
            all_stats.accepted_rows += per_trial_stats.accepted_rows
            all_stats.rejected_lines_total += per_trial_stats.rejected_lines_total
            all_stats.rejected_by_reason.update(per_trial_stats.rejected_by_reason)

            for n in range(delay, 0, -1):
                print(f"Measure in {n}")
                time.sleep(1)
    finally:
        try:
            if out_stream and out_stream is not sys.stdout:
                out_stream.close()
        except Exception:
            pass

        try:
            if "ser" in locals() and getattr(ser, "is_open", False):
                ser.close()
                logging.info("Serial port closed")
        except Exception:
            pass

    print(
        f"Capture summary: accepted_rows={all_stats.accepted_rows}, "
        f"rejected_lines={all_stats.rejected_lines_total}"
    )
    if all_stats.rejected_by_reason:
        breakdown = ", ".join(
            f"{reason}={count}"
            for reason, count in sorted(all_stats.rejected_by_reason.items())
        )
    else:
        breakdown = "none"
    print(f"Rejected breakdown: {breakdown}")


if __name__ == "__main__":
    main(prog_name="serial_data_collection_rawiq")
