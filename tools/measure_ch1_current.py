#!/usr/bin/env python3
"""Measure SmartUSBHub CH1 current for the ESP32-S3M environment station."""

from __future__ import annotations

import argparse
import statistics
import sys
import time
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM9")
    parser.add_argument("--channel", type=int, default=1)
    parser.add_argument("--samples", type=int, default=80)
    parser.add_argument("--interval", type=float, default=0.25)
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(repo_root / "smartusbhub"))
    from smartusbhub import SmartUSBHub  # type: ignore

    hub = SmartUSBHub(args.port)
    currents_ma: list[float] = []
    voltages_mv: list[float] = []
    try:
        for _ in range(args.samples):
            current_raw = hub.get_channel_current(args.channel)
            voltage_mv = hub.get_channel_voltage(args.channel)
            if current_raw is not None:
                currents_ma.append(float(current_raw))
            if voltage_mv is not None:
                voltages_mv.append(float(voltage_mv))
            time.sleep(args.interval)
    finally:
        hub.disconnect()

    if not currents_ma:
        raise SystemExit("No current samples collected")

    voltage_text = "n/a"
    if voltages_mv:
        voltage_text = f"{statistics.mean(voltages_mv):.0f}"
    print(
        "CH{ch} avg_mA={avg:.1f} min_mA={minv:.1f} max_mA={maxv:.1f} "
        "samples={n} avg_voltage_mV={voltage}".format(
            ch=args.channel,
            avg=statistics.mean(currents_ma),
            minv=min(currents_ma),
            maxv=max(currents_ma),
            n=len(currents_ma),
            voltage=voltage_text,
        )
    )


if __name__ == "__main__":
    main()
