#pragma once

/**
 * @file remoteLink.h
 * @brief Связь с пультом: опознание, чтение и запись конфигурации.
 *
 * Harmony 900 виден на ПК как сетевая карта (USB-Ethernet) и отвечает по
 * адресу 169.254.1.2. На Windows для этого нужен драйвер из Logitech Harmony
 * Remote Software, на Linux — настройка сети (см. README).
 */

#include "harmony/core/error.h"
#include "harmony/core/formats/ezhex.h"
#include "harmony/core/ir/irSignal.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace harmony::transport {

/// Адрес пульта в USB-сети по умолчанию.
inline constexpr const char* kDefaultRemoteHost = "169.254.1.2";

/// Параметры подключения к пульту.
struct Endpoint {
    std::string host = kDefaultRemoteHost; ///< IP-адрес пульта
    std::uint16_t port = 3074;             ///< Порт протокола
};

/// Сведения о подключённом пульте.
struct RemoteInfo {
    std::string model;                ///< Модель («Harmony 900» или «неизвестная модель (skin N)»)
    formats::RemoteIdentity identity; ///< Архитектура и модель — для сверки с конфигурацией
    std::string firmwareVersion;      ///< Версия прошивки («5.9»)
    std::string serial;               ///< Серийный номер (GUID пульта)
    std::uint32_t configSize = 0;     ///< Размер конфигурации на пульте, байт
};

/// Этап длительной операции — для подписи в окне прогресса.
enum class Stage { Connect, Read, Write, Verify, Restart, SetTime, Learn };

/**
 * @brief Обработчик прогресса: этап, выполнено и всего (байты или шаги).
 * @return false — отменить операцию (там, где это ещё безопасно).
 */
using ProgressHandler = std::function<bool(Stage stage, std::size_t done, std::size_t total)>;

/**
 * @brief Канал связи с пультом.
 *
 * Все операции синхронные и занимают секунды: вызывать их нужно из рабочего
 * потока, а не из потока интерфейса.
 */
class RemoteLink {
public:
    virtual ~RemoteLink() = default;

    RemoteLink(const RemoteLink&) = delete;
    RemoteLink& operator=(const RemoteLink&) = delete;

    /**
     * @brief Подключается к пульту и опознаёт его.
     * @return Сведения о пульте; ErrorCode::Io — пульт не найден, ErrorCode::Remote — неожиданный ответ.
     */
    [[nodiscard]] virtual Result<RemoteInfo> connect(const Endpoint& endpoint) = 0;

    /// Закрывает соединение. Безопасно вызывать повторно.
    virtual void disconnect() = 0;

    /// @return true, если соединение установлено.
    [[nodiscard]] virtual bool isConnected() const = 0;

    /**
     * @brief Читает конфигурацию с пульта.
     * @return Содержимое файла .EZHex (как у резервной копии concordance).
     */
    [[nodiscard]] virtual Result<std::vector<std::uint8_t>> readConfig(const ProgressHandler& progress) = 0;

    /**
     * @brief Записывает конфигурацию на пульт, перезапускает его и выставляет часы.
     * @param ezhex Содержимое .EZHex; модель в заголовке должна совпадать с пультом.
     * @return ErrorCode::Unsupported — конфигурация для другой модели пульта.
     * @warning Перед записью вызывающий код обязан сохранить резервную копию.
     */
    [[nodiscard]] virtual Result<void> writeConfig(const std::vector<std::uint8_t>& ezhex,
                                                   const ProgressHandler& progress) = 0;

    /**
     * @brief Учит ИК-команду: ждёт сигнал родного пульта, направленного на Harmony.
     *
     * Пульт слушает несколько секунд за заход; заходы повторяются, пока не придёт
     * сигнал, не истечёт @p timeout или обработчик прогресса не вернёт false.
     * Прогресс — Stage::Learn: прошло и всего (секунды).
     * @return Сигнал; ErrorCode::Cancelled — отменено или сигнала не было.
     */
    [[nodiscard]] virtual Result<ir::IrSignal> learnIr(const ProgressHandler& progress,
                                                       std::chrono::seconds timeout) = 0;

protected:
    RemoteLink() = default;
};

} // namespace harmony::transport
