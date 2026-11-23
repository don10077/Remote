"""Проверка пакетов Qt для CI без установки Qt; только стандартная библиотека Python."""

import argparse
import json
from pathlib import Path
import re
import sys
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
MIRRORS = ("https://download.qt.io", "https://qt.mirror.constant.com")


def validate_packages(xml, version, arch, archives):
    """Проверить базовый пакет архитектуры и архивы внутри него, не addon-пакеты."""
    package = f"qt.qt{version[0]}.{version.replace('.', '')}.{arch}"
    root = ET.fromstring(xml)
    for item in root.findall("PackageUpdate"):
        if item.findtext("Name") == package:
            available = {
                name.strip().split("-", 1)[0]
                for name in item.findtext("DownloadableArchives", "").split(",")
            }
            # Официальный Linux Qt собран с собственной версией ICU. Системная
            # libicu Ubuntu может иметь другой ABI: rcc не запустится без этого архива.
            if "icu" in available and "icu" not in archives:
                raise ValueError(f"{package}: требуется архив icu для запуска утилит Qt")
            missing = set(archives) - available
            if missing:
                raise ValueError(f"{package}: отсутствуют архивы {sorted(missing)}")
            return
    raise ValueError(f"Пакет {package} не найден")


def fetch_metadata(path):
    """Загрузить метаданные; зеркало позволяет обойти региональные ограничения Qt."""
    errors = []
    for mirror in MIRRORS:
        url = f"{mirror}/online/qtsdkrepository/{path}/Updates.xml"
        try:
            with urllib.request.urlopen(url, timeout=15) as response:
                return response.read()
        except (OSError, urllib.error.URLError) as error:
            errors.append(f"{url}: {error}")
    raise ValueError("Не удалось проверить Qt:\n" + "\n".join(errors))


def main():
    """Проверить все платформы локально или одну в CI и выдать параметры action."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", choices=("linux", "mac"))
    parser.add_argument("--github-output", type=Path)
    args = parser.parse_args()
    config = json.loads((ROOT / ".github/qt-packages.json").read_text(encoding="utf-8"))
    version = config["version"]
    base_archives = config["archives"]
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("Нужна точная версия Qt major.minor.patch")
    if args.github_output and not args.host:
        parser.error("--github-output требует --host")
    for host, platform in config["platforms"].items():
        if args.host and host != args.host:
            continue
        archives = base_archives + platform.get("extra_archives", [])
        if not archives or any(not re.fullmatch(r"(?:qt[a-z0-9]+|icu)", a) for a in archives):
            raise ValueError("Нужен непустой список имён архивов Qt/ICU")
        compact = version.replace(".", "")
        path = f"{platform['repository']}/desktop/qt{version[0]}_{compact}/qt{version[0]}_{compact}"
        validate_packages(fetch_metadata(path), version, platform["arch"], archives)
        print(f"OK: Qt {version}, {host}/{platform['arch']}: {' '.join(archives)}")
    if args.github_output:
        with args.github_output.open("a", encoding="utf-8") as output:
            output.write(f"version={version}\narchives={' '.join(archives)}\n")
            output.write(f"host={args.host}\narch={config['platforms'][args.host]['arch']}\n")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, OSError, ET.ParseError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
