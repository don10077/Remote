#!/usr/bin/env python3
"""Запись ИК-сигналов пульта Harmony через Broadlink RM4 — образцы для тестов carrierVm.

Пульт к компьютеру не подключается: его направляют на RM4 и нажимают кнопку.
Скрипт сохраняет записанный сигнал в tests/data/broadlink/<Id устройства>-<команда>.json
вместе с <Code> этой команды из конфигурации, чтобы тест мог сравнить запись
с тем, что считает наш исполнитель.

Нужна библиотека python-broadlink:  pip install broadlink
RM4 должен быть в той же локальной сети и не заблокирован в приложении Broadlink
(«Lock device» выключен), иначе он не пустит к себе.

Запуск:
  python tools/broadlinkLearn.py discover
  python tools/broadlinkLearn.py list                       устройства из конфигурации
  python tools/broadlinkLearn.py list Panasonic             команды устройства
  python tools/broadlinkLearn.py learn Panasonic PowerToggle Mute VolumeUp
  python tools/broadlinkLearn.py learn-raw sony_RMT-D177P Power Play 1   родной пульт устройства
  python tools/broadlinkLearn.py --host 192.168.7.128 send-rc6 0xA5 0x5A

Устройство задаётся Id или частью «Производитель Модель». Если в сети несколько
RM, нужный выбирается --host; если на компьютере несколько сетевых адаптеров
(например, ещё подключён Harmony по USB), поиск может уйти не туда —
тогда --local-ip <адрес компьютера в домашней сети>.

Ограничение RM4: длительности он меряет шагом ≈32,84 мкс, а несущую не сообщает.
"""

import argparse
import json
import re
import socket
import sys
import time
from datetime import datetime
from pathlib import Path
from xml.etree import ElementTree as ET

ROOT = Path(__file__).resolve().parent.parent
CONFIG = ROOT / "tests" / "data" / "userconfig" / "UserConfiguration.xml"
OUTPUT = ROOT / "tests" / "data" / "broadlink"

TICK_US = 269 / 8192 * 1000  # единица длительности Broadlink, мкс (≈32,84)
IR_PACKET = 0x26
LEARN_TIMEOUT_S = 30


# --- конфигурация пульта --------------------------------------------------------

def tag(element) -> str:
    return element.tag.rsplit("}", 1)[-1]


def child_text(element, name: str) -> str:
    for child in element:
        if tag(child) == name:
            return (child.text or "").strip()
    return ""


def load_devices(path: Path) -> list:
    """Устройства конфигурации: Id, название и команды {имя: (протокол, код)}."""
    root = ET.parse(path).getroot()
    devices = []
    for device in root.iter():
        if tag(device) != "Device" or not child_text(device, "Id"):
            continue
        commands = {}
        for command in device.iter():
            if tag(command) != "Command":
                continue
            data = next((c for c in command if tag(c) == "Data"), None)
            if data is None:
                continue
            commands[child_text(command, "Name")] = (child_text(data, "Protocol"), child_text(data, "Code"))
        devices.append({
            "id": child_text(device, "Id"),
            "name": f"{child_text(device, 'Manufacturer')} {child_text(device, 'Model')}".strip(),
            "commands": commands,
        })
    return devices


def find_device(devices: list, query: str) -> dict:
    exact = [d for d in devices if d["id"] == query]
    if exact:
        return exact[0]
    found = [d for d in devices if query.lower() in d["name"].lower()]
    if len(found) == 1:
        return found[0]
    if not found:
        sys.exit(f"Устройство «{query}» в конфигурации не найдено. Список: python tools/broadlinkLearn.py list")
    names = ", ".join(f"{d['id']} {d['name']}" for d in found)
    sys.exit(f"«{query}» подходит к нескольким устройствам: {names}. Укажите Id.")


def find_command(device: dict, name: str) -> str:
    if name in device["commands"]:
        return name
    folded = [c for c in device["commands"] if c.lower() == name.lower()]
    if len(folded) == 1:
        return folded[0]
    sys.exit(f"У «{device['name']}» нет команды «{name}». "
             f"Список: python tools/broadlinkLearn.py list {device['id']}")


# --- формат Broadlink -----------------------------------------------------------

def decode_packet(data: bytes) -> dict:
    """Пакет записи Broadlink → длительности, мкс (+импульс / −пауза).

    Устройство пакета: u8 тип (0x26 — ИК), u8 число повторов, u16 LE длина,
    затем длительности в тиках: байт, а если он 0 — следующие два байта (BE).
    Первая длительность — импульс, дальше чередуются. В конце обычно длинная пауза.
    """
    if len(data) < 4:
        raise ValueError(f"короткий пакет: {len(data)} байт")
    if data[0] != IR_PACKET:
        raise ValueError(f"не ИК-пакет: тип 0x{data[0]:02X}")
    length = data[2] | data[3] << 8
    body = data[4:4 + length]
    if len(body) < length:
        raise ValueError(f"пакет обрезан: длина {length}, есть {len(body)}")
    durations = []
    i = 0
    while i < len(body):
        ticks = body[i]
        i += 1
        if ticks == 0:
            if i + 2 > len(body):
                raise ValueError("пакет обрезан внутри длинной длительности")
            ticks = body[i] << 8 | body[i + 1]
            i += 2
        us = round(ticks * TICK_US)
        durations.append(us if len(durations) % 2 == 0 else -us)
    return {"repeat": data[1], "durations": durations}


def rc6_mode0(address: int, command: int, toggle: int = 0) -> list[int]:
    """Один кадр RC6 Mode 0, 36 кГц: лидер, старт, режим, toggle, адрес и команда."""
    if not 0 <= address <= 0xFF or not 0 <= command <= 0xFF or toggle not in (0, 1):
        raise ValueError("RC6 Mode 0: адрес и команда 0..255, toggle 0 или 1")
    durations = [2666, -889]

    def half(mark: bool, width: int) -> None:
        duration = width if mark else -width
        if durations[-1] * duration > 0:
            durations[-1] += duration
        else:
            durations.append(duration)

    def bit(value: int, width: int = 444) -> None:
        half(bool(value), width)
        half(not value, width)

    bit(1)  # старт
    for _ in range(3):
        bit(0)  # режим 0
    bit(toggle, 888)
    for shift in range(7, -1, -1):
        bit((address >> shift) & 1)
    for shift in range(7, -1, -1):
        bit((command >> shift) & 1)
    elapsed = sum(abs(value) for value in durations)
    half(False, max(20000, 107000 - elapsed))
    return durations


def encode_packet(durations: list[int]) -> bytes:
    """Длительности (+импульс / −пауза) → ИК-пакет Broadlink."""
    body = bytearray()
    for duration in durations:
        ticks = max(1, round(abs(duration) / TICK_US))
        if ticks > 0xFFFF:
            raise ValueError("длительность слишком велика для пакета Broadlink")
        if ticks < 256:
            body.append(ticks)
        else:
            body.extend((0, ticks >> 8, ticks & 0xFF))
    return bytes((IR_PACKET, 0, len(body) & 0xFF, len(body) >> 8)) + body


# --- RM4 ------------------------------------------------------------------------

def import_broadlink():
    try:
        import broadlink  # noqa: PLC0415 — нужен только командам, работающим с RM
    except ImportError:
        sys.exit("Нет библиотеки python-broadlink. Установите: pip install broadlink")
    return broadlink


def describe(device) -> str:
    host = device.host[0] if isinstance(device.host, tuple) else device.host
    mac = device.mac.hex(":") if isinstance(device.mac, (bytes, bytearray)) else device.mac
    return (f"{host}  {getattr(device, 'manufacturer', '')} {getattr(device, 'model', '')} "
            f"({getattr(device, 'type', '?')}, devtype 0x{device.devtype:04X}, MAC {mac})")


def local_addresses() -> list:
    """IPv4-адреса компьютера, кроме петли и USB-сети Harmony (169.254.*)."""
    try:
        infos = socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET)
    except socket.gaierror:
        return []
    found = []
    for info in infos:
        address = info[4][0]
        if address.startswith(("127.", "169.254.")) or address in found:
            continue
        found.append(address)
    return found


def discover(args) -> list:
    """Поиск RM. Windows шлёт широковещательный пакет только через один адаптер,
    поэтому без --local-ip поиск повторяется с каждого адреса компьютера."""
    broadlink = import_broadlink()
    options = {"timeout": args.timeout}
    if args.host:
        if args.local_ip:
            options["local_ip_address"] = args.local_ip
        return [broadlink.hello(args.host, **options)]
    if args.broadcast:
        options["discover_ip_address"] = args.broadcast
    addresses = [args.local_ip] if args.local_ip else local_addresses() or [None]
    devices = {}
    for address in addresses:
        print(f"Поиск через {address or 'адаптер по умолчанию'}…")
        for device in broadlink.discover(local_ip_address=address, **options):
            devices.setdefault(bytes(device.mac), device)
    return list(devices.values())


def connect(args):
    broadlink = import_broadlink()
    try:
        devices = discover(args)
    except broadlink.exceptions.NetworkTimeoutError:
        sys.exit(f"RM по адресу {args.host} не ответил.")
    remotes = [d for d in devices if hasattr(d, "enter_learning")]
    if not remotes:
        sys.exit("RM в сети не найден. Попробуйте --host <адрес RM> или --local-ip <адрес компьютера>.")
    if len(remotes) > 1:
        listing = "\n  ".join(describe(d) for d in remotes)
        sys.exit(f"Найдено несколько RM, выберите --host:\n  {listing}")
    device = remotes[0]
    unlock_hint = ("Выключите блокировку в приложении Broadlink: устройство → «…» → Property → "
                   "«Lock device» выкл.")
    if getattr(device, "is_locked", False):
        sys.exit(f"RM заблокирован. {unlock_hint}")
    errors = tuple(getattr(broadlink.exceptions, name) for name in ("AuthenticationError", "AuthorizationError")
                   if hasattr(broadlink.exceptions, name))
    try:
        device.auth()
    except errors as error:
        sys.exit(f"RM не пустил к себе ({error}). {unlock_hint}")
    print(f"RM: {describe(device)}")
    return broadlink, device


def learn_one(broadlink, device, timeout: int) -> bytes:
    device.enter_learning()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        time.sleep(0.5)
        try:
            return device.check_data()
        except (broadlink.exceptions.ReadError, broadlink.exceptions.StorageError):
            continue
    return b""


def slug(text: str) -> str:
    return re.sub(r"[^0-9A-Za-z_+-]+", "_", text).strip("_") or "command"


# --- команды --------------------------------------------------------------------

def cmd_discover(args) -> None:
    broadlink = import_broadlink()
    try:
        devices = discover(args)
    except broadlink.exceptions.NetworkTimeoutError:
        sys.exit(f"Устройство по адресу {args.host} не ответило.")
    if not devices:
        sys.exit("Устройства Broadlink не найдены. Попробуйте --local-ip <адрес компьютера в домашней сети>.")
    for device in devices:
        kind = "" if hasattr(device, "enter_learning") else "  (не ИК-пульт)"
        print(describe(device) + kind)


def cmd_list(args) -> None:
    devices = load_devices(args.config)
    if not args.device:
        for device in devices:
            print(f"{device['id']:>10}  {device['name']}  (команд: {len(device['commands'])})")
        return
    device = find_device(devices, args.device)
    print(f"{device['id']} {device['name']}")
    for name, (protocol, code) in device["commands"].items():
        print(f"  {name:<24} протокол {protocol:<3} {code}")


def record_buttons(args, title: str, prompt: str, buttons: list, prefix: str, extra) -> None:
    """Записывает кнопки по очереди; extra(имя) — поля записи, зависящие от источника."""
    broadlink, rm = connect(args)
    args.output.mkdir(parents=True, exist_ok=True)
    saved = 0
    for name in buttons:
        print(f"\n{title} — «{name}»: {prompt} (ждём {args.wait} с)…", flush=True)
        packet = learn_one(broadlink, rm, args.wait)
        if not packet:
            print("  Сигнал не пойман, пропускаю.")
            continue
        try:
            decoded = decode_packet(packet)
        except ValueError as error:
            print(f"  Не разобрал запись ({error}), сырые байты: {packet.hex()}")
            continue
        durations = decoded["durations"]
        record = {
            **extra(name),
            "command": name,
            "recorded": datetime.now().isoformat(timespec="seconds"),
            "rm": {"type": getattr(rm, "type", ""), "devtype": f"0x{rm.devtype:04X}"},
            "tickUs": TICK_US,
            "repeat": decoded["repeat"],
            "durations": durations,
            "packet": packet.hex(),
        }
        path = args.output / f"{prefix}-{slug(name)}.json"
        path.write_text(json.dumps(record, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        total_ms = sum(abs(d) for d in durations) / 1000
        print(f"  {len(durations)} длительностей, {total_ms:.1f} мс: {' '.join(map(str, durations[:8]))} …")
        print(f"  Сохранено: {path}", flush=True)
        saved += 1
    print(f"\nЗаписано {saved} из {len(buttons)}.")


def cmd_learn(args) -> None:
    device = find_device(load_devices(args.config), args.device)
    commands = [find_command(device, name) for name in args.commands]

    def extra(name: str) -> dict:
        protocol, code = device["commands"][name]
        return {"deviceId": device["id"], "device": device["name"], "protocol": protocol, "code": code}

    record_buttons(args, device["name"], "направьте Harmony на RM и коротко нажмите кнопку",
                   commands, device["id"], extra)


def cmd_learn_raw(args) -> None:
    """Запись родного пульта устройства: кода из конфигурации нет, тест разбирает сигнал сам."""
    record_buttons(args, args.remote, "направьте пульт на RM и коротко нажмите кнопку",
                   args.buttons, slug(args.remote), lambda _name: {"remote": args.remote})


def cmd_send_rc6(args) -> None:
    """RM4 излучает один кадр RC6 Mode 0 в ИК-приёмник Harmony."""
    _, rm = connect(args)
    durations = rc6_mode0(args.address, args.command, args.toggle)
    packet = encode_packet(durations)
    rm.send_data(packet)
    print(f"Отправлен RC6 Mode 0: адрес 0x{args.address:02X}, команда 0x{args.command:02X}, "
          f"toggle {args.toggle}; {len(durations)} длительностей, {len(packet)} байт")


def cmd_send_file(args) -> None:
    """Повторно отправляет ранее записанный RM4 пакет для проверки стенда."""
    packet = bytes.fromhex(json.loads(args.file.read_text(encoding="utf-8"))["packet"])
    _, rm = connect(args)
    rm.send_data(packet)
    print(f"Отправлен пакет {args.file.name}: {len(packet)} байт")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--config", type=Path, default=CONFIG, help="UserConfiguration.xml пульта")
    parser.add_argument("--host", help="адрес RM, если их несколько или поиск не находит")
    parser.add_argument("--local-ip", help="адрес компьютера в сети RM (при нескольких адаптерах)")
    parser.add_argument("--broadcast", help="широковещательный адрес сети RM, например 192.168.1.255")
    parser.add_argument("--timeout", type=int, default=5, help="ожидание ответа RM при поиске, с")
    sub = parser.add_subparsers(dest="action", required=True)

    sub.add_parser("discover", help="найти Broadlink в сети").set_defaults(run=cmd_discover)

    listing = sub.add_parser("list", help="устройства и команды конфигурации")
    listing.add_argument("device", nargs="?", help="Id или часть названия устройства")
    listing.set_defaults(run=cmd_list)

    learn = sub.add_parser("learn", help="записать команды с пульта Harmony")
    learn.add_argument("device", help="Id или часть названия устройства")
    learn.add_argument("commands", nargs="+", help="имена команд, как в list")
    learn.add_argument("--wait", type=int, default=LEARN_TIMEOUT_S, help="ожидание нажатия, с")
    learn.add_argument("--output", type=Path, default=OUTPUT, help="папка для записей")
    learn.set_defaults(run=cmd_learn)

    raw = sub.add_parser("learn-raw", help="записать кнопки родного пульта устройства (не Harmony)")
    raw.add_argument("remote", help="название пульта для имён файлов, например sony_RMT-D177P")
    raw.add_argument("buttons", nargs="+", help="названия кнопок (любые)")
    raw.add_argument("--wait", type=int, default=LEARN_TIMEOUT_S, help="ожидание нажатия, с")
    raw.add_argument("--output", type=Path, default=OUTPUT, help="папка для записей")
    raw.set_defaults(run=cmd_learn_raw)

    send = sub.add_parser("send-rc6", help="послать RC6 Mode 0 с RM4 на ИК-приёмник Harmony")
    send.add_argument("address", type=lambda value: int(value, 0), help="адрес 0..255, например 0xA5")
    send.add_argument("command", type=lambda value: int(value, 0), help="команда 0..255, например 0x5A")
    send.add_argument("--toggle", type=int, choices=(0, 1), default=0, help="бит переключения")
    send.set_defaults(run=cmd_send_rc6)

    replay = sub.add_parser("send-file", help="послать сохранённый пакет записи RM4")
    replay.add_argument("file", type=Path, help="JSON, сохранённый командой learn или learn-raw")
    replay.set_defaults(run=cmd_send_file)

    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
