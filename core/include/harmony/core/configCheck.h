#pragma once

/**
 * @file configCheck.h
 * @brief Проверка конфигурации: что на пульте не сработает или сработает не так.
 *
 * ConfigDocument::check() ищет проблемы: кнопки, которые шлют несуществующие
 * команды или удалённым устройствам, две экранные кнопки на одном месте,
 * одинаковые имена, устройства, которые кнопки используют, а активность не
 * включает, и т. п. Многие проблемы исправляются сами (Problem::fix):
 * ConfigDocument::fixProblem() и fixAll().
 *
 * Тексты — шаблоны с аргументами (Message): интерфейс переводит шаблон,
 * имена подставляются как есть.
 */

#include "harmony/core/error.h"

#include <string>
#include <vector>

namespace harmony {

/// Переводимый текст: шаблон (ключ перевода) и аргументы — как у Error.
using Message = Error;

/// Текст по шаблону: say("Key sends “%1”", {command}).
[[nodiscard]] inline Message say(std::string pattern, std::vector<std::string> args = {}) {
    return Error(ErrorCode::Format, std::move(pattern), std::move(args));
}

/// Одна проблема конфигурации.
struct Problem {
    enum class Severity {
        Error, ///< Не сработает: команды или устройства нет
        Warning, ///< Сработает, но, скорее всего, не так, как задумано
    };

    enum class Where {
        Config,   ///< Конфигурация целиком
        Device,   ///< Устройство id
        Activity, ///< Активность id
    };

    /// Как исправляется.
    enum class Fix {
        None,             ///< Только вручную
        FreeHardKey,      ///< Освободить аппаратную кнопку hardKey
        RemoveSoftButton, ///< Убрать экранную кнопку на softPosition (с несуществующей командой)
        MoveSoftButton, ///< Вторую экранную кнопку на softPosition — на свободное место
        RemoveBrokenSteps, ///< Убрать шаги запуска, которые не сработают
        RemoveDeleted, ///< Убрать из активности ссылки на удалённые устройства
        CompletePowerMap, ///< Устройства без «вкл/выкл» — в «выключить»
        PowerOnDevice, ///< Включать устройство fixDevice в этой активности
        Rename, ///< Одинаковое имя: добавить номер
    };

    Severity severity = Severity::Warning;
    Where where = Where::Config;
    std::string id; ///< Устройство или активность; пусто — вся конфигурация
    std::string hardKey; ///< Аппаратная кнопка, к которой относится; пусто — нет
    int softPosition = -1; ///< Место экранной кнопки; −1 — нет
    Message what{ErrorCode::Format, std::string()}; ///< Что не так
    Message place{ErrorCode::Format, std::string()}; ///< Где: «Смотреть ТВ · VolumeUp»
    Fix fix = Fix::None;
    std::string fixDevice; ///< Устройство для Fix::PowerOnDevice

    /// @return Исправляется автоматически.
    [[nodiscard]] bool fixable() const { return fix != Fix::None; }
};

/// Сколько проблем такой важности.
[[nodiscard]] std::size_t countProblems(const std::vector<Problem>& problems,
                                        Problem::Severity severity);

/// Устройств, которые помнит Harmony 900.
inline constexpr std::size_t kDeviceLimit = 15;

} // namespace harmony
