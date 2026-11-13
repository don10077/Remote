#pragma once

/**
 * @file configDocument.h
 * @brief Документ конфигурации: файл на диске, его разобранная модель и сохранение.
 *
 * Конфигурация на диске — папка или файл .EZHex:
 *   <папка>/UserConfiguration.xml   — устройства, активности, кнопки (обязателен);
 *   <папка>/ActionLists.xml, IrProto.bin, SsIr.bin — остальные файлы пульта (если есть);
 *   <файл>.EZHex — резервная копия с пульта (concordance, afterglow, эта программа):
 *   те же файлы плюс настройки пульта и скрипты установки — сохраняются как были.
 *
 * Документ держит исходный XML целиком, поэтому сохранение не теряет ничего,
 * в том числе разделы, которые модель пока не разбирает. Таблицы IrProto.bin
 * и SsIr.bin тоже в памяти: при переносе устройства в них добавляются его
 * протоколы. ActionLists.xml при сохранении приводится в соответствие с кнопками.
 */

#include "harmony/core/activityDraft.h"
#include "harmony/core/buttonCopy.h"
#include "harmony/core/buttonOwner.h"
#include "harmony/core/configCheck.h"
#include "harmony/core/deviceComparison.h"
#include "harmony/core/deviceReplacement.h"
#include "harmony/core/deviceStates.h"
#include "harmony/core/error.h"
#include "harmony/core/formats/ezhex.h"
#include "harmony/core/ir/irSignal.h"
#include "harmony/core/macro.h"
#include "harmony/core/model/remoteConfig.h"
#include "harmony/core/propertyCatalog.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace harmony {

namespace irdb {
struct DbDevice;
class ProtocolLibrary;
} // namespace irdb

/// Итог переноса устройства из базы ИК-кодов.
struct DbImportReport {
    std::string deviceId;                    ///< Id нового устройства
    int commands = 0;                        ///< Команд перенесено
    int protocolCommands = 0;                ///< Из них — протоколом (IrProto.bin), остальные — сигналом
    int hardButtons = 0;                     ///< Аппаратных кнопок назначено
    int softButtons = 0;                     ///< Экранных кнопок создано
    bool power = false;                      ///< Питание описано (пульт включает сам)
    bool inputs = false;                     ///< Входы описаны
    std::vector<std::string> skipped;        ///< Команды, которые не удалось перенести
};

/// Данные нового устройства, которое создаётся с нуля (команды потом выучиваются).
struct DeviceDraft {
    std::string label;        ///< Имя на пульте
    std::string type = "Television"; ///< Тип («Television», «Receiver», ...)
    std::string manufacturer; ///< Производитель
    std::string model;        ///< Модель
};

/**
 * @brief Открытая конфигурация пульта.
 *
 * Перемещаемый, не копируемый (внутри — дерево XML).
 */
class ConfigDocument {
public:
    /// Файлы пульта, которые лежат рядом с UserConfiguration.xml и сохраняются вместе с ним.
    static constexpr std::string_view kCompanionFiles[] = {"ActionLists.xml", "IrProto.bin",
                                                           "SsIr.bin"};

    ConfigDocument(ConfigDocument&&) noexcept;
    ConfigDocument& operator=(ConfigDocument&&) noexcept;
    ~ConfigDocument();

    /**
     * @brief Открывает конфигурацию.
     * @param path Папка конфигурации, файл UserConfiguration.xml или файл .EZHex.
     * @return Документ; ошибки — как у formats::readUserConfiguration() и parseEzHex().
     */
    [[nodiscard]] static Result<ConfigDocument> open(const std::filesystem::path& path);

    /**
     * @brief Собирает документ из содержимого .EZHex (например, прочитанного с пульта).
     * @param ezhex Содержимое файла.
     * @param name Имя для показа.
     * @return Документ без места на диске; ErrorCode::Format — не .EZHex или нет UserConfiguration.xml.
     */
    [[nodiscard]] static Result<ConfigDocument> fromEzHex(std::span<const std::uint8_t> ezhex, std::string name);

    /**
     * @brief Разбирает конфигурацию из текста (встроенный пример, тесты).
     * @param xml Содержимое UserConfiguration.xml.
     * @param name Имя для показа.
     */
    [[nodiscard]] static Result<ConfigDocument> fromXml(std::string_view xml, std::string name);

    /// Файлы конфигурации в памяти (например, встроенный в программу пример).
    struct Files {
        std::string userConfiguration;      ///< UserConfiguration.xml (обязателен)
        std::string actionLists;            ///< ActionLists.xml; пусто — нет
        std::vector<std::uint8_t> irProto;  ///< IrProto.bin; пусто — нет
        std::vector<std::uint8_t> ssIr;     ///< SsIr.bin; пусто — нет
    };

    /**
     * @brief Собирает документ из файлов в памяти (без папки: сохранять — через saveAs()).
     * @param files Содержимое файлов.
     * @param name Имя для показа.
     */
    [[nodiscard]] static Result<ConfigDocument> fromFiles(const Files& files, std::string name);

    /**
     * @brief Создаёт пустую конфигурацию: пульт и служебная активность «Выключено».
     * @param name Имя конфигурации (станет именем папки при сохранении).
     * @param powerOffLabel Подпись активности «выключить всё» на экране пульта.
     */
    [[nodiscard]] static ConfigDocument createNew(std::string name,
                                                  const std::string& powerOffLabel = "Power off");

    /// @return Разобранная модель.
    [[nodiscard]] const model::RemoteConfig& model() const;

    /// @return Имя конфигурации (по умолчанию — имя папки).
    [[nodiscard]] const std::string& name() const;

    /// @return Папка, из которой открыт или куда последний раз сохранён документ; пусто — ещё нигде.
    [[nodiscard]] const std::filesystem::path& folder() const;

    /// @return true, если у документа есть папка на диске.
    [[nodiscard]] bool hasFolder() const { return !folder().empty(); }

    /// @return Файл .EZHex, из которого открыт или куда последний раз сохранён документ; пусто — не файл.
    [[nodiscard]] const std::filesystem::path& ezhexFile() const;

    /// @return Где документ на диске: папка или файл .EZHex; пусто — ещё нигде.
    [[nodiscard]] const std::filesystem::path& location() const;

    /// @return true, если документ можно сохранить без выбора места.
    [[nodiscard]] bool hasLocation() const { return !location().empty(); }

    /// @return Для какого пульта конфигурация (из .EZHex; иначе — Harmony 900).
    [[nodiscard]] formats::RemoteIdentity identity() const;

    /**
     * @brief Сохраняет в папку документа.
     * @return Ошибка ErrorCode::Unsupported, если папки ещё нет (нужно saveAs), или ErrorCode::Io.
     */
    [[nodiscard]] Result<void> save();

    /**
     * @brief Сохраняет в другую папку и делает её папкой документа.
     *
     * Папка создаётся при необходимости. Файлы пульта из прежней папки
     * (kCompanionFiles) копируются рядом, чтобы конфигурация осталась полной.
     * @param folder Новая папка конфигурации.
     * @return Пустой результат или ErrorCode::Io.
     */
    [[nodiscard]] Result<void> saveAs(const std::filesystem::path& folder);

    /**
     * @brief Сохраняет в файл .EZHex и делает его местом документа.
     *
     * Если документ открыт из .EZHex, остальные файлы пульта (настройки,
     * скрипты установки) сохраняются как были.
     * @param file Путь к файлу.
     */
    [[nodiscard]] Result<void> saveAsEzHex(const std::filesystem::path& file);

    /**
     * @brief Конфигурация в формате .EZHex.
     * @param forRemote true — для записи на пульт: права файлов приводятся к тем,
     *        с которыми пульт их устанавливает (у снятой с пульта копии они другие,
     *        и записанные как есть ломают сохранение настроек), а помеченное «не
     *        отправлять на пульт» не входит; false — как есть.
     */
    [[nodiscard]] Result<std::vector<std::uint8_t>> toEzHex(bool forRemote) const;

    /**
     * @brief Конфигурация для записи на пульт, в которой настройки RF-бластеров — с пульта.
     *
     * Привязку бластера и назначение устройств делают на самом пульте; файлы
     * platformconfig/XmlUserRfSetting* в открытой копии могли устареть, и запись
     * вернула бы прежнее состояние. Поэтому они берутся из @p remote — только что
     * прочитанной конфигурации пульта; всё остальное — как в toEzHex(true).
     * @param remote Конфигурация, прочитанная с пульта перед записью.
     */
    [[nodiscard]] Result<std::vector<std::uint8_t>> toEzHexKeepingRemoteRf(const ConfigDocument& remote) const;

    /**
     * @brief Устройства, которых нет в карте RF-бластера, хотя бластер привязан.
     *
     * Такие устройства пульт шлёт с переднего ИК (обычно это новые устройства:
     * у них новый id). Назначают их на бластер на самом пульте.
     * @return Id устройств; пусто — бластера нет или все устройства в карте.
     */
    [[nodiscard]] std::vector<std::string> devicesOutsideRf() const;

    /// @return Текст UserConfiguration.xml в том виде, в каком он будет сохранён.
    [[nodiscard]] std::string toXml() const;

    // --- Правка ---------------------------------------------------------------
    // Каждая правка меняет только нужные элементы XML; остальной файл остаётся
    // байт в байт прежним. При ошибке документ не меняется.
    // Для отмены используйте undo::editDocument() (documentEdit.h).

    /**
     * @brief Заменяет содержимое содержимым другой конфигурации (например, снятой с пульта).
     *
     * Место на диске и имя документа остаются прежними: «Сохранить» запишет
     * новое содержимое туда же. Настройки пульта и скрипты (если у источника
     * они есть, как у .EZHex) тоже берутся из источника.
     */
    [[nodiscard]] Result<void> replaceContent(const ConfigDocument& source);

    /**
     * @brief Снимок содержимого для отмены и повтора.
     *
     * Копировать дёшево (данные общие); устройство скрыто — снимок можно
     * только вернуть в тот же документ через restore().
     */
    class Snapshot {
    public:
        Snapshot();
        ~Snapshot();
        Snapshot(const Snapshot&);
        Snapshot& operator=(const Snapshot&);

    private:
        friend class ConfigDocument;
        struct Data;
        std::shared_ptr<const Data> data_;
    };

    /// @return Снимок текущего содержимого (XML и таблицы ИК-данных).
    [[nodiscard]] Snapshot snapshot() const;

    /**
     * @brief Возвращает документ к снимку (для отмены и повтора).
     * @param snapshot Снимок этого же документа.
     * @return ErrorCode::NotFound — пустой снимок; ошибка разбора — снимок не читается.
     *         При ошибке документ не меняется.
     */
    [[nodiscard]] Result<void> restore(const Snapshot& snapshot);

    /**
     * @brief Переименовывает устройство.
     * @param deviceId Идентификатор устройства.
     * @param label Новое имя; не пустое.
     * @return ErrorCode::NotFound — нет устройства; ErrorCode::Format — пустое имя.
     */
    [[nodiscard]] Result<void> setDeviceLabel(const std::string& deviceId, const std::string& label);

    /**
     * @brief Переименовывает активность.
     * @param activityId Идентификатор активности.
     * @param label Новое имя; не пустое.
     */
    [[nodiscard]] Result<void> setActivityLabel(const std::string& activityId, const std::string& label);

    /**
     * @brief Переставляет активность: порядок в файле — порядок на экране пульта.
     * @param activityId Идентификатор активности.
     * @param index Новая позиция среди активностей (0 — первая); больше числа — в конец.
     */
    [[nodiscard]] Result<void> moveActivity(const std::string& activityId, std::size_t index);

    /**
     * @brief Переставляет устройство в файле конфигурации.
     * @param deviceId Идентификатор устройства.
     * @param index Новая позиция (0 — первая); больше числа — в конец.
     * @return Ошибка NotFound, если устройства нет.
     */
    [[nodiscard]] Result<void> moveDevice(const std::string& deviceId, std::size_t index);

    /**
     * @brief Удаляет активность.
     * @param activityId Идентификатор активности.
     * @return ErrorCode::Unsupported — служебную активность «выключить всё» удалить нельзя.
     */
    [[nodiscard]] Result<void> removeActivity(const std::string& activityId);

    /**
     * @brief Активности, которые используют устройство: включают его, дают ему роль
     *        или шлют ему команды кнопками.
     * @param deviceId Идентификатор устройства.
     * @return Имена активностей в порядке файла; пусто — устройство можно удалить.
     */
    [[nodiscard]] std::vector<std::string> activitiesUsingDevice(const std::string& deviceId) const;

    /**
     * @brief Удаляет устройство, если оно не участвует ни в одной активности.
     *
     * Вместе с устройством убираются его выключения из активностей
     * (в том числе из «выключить всё»).
     * @param deviceId Идентификатор устройства.
     * @return ErrorCode::Unsupported с перечнем активностей, если устройство используется.
     */
    [[nodiscard]] Result<void> removeDevice(const std::string& deviceId);

    /**
     * @brief Убирает устройство из всех активностей, чтобы его можно было удалить.
     *
     * Каждая активность меняется как в updateActivity() без переназначения кнопок:
     * пропадают включение, роли, входы, шаги запуска и кнопки этого устройства.
     * Роль управления переходит к первому оставшемуся устройству. Активность, в которой
     * других устройств нет, удаляется. «Выключить всё» не трогается (её чистит removeDevice()).
     * @param deviceId Идентификатор устройства.
     * @return Имена удалённых активностей; ErrorCode::NotFound — нет устройства. При ошибке
     *         документ не меняется.
     */
    [[nodiscard]] Result<std::vector<std::string>> removeDeviceFromActivities(const std::string& deviceId);

    // --- Кнопки активностей и устройств ----------------------------------------
    // У активности кнопка шлёт команду одному из устройств, которые она включает;
    // у устройства — только ему самому. Действие кнопки называется
    // «<устройство>_<команда>_Hold» и описано в ActionLists.xml (он обновляется
    // при сохранении).

    /**
     * @brief Назначает аппаратную кнопку пульта.
     * @param owner Активность или устройство.
     * @param key Кнопка пульта («Menu», «VolumeUp», «Number1», ...).
     * @param deviceId Устройство, которому уйдёт команда.
     * @param command Команда устройства; пусто — снять назначение.
     * @return ErrorCode::NotFound — нет владельца, устройства или команды;
     *         ErrorCode::Format — устройство не подходит (не включается в активности
     *         или это чужое устройство).
     */
    [[nodiscard]] Result<void> setHardButton(const ButtonOwner& owner, const std::string& key,
                                             const std::string& deviceId, const std::string& command);

    /**
     * @brief Переносит (или копирует) назначение с одной аппаратной кнопки на другую.
     *
     * При переносе на занятую кнопку назначения меняются местами, на свободную — @p fromKey
     * освобождается. При копировании прежнее назначение @p toKey заменяется.
     * @return ErrorCode::NotFound — у @p fromKey нет назначения.
     */
    [[nodiscard]] Result<void> moveHardButton(const ButtonOwner& owner, const std::string& fromKey,
                                              const std::string& toKey, bool copy);

    /**
     * @brief Создаёт или меняет экранную кнопку.
     * @param owner Активность или устройство.
     * @param position Место на экране (0 — первое; по 6 на страницу).
     * @param label Подпись; пусто — оставить значок, если он есть, иначе подписать именем команды.
     * @param deviceId Устройство, которому уйдёт команда.
     * @param command Команда устройства.
     */
    [[nodiscard]] Result<void> setSoftButton(const ButtonOwner& owner, int position, const std::string& label,
                                             const std::string& deviceId, const std::string& command);

    // --- Макросы (см. macro.h) ---------------------------------------------------

    /**
     * @brief Шаги макроса кнопки.
     * @param actionId Действие кнопки (isMacroAction()).
     * @return Шаги; ErrorCode::NotFound — такого действия нет в ActionLists.xml.
     */
    [[nodiscard]] Result<Macro> macro(const std::string& actionId) const;

    /**
     * @brief Назначает аппаратной кнопке макрос (прежнее назначение заменяется).
     *
     * Шаги проверяются, как у обычной кнопки: у устройства — только его команды, у
     * активности — команды устройств, которые она включает.
     * @return ErrorCode::Format — нет шагов или шаг не подходит; ErrorCode::NotFound — нет
     *         владельца, устройства или команды.
     */
    [[nodiscard]] Result<void> setHardMacro(const ButtonOwner& owner, const std::string& key, const Macro& steps);

    /// Создаёт или меняет экранную кнопку-макрос (подпись обязательна: имени команды нет).
    [[nodiscard]] Result<void> setSoftMacro(const ButtonOwner& owner, int position, const std::string& label,
                                            const Macro& steps);

    /**
     * @brief Значок экранной кнопки вместо подписи (см. buttonIcons.h).
     *
     * Подпись при этом очищается — так пишет пульт. Пустой @p icon убирает значок;
     * если подписи нет, кнопка подписывается именем команды.
     * @param owner Активность или устройство, чей экран.
     * @param position Место кнопки на экране (Position).
     * @param icon Имя значка из buttonIcons() или пустая строка.
     * @return ErrorCode::NotFound — нет кнопки на этом месте; ErrorCode::Format — значка нет
     *         в прошивке (пульт нарисовал бы пустую кнопку).
     */
    [[nodiscard]] Result<void> setSoftButtonIcon(const ButtonOwner& owner, int position, const std::string& icon);

    /// Удаляет экранную кнопку на месте @p position (место остаётся свободным).
    [[nodiscard]] Result<void> removeSoftButton(const ButtonOwner& owner, int position);

    /// Переставляет экранную кнопку; если место занято — кнопки меняются местами.
    [[nodiscard]] Result<void> moveSoftButton(const ButtonOwner& owner, int from, int to);

    /// @return Первое свободное место на экране (-1 — нет такого владельца).
    [[nodiscard]] int freeSoftPosition(const ButtonOwner& owner) const;

    /**
     * @brief Копирует кнопки активности (все или выбранные) в другие активности.
     * @return Итог; ErrorCode::NotFound — нет активности; ErrorCode::Unsupported — «выключить всё».
     */
    [[nodiscard]] Result<ButtonCopyReport> copyButtons(const ButtonCopy& copy);

    // --- Состав активности --------------------------------------------------------

    /**
     * @brief Меняет активность по черновику: имя, тип, устройства, роли, входы.
     *
     * Питание пересобирается (не попавшие в черновик устройства выключаются),
     * роли и переключения входов заменяются; другие действия при запуске
     * сохраняются, кроме адресованных убранным устройствам. Кнопки, которые шлют
     * команды убранным устройствам, удаляются — они бы не сработали.
     * @param activityId Активность.
     * @param draft Новый состав (см. draftOf()).
     * @param reassignHardButtons true — аппаратные кнопки назначаются заново по ролям
     *        (как у новой активности), false — остаются как есть.
     * @return Ошибки — как у addActivity(); ErrorCode::Unsupported — «выключить всё».
     */
    [[nodiscard]] Result<void> updateActivity(const std::string& activityId, const ActivityDraft& draft,
                                              bool reassignHardButtons);

    // --- Команды устройства -------------------------------------------------------

    /**
     * @brief Создаёт пустое устройство — без команд; их потом выучивают с родного пульта.
     *
     * Питание описывается само, когда появятся команды PowerOn/PowerOff или
     * PowerToggle; до тех пор устройство включают руками. Во всех активностях
     * новое устройство выключено.
     * @return Id устройства; ErrorCode::Format — нет имени или типа.
     */
    [[nodiscard]] Result<std::string> createDevice(const DeviceDraft& draft);

    /**
     * @brief Добавляет выученную команду или заменяет одноимённую.
     *
     * Сигнал хранится «сырым» в SsIr.bin (одинаковые не дублируются). Если это
     * PowerOn/PowerOff или PowerToggle, а питание у устройства ещё не описано,
     * оно описывается: отдельные «вкл» и «выкл» или одна кнопка-переключатель.
     * @param deviceId Устройство.
     * @param name Имя команды («PowerOn», «VolumeUp», «Netflix», ...).
     * @param signal Сигнал.
     * @return ErrorCode::Format — пустое имя, пустой сигнал или в SsIr.bin нет места (до 256 кодов).
     */
    [[nodiscard]] Result<void> learnCommand(const std::string& deviceId, const std::string& name,
                                            const ir::IrSignal& signal);

    /**
     * @brief Записывает выученный сигнал протоколом, если это возможно, иначе — как learnCommand().
     *
     * Протоколом (IrProto.bin) — когда ir::nativeProtocolOf() узнал весь сигнал: тогда при
     * удержании пульт шлёт настоящие кадры повтора и сам меняет бит переключения RC5/RC6.
     * Сигнал с чужими кадрами, протокол, который пульт так не хранит, или полная таблица
     * протоколов — сигналом в SsIr.bin. Питание описывается, как в learnCommand().
     * @return true — протоколом, false — сигналом; ошибки — как у learnCommand().
     */
    [[nodiscard]] Result<bool> storeLearned(const std::string& deviceId, const std::string& name,
                                            const ir::IrSignal& signal);

    /**
     * @brief Добавляет команду по протоколу, адресу и команде (или заменяет одноимённую).
     *
     * NEC, NEC (extended), Onkyo, Samsung32, Sony SIRC, JVC, RC5 и RC6 сохраняются протоколом
     * в IrProto.bin, как у команд Logitech: пульт сам шлёт кадры повтора при удержании и
     * меняет бит переключения RC5/RC6. Остальные протоколы — сигналом в SsIr.bin, как выученные.
     * Питание описывается, как в learnCommand().
     * @param protocol Название из ir::encodableProtocols().
     * @return true — протоколом, false — сигналом; ErrorCode::Format — значения не помещаются
     *         в протокол; ErrorCode::NotFound — нет устройства.
     */
    [[nodiscard]] Result<bool> setProtocolCommand(const std::string& deviceId, const std::string& name,
                                                  const std::string& protocol, std::uint32_t address,
                                                  std::uint32_t command);

    /// Где используется команда.
    struct CommandUse {
        enum class Place {
            DeviceButtons,   ///< Кнопки самого устройства
            ActivityButtons, ///< Кнопки активности name
            DeviceStates,    ///< Описание питания или входов устройства
            ActivityStart,   ///< Запуск активности name
            MacroButton,     ///< Кнопка-макрос (name — её действие)
        };
        Place place;
        std::string name; ///< Имя активности (для ActivityButtons и ActivityStart)

        /// @return Описание (по-английски, для консоли и журнала): «buttons of activity “TV”».
        [[nodiscard]] std::string text() const;
    };

    /**
     * @brief Где используется команда: кнопки, питание, запуск активностей.
     * @return Места; пусто — нигде.
     */
    [[nodiscard]] std::vector<CommandUse> commandUsage(const std::string& deviceId, const std::string& name) const;

    /**
     * @brief Удаляет команду, если она нигде не используется.
     * @return ErrorCode::Unsupported с перечнем мест, если используется.
     */
    [[nodiscard]] Result<void> removeCommand(const std::string& deviceId, const std::string& name);

    /**
     * @brief Убирает команду с кнопок и из запуска активностей, чтобы её можно было удалить.
     *
     * Удаляются кнопки устройства и активностей, которые шлют эту команду, и шаги запуска
     * активностей с ней. Питание и входы устройства и кнопки-макросы не трогаются:
     * их правят в своих окнах.
     * @return ErrorCode::NotFound — нет устройства или команды. При ошибке документ не меняется.
     */
    [[nodiscard]] Result<void> clearCommandUses(const std::string& deviceId, const std::string& name);

    /**
     * @brief Сигнал, который пульт передаёт по команде.
     *
     * Выученная команда — сигнал из SsIr.bin; команда по протоколу — собранный
     * из протокола IrProto.bin и битов <Code>.
     * @return nullopt — команды нет или ИК-данные не прочитаны.
     */
    [[nodiscard]] std::optional<ir::IrSignal> commandSignal(const std::string& deviceId,
                                                            const std::string& name) const;

    /// Что известно о сигнале команды.
    /**
     * @brief Восстанавливает отсутствующие или повреждённые IR-таблицы из совместимой копии.
     * @param source Резервная копия с теми же ссылками команд и хешем протоколов.
     * @return Число восстановленных таблиц; ошибка совместимости ничего не меняет.
     */
    [[nodiscard]] Result<std::size_t> recoverIrTables(const ConfigDocument& source);

    // --- Свойства устройств и активностей (см. propertyCatalog.h) -----------------

    /**
     * @brief Свойства <Property> в порядке файла, включая неизвестные программе.
     * @param scope Устройство, паузы его команд или активность.
     * @param id Устройство или активность.
     * @return Имя и значение; пусто — свойств нет или нет такого владельца.
     */
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> properties(PropertyScope scope,
                                                                              const std::string& id) const;

    /**
     * @brief Задаёт значение свойства (добавляет, если его нет); пустое значение убирает свойство.
     *
     * Логические значения пишутся в регистре пульта (formatPropertyValue()).
     * @return ErrorCode::NotFound — нет владельца; ErrorCode::Format — значение вне границ;
     *         ErrorCode::Unsupported — свойство пишет сам пульт.
     */
    [[nodiscard]] Result<void> setProperty(PropertyScope scope, const std::string& id, const std::string& name,
                                           const std::string& value);

    // --- Настройки пульта (platformconfig/system_*.dat, см. remotePreferences.h) ----

    /**
     * @brief Значение настройки пульта в конфигурации.
     * @param key Ключ («sound», «backlight_level», ...).
     * @return Значение; nullopt — в конфигурации такого файла нет (пульт оставит своё).
     */
    [[nodiscard]] std::optional<std::string> remotePreference(std::string_view key) const;

    /**
     * @brief Задаёт настройку пульта; при записи на пульт она станет его настройкой.
     *
     * Формат часов меняется и в свойстве TimeDisplayFormat (UserConfiguration.xml).
     * Пустое значение убирает файл: пульт оставит свою настройку.
     * Настройки живут в архиве .EZHex: у конфигурации без него архив создаётся;
     * при сохранении в папку они не записываются (там только userconfig).
     * @return ErrorCode::NotFound — нет такой настройки; ErrorCode::Format — неверное значение.
     */
    [[nodiscard]] Result<void> setRemotePreference(std::string_view key, const std::string& value);

    /// @return Целевая модель проекта; не изменяет идентичность исходного архива.
    [[nodiscard]] std::string targetRemote() const;

    /// Выбирает целевой пульт без потери устройств, активностей и исходных IR-таблиц.
    [[nodiscard]] Result<void> setTargetRemote(const std::string& id);

    struct CommandDetails {
        ir::IrSignal signal;                  ///< Что передаёт пульт
        bool learned = false;                 ///< Выученная («сырой» сигнал из SsIr.bin)
        std::optional<ir::IrDecoded> decoded; ///< Протокол, если узнан
        std::vector<bool> codeBits;           ///< Команда по протоколу: биты данных из <Code>

        /// @return Код как в Arduino IRremote («0xF708FB04»); пусто — неизвестен.
        [[nodiscard]] std::string rawData() const;
    };

    /**
     * @brief Сигнал команды, протокол и код.
     * @return ErrorCode::NotFound — нет команды; ErrorCode::Format — данные не разобрать.
     */
    [[nodiscard]] Result<CommandDetails> commandDetails(const std::string& deviceId, const std::string& name) const;

    /**
     * @brief Сравнивает команды двух устройств по именам и сигналам.
     * @return Все команды обоих, по алфавиту (без учёта регистра); пусто — нет устройства.
     */
    [[nodiscard]] std::vector<CommandComparison> compareDeviceCommands(const std::string& firstId,
                                                                       const std::string& secondId) const;

    /**
     * @brief Копирует команды одного устройства другому (одноимённые заменяются).
     *
     * Копируется запись сигнала как есть — выученный сигнал в SsIr.bin не
     * дублируется. Если появились команды питания, а питание у устройства не
     * описано, оно описывается, как при обучении.
     * @param fromId Откуда.
     * @param toId Куда.
     * @param names Какие команды.
     * @return Сколько скопировано; ErrorCode::NotFound — нет устройства или команды;
     *         ErrorCode::Format — устройство то же самое.
     */
    [[nodiscard]] Result<int> copyCommands(const std::string& fromId, const std::string& toId,
                                           const std::vector<std::string>& names);

    // --- Добавление -------------------------------------------------------------

    /**
     * @brief Можно ли переносить устройства из этого документа в другие.
     *
     * Для этого нужны таблицы ИК-данных, на которые ссылаются команды:
     * IrProto.bin (протоколы) и SsIr.bin (сырые коды), прочитанные без ошибок.
     * @return Пустой результат или ErrorCode::Format с объяснением, чего не хватает.
     */
    [[nodiscard]] Result<void> checkIrData() const;

    /**
     * @brief Переносит устройство из другой конфигурации вместе с ИК-командами.
     *
     * Протоколы и сырые коды команд добавляются в таблицы этого документа
     * (одинаковые не дублируются), номера в командах пересчитываются. Если
     * id устройства уже занят, выдаётся новый; одноимённому даётся имя «… (2)».
     * Новое устройство выключается всеми активностями, где оно не нужно,
     * и «выключить всё».
     * @param source Конфигурация-источник.
     * @param deviceId Устройство в источнике.
     * @return Id устройства в этом документе; ErrorCode::NotFound — нет устройства,
     *         ErrorCode::Format — в источнике нет или не разбираются нужные таблицы.
     */
    [[nodiscard]] Result<std::string> importDevice(const ConfigDocument& source,
                                                   const std::string& deviceId);

    /**
     * @brief Добавляет устройство из базы ИК-кодов Logitech.
     *
     * Команды становятся «сырыми» сигналами (SsIr.bin); аппаратные кнопки
     * назначаются по типам команд (громкость, цифры, стрелки…), остальные
     * команды — экранными кнопками (питание и входы первыми). Питание и входы
     * описываются, если база их описывает и нужные команды перенеслись; иначе
     * питание — вручную. В существующих активностях устройство выключено.
     * @param device Запись базы.
     * @param protocols Протоколы базы (для сигналов команд).
     * @return Итог; ErrorCode::Unsupported — ни одна команда не преобразовалась.
     */
    [[nodiscard]] Result<DbImportReport> importDbDevice(const irdb::DbDevice& device,
                                                        const irdb::ProtocolLibrary& protocols);

    /**
     * @brief Добавляет активность в конец списка.
     *
     * Аппаратные кнопки назначаются по planHardButtons(); устройства не из
     * черновика выключаются при её запуске.
     * @param draft Черновик активности.
     * @return Id новой активности; ErrorCode::Format — нет имени или устройств,
     *         ErrorCode::NotFound — устройство не из этой конфигурации.
     */
    [[nodiscard]] Result<std::string> addActivity(const ActivityDraft& draft);

    // --- Что не отправлять на пульт -----------------------------------------------
    // Помеченное остаётся в конфигурации и файлах, но в конфигурацию для пульта
    // (toEzHex(true)) не входит. Активность, которой нужно не отправляемое
    // устройство, тоже не отправляется — без устройства она бы не работала.

    /**
     * @brief Помечает устройство «не отправлять на пульт» или снимает пометку.
     * @return ErrorCode::NotFound — нет устройства.
     */
    [[nodiscard]] Result<void> setDeviceNotSent(const std::string& deviceId, bool notSent);

    /**
     * @brief Помечает активность «не отправлять на пульт» или снимает пометку.
     * @return ErrorCode::NotFound — нет активности; ErrorCode::Unsupported — «выключить всё».
     */
    [[nodiscard]] Result<void> setActivityNotSent(const std::string& activityId, bool notSent);

    /**
     * @brief Активности, которые не уйдут на пульт: помеченные и те, которым нужно
     *        не отправляемое устройство (включение, роль, кнопки).
     * @return Id в порядке конфигурации.
     */
    [[nodiscard]] std::vector<std::string> activitiesNotSent() const;

    /// @return true, если хоть что-то не отправляется на пульт.
    [[nodiscard]] bool hasNotSent() const;

    // --- Замена устройства --------------------------------------------------------

    /**
     * @brief Команды устройства, которые используют активности (кнопки и шаги запуска).
     * @param activities Активности (id); пусто — все.
     * @return Команда и сколько раз используется, по имени.
     */
    [[nodiscard]] std::vector<std::pair<std::string, int>> usedDeviceCommands(
        const std::string& deviceId, const std::vector<std::string>& activities = {}) const;

    /**
     * @brief Команда устройства @p newId для команды другого устройства.
     *
     * То же имя; то же без учёта регистра, пробелов и «_»; привычный синоним
     * («Mute» = «VolumeMute», «1» = «Number1», «Power» = «PowerToggle»…).
     * @return Пусто — не нашлось.
     */
    [[nodiscard]] std::string matchDeviceCommand(const std::string& newId, const std::string& command) const;

    /**
     * @brief Ставит новое устройство на место прежнего в активностях (см. deviceReplacement.h).
     * @return Что получилось; ErrorCode::NotFound — нет устройства.
     */
    [[nodiscard]] Result<DeviceReplacementReport> replaceDevice(const DeviceReplacement& replacement);

    // --- Питание и входы ----------------------------------------------------------

    /**
     * @brief Как пульт включает, выключает устройство и переключает входы (см. deviceStates.h).
     * @return ErrorCode::NotFound — нет устройства.
     */
    [[nodiscard]] Result<DeviceStates> deviceStates(const std::string& deviceId) const;

    /**
     * @brief Задаёт питание и входы.
     *
     * Другие состояния устройства (вспомогательные, как InputType у Logitech) не трогаются.
     * Питание «вручную» — устройство не выключается «кнопкой Off»; с командами — выключается.
     * Активности, которые переключали на исчезнувший вход, покажет проверка.
     * @return ErrorCode::NotFound — нет устройства или команды; ErrorCode::Format — нет шагов,
     *         нет входов, вход без имени или два с одним именем.
     */
    [[nodiscard]] Result<void> setDeviceStates(const std::string& deviceId, const DeviceStates& states);

    // --- Проверка ------------------------------------------------------------------

    /**
     * @brief Ищет, что на пульте не сработает или сработает не так (см. configCheck.h).
     * @return Проблемы: сначала ошибки, затем предупреждения, в порядке конфигурации.
     */
    [[nodiscard]] std::vector<Problem> check() const;

    /**
     * @brief Исправляет проблему, найденную check().
     * @return ErrorCode::Unsupported — не исправляется сама; ErrorCode::NotFound — её уже нет.
     */
    [[nodiscard]] Result<void> fixProblem(const Problem& problem);

    /**
     * @brief Исправляет всё, что исправляется само (после каждого исправления — проверка заново).
     * @return Сколько исправлено.
     */
    [[nodiscard]] Result<std::size_t> fixAll();

    /// Внутреннее устройство (непрозрачно; описано в реализации ядра).
    struct Impl;

private:
    explicit ConfigDocument(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

} // namespace harmony
