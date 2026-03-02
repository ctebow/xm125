from __future__ import annotations

import csv
from pathlib import Path
from typing import Callable


def _rewrite_edistance(
    csv_path: Path,
    edistance_fn: Callable[[int], float],
    trial_col: str = "trial",
    edistance_col: str = "edistance",
) -> None:
    csv_path = Path(csv_path)
    rows: list[dict[str, str]] = []

    with csv_path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        if reader.fieldnames is None:
            raise ValueError(f"{csv_path}: missing header row")
        fieldnames = list(reader.fieldnames)
        if trial_col not in fieldnames:
            raise ValueError(f"{csv_path}: missing '{trial_col}' column")
        if edistance_col not in fieldnames:
            raise ValueError(f"{csv_path}: missing '{edistance_col}' column")

        for row in reader:
            rows.append(row)

    for row in rows:
        trial = int(float(row[trial_col]))
        ed = edistance_fn(trial)
        # Write integers without trailing .0 to keep files tidy/consistent
        if float(ed).is_integer():
            row[edistance_col] = str(int(ed))
        else:
            row[edistance_col] = str(ed)

    tmp_path = csv_path.with_suffix(csv_path.suffix + ".tmp")
    with tmp_path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)

    tmp_path.replace(csv_path)


def main() -> None:
    repo_root = Path(__file__).resolve().parents[1]  # benchmarking/
    data_dir = repo_root / "data"

    _rewrite_edistance(
        data_dir / "profile_back_greenpvc_85-95-2mm_profile1_snr35000_CFAR.csv",
        edistance_fn=lambda trial: 95 - 2 * trial,
    )

    _rewrite_edistance(
        data_dir / "profile_back_greenpvc_90-145-5mm_profile2_snr35000_CFAR_calibrate-aware.csv",
        edistance_fn=lambda trial: 145 - 5 * trial,
    )


if __name__ == "__main__":
    main()

