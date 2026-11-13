#!/bin/sh
# Связь с Harmony 900 по USB на Linux: адрес компьютера и DHCP для пульта.
#
# Пульт виден как сетевая карта (USB 046d:c11f) и ждёт адрес по DHCP; на Windows его
# выдаёт драйвер Logitech, на Linux — этот скрипт. Запускает его правило udev
# (99-harmony-usbnet.rules) через systemd-run при подключении пульта; работает, пока
# пульт подключён: при отключении интерфейс пропадает и dnsmasq завершается.
#
#   harmony-usbnet.sh <интерфейс>
set -eu

IFACE="${1:?usage: harmony-usbnet.sh <interface>}"
HOST_IP=169.254.1.1
REMOTE_IP=169.254.1.2

case "$IFACE" in
    *[!A-Za-z0-9_.-]*) echo "bad interface name: $IFACE" >&2; exit 2 ;;
esac

# NetworkManager иначе пытается настроить интерфейс сам и снимает наш адрес.
if command -v nmcli >/dev/null 2>&1; then
    nmcli device set "$IFACE" managed no 2>/dev/null || true
fi

ip link set "$IFACE" up
ip addr flush dev "$IFACE" 2>/dev/null || true
ip addr add "$HOST_IP/16" dev "$IFACE"

# Один адрес для одного пульта; DNS не нужен (--port=0), аренды не пишутся на диск.
exec dnsmasq --no-daemon --port=0 --interface="$IFACE" --bind-interfaces \
    --except-interface=lo --leasefile-ro \
    --dhcp-range="$REMOTE_IP,$REMOTE_IP,255.255.0.0,12h"
