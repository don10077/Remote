#pragma once

/**
 * @file irDatabase.h
 * @brief База ИК-кодов Logitech: протоколы, записи устройств, своя компактная база для поиска.
 *
 * Исходные данные — архив logitech-harmony-ir-archive (GitHub):
 * - device_features_raw.jsonl(.gz) — по строке JSON на устройство: производитель,
 *   модель, тип, команды (код Logitech «G:<протокол>:(…)(…)(…):n») и описания
 *   питания и входов (около 6 ГБ без сжатия);
 * - AllProtocols.json(.gz) — описания ИК-протоколов, по которым код команды
 *   превращается в сигнал.
 *
 * buildDatabase() за один проход по сжатому файлу собирает свою базу
 * (около 50 МБ): каталог для поиска, записи устройств только с нужными
 * полями, сжатые блоками, и индекс сигналов для поиска модели по кнопкам
 * родного пульта (signalIndex.h). IrDatabase читает её: поиск по каталогу
 * мгновенный, запись устройства — распаковка одного блока, индекс сигналов
 * загружается при первом обращении.
 *
 * Формат файла (little endian):
 * @code
 *   "HARMONYIRDB\0" | u32 версия | u32 число устройств | u32 число блоков
 *   | u64 смещение и u64 размер каталога | u64 смещение и u64 размер протоколов
 *   | u64 смещение таблицы блоков | u64 смещение и u64 размер индекса сигналов
 *   | u16 длина и текст источника
 *   блоки: zlib(записи JSON через '\n'), по kBlockSize устройств
 *   таблица блоков: число блоков × (u64 смещение, u32 размер сжатый, u32 размер)
 *   каталог: zlib(строки «id\tпроизводитель\tмодель\tтип\tномер записи\n», по алфавиту)
 *   протоколы: zlib(AllProtocols.json)
 *   индекс сигналов: zlib(SignalIndex, см. signalIndex.h)
 *   версия 3: JSON паспорта сборки после индекса (дата UTC, источник, статистика)
 *   чтение поддерживает версии 2 и 3; новые базы записываются в версии 3
 * @endcode
 */

#include "harmony/core/error.h"
#include "harmony/core/ir/irSignal.h"
#include "harmony/core/ir/nativeProtocol.h"
#include "harmony/core/irdb/signalIndex.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace harmony::irdb {

/// Шаг действия из описания питания или входов.
struct DbAction {
    enum class Kind {
        Press, ///< Отправить команду
        Delay, ///< Пауза
        State, ///< Выставить другое состояние устройства
    };
    Kind kind = Kind::Press;
    std::string command;    ///< Press: имя команды
    int delayMs = 0;        ///< Delay: пауза, мс
    std::string stateName;  ///< State: имя состояния
    std::string stateValue; ///< State: значение

    bool operator==(const DbAction&) const = default;
};

/// Вход устройства.
struct DbInput {
    std::string name;              ///< «HDMI1»
    std::vector<DbAction> actions; ///< Прямой выбор входа; пусто — нет
};

/// Команда устройства.
struct DbCommand {
    std::string name;    ///< «PowerToggle», «0»
    std::string type;    ///< Тип команды Logitech: «Number0», «VolumeUp», «Unknown»
    std::string keyCode; ///< «G:NEC1:()(0x04FB08F7)():1»
    int protocolId = -1; ///< Номер протокола в AllProtocols.json
};

/// Устройство из базы.
struct DbDevice {
    std::uint64_t id = 0;
    std::string manufacturer;
    std::string model;
    int deviceType = 0; ///< Код типа Logitech (1 — телевизор, 5 — ресивер …)
    std::vector<DbCommand> commands;

    /// Питание: 1 — отдельные команды, 2 — переключатель, 3 — вручную, 0 — не описано.
    int powerType = 0;
    int powerOnDelayMs = 0; ///< Время загрузки после включения
    std::vector<DbAction> powerOn;
    std::vector<DbAction> powerOff;
    std::vector<DbAction> powerToggle;

    int inputDelayMs = 0;
    std::vector<DbInput> inputs; ///< По порядку Logitech
    std::vector<DbAction> inputNext;
    std::vector<DbAction> inputPrev;
};

/**
 * @brief Тип устройства конфигурации по коду Logitech.
 * @return «Television», «Receiver», … ; неизвестный код — «Unknown».
 */
[[nodiscard]] std::string deviceTypeName(int code);

/// Протоколы AllProtocols.json: код команды → ИК-сигнал.
class ProtocolLibrary {
public:
    /// Несущая, которую пульт умеет передавать, Гц.
    static constexpr std::uint32_t kCarrierMin = 31500;
    static constexpr std::uint32_t kCarrierMax = 250000;

    ProtocolLibrary();
    ProtocolLibrary(ProtocolLibrary&&) noexcept;
    ProtocolLibrary& operator=(ProtocolLibrary&&) noexcept;
    ~ProtocolLibrary();

    /**
     * @brief Разбирает AllProtocols.json.
     * @return ErrorCode::Format — не JSON или нет протоколов.
     */
    [[nodiscard]] static Result<ProtocolLibrary> parse(std::string_view json);

    /**
     * @brief Сигнал одного нажатия: начальные сегменты, повторы (сколько требует протокол),
     * конечные.
     * @return ErrorCode::Unsupported — протокол не найден, несущая вне возможностей пульта
     *         или код не подходит протоколу.
     */
    [[nodiscard]] Result<ir::IrSignal> render(const DbCommand& command) const;

    /**
     * @brief Команда в собственном формате пульта: запись IrProto.bin и <Code>.
     *
     * Как у Logitech: каждый сегмент протокола — элемент программы, части кода
     * Start / Repeat / Finish — этапы нажатия, удержания и отпускания. Пульт сам
     * повторяет кадр при удержании и переключает бит-переключатель. Запись
     * зависит только от протокола, так что все команды устройства (и устройства
     * с тем же протоколом) делят одну запись IrProto.bin.
     *
     * Перед выдачей команда исполняется (ir::runCarrier) и сверяется с render().
     * @return ErrorCode::Unsupported — код не разобрать или протокол так не записать
     *         (тогда команду сохраняют сигналом); ErrorCode::Format — проверка не прошла.
     */
    [[nodiscard]] Result<ir::NativeCommand> compile(const DbCommand& command) const;

    /// @return Число протоколов.
    [[nodiscard]] std::size_t size() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Устройство в каталоге базы.
struct CatalogueEntry {
    std::uint64_t id = 0;
    std::string manufacturer;
    std::string model;
    int deviceType = 0;
};

/// Своя база ИК-кодов (файл, собранный buildDatabase()).
class IrDatabase {
public:
    IrDatabase();
    IrDatabase(IrDatabase&&) noexcept;
    IrDatabase& operator=(IrDatabase&&) noexcept;
    ~IrDatabase();

    /**
     * @brief Открывает базу: читает каталог и протоколы (записи устройств — по запросу).
     * @return ErrorCode::NotFound/Io — нет файла; ErrorCode::Format — не база или другая версия.
     */
    [[nodiscard]] static Result<IrDatabase> open(const std::filesystem::path& file);

    /// @return Каталог, по алфавиту производителя и модели.
    [[nodiscard]] const std::vector<CatalogueEntry>& entries() const;

    /**
     * @brief Устройства, в «производитель модель» которых есть все слова запроса (без учёта
     * регистра).
     * @param text Запрос: «samsung ue55».
     * @param max Сколько номеров вернуть.
     * @param found [out] Сколько всего совпадений (может быть больше max).
     * @return Номера в entries().
     */
    [[nodiscard]] std::vector<std::size_t> search(std::string_view text, std::size_t max,
                                                  std::size_t* found = nullptr) const;

    /// @return true — в «производитель модель» устройства @p entry есть все слова @p text.
    [[nodiscard]] bool matches(std::size_t entry, std::string_view text) const;

    /**
     * @brief Индекс сигналов для поиска модели по кнопкам родного пульта.
     *
     * Загружается при первом вызове (около секунды), дальше — тот же. Можно звать из
     * другого потока.
     * @return ErrorCode::Format — индекс повреждён.
     */
    [[nodiscard]] Result<std::shared_ptr<const SignalIndex>> signalIndex() const;

    /**
     * @brief Запись устройства.
     * @param entry Номер в entries().
     */
    [[nodiscard]] Result<DbDevice> device(std::size_t entry) const;

    /// @return Протоколы базы.
    [[nodiscard]] const ProtocolLibrary& protocols() const;

    /// @return Откуда собрана база (например, «logitech-harmony-ir-archive raw-2026-09-02»).
    [[nodiscard]] const std::string& source() const;

    /// @return Версия формата открытого файла (старые базы версии 2 поддерживаются).
    [[nodiscard]] std::uint32_t formatVersion() const;

    /// @return Паспорт сборки в JSON; у баз версии 2 пустая строка.
    [[nodiscard]] const std::string& buildInfo() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Итог сборки базы.
struct BuildStats {
    std::size_t devices = 0;      ///< Устройств в базе
    std::size_t commands = 0;     ///< Команд всего
    std::size_t skippedLines = 0; ///< Неверных, неполных или повторных записей устройств
    std::size_t signalFrames = 0; ///< Различных сигналов в индексе
};

/**
 * @brief Ход сборки: обработано и всего байт исходного файла (сжатого, если .gz).
 * @return false — прервать.
 */
using BuildProgress = std::function<bool(std::uint64_t done, std::uint64_t total)>;

/**
 * @brief Собирает свою базу из файлов архива (сжатых .gz или распакованных).
 * @param devicesFile device_features_raw.jsonl(.gz).
 * @param protocolsFile AllProtocols.json(.gz).
 * @param output Файл базы (пишется через временный).
 * @param source Подпись источника для показа.
 * @param progress Ход сборки (может быть пустым).
 * @return Итог; ErrorCode::Cancelled — прервано.
 */
[[nodiscard]] Result<BuildStats> buildDatabase(const std::filesystem::path& devicesFile,
                                               const std::filesystem::path& protocolsFile,
                                               const std::filesystem::path& output,
                                               const std::string& source,
                                               const BuildProgress& progress = {});

} // namespace harmony::irdb
