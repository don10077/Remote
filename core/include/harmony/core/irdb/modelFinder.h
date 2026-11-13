#pragma once

/**
 * @file modelFinder.h
 * @brief Поиск модели по нескольким кнопкам родного пульта.
 *
 * Пользователь нажимает на родном пульте разные кнопки, Harmony их выучивает,
 * каждая добавляется в ModelFinder::add(). Модель получает очко за каждую кнопку,
 * сигнал которой есть среди её команд. Сверху — модели, узнавшие больше кнопок;
 * среди равных — производители, у которых таких моделей больше всего.
 */

#include "harmony/core/error.h"
#include "harmony/core/irdb/signalIndex.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace harmony::irdb {

class IrDatabase;

/// Поиск модели по кнопкам родного пульта.
class ModelFinder {
public:
    /// Что стало с нажатой кнопкой.
    enum class Press {
        Added,    ///< Учтена
        Repeated, ///< Эту кнопку уже нажимали
        TooShort, ///< Сигнал слишком короткий — нажать ещё раз
    };

    /// Модель в итогах.
    struct Hit {
        std::size_t entry = 0; ///< Номер в каталоге базы
        int matched = 0;       ///< Сколько кнопок узнала
    };

    /// Итоги поиска.
    struct Results {
        std::vector<Hit> hits; ///< Лучшие сначала (не больше запрошенного)
        std::size_t total = 0; ///< Сколько моделей узнали хоть одну кнопку (после фильтра)
        int best = 0;          ///< Лучший счёт
        std::size_t bestCount = 0; ///< Сколько моделей с лучшим счётом
        /// Производители моделей с лучшим счётом: (название, моделей), больше — первыми.
        std::vector<std::pair<std::string, std::size_t>> makers;
    };

    /**
     * @brief Создаёт поиск.
     * @param database База (живёт дольше поиска).
     * @param index Индекс сигналов базы (IrDatabase::signalIndex()).
     */
    ModelFinder(const IrDatabase& database, std::shared_ptr<const SignalIndex> index);

    /// Учитывает нажатую кнопку.
    Press add(const ir::IrSignal& signal);

    /// Начать сначала.
    void reset();

    /// @return Сколько кнопок учтено.
    [[nodiscard]] std::size_t presses() const { return pressed_.size(); }

    /**
     * @brief Итоги.
     * @param filter Слова, которые должны быть в «производитель модель» («samsung»); пусто — все.
     * @param max Сколько моделей вернуть в hits.
     */
    [[nodiscard]] Results results(std::string_view filter, std::size_t max) const;

private:
    const IrDatabase& database_;
    std::shared_ptr<const SignalIndex> index_;
    std::vector<Frame> pressed_;
    std::unordered_map<std::uint32_t, int> score_; ///< Номер в каталоге → узнанных кнопок
};

} // namespace harmony::irdb
