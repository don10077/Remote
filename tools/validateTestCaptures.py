#!/usr/bin/env python3
"""Сравнить записи Test с кадрами, которые собирает harmony-cli encode."""

import json
import os
import subprocess
from pathlib import Path

from protocolBench import CASES

ROOT = Path(__file__).resolve().parent.parent
CAPTURES = ROOT / "docs/harmony-test-captures"
CLI = ROOT / "build/win-release/cli/harmony-cli.exe"
NAMES = ["NEC", "NECext", "Samsung32", "SIRC12", "SIRC15", "SIRC20", "RC5", "RC5X",
         "RC6", "Panasonic", "LG", "JVC", "Onkyo", "Denon", "Sharp", "RCA"]


def frame(values: list[int]) -> list[int]:
    """Первый кадр без конечной длинной паузы."""
    return values[:next((i for i, value in enumerate(values) if value < -5200), len(values))]


def rc5(address: int, command: int, toggle: int) -> list[int]:
    """Первый кадр RC5/RC5X с явным битом переключения."""
    bits = [1, int((command & 0x40) == 0), toggle]
    bits += [(address >> shift) & 1 for shift in range(4, -1, -1)]
    bits += [(command >> shift) & 1 for shift in range(5, -1, -1)]
    levels = [level for bit in bits for level in (not bit, bool(bit))]
    out: list[int] = []
    for level in levels:
        value = 889 if level else -889
        if out and out[-1] * value > 0:
            out[-1] += value
        else:
            out.append(value)
    out = out[1:]  # начальная пауза до первого импульса не передаётся
    if out and out[-1] < 0:
        out.pop()  # заключительная пауза входит в межкадровый интервал
    return out


def main() -> None:
    environment = os.environ.copy()
    environment["PATH"] = "D:/Install/MSYS2/mingw64/bin;D:/Install/MSYS2/usr/bin;" + environment["PATH"]
    results = []
    for name, (protocol, address, command, _) in zip(NAMES, CASES):
        path = CAPTURES / f"44791927-{name}.json"
        if not path.exists():
            results.append({"name": name, "status": "missing"})
            continue
        recorded = json.loads(path.read_text(encoding="utf-8"))
        output = subprocess.check_output([CLI, "encode", protocol, str(address), str(command)],
                                         text=True, env=environment)
        expected = [int(v) for v in output.splitlines()[1].split(":", 1)[1].split()]
        got, want = frame(recorded["durations"]), frame(expected)
        variants = [want]
        if name in ("RC5", "RC5X"):
            variants = [rc5(address, command, toggle) for toggle in (0, 1)]
        def matches(candidate: list[int]) -> bool:
            return len(got) == len(candidate) and all(
                abs(a - b) <= max(200, abs(b) / 6) for a, b in zip(got, candidate))
        status = "pass" if any(matches(candidate) for candidate in variants) else "mismatch"
        matched_toggle = next((i for i, candidate in enumerate(variants) if matches(candidate)), None)
        results.append({"name": name, "protocol": protocol, "address": address, "command": command,
                        "status": status, "recordedPulses": (len(got) + 1) // 2,
                        "expectedPulses": (len(want) + 1) // 2, "file": str(path.relative_to(ROOT)),
                        "observedToggle": matched_toggle if name in ("RC5", "RC5X") else None})
        print(f"{name:<11} {status:<8} {len(got):>3}/{len(want):<3} durations")
    for result in results:
        if result["name"] not in ("RC5", "RC6"):
            continue
        name = result["name"]
        first = json.loads((CAPTURES / f"{name}-first.json").read_text(encoding="utf-8"))["durations"]
        second = json.loads((CAPTURES / f"{name}-second.json").read_text(encoding="utf-8"))["durations"]
        changed = [i for i, (a, b) in enumerate(zip(first[:-1], second[:-1])) if abs(a - b) > 200]
        result["toggleChangedDurationIndices"] = changed
        result["toggleBetweenPresses"] = (
            "confirmed" if len(first) == len(second) and changed == ({"RC5": [2, 4], "RC6": [8, 10]}[name])
            else "not confirmed")
    report = ROOT / "docs/harmony-test-transmit-2026-11-06.json"
    report.write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(report)


if __name__ == "__main__":
    main()
