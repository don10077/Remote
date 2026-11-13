#!/usr/bin/env python3
"""Обновляет переводы интерфейса app/resources/i18n/harmony_<язык>.ts по исходникам.

Строки интерфейса пишутся по-английски. Скрипт собирает их:
  - из app/src: tr("…"), QObject::tr("…"), QT_TR_NOOP("…"),
    QCoreApplication::translate("контекст", "…"), QT_TRANSLATE_NOOP("контекст", "…")
    (соседние литералы "…" "…" склеиваются, как в C++);
  - labels::plural(n, "device", "devices") — ключ «device|devices»
    (русский перевод — три формы через «|»: «устройство|устройства|устройств»);
  - в labels.cpp — названия из таблиц {"Ключ", "Название"};
  - шаблоны ошибок ядра и приложения: литералы в makeError(), wrapError(),
    Error(), fail(), say() и listMessage() (core/src, transport/src, app/src).
Готовые переводы сохраняются; новые строки добавляются пустыми
(type="unfinished"), исчезнувшие из кода — удаляются.

Запуск: python3 tools/updateTranslations.py [--report]
  --report  — показать литералы с кириллицей в коде (их быть не должно).
"""

import re
import sys
from pathlib import Path
from xml.etree import ElementTree as ET

ROOT = Path(__file__).resolve().parent.parent
SOURCES = ROOT / "app" / "src"
ERROR_SOURCES = [ROOT / "core" / "src", ROOT / "transport" / "src", SOURCES]
OUTPUT = ROOT / "app" / "resources" / "i18n"
LANGUAGES = {"ru": "ru_RU"}

LITERAL = r'"(?:[^"\\]|\\.)*"'
CHAR_LITERAL = re.compile(r"'(?:[^'\\\n]|\\.)'")
LITERALS = rf'{LITERAL}(?:\s*{LITERAL})*'  # соседние литералы склеиваются
TR = re.compile(rf'(?<![\w:])(?:QObject::|QCoreApplication::)?(?:tr|QT_TR_NOOP)\(\s*({LITERALS})')
TRANSLATE = re.compile(rf'(?:translate|QT_TRANSLATE_NOOP)\(\s*{LITERAL}\s*,\s*({LITERALS})')
PLURAL = re.compile(r'\bplural\(')
ERROR_CALL = re.compile(r"\b(?:makeError|wrapError|Error|fail|say|listMessage)\(")
CATALOG = ROOT / "core" / "src" / "propertyCatalog.cpp"
CATALOG_ENTRY = re.compile(r'(?<![\w:])(?:flag|number|choice)\((?=\s*[DTA]\s*,)')
TABLE_ENTRY =re.compile(rf'\{{\s*{LITERAL}\s*,\s*({LITERAL})\s*\}}')
CYRILLIC = re.compile('[А-Яа-яЁё]')
LETTER = re.compile('[A-Za-z]{2}')


def unescape(literal: str) -> str:
    """Текст C++-литерала (с кавычками) → строка."""
    body = literal[1:-1]
    out = []
    i = 0
    while i < len(body):
        c = body[i]
        if c == '\\' and i + 1 < len(body):
            nxt = body[i + 1]
            out.append({'n': '\n', 't': '\t', '"': '"', '\\': '\\', "'": "'"}.get(nxt, '\\' + nxt))
            i += 2
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def join_literals(text: str) -> str:
    return ''.join(unescape(m) for m in re.findall(LITERAL, text))


def blank_comments(code: str) -> str:
    """Комментарии → пробелы (позиции и переводы строк сохраняются), строки не трогаются."""
    out = list(code)
    i, n = 0, len(code)
    while i < n:
        if code.startswith('//', i):
            j = code.find('\n', i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = ' '
            i = j
        elif code.startswith('/*', i):
            j = code.find('*/', i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != '\n':
                    out[k] = ' '
            i = j
        elif code[i] == '"':
            i = re.compile(LITERAL).match(code, i).end()
        elif code[i] == "'":
            # Символьный литерал ('a', '\n'); иначе — разделитель разрядов (10'000).
            m = CHAR_LITERAL.match(code, i)
            i = m.end() if m else i + 1
        else:
            i += 1
    return ''.join(out)


def call_arguments(code: str, start: int):
    """Аргументы вызова от открывающей скобки: список (текст, начало, конец) верхнего уровня."""
    depth, i, arg_start, args = 0, start, start + 1, []
    while i < len(code):
        c = code[i]
        if c == '"':
            i = re.compile(LITERAL).match(code, i).end()
            continue
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
            if depth == 0:
                args.append((code[arg_start:i], arg_start, i))
                return args
        elif c == ',' and depth == 1:
            args.append((code[arg_start:i], arg_start, i))
            arg_start = i + 1
        i += 1
    return args


def collect():
    """Строки для перевода: текст → файлы, где встречается; и литералы с кириллицей."""
    found = {}
    cyrillic = []

    def add(text, rel):
        if text:
            found.setdefault(text, set()).add(rel)

    for path in sorted(SOURCES.rglob('*.cpp')) + sorted(SOURCES.rglob('*.h')):
        code = blank_comments(path.read_text(encoding='utf-8'))
        rel = path.relative_to(ROOT).as_posix()
        for regex in (TR, TRANSLATE):
            for m in regex.finditer(code):
                add(join_literals(m.group(1)), rel)
        for m in PLURAL.finditer(code):
            args = call_arguments(code, m.end() - 1)[1:3]
            forms = [re.fullmatch(rf'\s*({LITERAL})\s*', text) for text, _, _ in args]
            if len(forms) == 2 and all(forms):
                add(unescape(forms[0].group(1)) + '|' + unescape(forms[1].group(1)), rel)
        if path.name == 'labels.cpp':
            for m in TABLE_ENTRY.finditer(code):
                add(unescape(m.group(1)), rel)
    # Каталог свойств ядра: название и описание (3-й и 4-й аргументы flag/number/choice)
    # переводит окно свойств через tr().
    catalog = CATALOG.read_text(encoding='utf-8')
    catalog = blank_comments(catalog)
    for m in CATALOG_ENTRY.finditer(catalog):
        for text, _, _ in call_arguments(catalog, m.end() - 1)[2:4]:
            literal = re.fullmatch(rf'\s*({LITERALS})\s*', text)
            if literal:
                add(join_literals(literal.group(1)), CATALOG.relative_to(ROOT).as_posix())
    for path in [p for d in ERROR_SOURCES for p in sorted(d.rglob('*.cpp'))]:
        code = blank_comments(path.read_text(encoding='utf-8'))
        rel = path.relative_to(ROOT).as_posix()
        for m in ERROR_CALL.finditer(code):
            for text, _, _ in call_arguments(code, m.end() - 1):
                for group in re.finditer(LITERALS, text):
                    joined = join_literals(group.group(0))
                    if LETTER.search(joined) and ' ' in joined:
                        add(joined, rel)
    for directory in (SOURCES, ROOT / "core", ROOT / "transport", ROOT / "cli"):
        for path in sorted(directory.rglob('*')):
            if path.suffix not in ('.cpp', '.h') or not path.is_file():
                continue
            code = blank_comments(path.read_text(encoding='utf-8'))
            for m in re.finditer(LITERAL, code):
                if CYRILLIC.search(m.group(0)):
                    line = code.count('\n', 0, m.start()) + 1
                    cyrillic.append(f'{path.relative_to(ROOT).as_posix()}:{line}: {m.group(0)[:70]}')
    return found, cyrillic


def update(language: str, locale: str, strings: dict) -> tuple:
    path = OUTPUT / f'harmony_{language}.ts'
    old = {}
    if path.exists():
        for message in ET.parse(path).getroot().iter('message'):
            translation = message.find('translation')
            if translation is not None and translation.text and translation.get('type') != 'obsolete':
                old[message.findtext('source') or ''] = translation.text
    root = ET.Element('TS', version='2.1', language=locale, sourcelanguage='en_US')
    context = ET.SubElement(root, 'context')
    ET.SubElement(context, 'name').text = 'Harmony'
    done = 0
    for source in sorted(strings, key=str.lower):
        message = ET.SubElement(context, 'message')
        for file in sorted(strings[source]):
            ET.SubElement(message, 'location', filename='../../../' + file)
        ET.SubElement(message, 'source').text = source
        translation = ET.SubElement(message, 'translation')
        if source in old:
            translation.text = old[source]
            done += 1
        else:
            translation.set('type', 'unfinished')
    ET.indent(root)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    with open(path, 'w', encoding='utf-8', newline='\n') as out:
        out.write('<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>\n')
        out.write(ET.tostring(root, encoding='unicode'))
        out.write('\n')
    return done, len(strings)


def main():
    strings, cyrillic = collect()
    for language, locale in LANGUAGES.items():
        done, total = update(language, locale, strings)
        print(f'harmony_{language}.ts: переведено {done} из {total}')
    if '--report' in sys.argv:
        print('\nКириллица в коде:')
        print('\n'.join(cyrillic))
    elif cyrillic:
        print(f'Кириллица в коде: {len(cyrillic)} (подробно — с --report)')


if __name__ == '__main__':
    main()
