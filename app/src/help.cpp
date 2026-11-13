/**
 * @file help.cpp
 * @brief Показ справки и F1 в диалогах. Тексты разделов — helpContent.cpp.
 */

#include "help.h"

#include "mainWindow.h"
#include "widgets/activityDialog.h"
#include "widgets/addDeviceDialog.h"
#include "widgets/buttonDialog.h"
#include "widgets/checkDialog.h"
#include "widgets/commandDialog.h"
#include "widgets/compareConfigsDialog.h"
#include "widgets/compareDevicesDialog.h"
#include "widgets/copyButtonsDialog.h"
#include "widgets/deviceStatesDialog.h"
#include "widgets/findByRemoteDialog.h"
#include "widgets/helpDialog.h"
#include "widgets/journalDialog.h"
#include "widgets/learnWizardDialog.h"
#include "widgets/newConfigDialog.h"
#include "widgets/newDeviceDialog.h"
#include "widgets/propertiesDialog.h"
#include "widgets/protocolCommandDialog.h"
#include "widgets/quickSearch.h"
#include "widgets/remoteProfileDialog.h"
#include "widgets/remoteSettingsDialog.h"
#include "widgets/replaceDeviceDialog.h"
#include "widgets/settingsDialog.h"
#include "widgets/startDialog.h"
#include "widgets/versionsDialog.h"
#include "widgets/writePreviewDialog.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QVariant>

#include <optional>

namespace harmony::app::help {

namespace {

/**
 * @return Раздел справки для окна программы; nullopt — окно незнакомое.
 *
 * Сопоставление по типу, а не по имени класса: переименование диалога
 * сломает сборку, а не молча собьёт справку.
 */
std::optional<Topic> topicOf(const QWidget* window) {
    // Окно без своего класса (QMessageBox) задаёт раздел свойством.
    if (const QVariant property = window->property(kTopicProperty); property.isValid()) {
        return static_cast<Topic>(property.toInt());
    }
    const QObject* object = window;
    if (qobject_cast<const ActivityDialog*>(object)) {
        return Topic::Activities;
    }
    if (qobject_cast<const AddDeviceDialog*>(object) || qobject_cast<const NewDeviceDialog*>(object) ||
        qobject_cast<const FindByRemoteDialog*>(object) || qobject_cast<const ReplaceDeviceDialog*>(object) ||
        qobject_cast<const CompareDevicesDialog*>(object) || qobject_cast<const PropertiesDialog*>(object)) {
        return Topic::Devices;
    }
    if (qobject_cast<const LearnWizardDialog*>(object) || qobject_cast<const ProtocolCommandDialog*>(object) ||
        qobject_cast<const CommandDialog*>(object) || qobject_cast<const DeviceStatesDialog*>(object)) {
        return Topic::Commands;
    }
    if (qobject_cast<const ButtonDialog*>(object) || qobject_cast<const CopyButtonsDialog*>(object)) {
        return Topic::Buttons;
    }
    if (qobject_cast<const CheckDialog*>(object) || qobject_cast<const WritePreviewDialog*>(object) ||
        qobject_cast<const RemoteSettingsDialog*>(object)) {
        return Topic::Remote;
    }
    // У RemoteProfileDialog нет Q_OBJECT — qobject_cast к нему не собирается.
    if (dynamic_cast<const RemoteProfileDialog*>(window) != nullptr) {
        return Topic::Connect;
    }
    if (qobject_cast<const VersionsDialog*>(object) || qobject_cast<const CompareConfigsDialog*>(object) ||
        qobject_cast<const JournalDialog*>(object) || qobject_cast<const QuickSearch*>(object)) {
        return Topic::Versions;
    }
    if (qobject_cast<const SettingsDialog*>(object)) {
        return Topic::Settings;
    }
    if (qobject_cast<const StartDialog*>(object) || qobject_cast<const NewConfigDialog*>(object)) {
        return Topic::Start;
    }
    return std::nullopt;
}

/// F1 в диалогах: раздел по типу диалога или по окну, из которого он открыт.
class F1Handler final : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() != QEvent::KeyPress || !watched->isWidgetType()) {
            return QObject::eventFilter(watched, event);
        }
        if (!static_cast<QKeyEvent*>(event)->matches(QKeySequence::HelpContents)) {
            return QObject::eventFilter(watched, event);
        }
        QWidget* window = static_cast<QWidget*>(watched)->window();
        // Главное окно выбирает раздел само (действие с F1), справка F1 не перехватывает.
        if (qobject_cast<MainWindow*>(window) != nullptr || qobject_cast<HelpDialog*>(window) != nullptr) {
            return QObject::eventFilter(watched, event);
        }
        // Незнакомое окно (сообщение, вложенный диалог шагов) — раздел окна, из которого оно открыто.
        std::optional<Topic> topic;
        for (const QWidget* owner = window; owner != nullptr && !topic;
             owner = owner->parentWidget() != nullptr ? owner->parentWidget()->window() : nullptr) {
            if (qobject_cast<const MainWindow*>(owner) != nullptr) {
                break;
            }
            topic = topicOf(owner);
        }
        show(window, topic.value_or(Topic::Start));
        return true;
    }
};

} // namespace

QList<Topic> topics() {
    return {Topic::Start,   Topic::Connect, Topic::Activities, Topic::Devices,  Topic::Commands,
            Topic::Buttons, Topic::Remote,  Topic::Versions,   Topic::Settings, Topic::Keys};
}

QString link(Topic topic) {
    return QStringLiteral("topic:%1").arg(static_cast<int>(topic));
}

void show(QWidget* parent, Topic topic) {
    HelpDialog* dialog =
        parent != nullptr ? parent->findChild<HelpDialog*>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
    if (dialog == nullptr) {
        dialog = new HelpDialog(parent);
    }
    dialog->setTopic(topic);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void installF1Handler(QApplication& app) {
    app.installEventFilter(new F1Handler(&app));
}

} // namespace harmony::app::help
