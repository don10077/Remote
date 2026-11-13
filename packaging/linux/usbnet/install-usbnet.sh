#!/bin/sh
# Ставит доступ к пультам Harmony по USB (запускать от root: pkexec или sudo):
#   - Harmony 900: адрес и DHCP при подключении (99-harmony-usbnet.rules + harmony-usbnet.sh);
#   - Harmony 600/650/665/700: чтение по USB HID без root (70-harmony-hid.rules).
#   install-usbnet.sh            — поставить (или обновить)
#   install-usbnet.sh --remove   — убрать
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
SCRIPT=/usr/local/libexec/harmony-usbnet.sh
RULE=/etc/udev/rules.d/99-harmony-usbnet.rules
HID_RULE=/etc/udev/rules.d/70-harmony-hid.rules

if [ "$(id -u)" -ne 0 ]; then
    echo "run as root (pkexec or sudo)" >&2
    exit 1
fi

if [ "${1:-}" = "--remove" ]; then
    rm -f "$RULE" "$SCRIPT" "$HID_RULE"
    udevadm control --reload-rules
    echo "removed"
    exit 0
fi

# Правило HID нужно и без dnsmasq: ставим его первым.
install -D -m 0644 -o root -g root "$HERE/70-harmony-hid.rules" "$HID_RULE"
udevadm control --reload-rules
# Уже подключённый HID-пульт получает доступ сразу (change безопасен: ничего не запускает).
udevadm trigger --action=change --subsystem-match=hidraw 2>/dev/null || true

for tool in dnsmasq ip systemd-run udevadm; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "HID access installed; for the Harmony 900 missing: $tool (install it, e.g. sudo apt install dnsmasq-base)" >&2
        exit 3
    fi
done

install -D -m 0755 -o root -g root "$HERE/harmony-usbnet.sh" "$SCRIPT"
install -D -m 0644 -o root -g root "$HERE/99-harmony-usbnet.rules" "$RULE"
udevadm control --reload-rules
# Правило 900 срабатывает при подключении: уже подключённый пульт нужно переподключить.
echo "installed"
