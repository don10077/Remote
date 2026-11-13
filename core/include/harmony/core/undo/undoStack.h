#pragma once

/**
 * @file undoStack.h
 * @brief Отмена и повтор изменений (undo/redo) без зависимости от Qt.
 *
 * Любое изменение документа оформляется командой UndoCommand и проходит
 * через UndoStack::push(). Стек поддерживает:
 *  - слияние однотипных команд (например, посимвольный ввод имени);
 *  - макрокоманды: несколько команд отменяются одним шагом;
 *  - «чистое» состояние — совпадает ли документ с сохранённым файлом;
 *  - ограничение глубины истории;
 *  - уведомление интерфейса об изменениях.
 */

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace harmony::undo {

/**
 * @brief Базовый класс команды, которую можно отменить и повторить.
 *
 * Контракт: redo() переводит документ из состояния «до» в «после»,
 * undo() — обратно. Первый redo() вызывает сам стек в push().
 */
class UndoCommand {
public:
    virtual ~UndoCommand() = default;

    /// Применяет изменение (также вызывается при первом добавлении в стек).
    virtual void redo() = 0;

    /// Отменяет изменение.
    virtual void undo() = 0;

    /**
     * @brief Текст для меню «Отменить …» / «Повторить …».
     * @return Короткое описание действия.
     */
    [[nodiscard]] virtual std::string text() const = 0;

    /**
     * @brief Идентификатор типа команды для слияния.
     * @return -1 — команда никогда не сливается; иначе команды с одинаковым id
     *         пробуют слиться через mergeWith().
     */
    [[nodiscard]] virtual int id() const { return -1; }

    /**
     * @brief Пытается поглотить следующую команду того же типа.
     *
     * Вызывается, когда новая команда уже применена (redo() выполнен).
     * При успехе текущая команда должна при undo() откатывать и эффект @p next.
     *
     * @param next Новая команда с тем же id().
     * @return true, если слияние выполнено и @p next больше не нужна.
     */
    virtual bool mergeWith(const UndoCommand& next) {
        (void)next;
        return false;
    }

protected:
    UndoCommand() = default;
    UndoCommand(const UndoCommand&) = default;
    UndoCommand& operator=(const UndoCommand&) = default;
};

class MacroCommand; // Внутренний составной класс для beginMacro()/endMacro()

/**
 * @brief Стек истории изменений документа.
 *
 * Не потокобезопасен: используется из одного (GUI) потока.
 */
class UndoStack {
public:
    /// Лимит шагов нового стека: каждый шаг правки документа хранит его снимки.
    static constexpr std::size_t kDefaultLimit = 100;

    UndoStack();
    ~UndoStack();

    UndoStack(const UndoStack&) = delete;
    UndoStack& operator=(const UndoStack&) = delete;
    UndoStack(UndoStack&&) noexcept;
    UndoStack& operator=(UndoStack&&) noexcept;

    /**
     * @brief Применяет команду и добавляет её в историю.
     *
     * Отменённые ранее шаги (ветка redo) удаляются. Если открыт макрос,
     * команда становится его частью.
     *
     * @param command Команда; не должна быть nullptr.
     * @throws std::invalid_argument если @p command пустой.
     * @note Если redo() команды бросает исключение, стек не изменяется.
     */
    void push(std::unique_ptr<UndoCommand> command);

    /**
     * @brief Отменяет последний применённый шаг.
     * @return false, если отменять нечего.
     * @throws std::logic_error если открыт макрос.
     * @note Если undo() команды бросает исключение, позиция в истории не меняется.
     */
    bool undo();

    /**
     * @brief Повторяет последний отменённый шаг.
     * @return false, если повторять нечего.
     * @throws std::logic_error если открыт макрос.
     */
    bool redo();

    /**
     * @brief Открывает макрокоманду: все push() до endMacro() отменяются одним шагом.
     *
     * Макросы могут быть вложенными.
     * @param text Описание шага для меню.
     */
    void beginMacro(std::string text);

    /**
     * @brief Закрывает текущую макрокоманду. Пустой макрос в историю не попадает.
     * @throws std::logic_error если макрос не был открыт.
     */
    void endMacro();

    /// Удаляет всю историю и закрывает незавершённые макросы. Состояние становится «чистым».
    void clear();

    /// Отмечает текущее состояние как сохранённое («чистое»).
    void setClean();

    /**
     * @brief Ограничивает число шагов в истории (по умолчанию kDefaultLimit).
     * @param maxSteps Максимум шагов; 0 — без ограничения.
     * Лишние старые шаги удаляются при следующем push().
     */
    void setLimit(std::size_t maxSteps);

    /**
     * @brief Задаёт обработчик, вызываемый после любого изменения стека.
     * @param handler Функция без параметров (например, обновить пункты меню). Может быть пустой.
     */
    void setOnChanged(std::function<void()> handler);

    /// @return true, если есть шаг для отмены.
    [[nodiscard]] bool canUndo() const;

    /// @return true, если есть шаг для повтора.
    [[nodiscard]] bool canRedo() const;

    /// @return Описание шага, который будет отменён, или пустая строка.
    [[nodiscard]] std::string undoText() const;

    /// @return Описание шага, который будет повторён, или пустая строка.
    [[nodiscard]] std::string redoText() const;

    /// @return true, если документ совпадает с последним сохранённым состоянием.
    [[nodiscard]] bool isClean() const;

    /// @return true, если открыт хотя бы один макрос.
    [[nodiscard]] bool isMacroOpen() const;

    /// @return Общее число шагов в истории (применённые + отменённые).
    [[nodiscard]] std::size_t count() const;

    /// @return Число применённых шагов (позиция в истории).
    [[nodiscard]] std::size_t index() const;

private:
    /// Добавляет уже применённую команду в историю (слияние, обрезка, лимит).
    void appendApplied(std::unique_ptr<UndoCommand> command);

    /// Удаляет самые старые шаги сверх лимита.
    void applyLimit();

    /// Вызывает обработчик изменений, если он задан.
    void notifyChanged() const;

    std::vector<std::unique_ptr<UndoCommand>> commands_;   ///< История шагов
    std::size_t index_ = 0;                                ///< Число применённых шагов
    std::optional<std::size_t> cleanIndex_ = 0;            ///< Позиция «чистого» состояния; nullopt — недостижимо
    std::size_t limit_ = kDefaultLimit;                    ///< Лимит шагов; 0 — без лимита
    std::vector<std::unique_ptr<MacroCommand>> macroStack_; ///< Открытые макросы (вложенные)
    std::function<void()> onChanged_;                      ///< Обработчик изменений
};

} // namespace harmony::undo
