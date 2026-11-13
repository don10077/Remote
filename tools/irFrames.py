#!/usr/bin/env python3
"""Кадры ИК-протоколов для стенда RM4 → Harmony 900, которых не собирает harmony-cli encode.

Длительности — мкс, импульс положительный, пауза отрицательная (как в harmony-cli).
Тайминги — по описаниям протоколов (нотация IRP, IrpTransmogrifier) и Arduino IRremote;
на настоящих пультах эти генераторы не сверялись. RM4 излучает около 38 кГц при любом
протоколе, поэтому несущая здесь — только справка.

Запуск без аргументов печатает список кадров (проверка генераторов без RM4 и пульта).
"""

from __future__ import annotations

from dataclasses import dataclass, field


class Frame:
    """Длительности кадра; соседние импульсы (или паузы) складываются."""

    def __init__(self) -> None:
        self.d: list[int] = []

    def mark(self, us: float) -> None:
        self._add(round(us))

    def space(self, us: float) -> None:
        self._add(-round(us))

    def _add(self, v: int) -> None:
        if v == 0:
            return
        if self.d and (self.d[-1] > 0) == (v > 0):
            self.d[-1] += v
        else:
            self.d.append(v)

    def elapsed(self) -> int:
        return sum(abs(v) for v in self.d)

    def gap_to(self, start: int, period: int, min_gap: int) -> None:
        """Пауза до длины кадра period от start, но не короче min_gap."""
        self.space(max(min_gap, period - (self.elapsed() - start)))

    def pulse_distance(self, bits: list[int], mark: int, zero: int, one: int) -> None:
        for b in bits:
            self.mark(mark)
            self.space(one if b else zero)

    def biphase(self, bits: list[int], half: int, one_mark_first: bool = True) -> None:
        for b in bits:
            first_mark = bool(b) == one_mark_first
            if first_mark:
                self.mark(half)
                self.space(half)
            else:
                self.space(half)
                self.mark(half)


def lsb(value: int, n: int) -> list[int]:
    return [(value >> i) & 1 for i in range(n)]


def msb(value: int, n: int) -> list[int]:
    return [(value >> i) & 1 for i in reversed(range(n))]


# --- Протоколы, которые программа распознаёт, но не собирает --------------------

def rc6(mode: int, bits: list[int], toggle: int = 0, period: int = 107000) -> list[int]:
    """RC6 любого режима: ведущий 2666/889, старт, 3 бита режима, двойной toggle, данные (1 — импульс, пауза)."""
    f = Frame()
    f.mark(2666)
    f.space(889)
    f.biphase([1] + msb(mode, 3), 444)
    f.biphase([toggle], 888)
    f.biphase(bits, 444)
    f.gap_to(0, period, 2666)
    return f.d


def kaseikyo(vendor: int, address: int, command: int) -> list[int]:
    """Kaseikyo, 48 бит: производитель, его чётность, 12 бит адреса, команда, проверка (как irEncode.cpp)."""
    folded = vendor ^ (vendor >> 8)
    parity = (folded ^ (folded >> 4)) & 0xF
    b2 = parity | ((address & 0xF) << 4)
    b3 = (address >> 4) & 0xFF
    bits = lsb(vendor, 16) + lsb(b2, 8) + lsb(b3, 8) + lsb(command, 8) + lsb((b2 ^ b3 ^ command) & 0xFF, 8)
    f = Frame()
    f.mark(3456)
    f.space(1728)
    f.pulse_distance(bits, 432, 432, 1296)
    f.mark(432)
    f.gap_to(0, 130000, 74000)
    return f.d


# --- Протоколы, которых программа не знает ---------------------------------------

def nec_frame(f: Frame, bits: list[int], unit: int = 564) -> None:
    start = f.elapsed()
    f.mark(16 * unit)
    f.space(8 * unit)
    f.pulse_distance(bits, unit, unit, 3 * unit)
    f.mark(unit)
    f.gap_to(start, 108000, 20000)


def pioneer_two_frames(d1: int, f1: int, d2: int, f2: int) -> list[int]:
    """Pioneer: NEC на 40 кГц; у многих кнопок два кадра с разными адресами."""
    f = Frame()
    for d, c in ((d1, f1), (d2, f2)):
        nec_frame(f, lsb(d, 8) + lsb(~d & 0xFF, 8) + lsb(c, 8) + lsb(~c & 0xFF, 8))
    return f.d


def sanyo_lc7461(address: int, command: int) -> list[int]:
    """Sanyo LC7461 (NEC42): 9000/4500, 13 бит адреса, его инверсия, команда, её инверсия; затем повтор."""
    f = Frame()
    bits = lsb(address, 13) + lsb(~address & 0x1FFF, 13) + lsb(command, 8) + lsb(~command & 0xFF, 8)
    f.mark(16 * 562)
    f.space(8 * 562)
    f.pulse_distance(bits, 562, 562, 1687)
    f.mark(562)
    f.space(42 * 562)
    f.mark(16 * 562)  # код повтора
    f.space(4 * 562)
    f.mark(562)
    f.space(173 * 562)
    return f.d


def mitsubishi16(address: int, command: int) -> list[int]:
    """Mitsubishi (старые ТВ): без заголовка, импульс 300, паузы 900/2100, 16 бит; два кадра."""
    f = Frame()
    for _ in range(2):
        f.pulse_distance(lsb(address, 8) + lsb(command, 8), 300, 900, 2100)
        f.mark(300)
        f.space(80 * 300)
    return f.d


def rc_mm(value: int, bits: int = 24) -> list[int]:
    """Philips RC-MM: заголовок 417/278, символ — 2 бита: импульс 167, пауза 278/444/611/778."""
    f = Frame()
    f.mark(417)
    f.space(278)
    spaces = (278, 444, 611, 778)
    for shift in range(bits - 2, -1, -2):
        f.mark(167)
        f.space(spaces[(value >> shift) & 3])
    f.mark(167)
    f.gap_to(0, 27778, 6000)
    return f.d


def xmp1(subdevice: int, oem: int, device: int, function: int) -> list[int]:
    """Motorola XMP-1: полубайт — импульс 210 и пауза 760 + 136·n; два полукадра по 8 полубайт."""
    def checksum(nibbles: list[int]) -> int:
        return -sum(nibbles) & 0xF

    def nibbles_of(value: int, count: int) -> list[int]:
        return [(value >> (4 * i)) & 0xF for i in reversed(range(count))]

    s_hi, s_lo = (subdevice >> 4) & 0xF, subdevice & 0xF
    first_rest = [s_lo, 15] + nibbles_of(oem, 2) + nibbles_of(device, 2)
    first = [s_hi, checksum([s_hi] + first_rest)] + first_rest
    toggle = 0
    second_rest = [toggle, s_lo] + nibbles_of(function, 4)
    second = [s_hi, checksum([s_hi] + second_rest)] + second_rest
    f = Frame()
    for half, gap in ((first, 13800), (second, 80400)):
        for n in half:
            f.mark(210)
            f.space(760 + 136 * n)
        f.mark(210)
        f.space(gap)
    return f.d


def nokia_nrc17(address: int, command: int, subcode: int = 0) -> list[int]:
    """Nokia NRC17: предымпульс 500/2500, 17 бифазных бит по 1 мс (старт, команда 8, адрес 4, подкод 4)."""
    f = Frame()
    f.mark(500)
    f.space(2500)
    f.biphase([1] + lsb(command, 8) + lsb(address, 4) + lsb(subcode, 4), 500)
    f.gap_to(0, 100000, 20000)
    return f.d


def dish(address: int, command: int, subdevice: int = 0) -> list[int]:
    """Dish Network (57,6 кГц): 400/6000, 16 бит: команда 6 (старшим вперёд), подустройство 5, адрес 5."""
    f = Frame()
    f.mark(400)
    f.space(6000)
    bits = msb(command, 6) + lsb(subdevice, 5) + lsb(address, 5)
    for _ in range(3):
        f.pulse_distance(bits, 400, 2800, 1600)
        f.mark(400)
        f.space(6000)
    f.space(20000)
    return f.d


def directv(device: int, command: int) -> list[int]:
    """DirecTV: заголовок 6000/1200, символ — 2 бита (импульс 600/1200, пауза 600/1200), проверка 4 бита."""
    check = (7 * ((command >> 6) & 3) + 5 * ((command >> 4) & 3) + 3 * ((command >> 2) & 3) + (command & 3)) & 0xF
    value = (device & 0xF) << 12 | (command & 0xFF) << 4 | check
    f = Frame()
    f.mark(6000)
    f.space(1200)
    for shift in range(14, -1, -2):
        symbol = (value >> shift) & 3
        f.mark(1200 if symbol & 2 else 600)
        f.space(1200 if symbol & 1 else 600)
    f.mark(600)
    f.space(30000)
    return f.d


def bose(command: int) -> list[int]:
    """Bose Wave (IRremote): 1060/1450, импульс 534, паузы 468/1468, команда и её инверсия."""
    f = Frame()
    f.mark(1060)
    f.space(1450)
    f.pulse_distance(lsb(command, 8) + lsb(~command & 0xFF, 8), 534, 468, 1468)
    f.mark(534)
    f.space(50000)
    return f.d


def samsung36(address: int, command: int) -> list[int]:
    """Samsung36 (BD-плееры): 4500/4500, 16 бит, разрыв 500/4500, ещё 20 бит (4 расширения, команда, ~команда)."""
    f = Frame()
    f.mark(4500)
    f.space(4500)
    f.pulse_distance(lsb(address, 16), 500, 500, 1500)
    f.mark(500)
    f.space(4500)
    f.pulse_distance(lsb(0, 4) + lsb(command, 8) + lsb(~command & 0xFF, 8), 500, 500, 1500)
    f.mark(500)
    f.gap_to(0, 108000, 20000)
    return f.d


def thomson(address: int, command: int, toggle: int = 0) -> list[int]:
    """Thomson/RCA Europe (33 кГц): без заголовка, импульс 500, паузы 2000/4500, 12 бит."""
    f = Frame()
    for _ in range(2):
        start = f.elapsed()
        f.pulse_distance(lsb(address, 4) + [toggle] + lsb(command, 7), 500, 2000, 4500)
        f.mark(500)
        f.gap_to(start, 80000, 20000)
    return f.d


def konka(address: int, command: int) -> list[int]:
    """Konka: 3000/3000, импульс 500, паузы 1500/2500, 16 бит старшим вперёд, хвост 500/4000/500."""
    f = Frame()
    f.mark(3000)
    f.space(3000)
    f.pulse_distance(msb(address, 8) + msb(command, 8), 500, 1500, 2500)
    f.mark(500)
    f.space(4000)
    f.mark(500)
    f.space(23000)
    return f.d


def mitsubishi_ac() -> list[int]:
    """Кондиционер Mitsubishi Electric: 3400/1750, 18 байт (144 бита), импульс 450, паузы 420/1300; дважды."""
    payload = [0x23, 0xCB, 0x26, 0x01, 0x00, 0x20, 0x08, 0x06, 0x30, 0x45, 0x67,
               0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
    payload.append(sum(payload) & 0xFF)
    bits = [b for byte in payload for b in lsb(byte, 8)]
    f = Frame()
    for _ in range(2):
        f.mark(3400)
        f.space(1750)
        f.pulse_distance(bits, 450, 420, 1300)
        f.mark(450)
        f.space(17100)
    return f.d


# --- Набор для стенда -------------------------------------------------------------

@dataclass
class Case:
    name: str
    group: str                 # «decoded» — программа должна узнать; «unknown» — протокола нет в программе
    carrier: int               # Несущая протокола, Гц (RM4 всё равно излучает ~38 кГц)
    durations: list[int]
    expect: tuple[str, int, int] | None = None
    note: str = ""
    extra: dict = field(default_factory=dict)


def cases() -> list[Case]:
    return [
        # Распознаются, но собрать их программа не умеет — стенд раньше их не проверял.
        Case("RC6A (MCE 0x800F)", "decoded", 36000, rc6(6, msb(0x800F, 16) + msb(0x04, 8) + msb(0x0C, 8)),
             ("RC6A", 0x0F04, 0x0C), "адрес — 24 бита без старшего: 0x800F04 & 0x7FFFFF"),
        Case("Kaseikyo Denon 0x3254", "decoded", 37000, kaseikyo(0x3254, 0x123, 0x45), ("Denon (Kaseikyo)", 0x123, 0x45)),
        Case("Kaseikyo JVC 0x0103", "decoded", 37000, kaseikyo(0x0103, 0x0A1, 0x2C), ("JVC (Kaseikyo)", 0x0A1, 0x2C)),
        Case("Kaseikyo Sharp 0x5AAA", "decoded", 37000, kaseikyo(0x5AAA, 0x3F0, 0x81), ("Sharp (Kaseikyo)", 0x3F0, 0x81)),
        Case("Kaseikyo Mitsubishi 0xCB23", "decoded", 37000, kaseikyo(0xCB23, 0x007, 0x19),
             ("Mitsubishi (Kaseikyo)", 0x007, 0x19)),
        Case("Kaseikyo 0x1234 (other vendor)", "decoded", 37000, kaseikyo(0x1234, 0x456, 0x78), ("Kaseikyo", 0x456, 0x78)),
        Case("Samsung36", "decoded", 38000, samsung36(0x0400, 0x5A), ("Samsung36", 0x0400, 0x5A)),
        Case("Konka", "decoded", 38000, konka(0x02, 0x4F), ("Konka", 0x02, 0x4F)),
        Case("RC6 Mode 6, 20 bits (Sky)", "decoded", 36000, rc6(6, msb(0x0C, 8) + msb(0x0, 4) + msb(0x5C, 8)),
             ("RC6-6-20", 0x0C0, 0x5C), "адрес — все биты, кроме последних 8: устройство 0x0C, подустройство 0"),
        Case("RC6 Mode 6, 24 bits", "decoded", 36000, rc6(6, msb(0x1234, 16) + msb(0x56, 8)), ("RC6-6-24", 0x1234, 0x56)),
        # Протоколы, которых в программе нет: ожидается «не распознан»; «узнан как …» — ошибка декодера.
        Case("Pioneer (2 frames)", "unknown", 40000, pioneer_two_frames(0xA5, 0x10, 0xA8, 0x1C),
             note="NEC на 40 кГц; без несущей неотличим от NEC, второй кадр теряется"),
        Case("Sanyo LC7461 (NEC42)", "unknown", 38000, sanyo_lc7461(0x1A5, 0x33)),
        Case("Mitsubishi 16", "unknown", 32600, mitsubishi16(0x47, 0x2B)),
        Case("RC-MM 24", "unknown", 36000, rc_mm(0x3A5C7E), note="импульсы 167 мкс — проверка разрешения приёмника"),
        Case("XMP-1", "unknown", 38000, xmp1(0x0E, 0x44, 0x2A, 0x0B5C)),
        Case("Nokia NRC17", "unknown", 38000, nokia_nrc17(0x4, 0x2D)),
        Case("Dish Network", "unknown", 57600, dish(0x01, 0x15)),
        Case("DirecTV", "unknown", 38000, directv(0x0C, 0xA6)),
        Case("Bose Wave", "unknown", 38000, bose(0x4D)),
        Case("Thomson 12", "unknown", 33000, thomson(0x3, 0x2E)),
        Case("AC Mitsubishi Electric (144 bits ×2)", "unknown", 38000, mitsubishi_ac(),
             note="длинный кадр: сколько длительностей Harmony 900 принимает при обучении"),
    ]


def main() -> None:
    for case in cases():
        marks = sum(1 for v in case.durations if v > 0)
        shortest = min(abs(v) for v in case.durations)
        total = sum(abs(v) for v in case.durations) / 1000
        print(f"{case.name:<40} {case.group:<8} {len(case.durations):>4} длит., {marks:>3} имп., "
              f"мин. {shortest:>4} мкс, {total:7.1f} мс")


if __name__ == "__main__":
    main()
