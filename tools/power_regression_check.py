#!/usr/bin/env python3
"""SmartUSBHub CH1 power regression gate for ESP32Mini."""

from __future__ import annotations

import argparse
import json
import statistics
import sys
import time
from pathlib import Path
from typing import Any


MODE_TARGETS_MA = {
    "idle": 75.0,
    "boost": 160.0,
    "backlight": 180.0,
}


def read_samples_from_hub(port: str, channel: int, samples: int, interval: float) -> tuple[list[float], list[float]]:
    repo_root = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(repo_root / "smartusbhub"))
    from smartusbhub import SmartUSBHub  # type: ignore

    hub = SmartUSBHub(port)
    currents_ma: list[float] = []
    voltages_mv: list[float] = []
    try:
        for _ in range(samples):
            current_raw = hub.get_channel_current(channel)
            voltage_mv = hub.get_channel_voltage(channel)
            if current_raw is not None:
                currents_ma.append(float(current_raw))
            if voltage_mv is not None:
                voltages_mv.append(float(voltage_mv))
            time.sleep(interval)
    finally:
        hub.disconnect()
    return currents_ma, voltages_mv


def read_samples_from_json(path: Path) -> tuple[list[float], list[float]]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError("input JSON must be an object")
    currents = data.get("currents_ma")
    voltages = data.get("voltages_mv", [])
    if not isinstance(currents, list) or not all(isinstance(item, (int, float)) for item in currents):
        raise ValueError("input JSON must contain numeric currents_ma list")
    if not isinstance(voltages, list) or not all(isinstance(item, (int, float)) for item in voltages):
        raise ValueError("input JSON voltages_mv must be numeric when present")
    return [float(item) for item in currents], [float(item) for item in voltages]


def summarize(currents_ma: list[float], voltages_mv: list[float]) -> dict[str, float]:
    if not currents_ma:
        raise ValueError("No current samples collected")
    summary = {
        "avg_mA": statistics.mean(currents_ma),
        "min_mA": min(currents_ma),
        "max_mA": max(currents_ma),
        "samples": float(len(currents_ma)),
    }
    if voltages_mv:
        summary["avg_voltage_mV"] = statistics.mean(voltages_mv)
    return summary


def verdict(summary: dict[str, float], mode: str, target_ma: float, warn_margin_ma: float) -> tuple[int, str]:
    avg = summary["avg_mA"]
    warn_threshold = target_ma + warn_margin_ma
    if avg <= target_ma:
        return 0, "PASS"
    if mode != "idle" and avg <= warn_threshold:
        return 0, "WARN"
    if mode == "idle":
        return 1, "FAIL"
    return 1, "FAIL"


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="ESP32Mini SmartUSBHub power regression gate")
    parser.add_argument("--mode", choices=sorted(MODE_TARGETS_MA), default="idle")
    parser.add_argument("--target-ma", type=float, default=None)
    parser.add_argument("--warn-margin-ma", type=float, default=20.0)
    parser.add_argument("--input-json", type=Path, help="Offline samples: {currents_ma:[], voltages_mv:[]}")
    parser.add_argument("--port", default="COM9", help="SmartUSBHub control port, not ESP32 upload port")
    parser.add_argument("--channel", type=int, default=1)
    parser.add_argument("--samples", type=int, default=80)
    parser.add_argument("--interval", type=float, default=0.25)
    args = parser.parse_args(argv)
    if args.samples <= 0:
        parser.error("--samples must be positive")
    if args.channel <= 0:
        parser.error("--channel must be positive")
    return args


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    target_ma = args.target_ma if args.target_ma is not None else MODE_TARGETS_MA[args.mode]

    try:
        if args.input_json:
            currents_ma, voltages_mv = read_samples_from_json(args.input_json)
        else:
            currents_ma, voltages_mv = read_samples_from_hub(args.port, args.channel, args.samples, args.interval)
        summary = summarize(currents_ma, voltages_mv)
    except Exception as exc:  # noqa: BLE001 - CLI should report all collection failures.
        print(f"[FAIL] power regression input error: {exc}")
        return 2

    exit_code, level = verdict(summary, args.mode, target_ma, args.warn_margin_ma)
    voltage_text = "n/a"
    if "avg_voltage_mV" in summary:
        voltage_text = f"{summary['avg_voltage_mV']:.0f}"
    print(
        "[METRIC] mode={mode} channel=CH{channel} avg_mA={avg:.1f} min_mA={minv:.1f} "
        "max_mA={maxv:.1f} samples={samples:.0f} avg_voltage_mV={voltage} "
        "target_mA={target:.1f} warn_margin_mA={margin:.1f}".format(
            mode=args.mode,
            channel=args.channel,
            avg=summary["avg_mA"],
            minv=summary["min_mA"],
            maxv=summary["max_mA"],
            samples=summary["samples"],
            voltage=voltage_text,
            target=target_ma,
            margin=args.warn_margin_ma,
        )
    )

    if level == "PASS":
        print(f"[PASS] power regression passed for {args.mode}")
    elif level == "WARN":
        print(f"[WARN] power average exceeds target but remains within margin for {args.mode}")
    else:
        print(f"[FAIL] power average exceeds target for {args.mode}")
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
