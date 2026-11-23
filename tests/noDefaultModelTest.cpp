/**
 * @file noDefaultModelTest.cpp
 * @brief U2: модели по умолчанию нет — что программа решает без документа.
 *
 * Здесь только ожидания (правило 2 [00-INDEX.md]). Заглушки этапа А:
 * `NewConfigDialog::modelId()` отвечает пустой строкой и списка моделей нет;
 * `RemoteView::setRemoteProfile("")` подставляет модель по умолчанию; `ModernShell` показывает
 * название пульта по умолчанию.
 *
 * Решения шага:
 * 1. пустой id — модель не выбрана, ничего не подставляется (Q11);
 * 2. без документа карточка показывает название программы, а не название пульта по умолчанию;
 * 3. новый проект: модель выбирает пользователь — весь каталог и пункт «Без модели», по
 *    умолчанию — «Без модели»;
 * 4. тест 4 — проверка фактов M2/M3, красным быть не мог (правило 19): там решается skin
 *    документа, а не модель по умолчанию. Ожидание шага «новый проект „Без модели“ →
 *    `targetRemote()` пуст» этому факту противоречит — вопрос записан в отчёте.
 */

#include "../app/src/modern/modernShell.h"
#include "../app/src/modern/remoteView.h"
#include "../app/src/widgets/newConfigDialog.h"

#include "harmony/core/configDocument.h"
#include "harmony/core/remoteCatalog.h"

#include <QComboBox>
#include <QLabel>
#include <QString>
#include <gtest/gtest.h>

using harmony::app::ModernShell;
using harmony::app::NewConfigDialog;
using harmony::app::RemoteView;

// 1. Пустой id — модель не выбрана: подстановки модели по умолчанию нет (Q11).
TEST(NoDefaultModelTest, RemoteViewWithoutModelHasNoProfile) {
    RemoteView view;
    view.setRemoteProfile(QString());
    EXPECT_FALSE(view.hasProfile()) << "U2: модели по умолчанию нет — оформлению отвечать нечем";
}

// 2. Без документа карточка показывает название программы.
TEST(NoDefaultModelTest, CardTitleWithoutDocumentIsProgramName) {
    ModernShell shell;
    auto* title = shell.findChild<QLabel*>(QStringLiteral("appTitle"));
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->text(), QStringLiteral("Harmony"))
        << "название программы, а не название пульта по умолчанию";
}

// 3. Новый проект: модель выбирает пользователь, список — весь каталог, первый пункт — «Без модели».
TEST(NoDefaultModelTest, NewProjectModelComesFromTheChoice) {
    NewConfigDialog dialog;
    auto* choice = dialog.findChild<QComboBox*>(QStringLiteral("modelChoice"));
    ASSERT_NE(choice, nullptr) << "U2: в окне создания есть выбор модели из каталога";

    EXPECT_EQ(dialog.modelId(), QString()) << "по умолчанию — «Без модели»";

    const int index = choice->findData(QStringLiteral("885"));
    ASSERT_GE(index, 0) << "список — весь каталог: модели без профиля тоже";
    choice->setCurrentIndex(index);
    EXPECT_EQ(dialog.modelId(), QStringLiteral("885")) << "окно отвечает выбранную модель";

    EXPECT_EQ(choice->count(), 1 + static_cast<int>(harmony::remoteCatalog().size()))
        << "один пункт «Без модели» и каждое модель каталога";
}

// 4. Факт M2/M3 (красным быть не мог): свойство важнее skin; без свойства — модель по skin.
//   Для нового пустого проекта skin по умолчанию 61, то есть 900, поэтому «Без модели» даёт не
//   пустой список. Это не решение U2 — вопрос записан в отчёте.
TEST(NoDefaultModelTest, PropertyWinsOverSkinAndDefaultSkinMeansModel900) {
    auto document = harmony::ConfigDocument::fromXml("<Root><Properties></Properties></Root>", "u2");
    ASSERT_TRUE(document) << document.error().message;
    EXPECT_EQ(document->identity().skin, 61) << "skin по умолчанию в `formats::RemoteIdentity`";
    EXPECT_EQ(document->targetRemote(), "900") << "M2: без свойства — модель по skin";

    const auto applied = document->setTargetRemote("885");
    ASSERT_TRUE(applied) << applied.error().message;
    EXPECT_EQ(document->targetRemote(), "885") << "свойство важнее skin (M3)";

    const auto cleared = document->setTargetRemote("");
    ASSERT_TRUE(cleared) << cleared.error().message;
    EXPECT_EQ(document->targetRemote(), "900") << "свойства нет — снова по skin";
}
