#pragma once

/**
 * @file hubConfigReader.h
 * @brief Чтение конфигурации Harmony Hub по локальной сети без рутинга и облака.
 *
 * Стоковая прошивка Hub отвечает на порту 8088: HTTP-запрос getProvisionInfo
 * сообщает remoteId, затем по WebSocket команда engine?config возвращает
 * устройства, команды и активности всех пультов, привязанных к Hub (Elite, Ultimate,
 * Ultimate Home, 950, Companion, Smart Control, Pro 2400). ИК-коды в ответе отсутствуют:
 * только имена команд, производители и модели устройств.
 *
 * Реализация только читает. Запуск активностей, отправка ИК и изменение настроек
 * не выполняются.
 */

#include "harmony/transport/channel.h"
#include "harmony/transport/remoteLink.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace harmony::transport {

/// Порт локального протокола стокового Harmony Hub.
inline constexpr std::uint16_t kHubPort = 8088;

/// Устройство из конфигурации Hub — для сводки в CLI и окне.
struct HubDeviceSummary {
    std::string label;        ///< Имя устройства у пользователя
    std::string manufacturer; ///< Производитель
    std::string model;        ///< Модель
    std::string type;         ///< Тип устройства Logitech (Television, AVReceiver…)
    std::size_t commands = 0; ///< Число команд во всех группах controlGroup
};

/// Прочитанная конфигурация Hub и готовый файл для отправки.
struct HubConfig {
    std::string host;                       ///< Адрес, с которого прочитано (в файл не пишется)
    std::uint16_t port = kHubPort;          ///< Порт, с которого прочитано
    std::string remoteId;                   ///< activeRemoteId Hub
    std::string hubName;                    ///< Имя Hub
    std::string firmware;                   ///< Версия прошивки Hub
    std::vector<HubDeviceSummary> devices;  ///< Устройства
    std::vector<std::string> activities;    ///< Активности, без PowerOff
    std::string backupJson;                 ///< Файл *.hub.json (UTF-8), без email и accountId
};

/**
 * @brief Читает конфигурацию Hub через заданную фабрику каналов.
 *
 * Фабрика вызывается дважды: для HTTP-запроса provisioning и для WebSocket.
 * Перегрузка нужна тестам, чтобы подставить сценарий ответов вместо сети.
 * @param connect Создаёт новое TCP-соединение с Hub.
 * @param host Адрес Hub — для заголовка Host и текста ошибок.
 * @param port Порт Hub.
 * @param progress Прогресс: Stage::Connect (шаги), Stage::Read (байты ответа); false — отмена.
 * @return Конфигурация; ErrorCode::Io — Hub недоступен, ErrorCode::Remote — Hub отказал
 *         или закрыл соединение, ErrorCode::Format — неожиданный ответ, ErrorCode::Cancelled — отмена.
 */
[[nodiscard]] Result<HubConfig> readHubConfig(const ChannelFactory& connect, const std::string& host,
                                              std::uint16_t port, const ProgressHandler& progress = {});

/**
 * @brief Читает конфигурацию Hub по TCP.
 * @param host IP-адрес Hub в локальной сети.
 * @param port Порт (по умолчанию 8088).
 * @param progress Прогресс и отмена.
 * @return См. перегрузку с фабрикой каналов.
 */
[[nodiscard]] Result<HubConfig> readHubConfig(const std::string& host, std::uint16_t port = kHubPort,
                                              const ProgressHandler& progress = {});

/**
 * @brief Открывает ранее сохранённый файл *.hub.json и строит сводку.
 * @param json Содержимое файла.
 * @return Сводка с backupJson = исходный текст; ErrorCode::Format — не файл Hub или другая версия.
 */
[[nodiscard]] Result<HubConfig> openHubBackup(std::string_view json);

} // namespace harmony::transport
