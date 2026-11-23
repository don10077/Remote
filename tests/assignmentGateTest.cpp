// A1: решения гейта назначения.
//
// T4 фиксировал текущее поведение: «список клавиш модели пуст — действия закрыты». A1 меняет
// правило: гейт — «есть документ», а список нужен только там, где решается, на какую клавишу
// можно назначить. Поэтому прежние проверки T4 здесь переворачиваются (правило блока A, п. 5).
//
// `MainWindow` в тестовые цели не входит, поэтому тест вызывает само решение: `gateOpen()` и
// `buttonAllowed()`. Метод `MainWindow::assignmentAllowed()` — тонкая обёртка над ними.

#include "../app/src/assignmentGate.h"
#include "../app/src/modelKeysQt.h"
#include "../app/src/remoteResources.h"

#include "harmony/core/modelKeys.h"

#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <string>

using harmony::app::ButtonAction;
using harmony::app::ButtonRef;
using harmony::app::buttonAllowed;
using harmony::app::gateOpen;
using harmony::app::modelKeysFor;

namespace {
namespace resources = harmony::app::remoteResources;

/// Список клавиш читается через снимок ресурсов — так же, как его читает приложение.
class AssignmentGateTest : public testing::Test {
protected:
    QTemporaryDir root;
    void SetUp() override {
        ASSERT_TRUE(root.isValid());
        ASSERT_TRUE(resources::reload(root.path()).isEmpty());
    }
    void TearDown() override { resources::reload(root.path()); }
};

bool hasName(const harmony::ModelKeys& keys, const std::string& name) {
    for (const auto& key : keys.keys)
        if (key.name == name)
            return true;
    return false;
}

} // namespace

// Гейт A1: есть документ — действия открыты; пустой список клавиш ничего не запрещает.
// Прежнее ожидание T4 (`NoDocumentMeansEmptyList`) остаётся: без документа закрыто.
TEST_F(AssignmentGateTest, GateOpenMeansDocument) {
    EXPECT_FALSE(gateOpen(false));
    EXPECT_TRUE(gateOpen(true)) << "изменится в A1: было «список пуст — закрыто»";
}

// 525 — модель без профиля и без таблицы кодов: прежде список был пуст и гейт закрыт.
// После A1 список даёт документ, и гейт открыт.
TEST_F(AssignmentGateTest, ModelWithoutTableOpensGateForItsDocument) {
    const auto keys = modelKeysFor("525", nullptr);
    EXPECT_TRUE(keys.vocabularyFallback);
    EXPECT_TRUE(gateOpen(true));
    // Без исчерпывающего списка запрещать по нему нельзя: любая клавиша словаря допустима.
    EXPECT_TRUE(buttonAllowed(ButtonRef::hard("Teletext"), keys, ButtonAction::AssignTo, false));
}

// Экранные кнопки назначаются у модели без профиля: решение о кнопке не зависит от списка
// клавиш модели. Место кнопки проверяется на месте вызова (`assignCommand`), как в T4.
TEST_F(AssignmentGateTest, ScreenButtonsAreAllowedWithoutModelKeys) {
    const harmony::ModelKeys empty;
    EXPECT_TRUE(buttonAllowed(ButtonRef::screen(0), empty, ButtonAction::AssignTo, false));
    EXPECT_TRUE(buttonAllowed(ButtonRef::screen(3), empty, ButtonAction::TakeFrom, false));
    EXPECT_TRUE(buttonAllowed(ButtonRef::screen(-1), empty, ButtonAction::TakeFrom, false))
        << "новая экранная кнопка: место проверяется на месте вызова, не в гейте";
}

// Физическая клавиша, которой у модели нет: назначить нельзя, когда список исчерпывающий.
TEST_F(AssignmentGateTest, PhysicalKeyOutsideCompleteListCannotBeAssigned) {
    const auto keys = modelKeysFor("650", nullptr);
    ASSERT_TRUE(keys.complete) << "650 — профиль ответил, список исчерпывающий";

    const std::string outside = "SoftRight4";
    ASSERT_FALSE(hasName(keys, outside)) << "имени в списке нет — иначе тест ничего не решает";
    EXPECT_FALSE(buttonAllowed(ButtonRef::hard(QString::fromStdString(outside)), keys,
                               ButtonAction::AssignTo, false));
    // Снять или перенести существующее назначение такой клавиши можно — иначе предупреждение C1
    // нельзя исправить (правило A1.3).
    EXPECT_TRUE(buttonAllowed(ButtonRef::hard(QString::fromStdString(outside)), keys,
                              ButtonAction::TakeFrom, false));

    const std::string inside = keys.keys.front().name;
    EXPECT_TRUE(buttonAllowed(ButtonRef::hard(QString::fromStdString(inside)), keys,
                              ButtonAction::AssignTo, false)) << inside;
}

// Неполный список (880): клавиша вне него может существовать — назначения не запрещаем.
TEST_F(AssignmentGateTest, KeyOutsideIncompleteTableCanBeAssigned) {
    const auto keys = modelKeysFor("880", nullptr);
    ASSERT_FALSE(keys.complete) << "таблица 880 неполная";
    EXPECT_TRUE(buttonAllowed(ButtonRef::hard("Glow"), keys, ButtonAction::AssignTo, false));
}

// У модели без профиля (885) физическая клавиша из таблицы модели назначается.
TEST_F(AssignmentGateTest, ModelWithoutProfileAssignsKeysOfItsTable) {
    const auto keys = modelKeysFor("885", nullptr);
    ASSERT_TRUE(keys.complete);
    const std::string name = keys.keys.front().name;
    EXPECT_TRUE(buttonAllowed(ButtonRef::hard(QString::fromStdString(name)), keys,
                              ButtonAction::AssignTo, false)) << name;
}

// Пустое имя: в окне правки клавишу ещё выбирается, а назначать на пустую клавишу нельзя.
TEST_F(AssignmentGateTest, EmptyNameAllowedOnlyInDialog) {
    const auto keys = modelKeysFor("650", nullptr);
    EXPECT_FALSE(buttonAllowed(ButtonRef::hard(""), keys, ButtonAction::AssignTo, false));
    EXPECT_TRUE(buttonAllowed(ButtonRef::hard(""), keys, ButtonAction::TakeFrom, true));
}
