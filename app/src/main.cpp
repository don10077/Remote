/**
 * @file main.cpp
 * @brief Точка входа графического приложения Harmony.
 */

#include "appSettings.h"
#include "help.h"
#include "i18n.h"
#include "journal.h"
#include "mainWindow.h"
#include "theme.h"
#include "widgets/disclaimerDialog.h"

#include "harmony/core/version.h"

#include <QApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QIcon>
#include <QScreen>
#include <QStyleHints>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QWidget>

namespace {

/**
 * @brief Не даёт окнам быть больше экрана.
 *
 * Размеры окон заданы для 1080p при 100 %; при 150 % доступно около 1280×720
 * логических точек, и окно 1320×820 или диалог высотой 720 выходят за край.
 * При показе любого окна его размер и минимальный размер ужимаются до
 * свободной области экрана (за вычетом рамки).
 */
class FitToScreen final : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Show && watched->isWidgetType()) {
            auto* widget = static_cast<QWidget*>(watched);
            const Qt::WindowType type = widget->windowType();
            if (widget->isWindow() && (type == Qt::Window || type == Qt::Dialog) && widget->screen() != nullptr &&
                !(widget->windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen))) {
                constexpr QSize kFrame(16, 48); // рамка и заголовок окна (размер окна их не включает)
                const QSize limit = widget->screen()->availableGeometry().size() - kFrame;
                if (widget->minimumWidth() > limit.width() || widget->minimumHeight() > limit.height()) {
                    widget->setMinimumSize(widget->minimumSize().boundedTo(limit));
                }
                if (widget->width() > limit.width() || widget->height() > limit.height()) {
                    widget->resize(widget->size().boundedTo(limit));
                }
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

} // namespace

/**
 * @brief Создаёт приложение Qt и главное окно.
 * @return Код завершения цикла событий.
 */
int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    FitToScreen fitToScreen;
    app.installEventFilter(&fitToScreen);
    harmony::app::help::installF1Handler(app);

    // Эти значения используются QSettings для пути к настройкам.
    QApplication::setApplicationName("Harmony");
    QApplication::setApplicationVersion(QString::fromUtf8(harmony::kVersion));
    QApplication::setOrganizationName("Harmony");

    // Иконка всех окон приложения (заголовок, панель задач, Alt+Tab).
    // Несколько размеров — Qt сам выберет подходящий без размытия.
    QIcon appIcon;
    for (const int size : {16, 24, 32, 48, 64, 128, 256}) {
        appIcon.addFile(QString(":/res/app/harmony-%1.png").arg(size), QSize(size, size));
    }
    QApplication::setWindowIcon(appIcon);

    // Язык интерфейса — из настроек (по умолчанию язык системы); до создания окон.
    harmony::app::i18n::install(app, harmony::app::settings::language());

    // Журнал: сообщения Qt — туда же; запуск — первой записью.
    harmony::app::journal::captureQtMessages();
    harmony::app::journal::info(QStringLiteral("Harmony %1, Qt %2").arg(QString::fromUtf8(harmony::kVersion),
                                                                        QString::fromUtf8(qVersion())));

    // Единственный дизайн приложения; применяется до создания окон.
    harmony::app::theme::apply();
    // «Как в системе»: система переключилась на тёмное или светлое — следом.
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app, [] {
        if (harmony::app::settings::themeMode() == harmony::app::settings::ThemeMode::System) {
            harmony::app::theme::apply();
        }
    });

    // Предупреждение о рисках — при первом запуске (и после изменения текста); отказ закрывает программу.
    if (!harmony::app::DisclaimerDialog::ensureAccepted()) {
        harmony::app::journal::info(QStringLiteral("Disclaimer declined, exiting"));
        return 0;
    }

    harmony::app::MainWindow window;
    window.show();
    // После появления окна: открыть конфигурацию «при запуске» или показать выбор.
    QTimer::singleShot(0, &window, [&window] { window.startup(); });

    return QApplication::exec();
}
