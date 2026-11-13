#!/usr/bin/env bash
# Короткий прогон всех фаззеров: fuzz/run.sh <каталог сборки> [секунд на цель]
#   например: fuzz/run.sh build/fuzz 60
#
# Начальный корпус — файлы из tests/data (только читаются). Новые входы libFuzzer
# пишет в <каталог сборки>/fuzz-corpus/<цель>, падения — в <каталог сборки>/fuzz-crashes.

set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Использование: $0 <каталог сборки> [секунд на цель]" >&2
    exit 2
fi
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(realpath "$1")"
seconds="${2:-60}"
data="$root/tests/data"
seeds="$build/fuzz-seeds"
crashes="$build/fuzz-crashes"
mkdir -p "$seeds" "$crashes"

# --- Начальные корпуса ------------------------------------------------------
mkdir -p "$seeds/ezhex" "$seeds/zip" "$seeds/irtables" "$seeds/usbnet"
cp "$data"/ezhex/*.EZHex "$seeds/ezhex/"
# Для zip — ZIP из .EZHex: всё, начиная с первой сигнатуры «PK\3\4».
for f in "$data"/ezhex/*.EZHex; do
    python3 -I -c 'import sys; b = open(sys.argv[1], "rb").read(); open(sys.argv[2], "wb").write(b[b.index(b"PK\x03\x04"):])' \
        "$f" "$seeds/zip/$(basename "$f").zip"
done
cp "$data"/irtables/*.bin "$data"/userconfig/*.bin "$seeds/irtables/"
# usbnet: ответ GET_SYSTEM_INFO (статус 1) с одним двухбайтовым параметром (заголовок, длина, данные).
printf '\x10\x61\x01\x01\x02\x00\x0f' > "$seeds/usbnet/systemInfo.bin"

# --- Прогон -------------------------------------------------------------------
status=0
for target in ezhex zip irtables usbnet; do
    corpus="$build/fuzz-corpus/$target"
    mkdir -p "$corpus"
    echo "--- fuzz_$target: $seconds с ---"
    "$build/fuzz/fuzz_$target" \
        -max_total_time="$seconds" -timeout=10 -rss_limit_mb=2048 -print_final_stats=1 \
        -artifact_prefix="$crashes/$target-" \
        "$corpus" "$seeds/$target" || status=1
done
exit "$status"
