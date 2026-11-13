/**
 * @file undoStack.cpp
 * @brief Реализация стека отмены и макрокоманд.
 */

#include "harmony/core/undo/undoStack.h"

#include <ranges>
#include <stdexcept>
#include <utility>

namespace harmony::undo {

/**
 * @brief Составная команда: группа уже применённых команд, отменяемых одним шагом.
 */
class MacroCommand final : public UndoCommand {
public:
    /**
     * @brief Создаёт пустой макрос.
     * @param text Описание шага для меню.
     */
    explicit MacroCommand(std::string text) : text_(std::move(text)) {}

    /// Добавляет уже применённую команду в конец макроса.
    void add(std::unique_ptr<UndoCommand> command) { children_.push_back(std::move(command)); }

    /// @return true, если в макросе нет ни одной команды.
    [[nodiscard]] bool empty() const { return children_.empty(); }

    void redo() override {
        // Повтор — в прямом порядке.
        for (auto& child : children_) {
            child->redo();
        }
    }

    void undo() override {
        // Отмена — в обратном порядке, чтобы каждая команда видела своё состояние «после».
        for (auto& child : children_ | std::views::reverse) {
            child->undo();
        }
    }

    [[nodiscard]] std::string text() const override { return text_; }

private:
    std::string text_;
    std::vector<std::unique_ptr<UndoCommand>> children_;
};

UndoStack::UndoStack() = default;
UndoStack::~UndoStack() = default;
UndoStack::UndoStack(UndoStack&&) noexcept = default;
UndoStack& UndoStack::operator=(UndoStack&&) noexcept = default;

void UndoStack::push(std::unique_ptr<UndoCommand> command) {
    if (!command) {
        throw std::invalid_argument("UndoStack::push: command is null");
    }

    // Сначала применяем: если redo() бросит исключение, стек останется прежним.
    command->redo();

    if (!macroStack_.empty()) {
        // Внутри макроса команда копится в нём, история изменится в endMacro().
        macroStack_.back()->add(std::move(command));
        return;
    }

    appendApplied(std::move(command));
    notifyChanged();
}

void UndoStack::appendApplied(std::unique_ptr<UndoCommand> command) {
    // Новая правка отменяет ветку redo.
    if (commands_.size() > index_) {
        commands_.erase(commands_.begin() + static_cast<std::ptrdiff_t>(index_), commands_.end());
        // «Чистое» состояние было в удалённой ветке — вернуться к нему больше нельзя.
        if (cleanIndex_ && *cleanIndex_ > index_) {
            cleanIndex_.reset();
        }
    }

    // Пробуем слить с предыдущим шагом. В «чистое» состояние не сливаем,
    // иначе отмена до сохранённой версии стала бы невозможной.
    if (index_ > 0 && command->id() >= 0 && cleanIndex_ != index_) {
        UndoCommand& last = *commands_[index_ - 1];
        if (last.id() == command->id() && last.mergeWith(*command)) {
            return;
        }
    }

    commands_.push_back(std::move(command));
    ++index_;
    applyLimit();
}

void UndoStack::applyLimit() {
    if (limit_ == 0) {
        return;
    }
    // Удаляем только применённые шаги с начала истории; ветку redo не трогаем.
    while (commands_.size() > limit_ && index_ > 0) {
        commands_.erase(commands_.begin());
        --index_;
        if (cleanIndex_) {
            cleanIndex_ = (*cleanIndex_ == 0) ? std::nullopt : std::optional(*cleanIndex_ - 1);
        }
    }
}

bool UndoStack::undo() {
    if (!macroStack_.empty()) {
        throw std::logic_error("UndoStack::undo: macro is open");
    }
    if (!canUndo()) {
        return false;
    }
    // Сначала отмена: если она бросит исключение, позиция в истории не изменится.
    commands_[index_ - 1]->undo();
    --index_;
    notifyChanged();
    return true;
}

bool UndoStack::redo() {
    if (!macroStack_.empty()) {
        throw std::logic_error("UndoStack::redo: macro is open");
    }
    if (!canRedo()) {
        return false;
    }
    commands_[index_]->redo();
    ++index_;
    notifyChanged();
    return true;
}

void UndoStack::beginMacro(std::string text) {
    macroStack_.push_back(std::make_unique<MacroCommand>(std::move(text)));
}

void UndoStack::endMacro() {
    if (macroStack_.empty()) {
        throw std::logic_error("UndoStack::endMacro: no open macro");
    }

    std::unique_ptr<MacroCommand> macro = std::move(macroStack_.back());
    macroStack_.pop_back();

    if (macro->empty()) {
        return; // Пустой макрос не создаёт шаг истории.
    }

    if (!macroStack_.empty()) {
        // Вложенный макрос становится частью внешнего.
        macroStack_.back()->add(std::move(macro));
        return;
    }

    // Команды макроса уже применены в push(), повторно redo() не вызываем.
    appendApplied(std::move(macro));
    notifyChanged();
}

void UndoStack::clear() {
    commands_.clear();
    macroStack_.clear();
    index_ = 0;
    cleanIndex_ = 0;
    notifyChanged();
}

void UndoStack::setClean() {
    cleanIndex_ = index_;
    notifyChanged();
}

void UndoStack::setLimit(std::size_t maxSteps) {
    limit_ = maxSteps;
}

void UndoStack::setOnChanged(std::function<void()> handler) {
    onChanged_ = std::move(handler);
}

bool UndoStack::canUndo() const {
    return macroStack_.empty() && index_ > 0;
}

bool UndoStack::canRedo() const {
    return macroStack_.empty() && index_ < commands_.size();
}

std::string UndoStack::undoText() const {
    return canUndo() ? commands_[index_ - 1]->text() : std::string{};
}

std::string UndoStack::redoText() const {
    return canRedo() ? commands_[index_]->text() : std::string{};
}

bool UndoStack::isClean() const {
    return macroStack_.empty() && cleanIndex_ == index_;
}

bool UndoStack::isMacroOpen() const {
    return !macroStack_.empty();
}

std::size_t UndoStack::count() const {
    return commands_.size();
}

std::size_t UndoStack::index() const {
    return index_;
}

void UndoStack::notifyChanged() const {
    if (onChanged_) {
        onChanged_();
    }
}

} // namespace harmony::undo
