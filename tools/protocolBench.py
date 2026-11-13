#!/usr/bin/env python3
"""Стенд RM4 → Harmony 900: RM4 излучает кадр, Harmony учит его, harmony-cli распознаёт.

Группы кадров:
  encode  — протоколы ir::encodableProtocols(), кадр собирает harmony-cli encode;
  decoded — распознаются программой, но не собираются (кадр — tools/irFrames.py);
  unknown — протоколов нет в программе: ожидается «не распознан» (tools/irFrames.py).
Для каждого случая в отчёт пишутся отправленные и выученные длительности — по ним
потом пишутся и проверяются декодеры.

  python tools/protocolBench.py                      # все группы
  python tools/protocolBench.py --group unknown      # одна группа
  python tools/protocolBench.py --only Kaseikyo      # случаи с подстрокой в названии
"""

import argparse
import json
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

import broadlinkLearn as bl
import irFrames

ROOT = Path(__file__).resolve().parent.parent
CASES = [
    ("NEC", 0x04, 0x08, "NEC"),
    ("NEC (extended)", 0x1E7, 0x45, "NEC (extended)"),
    ("Samsung32", 0x0707, 0x02, "Samsung32"),
    ("Sony SIRC 12", 0x01, 0x15, "Sony SIRC 12"),
    ("Sony SIRC 15", 0x97, 0x2A, "Sony SIRC 15"),
    ("Sony SIRC 20", 0x1A3, 0x3C, "Sony SIRC 20"),
    ("RC5", 0x05, 0x35, "RC5"),
    ("RC5", 0x10, 0x4C, "RC5X"),
    ("RC6", 0xA5, 0x5A, "RC6"),
    ("Panasonic", 0x080, 0x3D, "Panasonic"),
    ("LG", 0x04, 0x10EF, "LG"),
    ("JVC", 0x03, 0x17, "JVC"),
    ("Onkyo", 0xD2D2, 0x1234, "Onkyo"),
    ("Denon", 0x02, 0xE1, "Denon"),
    ("Sharp", 0x01, 0x16, "Sharp"),
    ("RCA", 0x0F, 0x54, "RCA"),
]


def encode_case(cli: Path, protocol: str, address: int, command: int, expected: str) -> dict:
    """Случай группы encode: кадр от harmony-cli encode."""
    case = {"group": "encode", "name": f"{protocol} {address:#x} {command:#x}", "protocol": protocol,
            "expect": {"protocol": expected, "address": address, "command": command}}
    generated = subprocess.run([cli, "encode", protocol, str(address), str(command)],
                               capture_output=True, text=True, encoding="utf-8")
    if generated.returncode:
        case["error"] = generated.stderr.strip()
        return case
    lines = generated.stdout.splitlines()
    case["requestedCarrierHz"] = int(lines[0].split(":", 1)[1])
    case["durations"] = [int(value) for value in lines[1].split(":", 1)[1].split()]
    return case


def frame_case(frame: irFrames.Case) -> dict:
    """Случай групп decoded и unknown: кадр от tools/irFrames.py."""
    expect = ({"protocol": frame.expect[0], "address": frame.expect[1], "command": frame.expect[2]}
              if frame.expect else None)
    case = {"group": frame.group, "name": frame.name, "expect": expect,
            "requestedCarrierHz": frame.carrier, "durations": frame.durations}
    if frame.note:
        case["note"] = frame.note
    return case


def learn(rm, cli: Path, durations: list[int]) -> str:
    """Запускает обучение, через 2,5 с излучает кадр; вывод harmony-cli learn."""
    packet = bl.encode_packet(durations)
    output = ""
    for attempt in range(2):
        learner = subprocess.Popen([cli, "learn"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, encoding="utf-8", errors="replace")
        time.sleep(2.5)
        rm.send_data(packet)
        try:
            output, _ = learner.communicate(timeout=35)
        except subprocess.TimeoutExpired:
            learner.kill()
            output, _ = learner.communicate()
        if "remote not found" not in output or attempt == 1:
            break
        time.sleep(2)
    return output


def judge(case: dict, output: str) -> None:
    """Разбирает вывод harmony-cli learn и ставит итог случаю."""
    learned = re.search(r"(?m)^Durations, \S+ \(\S+\):(.*)$", output)
    measured = re.search(r"(?m)^Carrier, Hz: (\d+)$", output)
    match = re.search(r"(?m)^(.+?) · address 0x([0-9A-F]+), command 0x([0-9A-F]+)$", output)
    decoded = ({"protocol": match[1], "address": int(match[2], 16), "command": int(match[3], 16)}
               if match else None)
    case["decoded"] = decoded
    case["measuredCarrierHz"] = int(measured[1]) if measured else None
    case["learnedDurations"] = [int(v) for v in learned[1].split()] if learned else None
    if not learned:
        case["status"] = "no signal"
        case["detail"] = output.strip().splitlines()[-1] if output.strip() else "no output"
        return
    sent = sum(1 for v in case["durations"] if v > 0)
    got = sum(1 for v in case["learnedDurations"] if v > 0)
    case["pulses"] = {"sent": sent, "learned": got}
    if case["expect"] is not None:
        case["status"] = "pass" if decoded == case["expect"] else "fail"
    else:
        case["status"] = "not recognised" if decoded is None else "misrecognised"
    # Описание сигнала — строка перед «Carrier, Hz».
    summary = re.search(r"(?m)^(.*)\n(?=Carrier, Hz:)", output)
    case["detail"] = match[0] if match else (summary[1].strip() if summary else "")


def main() -> None:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    parser.add_argument("--host", default="192.168.7.128", help="адрес RM4")
    parser.add_argument("--cli", type=Path, default=ROOT / "build/win-release/cli/harmony-cli.exe")
    parser.add_argument("--group", choices=["all", "encode", "decoded", "unknown"], default="all")
    parser.add_argument("--only", help="только случаи с этой подстрокой в названии")
    parser.add_argument("--report", type=Path,
                        default=ROOT / f"docs/protocol-bench-{datetime.now():%Y-%m-%d}.json")
    args = parser.parse_args()

    planned = []
    if args.group in ("all", "encode"):
        planned += [lambda c=c: encode_case(args.cli, *c) for c in CASES]
    planned += [lambda f=f: frame_case(f) for f in irFrames.cases() if args.group in ("all", f.group)]

    rm = bl.import_broadlink().hello(args.host, timeout=5)
    rm.auth()
    results = []
    for make in planned:
        case = make()
        if args.only and args.only.lower() not in case["name"].lower():
            continue
        if "error" in case:
            case["status"] = "encode error"
            case["detail"] = case.pop("error")
        else:
            judge(case, learn(rm, args.cli, case["durations"]))
        results.append(case)
        pulses = case.get("pulses")
        counted = f" [импульсов {pulses['sent']}→{pulses['learned']}]" if pulses else ""
        print(f"{case['group']:<8} {case['name']:<40} {case['status']}: {case['detail']}{counted}", flush=True)
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps({
            "date": datetime.now().isoformat(timespec="seconds"),
            "rmHost": args.host,
            "note": "RM4 emits ~38 kHz for every case; requestedCarrierHz is the protocol's own carrier. "
                    "pass/fail — decoded vs expect; for group unknown: not recognised (expected) or misrecognised.",
            "cases": results}, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    counts: dict[str, int] = {}
    for case in results:
        counts[case["status"]] = counts.get(case["status"], 0) + 1
    print("Итог: " + ", ".join(f"{k} — {v}" for k, v in counts.items()) + f". Отчёт: {args.report}")


if __name__ == "__main__":
    main()
