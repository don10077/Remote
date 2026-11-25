#pragma once

/**
 * @file configBackend.h
 * @brief Нейтральный вид открытой конфигурации пульта, не зависящий от формата файла.
 *
 * Этап 2a отвязки ядра от Harmony 900 (docs/core-refactoring-plan.md): интерфейс пока
 * только читающий — модель, имя, место на диске, кому предназначена. Операции
 * редактирования остаются в ConfigDocument и появятся здесь по подэтапам 2b–2e.
 * Окно редактора и проверки могут работать с этим интерфейсом, не зная, чей формат за ним.
 */

#include "harmony/core/formats/ezhex.h"
#include "harmony/core/model/remoteConfig.h"

#include <filesystem>
#include <string>

namespace harmony {

/// Формат файла, который хранит конфигурацию.
enum class ConfigFormat {
    Harmony900, ///< UserConfiguration.xml + таблицы IrProto.bin/SsIr.bin (в том числе .EZHex)
    Hub,        ///< Файлы ресурсов Harmony Hub (правка и сохранение в папку; на пульт не пишется)
    BinaryHid,  ///< Импортированный бинарный HID: редактируемый проект с сохранённым исходным образом
};

/// Что умеет формат документа. UI спрашивает эти свойства, а не строку формата (B2).
struct BackendCapabilities {
    bool writeToRemote = false;         ///< Формат пишется на пульт; право конкретной модели решает canWriteTo().
    bool saveToSource = false;          ///< Можно сохранить обратно в исходный формат (EZHex, ZIP, папка хаба).
    bool importedApproximation = false; ///< Модель получена импортом с потерями: часть данных осталась в исходном образе.
    bool powerStateKnown = true;        ///< Известно, какие устройства включает и выключает активность.
    bool commandCodesEditable = true;   ///< ИК-код команды можно править.
    bool identityKnown = true;          ///< Формат хранит identity (архитектуру и skin); у хаба её нет.
};

/// Чтение общих сведений о конфигурации. Реализации не владеют документом: это вид на него.
class ConfigBackend {
public:
    virtual ~ConfigBackend() = default;

    /// @return Формат исходного файла.
    [[nodiscard]] virtual ConfigFormat format() const = 0;

    /// @return Что умеет формат этого документа.
    [[nodiscard]] virtual BackendCapabilities capabilities() const = 0;

    /// @return Общая модель для показа и проверок.
    [[nodiscard]] virtual const model::RemoteConfig& model() const = 0;

    /// @return Имя конфигурации для показа.
    [[nodiscard]] virtual const std::string& name() const = 0;

    /// @return Файл или папка на диске; пусто — документ ещё нигде не сохранён.
    [[nodiscard]] virtual const std::filesystem::path& location() const = 0;

    /// @return Для какого пульта конфигурация; пустое значение, если формат этого не хранит.
    [[nodiscard]] virtual formats::RemoteIdentity identity() const = 0;

    /// @return true, если формат вообще пишется на пульт.
    /// @note Право записи конкретного документа решает только canWriteTo() (W1), а не это свойство.
    [[nodiscard]] bool canWriteToRemote() const { return capabilities().writeToRemote; }
};

} // namespace harmony
