/**
 * @file undoStackTest.cpp
 * @brief Тесты стека отмены: undo/redo, слияние, макросы, «чистое» состояние, лимит.
 */

#include "harmony/core/undo/undoStack.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

using harmony::undo::UndoCommand;
using harmony::undo::UndoStack;

namespace {

/**
 * @brief Тестовая команда: прибавляет delta к целому значению.
 *
 * При mergeable = true команды сливаются (как посимвольный ввод).
 */
class AddCommand final : public UndoCommand {
public:
    AddCommand(int& target, int delta, bool mergeable = false)
        : target_(target), delta_(delta), mergeable_(mergeable) {}

    void redo() override { target_ += delta_; }
    void undo() override { target_ -= delta_; }
    [[nodiscard]] std::string text() const override { return "add " + std::to_string(delta_); }
    [[nodiscard]] int id() const override { return mergeable_ ? 1 : -1; }

    bool mergeWith(const UndoCommand& next) override {
        // id() совпал, значит next — тоже AddCommand.
        delta_ += static_cast<const AddCommand&>(next).delta_;
        return true;
    }

private:
    int& target_;
    int delta_;
    bool mergeable_;
};

/// Команда, у которой redo() всегда бросает исключение.
class FailingCommand final : public UndoCommand {
public:
    void redo() override { throw std::runtime_error("redo failed"); }
    void undo() override {}
    [[nodiscard]] std::string text() const override { return "fail"; }
};

/// Сокращение для создания AddCommand.
std::unique_ptr<UndoCommand> add(int& target, int delta, bool mergeable = false) {
    return std::make_unique<AddCommand>(target, delta, mergeable);
}

} // namespace

TEST(UndoStack, PushAppliesCommand) {
    UndoStack stack;
    int value = 0;

    stack.push(add(value, 5));

    EXPECT_EQ(value, 5);
    EXPECT_TRUE(stack.canUndo());
    EXPECT_FALSE(stack.canRedo());
    EXPECT_EQ(stack.undoText(), "add 5");
}

TEST(UndoStack, UndoAndRedoRestoreState) {
    UndoStack stack;
    int value = 0;
    stack.push(add(value, 1));
    stack.push(add(value, 2));

    EXPECT_TRUE(stack.undo());
    EXPECT_EQ(value, 1);
    EXPECT_EQ(stack.redoText(), "add 2");

    EXPECT_TRUE(stack.undo());
    EXPECT_EQ(value, 0);
    EXPECT_FALSE(stack.undo()); // Отменять больше нечего

    EXPECT_TRUE(stack.redo());
    EXPECT_TRUE(stack.redo());
    EXPECT_EQ(value, 3);
    EXPECT_FALSE(stack.redo()); // Повторять больше нечего
}

TEST(UndoStack, PushAfterUndoDropsRedoBranch) {
    UndoStack stack;
    int value = 0;
    stack.push(add(value, 1));
    stack.push(add(value, 2));
    stack.undo();

    stack.push(add(value, 10));

    EXPECT_EQ(value, 11);
    EXPECT_EQ(stack.count(), 2U);
    EXPECT_FALSE(stack.canRedo());
}

TEST(UndoStack, NullCommandThrows) {
    UndoStack stack;

    EXPECT_THROW(stack.push(nullptr), std::invalid_argument);
}

TEST(UndoStack, FailedRedoLeavesStackUnchanged) {
    UndoStack stack;
    int value = 0;
    stack.push(add(value, 1));

    EXPECT_THROW(stack.push(std::make_unique<FailingCommand>()), std::runtime_error);

    EXPECT_EQ(stack.count(), 1U);
    EXPECT_EQ(stack.undoText(), "add 1");
}

TEST(UndoStack, MergesCommandsWithSameId) {
    UndoStack stack;
    int value = 0;

    stack.push(add(value, 1, true));
    stack.push(add(value, 2, true));
    stack.push(add(value, 3, true));

    EXPECT_EQ(value, 6);
    EXPECT_EQ(stack.count(), 1U);

    stack.undo();
    EXPECT_EQ(value, 0); // Один шаг отменяет всё слитое
}

TEST(UndoStack, DoesNotMergeIntoCleanState) {
    UndoStack stack;
    int value = 0;
    stack.push(add(value, 1, true));
    stack.setClean();

    stack.push(add(value, 2, true));

    EXPECT_EQ(stack.count(), 2U);
    stack.undo();
    EXPECT_TRUE(stack.isClean()); // До сохранённой версии можно вернуться
    EXPECT_EQ(value, 1);
}

TEST(UndoStack, MacroIsUndoneAsOneStep) {
    UndoStack stack;
    int value = 0;

    stack.beginMacro("group");
    stack.push(add(value, 1));
    stack.push(add(value, 2));
    EXPECT_TRUE(stack.isMacroOpen());
    EXPECT_FALSE(stack.canUndo()); // Пока макрос открыт, отмена недоступна
    stack.endMacro();

    EXPECT_EQ(value, 3);
    EXPECT_EQ(stack.count(), 1U);
    EXPECT_EQ(stack.undoText(), "group");

    stack.undo();
    EXPECT_EQ(value, 0);
    stack.redo();
    EXPECT_EQ(value, 3);
}

TEST(UndoStack, NestedMacroBecomesPartOfOuter) {
    UndoStack stack;
    int value = 0;

    stack.beginMacro("outer");
    stack.push(add(value, 1));
    stack.beginMacro("inner");
    stack.push(add(value, 10));
    stack.endMacro();
    stack.endMacro();

    EXPECT_EQ(stack.count(), 1U);
    stack.undo();
    EXPECT_EQ(value, 0);
}

TEST(UndoStack, EmptyMacroIsDiscarded) {
    UndoStack stack;

    stack.beginMacro("nothing");
    stack.endMacro();

    EXPECT_EQ(stack.count(), 0U);
    EXPECT_TRUE(stack.isClean());
}

TEST(UndoStack, MacroMisuseThrows) {
    UndoStack stack;
    int value = 0;

    EXPECT_THROW(stack.endMacro(), std::logic_error);

    stack.beginMacro("open");
    stack.push(add(value, 1));
    EXPECT_THROW(stack.undo(), std::logic_error);
    EXPECT_THROW(stack.redo(), std::logic_error);
}

TEST(UndoStack, TracksCleanState) {
    UndoStack stack;
    int value = 0;
    EXPECT_TRUE(stack.isClean());

    stack.push(add(value, 1));
    EXPECT_FALSE(stack.isClean());

    stack.setClean();
    EXPECT_TRUE(stack.isClean());

    stack.undo();
    EXPECT_FALSE(stack.isClean());
    stack.redo();
    EXPECT_TRUE(stack.isClean());
}

TEST(UndoStack, CleanStateLostWhenBranchDropped) {
    UndoStack stack;
    int value = 0;
    stack.push(add(value, 1));
    stack.setClean();
    stack.undo();

    stack.push(add(value, 5)); // Сохранённая версия была в удалённой ветке

    EXPECT_FALSE(stack.isClean());
    stack.undo();
    EXPECT_FALSE(stack.isClean());
}

TEST(UndoStack, LimitDropsOldestSteps) {
    UndoStack stack;
    stack.setLimit(2);
    int value = 0;

    stack.push(add(value, 1));
    stack.push(add(value, 2));
    stack.push(add(value, 3));

    EXPECT_EQ(stack.count(), 2U);
    stack.undo();
    stack.undo();
    EXPECT_FALSE(stack.canUndo());
    EXPECT_EQ(value, 1); // Самый старый шаг отменить уже нельзя
}

TEST(UndoStack, DefaultLimitIsOneHundredSteps) {
    UndoStack stack;
    int value = 0;
    for (std::size_t i = 0; i < UndoStack::kDefaultLimit + 5; ++i) {
        stack.push(add(value, 1));
    }
    EXPECT_EQ(UndoStack::kDefaultLimit, 100U);
    EXPECT_EQ(stack.count(), UndoStack::kDefaultLimit);
    stack.setLimit(0); // без ограничения
    stack.push(add(value, 1));
    EXPECT_EQ(stack.count(), UndoStack::kDefaultLimit + 1);
}

TEST(UndoStack, FailedUndoKeepsPosition) {
    // undo() бросает исключение: шаг остаётся применённым, его можно отменить снова.
    class FailingUndo final : public UndoCommand {
    public:
        void redo() override {}
        void undo() override { throw std::runtime_error("undo failed"); }
        [[nodiscard]] std::string text() const override { return "fail"; }
    };
    UndoStack stack;
    stack.push(std::make_unique<FailingUndo>());
    EXPECT_THROW(stack.undo(), std::runtime_error);
    EXPECT_EQ(stack.index(), 1U);
    EXPECT_TRUE(stack.canUndo());
    EXPECT_FALSE(stack.canRedo());
}

TEST(UndoStack, ClearResetsHistory) {
    UndoStack stack;
    int value = 0;
    stack.push(add(value, 1));
    stack.push(add(value, 2));

    stack.clear();

    EXPECT_EQ(stack.count(), 0U);
    EXPECT_FALSE(stack.canUndo());
    EXPECT_TRUE(stack.isClean());
}

TEST(UndoStack, NotifiesOnChanges) {
    UndoStack stack;
    int value = 0;
    int notifications = 0;
    stack.setOnChanged([&notifications] { ++notifications; });

    stack.push(add(value, 1));   // 1
    stack.undo();                // 2
    stack.redo();                // 3
    stack.setClean();            // 4
    stack.beginMacro("m");
    stack.push(add(value, 1));   // внутри макроса — без уведомления
    stack.endMacro();            // 5

    EXPECT_EQ(notifications, 5);
}
