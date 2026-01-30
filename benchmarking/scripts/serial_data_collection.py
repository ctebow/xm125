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
DEFAULT_NUM_SAMPLES = 50
DEFAULT_NUM_TRIALS = 1
DEFAULT_DELAY = 10

def open_serial(baud_rate: int):
    ports = serial.tools.list_ports.comports()
    if not ports:
        raise IOError("No serial ports found. Make sure your device is connected.")

    # Use the first available port.
    port_name = ports[0].device
    print(f"Connecting to port: {port_name}")

    # communicate with serial port connected to teensy
    serialCom = serial.Serial(port_name, baud_rate)
    serialCom.setDTR(False)
    time.sleep(1)
    serialCom.flushInput()
    serialCom.setDTR(True)
    return serialCom

def get_reading(num_samples, writer, serialCom, trial):
    serialCom.write(b'START\n')
    for k in range(num_samples):
        try:
            s_bytes = serialCom.readline()
            decoded_bytes = s_bytes.decode("utf-8").strip('\r\n')
            values = [float(x) for x in decoded_bytes.split()]
            row = [trial, values[0], values[1], values[2], 100 - 4 * trial]
            writer.writerow(row)
            print(values)
        except:
            print("Line not recorded, failed to get reading")
            return
    serialCom.write(b'STOP\n')


@click.command()
@click.option("--baudrate", "-b", default=DEFAULT_BAUDRATE, show_default=True, help="Baud rate.")
@click.option("--timeout", "-t", type=float, default=1.0, show_default=True, help="Read timeout (seconds).")
@click.option("--outfile", "-o", type=click.Path(path_type=Path, writable=True), default=None, help="File to write data to. If omitted, prints to stdout.")
@click.option("--duration", "-d", type=float, default=None, help="Total time to collect data (seconds). Mutually exclusive with --lines.")
@click.option("--num-samples", "-n", type=int, default=DEFAULT_NUM_SAMPLES, help="Number of samples to take. Mutually exclusive with --duration.")
@click.option("--delimiter", "-D", default="\\n", show_default=True, help="Line delimiter/terminator used by device.")
@click.option("--dry-run", is_flag=True, help="Print the resolved options and exit without opening serial port.")
@click.option("--verbose", "-v", count=True, help="Increase verbosity (use -v, -vv).")
@click.option("--num_trials", type=int, default=DEFAULT_NUM_TRIALS, help="Number of trials to take, each trial has num_samples samples")
@click.option("--delay", type=int, default=DEFAULT_DELAY, help="delay inbetween each trial")
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

    logging.debug("Resolved options: baud=%d timeout=%.3f outfile=%s duration=%s lines=%s delimiter=%r dry_run=%s",
                  baudrate, timeout, outfile, duration, num_samples, delimiter, dry_run)

    if dry_run:
        click.echo("Dry run -- no serial port will be opened. Options:")
        click.echo(f"  baudrate={baudrate}")
        click.echo(f"  timeout={timeout}")
        click.echo(f"  outfile={outfile}")
        click.echo(f"  duration={duration}")
        click.echo(f"  num samples={num_samples}")
        click.echo(f"  delimiter={repr(delimiter)}")
        return
    
    file_empty = False
    if os.path.getsize(outfile) == 0:
        file_empty = True
    
    # Open output stream
    out_stream = None
    try:
        out_stream = open(outfile, "a", encoding="utf-8", newline="") if outfile else sys.stdout
        writer = csv.writer(out_stream, delimiter=",")
        if file_empty:
            writer.writerow(["trial", "register", "rdistance","strength", "edistance"])
        ser = open_serial(baudrate)
        logging.info("Starting read loop")

        trial = 0
        while trial < num_trials:
            get_reading(num_samples, writer, ser, trial)
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