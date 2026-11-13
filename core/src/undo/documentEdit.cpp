/**
 * @file documentEdit.cpp
 * @brief Команда правки документа по снимкам «до» и «после» (XML и таблицы ИК-данных).
 */

#include "harmony/core/undo/documentEdit.h"

#include <stdexcept>
#include <utility>

namespace harmony::undo {

namespace {

/// Возвращает документ к снимку; не вышло — исключение: UndoStack тогда не сдвигает историю.
void restoreOrThrow(ConfigDocument& document, const ConfigDocument::Snapshot& snapshot) {
    if (auto restored = document.restore(snapshot); !restored) {
        throw std::runtime_error(restored.error().message);
    }
}

/// Шаг истории: два снимка документа.
class DocumentEdit final : public UndoCommand {
public:
    DocumentEdit(ConfigDocument& document, std::string text, ConfigDocument::Snapshot before,
                 ConfigDocument::Snapshot after)
        : document_(document), text_(std::move(text)), before_(std::move(before)),
          after_(std::move(after)) {}

    void redo() override {
        // Первый вызов делает UndoStack::push сразу после правки — она уже применена.
        if (applied_) {
            applied_ = false;
            return;
        }
        restoreOrThrow(document_, after_);
    }

    void undo() override { restoreOrThrow(document_, before_); }

    [[nodiscard]] std::string text() const override { return text_; }

private:
    ConfigDocument& document_;
    std::string text_;
    ConfigDocument::Snapshot before_;
    ConfigDocument::Snapshot after_;
    bool applied_ = true; ///< Правка уже применена при создании команды
};

} // namespace

Result<std::unique_ptr<UndoCommand>> editDocument(
    ConfigDocument& document, std::string text,
    const std::function<Result<void>(ConfigDocument&)>& change) {
    ConfigDocument::Snapshot before = document.snapshot();
    if (auto changed = change(document); !changed) {
        // Правка из нескольких шагов могла упасть на середине — возвращаем всё как было.
        if (auto restored = document.restore(before); !restored) {
            return wrapError(restored.error(),
                             "the configuration could not be returned to its state before the failed edit (%1)",
                             {changed.error().message});
        }
        return std::unexpected(std::move(changed.error()));
    }
    return std::make_unique<DocumentEdit>(document, std::move(text), std::move(before),
                                          document.snapshot());
}

} // namespace harmony::undo
