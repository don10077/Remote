#include "../app/src/widgets/commandDialog.h"

#include <QApplication>
#include "../app/src/theme.h"
#include <QDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>

TEST(CommandDialog, FitsContentWithoutSignalTabs) {
    harmony::ConfigDocument::CommandDetails details;
    harmony::app::CommandDialog dialog("Dune BD PRIME", "PowerToggle", details);
    dialog.show();
    QApplication::processEvents();
    EXPECT_LT(dialog.height(), 400);
    EXPECT_GE(dialog.width(), 440);
    EXPECT_GE(dialog.minimumWidth(), 440);
    EXPECT_TRUE(dialog.findChildren<QTabWidget*>().isEmpty());
    for (const auto* label : dialog.findChildren<QLabel*>()) {
        EXPECT_TRUE(dialog.rect().contains(QRect(label->mapTo(&dialog, QPoint()), label->size())));
    }
    const auto buttons = dialog.findChildren<QPushButton*>();
    ASSERT_EQ(buttons.size(), 1);
    buttons.front()->click();
    EXPECT_EQ(dialog.result(), QDialog::Accepted);
}

namespace {

/// Имя идущего теста и время его начала: для сторожа зависаний.
std::mutex currentMutex;
std::string currentTest;
std::chrono::steady_clock::time_point currentStart;

/// Печатает начало каждого теста сразу (ctest без -V показывает вывод только в конце) и запоминает его.
class Announcer final : public ::testing::EmptyTestEventListener {
    void OnTestStart(const ::testing::TestInfo& info) override {
        const std::string name = std::string(info.test_suite_name()) + "." + info.name();
        {
            std::lock_guard lock(currentMutex);
            currentTest = name;
            currentStart = std::chrono::steady_clock::now();
        }
        std::fprintf(stderr, "[ START    ] %s\n", name.c_str());
        std::fflush(stderr);
    }
    void OnTestEnd(const ::testing::TestInfo&) override {
        std::lock_guard lock(currentMutex);
        currentTest.clear();
    }
};

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    if (!qEnvironmentVariableIsEmpty("HARMONY_ZONE_PREVIEW_DIR")) harmony::app::theme::apply();
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::UnitTest::GetInstance()->listeners().Append(new Announcer);

    // Модальное окно в offscreen некому закрыть: тест падает с текстом окна, а не висит.
    QTimer modalGuard;
    QObject::connect(&modalGuard, &QTimer::timeout, [] {
        QWidget* modal = QApplication::activeModalWidget();
        if (modal == nullptr) return;
        QString text = modal->windowTitle();
        if (const auto* box = qobject_cast<QMessageBox*>(modal))
            text += QStringLiteral(": ") + box->text() + QLatin1Char(' ') + box->informativeText();
        ADD_FAILURE() << "Unexpected modal " << modal->metaObject()->className() << " — " << text.toStdString();
        if (auto* dialog = qobject_cast<QDialog*>(modal)) dialog->reject();
        else modal->close();
    });
    modalGuard.start(200);

    // Зависание без цикла событий: через 2 минуты назвать тест и завершить процесс.
    std::atomic_bool finished = false;
    std::thread watchdog([&finished] {
        while (!finished) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::lock_guard lock(currentMutex);
            if (!currentTest.empty() && std::chrono::steady_clock::now() - currentStart > std::chrono::minutes(2)) {
                std::fprintf(stderr, "[ HANG     ] %s runs longer than 2 minutes\n", currentTest.c_str());
                std::fflush(stderr);
                std::abort();
            }
        }
    });
    const int result = RUN_ALL_TESTS();
    finished = true;
    watchdog.join();
    return result;
}
