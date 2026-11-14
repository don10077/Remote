#pragma once

/**
 * @file hubConfig.h
 * @brief Разбор конфигурации Harmony Hub: устройства, команды и активности.
 *
 * Принимаются три источника одного и того же набора данных:
 *  - `*.hub.json` нашего чтения по сети (ответ `engine?config` стокового хаба):
 *    имена устройств, моделей и команд, активности. **ИК-кодов в нём нет.**
 *  - Родные файлы ресурсов хаба `DeviceList.json`, `ActivityList.json`
 *    (`DevicesWithFeatures`, `Activities`): в командах есть `KeyCode`,
 *    то есть протокол и код. Доступны с рутованного хаба.
 *  - Резервная копия Melodious (`harmony-hub-control`): объект, ключи которого —
 *    имена тех же файлов.
 *
 * Разбор только читает: ничего не записывается ни на хаб, ни в конфигурацию пульта.
 * Структуры намеренно не совпадают с моделью редактора Harmony 900: это исходные
 * данные хаба, а не документ редактора.
 */

#include "harmony/core/error.h"
#include "harmony/core/model/remoteConfig.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace harmony::formats {

/// Как хаб отправляет команду (`TransportType` в файлах хаба).
enum class HubTransport {
    Unknown,       ///< Не указан или неизвестный код
    Ir,            ///< 1 — инфракрасный сигнал
    Bluetooth,     ///< 32 — клавиатура или пульт Bluetooth HID
    HomeAssistant, ///< 64 — устройство Home Assistant (Melodious)
};

/// Команда устройства хаба.
struct HubCommand {
    std::string name;      ///< Имя команды («PowerOn», «VolumeUp»)
    HubTransport transport = HubTransport::Unknown; ///< Канал отправки
    int transportCode = 0; ///< Исходный TransportType, в том числе неизвестный
    std::string protocol;  ///< Имя протокола из KeyCode («Toshiba 32 Bit»), как у Logitech
    std::string code;      ///< Первая непустая группа KeyCode («0x20DF3AC5»)
    std::string keyCode;   ///< Исходная строка KeyCode целиком
    bool repeat = false;   ///< В KeyCode есть признак «Repeat»
    bool learned = false;  ///< Команда выучена с родного пульта (IsLearned)

    /// @return true, если известны протокол и код, то есть команду можно воспроизвести.
    [[nodiscard]] bool hasCode() const { return !protocol.empty() && !code.empty(); }
};

/// Устройство, которым управляет хаб.
struct HubDevice {
    std::string id;           ///< Идентификатор устройства в конфигурации хаба
    std::string label;        ///< Имя, которое видит пользователь
    std::string manufacturer; ///< Производитель («LG», «NVIDIA»)
    std::string model;        ///< Модель; у части устройств пусто
    std::string type;         ///< Тип текстом («Television»); пусто — только код типа
    int typeCode = 0;         ///< Числовой DeviceType Logitech; 0 — не указан
    std::vector<HubCommand> commands; ///< Команды всех групп controlGroup

    /// @return Сколько команд пришло с протоколом и кодом.
    [[nodiscard]] std::size_t commandsWithCodes() const;
    /// @return Имя для показа: подпись, иначе «производитель модель».
    [[nodiscard]] std::string displayName() const;
};

/// Участие устройства в активности.
struct HubActivityRole {
    std::string role;     ///< Роль Logitech («DisplayActivityRole»); пусто — не указана
    std::string deviceId; ///< Устройство
    std::string input;    ///< Вход, на который активность переключает («HDMI 1»); может быть пусто
};

/// Активность хаба («Смотреть ТВ»).
struct HubActivity {
    std::string id;    ///< Идентификатор («-1» — «Выключить всё»)
    std::string label; ///< Имя, которое видит пользователь
    std::string type;  ///< Тип активности; пусто — не указан
    int order = 0;     ///< Порядок на экране пульта
    std::vector<HubActivityRole> roles; ///< Устройства активности и их входы

    /// @return true для служебной активности «Выключить всё».
    [[nodiscard]] bool isPowerOff() const { return id == "-1" || type == "PowerOff"; }
};

/// Кнопка из MapList.json: аппаратная клавиша или экранная кнопка с командой устройства.
struct HubButton {
    std::string key;       ///< Аппаратная клавиша («Menu», «VolumeUp»); пусто — экранная кнопка
    std::string label;     ///< Подпись экранной кнопки (TextOnRemote)
    std::string deviceId;  ///< Устройство, которому уходит команда
    std::string command;   ///< Имя команды
    bool longPress = false; ///< Команда выполняется при долгом нажатии (ButtonLongPressAction)
};

/// Раскладка кнопок одного пульта в одной активности («16422Activity101»).
struct HubButtonMap {
    std::string identifier;   ///< ButtonMapIdentifier: «<EquadID>Activity<id>» или «<EquadID>Root»
    std::string activityId;   ///< Активность; пусто — корневая раскладка
    std::vector<HubButton> buttons; ///< Только кнопки с командой устройства
    int skipped = 0;          ///< Кнопки с другим действием (активность, последовательность) — не перенесены
};

/// Откуда взяты данные — от этого зависит, есть ли в них ИК-коды.
enum class HubConfigSource {
    NetworkConfig, ///< Ответ engine?config: ИК-кодов нет
    Resources,     ///< Родные файлы ресурсов хаба: ИК-коды есть
};

/// Разобранная конфигурация хаба.
struct HubConfiguration {
    HubConfigSource source = HubConfigSource::NetworkConfig;
    std::string hubName;   ///< Имя хаба; может быть пусто
    std::string firmware;  ///< Версия прошивки хаба; может быть пусто
    std::string remoteId;  ///< Идентификатор хаба; может быть пусто
    std::string createdUtc; ///< Когда снята копия; может быть пусто
    std::vector<HubDevice> devices;
    std::vector<HubActivity> activities;
    /// Раскладки кнопок из MapList.json. Формат взят из описаний и синтетических примеров
    /// harmony-hub-control, на файле с настоящего хаба не проверялся.
    std::vector<HubButtonMap> buttonMaps;
    std::vector<std::string> warnings; ///< Что пропущено при разборе

    /// @return Общее число команд всех устройств.
    [[nodiscard]] std::size_t commandCount() const;
    /// @return Из них — с протоколом и кодом.
    [[nodiscard]] std::size_t commandsWithCodes() const;
};

/**
 * @brief Разбирает конфигурацию хаба любого из поддержанных видов.
 * @param json Содержимое файла в UTF-8.
 * @return Конфигурация; ErrorCode::Format — не JSON или не конфигурация хаба,
 *         ErrorCode::Unsupported — неизвестная версия нашего файла.
 */
[[nodiscard]] Result<HubConfiguration> parseHubConfig(std::string_view json);

/**
 * @brief Разбирает KeyCode команды хаба.
 *
 * Строка вида `G:Toshiba 32 Bit:(0x20DF3AC5)(Repeat)():3` или
 * `G:HID 16 Bit:()(07000027)():1`: протокол, затем группы в скобках, затем
 * число повторов. Код — первая непустая группа.
 * @param keyCode Исходная строка; пустая строка допустима.
 * @param[out] command Поля protocol, code, keyCode и repeat.
 * @return false — строку не удалось разобрать; command.keyCode при этом заполнен.
 */
bool parseKeyCode(std::string_view keyCode, HubCommand& command);

/**
 * @brief Представляет конфигурацию хаба моделью редактора.
 *
 * Переносятся устройства с командами, активности с ролями и выбранными входами.
 * ИК-коды попадают в @c Command::protocolName и @c Command::native как есть:
 * в номера записей `IrProto.bin` они не переводятся, потому что это формат другого пульта.
 *
 * Кнопки активностей берутся из `MapList.json`, если он есть: из раскладок активности
 * выбирается та, где больше кнопок с командами устройств (у каждого пульта своя).
 * Кнопки устройств и действия-активности/последовательности не переносятся.
 * @param hub Разобранная конфигурация.
 * @return Модель для показа; правка хаба через неё пока не поддержана.
 */
[[nodiscard]] model::RemoteConfig toRemoteConfig(const HubConfiguration& hub);

} // namespace harmony::formats
