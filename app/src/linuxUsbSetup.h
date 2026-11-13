#pragma once

/**
 * @file linuxUsbSetup.h
 * @brief Linux: однократная настройка связи с пультами Harmony по USB.
 *
 * Программа ставит правила udev и скрипт (packaging/linux/usbnet, встроены в ресурсы
 * ":/usbnet/"). Harmony 900 виден как сетевая карта и ждёт адрес по DHCP: при каждом
 * подключении компьютер получает адрес 169.254.1.1, а пульт — 169.254.1.2.
 * Harmony 600/650/665/700 — устройство USB HID: правило даёт пользователю за
 * компьютером доступ к /dev/hidraw* (тег uaccess).
 * Установка требует прав root, их запрашивает pkexec.
 */

#include <QString>

class QWidget;

namespace harmony::app::linuxUsb {

/// @return true, если программа умеет настраивать связь здесь (Linux, есть ресурсы).
[[nodiscard]] bool available();

/// @return true, если правило udev уже установлено.
[[nodiscard]] bool installed();

/// Команда для терминала, если pkexec нет или он не сработал.
[[nodiscard]] QString manualCommand();

/**
 * @brief Ставит (или убирает) настройку через pkexec и сообщает итог в окне.
 * @param parent Окно для сообщений.
 * @param uninstall true — убрать установленную настройку.
 */
void install(QWidget* parent, bool uninstall = false);

} // namespace harmony::app::linuxUsb
