#pragma once

/**
 * @file appSettings.h
 * @brief Настройки программы (QSettings): язык, запуск, связь с пультом.
 */

#include <QString>

namespace harmony::app::settings {

/// Что открывать при запуске, если конфигурация «при запуске» не выбрана.
enum class Startup {
    Choose, ///< Окно выбора конфигурации
    Last,   ///< Последнюю открытую
};

/// @return Код языка интерфейса: «en», «ru» или свой перевод (по умолчанию — язык системы).
[[nodiscard]] QString language();
void setLanguage(const QString& code);

[[nodiscard]] Startup startup();
void setStartup(Startup value);

/// @return Папка резервных копий (по умолчанию «Документы/Harmony/BackUp»).
[[nodiscard]] QString backupFolder();
/// @param folder Пусто — по умолчанию.
void setBackupFolder(const QString& folder);
[[nodiscard]] QString defaultBackupFolder();

/// @return Адрес пульта (по умолчанию 169.254.1.2).
[[nodiscard]] QString remoteHost();
void setRemoteHost(const QString& host);

/// @return Порт пульта (по умолчанию 3074).
[[nodiscard]] int remotePort();
void setRemotePort(int port);

/// @return Последний адрес Harmony Hub в локальной сети (пусто — ещё не вводился).
[[nodiscard]] QString hubHost();
void setHubHost(const QString& host);

/// @return Проверять подключение пульта каждые 10 секунд.
[[nodiscard]] bool autoProbe();
void setAutoProbe(bool on);

/// @return Сколько ждать сигнал родного пульта при обучении, с.
[[nodiscard]] int learnTimeout();
void setLearnTimeout(int seconds);

/// Оформление.
enum class ThemeMode {
    System, ///< Как в системе (светлое или тёмное)
    Light,
    Dark,
};

[[nodiscard]] ThemeMode themeMode();
void setThemeMode(ThemeMode mode);

// --- База ИК-кодов Logitech ----------------------------------------------------------

/// Файл архива базы.
enum class IrdbArchive {
    Devices,   ///< device_features_raw.jsonl.gz — устройства и команды
    Protocols, ///< AllProtocols.json.gz — ИК-протоколы
};

/**
 * @return Полный адрес файла архива. Имя скачанного файла — последняя часть адреса,
 *         поэтому другое имя в новом релизе — просто другой адрес.
 */
[[nodiscard]] QString irdbUrl(IrdbArchive archive);
void setIrdbUrl(IrdbArchive archive, const QString& url);
[[nodiscard]] QString defaultIrdbUrl(IrdbArchive archive);

/// @return Папка файлов архива: сюда скачиваются и отсюда собирается база (например, E:/DatabaseIR).
[[nodiscard]] QString irdbSourceFolder();
void setIrdbSourceFolder(const QString& folder);

/// @return Файл собранной базы (по умолчанию «Документы/Harmony/IRDatabase/harmony.irdb»).
[[nodiscard]] QString irdbFile();
void setIrdbFile(const QString& file);

/// @return Папка по умолчанию для базы и файлов архива: «Документы/Harmony/IRDatabase».
[[nodiscard]] QString defaultIrdbFolder();

/// @return Версия предупреждения о рисках, которую пользователь принял (0 — ещё не принимал).
[[nodiscard]] int acceptedDisclaimer();
void setAcceptedDisclaimer(int version);

/// Сбрасывает настройки этого окна к значениям по умолчанию (список конфигураций остаётся).
void reset();

} // namespace harmony::app::settings
