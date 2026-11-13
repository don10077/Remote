#pragma once

/**
 * @file signalIndex.h
 * @brief «Какая модель шлёт такой сигнал?» — первый кадр каждой команды базы ИК-кодов.
 *
 * Кадр — длительности (импульс, пауза, импульс, …, мкс) до первой длинной паузы,
 * за которой обычно идёт повтор. Одинаковые коды у многих моделей, поэтому каждый
 * различный кадр хранится один раз со списком устройств, которые его шлют.
 *
 * Сигнал родного пульта, выученный Harmony, ищется в индексе с допуском
 * (SignalIndex::similar): у живого пульта длительности «плавают» на десятки мкс.
 *
 * Формат (little endian, в базе хранится сжатым zlib):
 * @code
 *   u32 число кадров
 *   кадры (по возрастанию длины) × { u16 длина | u16 × длина | u32 число устройств | u32 × число }
 * @endcode
 * Номера устройств — номера в каталоге базы (IrDatabase::entries()).
 */

#include "harmony/core/error.h"
#include "harmony/core/ir/irSignal.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace harmony::irdb {

/// Кадр сигнала: длительности по модулю, мкс (чётные — импульсы, нечётные — паузы).
using Frame = std::vector<std::uint16_t>;

/// Индекс сигналов базы для поиска модели по кнопкам родного пульта.
class SignalIndex {
public:
    /// Пауза длиннее этой заканчивает кадр, мкс.
    static constexpr int kGapUs = 10000;
    /// Кадры короче этого ничего не говорят о модели.
    static constexpr std::size_t kMinFrame = 8;

    /**
     * @brief Первый кадр сигнала: от первого импульса до первой длинной паузы (её не включая).
     * @return Пусто — в сигнале нет импульсов.
     */
    [[nodiscard]] static Frame firstFrame(const ir::IrSignal& signal);

    /**
     * @brief Та же кнопка: длины равны, каждая длительность в пределах 25 % (не меньше 200 мкс).
     *
     * Последняя длительность (конец кадра) не сравнивается: у разных пультов
     * и протоколов последний импульс бывает разной длины.
     */
    [[nodiscard]] static bool similar(std::span<const std::uint16_t> a,
                                      std::span<const std::uint16_t> b);

    /**
     * @brief Разбирает индекс (уже распакованный).
     * @return ErrorCode::Format — данные повреждены.
     */
    [[nodiscard]] static Result<SignalIndex> parse(std::string_view data);

    /**
     * @brief Устройства, которые шлют похожий кадр.
     * @return Номера в каталоге базы, по возрастанию, без повторов.
     */
    [[nodiscard]] std::vector<std::uint32_t> lookup(std::span<const std::uint16_t> frame) const;

    /// @return Число различных кадров.
    [[nodiscard]] std::size_t frameCount() const { return frames_.size(); }

private:
    /// Кадр в общих массивах значений и устройств.
    struct Entry {
        std::size_t values = 0; ///< Начало в values_
        std::uint16_t length = 0;
        std::size_t devices = 0; ///< Начало в devices_
        std::uint32_t deviceCount = 0;
    };

    std::vector<std::uint16_t> values_;
    std::vector<std::uint32_t> devices_;
    std::vector<Entry> frames_; ///< По возрастанию длины
    /// Длина кадра → диапазон [первый, последний) в frames_.
    std::unordered_map<std::uint16_t, std::pair<std::size_t, std::size_t>> byLength_;
};

/// Собирает индекс при сборке базы.
class SignalIndexBuilder {
public:
    SignalIndexBuilder() = default;
    // frames_ указывают на ключи ids_: копия указывала бы в чужую таблицу.
    SignalIndexBuilder(const SignalIndexBuilder&) = delete;
    SignalIndexBuilder& operator=(const SignalIndexBuilder&) = delete;
    SignalIndexBuilder(SignalIndexBuilder&&) noexcept = default;
    SignalIndexBuilder& operator=(SignalIndexBuilder&&) noexcept = default;
    /**
     * @brief Добавляет сигнал команды устройства.
     * @param device Номер устройства (любой; при записи переводится через @p order в write()).
     */
    void add(std::uint32_t device, const ir::IrSignal& signal);

    /**
     * @brief Индекс в формате файла (без сжатия).
     * @param order Номер устройства при add() → номер в каталоге; пусто — без перевода.
     */
    [[nodiscard]] std::string write(const std::vector<std::uint32_t>& order = {}) const;

    /// @return Число различных кадров.
    [[nodiscard]] std::size_t frameCount() const { return frames_.size(); }

private:
    /// Кадр (значения u16 подряд, как в памяти) → номер. Ключ и есть хранилище кадра.
    std::unordered_map<std::string, std::uint32_t> ids_;
    std::vector<const std::string*> frames_; ///< Номер → ключ в ids_ (узлы не перемещаются)
    std::vector<std::vector<std::uint32_t>> devices_;
};

} // namespace harmony::irdb
