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
};

/// Чтение общих сведений о конфигурации. Реализации не владеют документом: это вид на него.
class ConfigBackend {
public:
    virtual ~ConfigBackend() = default;

    /// @return Формат исходного файла.
    [[nodiscard]] virtual ConfigFormat format() const = 0;

    /// @return Общая модель для показа и проверок.
    [[nodiscard]] virtual const model::RemoteConfig& model() const = 0;

    /// @return Имя конфигурации для показа.
    [[nodiscard]] virtual const std::string& name() const = 0;

    /// @return Файл или папка на диске; пусто — документ ещё нигде не сохранён.
    [[nodiscard]] virtual const std::filesystem::path& location() const = 0;

    /// @return Для какого пульта конфигурация; пустое значение, если формат этого не хранит.
    [[nodiscard]] virtual formats::RemoteIdentity identity() const = 0;

    /// @return true, если конфигурацию можно записать на пульт (правятся и сохраняются все).
    [[nodiscard]] virtual bool canWriteToRemote() const = 0;
};

} // namespace harmony
