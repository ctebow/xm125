from __future__ import annotations
import logging
import sys
import os
import time
import csv
import serial
import serial.tools.list_ports
from pathlib import Path
from typing import Optional
import click

# /C:/Users/caden/vieregg-lab/xm125/benchmarking/scripts/serial_data_collection.py
"""
Template CLI entrypoint for serial data collection using click.

This file provides a main() function wired to click that parses common
command-line options (serial port, baudrate, timeout, output file,
duration or number of lines to read, verbosity). The reading loop below
is a lightweight example and can be extended for your device/protocol.
"""


DEFAULT_BAUDRATE = 115200
DEFAULT_NUM_SAMPLES = 200
DEFAULT_NUM_FRAMES = None
DEFAULT_NUM_TRIALS = 1
DEFAULT_DELAY = 10

# Serial format: each line = \"register rdistance strength [recalibrated]\" (space-separated)
# - recalibrated is optional; when present it is 0/1 indicating whether a recalibration
#   command was applied for that measurement frame.
# Single sensor: register 0-9 = peaks 0-9
# Dual sensor (dual_xm125_sketch): register 0-9 = back peaks, 10-19 = front peaks
# edistance: expected distance in mm; if None, uses 200 - 10*trial


def open_serial(baud_rate: int, timeout: float = 1.0):
    ports = serial.tools.list_ports.comports()
    if not ports:
        raise IOError("No serial ports found. Make sure your device is connected.")

    # Use the first available port.
    port_name = ports[0].device
    print(f"Connecting to port: {port_name}")

    # communicate with serial port connected to teensy
    serialCom = serial.Serial(port_name, baud_rate, timeout=timeout)
    serialCom.setDTR(False)
    time.sleep(1)
    serialCom.flushInput()
    serialCom.setDTR(True)
    return serialCom

def get_reading(num_samples, writer, serialCom, trial, edistance=None, num_frames=None):
    """Read Teensy IQ output and write CSV rows.

    - Legacy mode (default): collect `num_samples` numeric IQ rows.
    - Frame mode: if `num_frames` is set, collect that many complete
      FRAME..ENDFRAME blocks and record numeric rows inside them.
    """
    # Flush any leftover lines from previous trial so first read after START is fresh (fixes stale I2C/buffer)
    if serialCom.in_waiting > 0:
        serialCom.reset_input_buffer()
    serialCom.write(b"START\n")
    exp_dist = edistance if edistance is not None else 100 - 2 * trial
    rows_target = num_samples if num_frames is None else None
    rows_recorded = 0
    frames_target = num_frames if num_frames is not None else None
    frames_recorded = 0
    inside_frame = False

    while True:
        if rows_target is not None and rows_recorded >= rows_target:
            break
        if frames_target is not None and frames_recorded >= frames_target:
            break

        try:
            s_bytes = serialCom.readline()
            decoded_bytes = s_bytes.decode("utf-8").strip("\r\n")
            if not decoded_bytes:
                continue
            if decoded_bytes.startswith("FRAME "):
                inside_frame = True
                continue
            if decoded_bytes.startswith("ENDFRAME "):
                if inside_frame:
                    frames_recorded += 1
                    inside_frame = False
                continue
            if frames_target is not None and not inside_frame:
                continue
            parts = decoded_bytes.split()
            # Expect at least register, rdistance, strength
            if len(parts) < 3:
                continue
            try:
                reg = float(parts[0])
                rdist = float(parts[1])
                strength = float(parts[2])
                # Optional recalibrated flag (0/1). Default to 0 if missing.
                if len(parts) >= 4:
                    try:
                        recalibrated = int(parts[3])
                    except ValueError:
                        recalibrated = 0
                else:
                    recalibrated = 0
            except ValueError:
                # Skip malformed numeric lines
                continue

            row = [trial, reg, rdist, strength, recalibrated, exp_dist]
            writer.writerow(row)
            rows_recorded += 1
            print(parts)
        except Exception:
            print("Line not recorded, failed to get reading")
            return
    serialCom.write(b"STOP\n")


@click.command()
@click.option("--baudrate", "-b", default=DEFAULT_BAUDRATE, show_default=True, help="Baud rate.")
@click.option("--timeout", "-t", type=float, default=1.0, show_default=True, help="Read timeout (seconds).")
@click.option("--outfile", "-o", type=click.Path(path_type=Path, writable=True), default=None, help="File to write data to. If omitted, prints to stdout.")
@click.option("--duration", "-d", type=float, default=None, help="Total time to collect data (seconds). Mutually exclusive with --lines.")
@click.option("--num-samples", "-n", type=int, default=DEFAULT_NUM_SAMPLES, help="Number of samples to take. Mutually exclusive with --duration.")
@click.option("--num-frames", type=int, default=DEFAULT_NUM_FRAMES, help="Number of complete FRAME..ENDFRAME IQ frames to collect per trial.")
@click.option("--delimiter", "-D", default="\\n", show_default=True, help="Line delimiter/terminator used by device.")
@click.option("--dry-run", is_flag=True, help="Print the resolved options and exit without opening serial port.")
@click.option("--verbose", "-v", count=True, help="Increase verbosity (use -v, -vv).")
@click.option("--num-trials", type=int, default=DEFAULT_NUM_TRIALS, help="Number of trials to take, each trial has num_samples samples")
@click.option("--delay", type=int, default=DEFAULT_DELAY, help="Delay in seconds between each trial")
@click.option("--edistance", "-e", type=str, default=None, help="Expected distance (mm) per trial. Comma-separated for multiple trials (e.g. '200,190,180'). If omitted, uses 200-10*trial")
def main(
    baudrate: int,
    timeout: float,
    outfile: Optional[Path],
    duration: Optional[float],
    num_samples: Optional[int],
    num_frames: Optional[int],
    delimiter: str,
    dry_run: bool,
    verbose: int,
    num_trials: int,
    delay: int,
    edistance: Optional[str],
) -> None:
    """
    Collect serial data and write to OUTFILE or stdout.

    This is a template. Replace the simple read loop with device-specific
    framing/parsing as needed.
    """
    # Configure logging level
    log_level = logging.WARNING
    if verbose >= 2:
        log_level = logging.DEBUG
    elif verbose == 1:
        log_level = logging.INFO
    logging.basicConfig(level=log_level, format="%(asctime)s %(levelname)s: %(message)s")

    # Basic validation
    if duration is not None and num_samples is not None:
        raise click.UsageError("Options --duration and --lines are mutually exclusive.")
    if num_frames is not None and num_frames <= 0:
        raise click.UsageError("--num-frames must be greater than 0.")

    logging.debug("Resolved options: baud=%d timeout=%.3f outfile=%s duration=%s lines=%s delimiter=%r dry_run=%s",
                  baudrate, timeout, outfile, duration, num_samples, delimiter, dry_run)

    if dry_run:
        click.echo("Dry run -- no serial port will be opened. Options:")
        click.echo(f"  baudrate={baudrate}")
        click.echo(f"  timeout={timeout}")
        click.echo(f"  outfile={outfile}")
        click.echo(f"  duration={duration}")
        click.echo(f"  num samples={num_samples}")
        click.echo(f"  num frames={num_frames}")
        click.echo(f"  delimiter={repr(delimiter)}")
        return
    
    file_empty = True
    if outfile and os.path.exists(outfile) and os.path.getsize(outfile) > 0:
        file_empty = False
    
    # Open output stream
    out_stream = None
    try:
        out_stream = open(outfile, "a", encoding="utf-8", newline="") if outfile else sys.stdout
        writer = csv.writer(out_stream, delimiter=",")
        if file_empty:
            writer.writerow(["trial", "register", "rdistance", "strength", "recalibrated", "edistance"])
        ser = open_serial(baudrate, timeout=timeout)
        logging.info("Starting read loop")

        # Parse edistance: comma-separated list or single value
        edist_list = None
        if edistance:
            edist_list = [float(x.strip()) for x in edistance.split(",")]

        trial = 0
        while trial < num_trials:
            exp_dist = edist_list[trial] if edist_list and trial < len(edist_list) else None
            get_reading(num_samples, writer, ser, trial, edistance=exp_dist, num_frames=num_frames)
            for n in range(delay, 0, -1):
                    print(f'Measure in {n}')
                    time.sleep(1)
            trial += 1

    finally:
        # Clean up
        try:
            if out_stream and out_stream is not sys.stdout:
                out_stream.close()
        except Exception:
            pass

        try:
            # Closing serial port if it was opened
            if "ser" in locals() and getattr(ser, "is_open", False):
                ser.close()
                logging.info("Serial port closed")
        except Exception:
            pass


if __name__ == "__main__":
    main(prog_name="serial_data_collection")