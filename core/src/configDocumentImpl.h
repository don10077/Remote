#pragma once

/**
 * @file configDocumentImpl.h
 * @brief Внутреннее устройство ConfigDocument, общее для его файлов реализации.
 */

#include "harmony/core/configDocument.h"
#include "harmony/core/formats/ezhex.h"
#include "harmony/core/formats/irTables.h"
#include "harmony/core/formats/zipArchive.h"

#include "xmlDom.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace harmony {

namespace detail {

/// Двоичная таблица из папки конфигурации.
struct TableFile {
    bool present = false;                   ///< Файл есть (прочитан или создан)
    std::optional<formats::IrTable> table;  ///< Разобранная таблица; nullopt — формат не понят
    formats::Bytes raw;                     ///< Исходные байты, если таблица не разобрана
    std::optional<Error> error;             ///< Почему не разобрана
};

/// Таблицы ИК-данных документа.
struct IrData {
    TableFile irProto; ///< IrProto.bin — протоколы
    TableFile ssIr;    ///< SsIr.bin — сырые коды
};

/// Путь в UTF-8 для сообщений об ошибках (на Windows path::string() портит кириллицу).
[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path);

/// Конфигурация, открытая из .EZHex: заголовок контейнера и весь архив.
struct Package {
    std::string header;               ///< Заголовок .EZHex (сохраняется как был)
    formats::RemoteIdentity identity; ///< Для какого пульта
    formats::ZipArchive archive;      ///< Все файлы пульта, включая platformconfig и скрипты
};

} // namespace detail

/**
 * @brief Содержимое документа.
 *
 * Таблицы и архив неизменяемы и разделяются со снимками отмены: правка собирает
 * новую копию и подменяет указатель, поэтому снимок не копирует их целиком.
 */
struct ConfigDocument::Impl {
    /// Формат источника. Hub — xml собран из модели хаба: правится и сохраняется как обычный, но на пульт не пишется.
    ConfigFormat format = ConfigFormat::Harmony900;
    std::string xml;              ///< Текст UserConfiguration.xml (сохраняется как есть)
    model::RemoteConfig model;    ///< Разобранная модель
    std::string name;             ///< Имя для показа
    std::filesystem::path folder; ///< Папка на диске; пусто — не папка
    std::filesystem::path ezhexFile; ///< Файл .EZHex на диске; пусто — не файл
    std::string actionLists;      ///< Прочитанный ActionLists.xml; пусто — файла не было
    /// IrProto.bin и SsIr.bin; не nullptr
    std::shared_ptr<const detail::IrData> ir = std::make_shared<const detail::IrData>();
    std::shared_ptr<const detail::Package> package; ///< Если открыт из .EZHex; nullptr — нет
};

struct ConfigDocument::Snapshot::Data {
    std::string xml;
    std::shared_ptr<const detail::IrData> ir;
    std::string actionLists;
    std::shared_ptr<const detail::Package> package;
};

namespace detail {

/// .EZHex для пульта без помеченного «не отправлять на пульт» (configDocumentNotSent.cpp).
[[nodiscard]] Result<std::vector<std::uint8_t>> remoteEzHex(const ConfigDocument& document);

/// Ошибка «формат только для чтения» (ErrorCode::Unsupported).
[[nodiscard]] Result<void> readOnlyError();

/// Правка дерева XML и таблиц; при ошибке документ не меняется.
using Change = std::function<Result<void>(pugi::xml_node root, IrData& ir)>;

/**
 * @brief Применяет правку к документу.
 *
 * Дерево собирается из текущего текста, таблицы копируются; после правки
 * дерево записывается обратно и заново разбирается в модель. Документ
 * меняется только при успехе всех шагов.
 */
[[nodiscard]] Result<void> mutate(ConfigDocument::Impl& impl, const Change& change);

/**
 * @brief Текущее содержимое файлов конфигурации для записи.
 *
 * ActionLists.xml приводится в соответствие с кнопками, хеш протоколов — с IrProto.bin.
 * Пустые irProto/ssIr — файла нет.
 */
[[nodiscard]] Result<ConfigDocument::Files> currentFiles(const ConfigDocument::Impl& impl);

/// Записывает файл через временный, чтобы при сбое не остался испорченный файл.
[[nodiscard]] Result<void> writeFileAtomic(const std::filesystem::path& file, std::string_view content);

/// Новый идентификатор из 8 цифр, не занятый ни устройством, ни активностью.
[[nodiscard]] std::string uniqueId(const model::RemoteConfig& config);

/// Имя, не совпадающее с именами устройств: «Имя», «Имя (2)», «Имя (3)»…
[[nodiscard]] std::string uniqueLabel(const model::RemoteConfig& config, const std::string& label);

/// Код ссылки на запись SsIr.bin: «0xFFFF» + номер (16 бит, младший байт первым) — «0xFFFF0300».
[[nodiscard]] std::string rawCode(std::size_t index);

/// Номер записи SsIr.bin из кода «0xFFFF…»; nullopt — не ссылка на сырой код.
[[nodiscard]] std::optional<std::size_t> rawIndex(std::string_view code);

/**
 * @brief Наибольший номер записи SsIr.bin, который можно выдать новой команде.
 *
 * В коде под номер отведено два байта, но на пульте проверен только третий байт
 * кода (0xFFFF<номер>00); как прошивка читает четвёртый, неизвестно. До 255
 * обе записи совпадают байт в байт, дальше — риск, поэтому новые коды ограничены
 * одним байтом (так же делает afterglow). Чтение существующих номеров не меняется.
 */
inline constexpr std::size_t kMaxRawIndex = 0xFF;

/// Наибольший номер записи IrProto.bin для нового протокола (номер — и в <Protocol>, и в начале <Code>).
inline constexpr std::size_t kMaxProtocolIndex = 0xFF;

/// Таблица, в которую можно добавлять записи; создаёт пустую, если файла не было.
[[nodiscard]] Result<formats::IrTable*> writableTable(TableFile& file, std::string_view fileName);

/**
 * @brief <Command> с именем @p name в <Commands> устройства, без <Data>.
 *
 * Есть такая команда — у неё убирается <Data> (сигнал заменяется), нет — добавляется
 * в конец <Commands> (создаётся и он). <Data> добавляет вызывающий.
 */
pugi::xml_node commandWithoutData(pugi::xml_node device, const std::string& name);

/// Текст длительности паузы SendDelay (параметр «Delay»).
[[nodiscard]] std::string delayText(pugi::xml_node operation);

/**
 * @brief Добавляет пустое устройство после последнего (без команд; питание — вручную).
 *
 * В существующих активностях новое устройство выключено.
 */
pugi::xml_node appendDevice(pugi::xml_node root, const std::string& id, const DeviceDraft& draft);

/**
 * @brief Записывает хеш IrProto.bin в свойство ProtocolCacheHash и в <Protocols><Hash>
 *        (таблицы нет — ничего).
 * @return ErrorCode::Format — таблицу нельзя записать (formats::buildIrProto()).
 */
[[nodiscard]] Result<void> updateProtocolHash(pugi::xml_node root, const TableFile& irProto);

/// Действие «отправить команду устройству» (Press).
void appendSendCommand(pugi::xml_node parent, const std::string& deviceId, const std::string& command);

/// «Выключить всё» (служебная активность PowerOff) выключает и это устройство; ManualPower — false.
void enablePowerOff(pugi::xml_node root, pugi::xml_node device, const std::string& deviceId);

/// Добавляет аппаратную кнопку владельцу (активности или устройству), у которого её ещё нет.
void appendHardButton(pugi::xml_node owner, const std::string& key, const std::string& deviceId,
                      const std::string& command);

/// Добавляет экранную кнопку на свободное место @p position.
void appendSoftButton(pugi::xml_node owner, int position, const std::string& label, const std::string& deviceId,
                      const std::string& command);

/// То же для правок только XML.
[[nodiscard]] Result<void> mutate(ConfigDocument::Impl& impl,
                                  const std::function<Result<void>(pugi::xml_node root)>& change);

} // namespace detail

} // namespace harmony
