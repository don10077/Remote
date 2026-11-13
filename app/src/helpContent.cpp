/**
 * @file helpContent.cpp
 * @brief Тексты разделов справки.
 *
 * Каждый абзац — отдельная строка tr(): переводчику не приходится держать
 * в голове весь раздел, а правка одного абзаца не сбрасывает перевод остальных.
 * Названия пунктов меню и кнопок в текстах совпадают с английскими строками
 * интерфейса — в переводе их надо писать так же, как они переведены в программе.
 */

#include "help.h"

#include "harmony/transport/remoteLink.h"

#include <QCoreApplication>
#include <QKeySequence>
#include <QStringList>

namespace harmony::app::help {

namespace {

/// Контекст tr() для текстов справки.
class HelpText {
    Q_DECLARE_TR_FUNCTIONS(HelpText)

public:
    static QString title(Topic topic);
    static QString body(Topic topic);

private:
    static QString start();
    static QString connect();
    static QString activities();
    static QString devices();
    static QString commands();
    static QString buttons();
    static QString remote();
    static QString versions();
    static QString settings();
    static QString keys();
};

QString heading(const QString& text) {
    return QStringLiteral("<h3>%1</h3>").arg(text);
}

QString para(const QString& text) {
    return QStringLiteral("<p>%1</p>").arg(text);
}

QString note(const QString& text) {
    return QStringLiteral("<p class=\"note\">%1</p>").arg(text);
}

QString list(const QStringList& items) {
    QString html = QStringLiteral("<ul>");
    for (const QString& item : items) {
        html += QStringLiteral("<li>%1</li>").arg(item);
    }
    return html + QStringLiteral("</ul>");
}

/// Ссылка на раздел справки с его заголовком.
QString ref(Topic topic) {
    return QStringLiteral("<a href=\"%1\">%2</a>").arg(link(topic), HelpText::title(topic).toHtmlEscaped());
}

/// Сочетание клавиш в виде, принятом в системе (Ctrl+Z или ⌘Z).
QString keyText(const QKeySequence& sequence) {
    return QStringLiteral("<b>%1</b>").arg(sequence.toString(QKeySequence::NativeText).toHtmlEscaped());
}

QString keyText(QKeySequence::StandardKey key) {
    return keyText(QKeySequence(key));
}

QString HelpText::title(Topic topic) {
    switch (topic) {
    case Topic::Start: return tr("Getting started");
    case Topic::Connect: return tr("Connecting the remote");
    case Topic::Activities: return tr("Activities");
    case Topic::Devices: return tr("Devices");
    case Topic::Commands: return tr("Commands and learning");
    case Topic::Buttons: return tr("Buttons and screen");
    case Topic::Remote: return tr("Check and write to the remote");
    case Topic::Versions: return tr("Versions, search and journal");
    case Topic::Settings: return tr("Settings and translations");
    case Topic::Keys: return tr("Keyboard shortcuts");
    }
    return {};
}

QString HelpText::body(Topic topic) {
    QString text;
    switch (topic) {
    case Topic::Start: text = start(); break;
    case Topic::Connect: text = connect(); break;
    case Topic::Activities: text = activities(); break;
    case Topic::Devices: text = devices(); break;
    case Topic::Commands: text = commands(); break;
    case Topic::Buttons: text = buttons(); break;
    case Topic::Remote: text = remote(); break;
    case Topic::Versions: text = versions(); break;
    case Topic::Settings: text = settings(); break;
    case Topic::Keys: text = keys(); break;
    }
    return QStringLiteral("<h2>%1</h2>").arg(title(topic).toHtmlEscaped()) + text;
}

QString HelpText::start() {
    return para(tr("Harmony edits the configuration of a Logitech Harmony 900 remote without Logitech's "
                   "software and cloud. You change the configuration on the computer and write it to the "
                   "remote when it is ready.")) +
           heading(tr("Where to begin")) +
           list({tr("<b>Read from remote</b> (top right): reads the configuration from a connected remote, "
                    "saves it as a backup and opens it. See %1.")
                     .arg(ref(Topic::Connect)),
                 tr("<b>Open configuration</b>: an .EZHex file (a remote backup, also made by concordance "
                    "and afterglow) or UserConfiguration.xml in a configuration folder."),
                 tr("<b>Create new</b>: an empty configuration, no remote needed."),
                 tr("<b>View example</b>: a ready-made configuration to look around.")}) +
           heading(tr("The main window")) +
           para(tr("On the left are the activities and devices of the configuration. Choose one to see its "
                   "tabs: devices and buttons of an activity; commands, buttons and activities of a device. "
                   "“View → Remote overview” shows the remote itself: click a button on the picture to see "
                   "or change what it does.")) +
           para(tr("The status bar shows how many problems the configuration has; click the number to open "
                   "the check. Every change can be undone with the arrows in the header or %1.")
                    .arg(keyText(QKeySequence::Undo))) +
           heading(tr("Saving")) +
           para(tr("“File → Save” writes a single EZHex archive. “Save as” chooses a different file. "
                   "A legacy folder can be opened, but saving creates an EZHex archive. "
                   "Unsaved changes are also kept as versions every three minutes — see %1.")
                    .arg(ref(Topic::Versions))) +
           note(tr("Harmony 600, 650, 665 and 700 are experimental: the program can only read them by USB and "
                   "save the data for analysis. Writing to these models is blocked."));
}

QString HelpText::connect() {
    return para(tr("The Harmony 900 connects by USB and shows up as a network adapter. The program finds "
                   "the remote at %1 by itself; the state is shown at the top right. Its menu has “Test "
                   "connection” and the other remote commands.")
                    .arg(QString::fromLatin1(transport::kDefaultRemoteHost))) +
           heading(tr("Windows")) +
           para(tr("You need the USB driver from Logitech Harmony Remote Software 7.8. Logitech's servers "
                   "are closed; the installer is available in archives, for example on archive.org. You "
                   "don't need to run the Logitech program itself.")) +
           heading(tr("Linux")) +
           para(tr("The remote waits for an address via DHCP. Choose “Set up USB connection…” once (the "
                   "administrator password is needed), then reconnect the remote. The setup needs dnsmasq, "
                   "ip, systemd and udev. If a firewall is on, allow incoming UDP port 67 on this "
                   "interface.")) +
           heading(tr("If the remote is not found")) +
           list({tr("Wait a few seconds after connecting: the remote is not ready immediately."),
                 tr("Check the address in “Settings → Remote”."),
                 tr("“Help → Program journal…” shows what went wrong.")}) +
           note(tr("Before any write the program saves a backup of the remote's current configuration to "
                   "the backups folder (“Remote → Backups folder”)."));
}

QString HelpText::activities() {
    return para(tr("An activity is what you do: “Watch TV”, “Listen to music”. When it starts, the remote "
                   "turns on the ticked devices, turns off the others and switches their inputs.")) +
           heading(tr("Setting up")) +
           para(tr("“Edit → Add activity…” creates an activity, “Setup” on its page changes it: the devices "
                   "it needs, the inputs at start and the roles — which device controls the volume, which "
                   "shows the picture and which is controlled by menu, arrows and playback buttons. By "
                   "the roles the program assigns the remote's buttons.")) +
           para(tr("The “Devices” tab shows how each device is powered and which role it has; the "
                   "“Buttons” tab — what the buttons do in this activity (see %1).")
                    .arg(ref(Topic::Buttons))) +
           heading(tr("Properties and the Active switch")) +
           para(tr("“Properties” holds what the remote reads from the activity besides buttons: the start "
                   "page, turning off devices that are not needed, Play at start and so on.")) +
           para(tr("The “Active” switch next to the name keeps the activity in the configuration but does "
                   "not write it to the remote.")) +
           note(tr("The activity that turns everything off is needed by the remote: it cannot be deleted or "
                   "switched off."));
}

QString HelpText::devices() {
    return para(tr("A device is what the remote controls: a TV, a receiver, a player. A device has commands "
                   "(IR signals), buttons and a description of how it is powered and how its inputs are "
                   "switched.")) +
           heading(tr("Adding a device")) +
           para(tr("“Edit → Add device…” opens a window with several ways:")) +
           list({tr("<b>Logitech database</b>: type a manufacturer and a model. The commands, power and "
                    "inputs come from the archive of Logitech's codes. The database is built once in "
                    "“Settings → IR database”."),
                 tr("<b>Find by remote…</b>: the model is unknown? Point the device's own remote at the "
                    "Harmony and press several different buttons; the program finds the models that know "
                    "these signals. Three to five buttons are usually enough."),
                 tr("<b>Other configuration</b>: take devices from another configuration as they are."),
                 tr("<b>New device…</b>: an empty device; then learn its commands or enter them by "
                    "protocol (see %1).")
                     .arg(ref(Topic::Commands))}) +
           heading(tr("On the device page")) +
           list({tr("<b>Properties</b>: pauses between key presses, repeat rate when a button is held, "
                    "“always on” and other settings the remote reads."),
                 tr("<b>Compare</b>: the commands of two devices side by side; the marked ones can be copied "
                    "from one to the other."),
                 tr("<b>Replace</b>: a new TV instead of the old one. Power, roles, inputs, buttons and macro "
                    "steps move to the new device; commands are matched by name."),
                 tr("<b>Active</b>: the device stays in the configuration but is not written to the remote. "
                    "Activities that need it are not written either: the program lists them before switching "
                    "the device off, and the activity page says which device holds it."),
                 tr("<b>Delete</b>: a device used in activities can be removed from them in one step (an "
                    "activity with no other devices is deleted with it) or replaced by another device.")});
}

QString HelpText::commands() {
    return para(tr("The “Commands” tab of a device lists its IR commands. Double-click or %1 shows the "
                   "signal, the protocol and the code.")
                    .arg(keyText(QKeySequence(Qt::Key_Return)))) +
           heading(tr("Learning from the original remote")) +
           para(tr("“Learn…” opens a wizard for one command or many. Tick the commands, point the original "
                   "remote at the top end of the Harmony from 5–10 cm and press the buttons one by one. "
                   "With verification each button is pressed twice: the command is saved only if both "
                   "presses gave the same signal. Every learned command is a separate undo step.")) +
           para(tr("Commands named PowerOn and PowerOff (or PowerToggle) are used by the remote to turn the "
                   "device on and off.")) +
           heading(tr("Entering a command by protocol")) +
           para(tr("“Manual…”: choose the protocol (NEC, Samsung32, Sony, RC5, RC6 and others), the address "
                   "and the command; the window shows the signal at once. “Save and next” is for entering "
                   "many commands in a row.")) +
           heading(tr("Power and inputs")) +
           para(tr("“Power…” describes how the remote turns the device on and off: separate on and off "
                   "commands, one toggle button or manually, and the pause after turning on. “Inputs…” "
                   "describes how inputs are selected: a command for each or one command in a cycle. "
                   "“…” next to an action opens its steps: several commands and pauses.")) +
           note(tr("A command on buttons or in the start of activities is deleted together with them after a "
                   "question. A command used by a macro or the power description cannot be deleted: change "
                   "them first. Copied commands go to the system clipboard in Pronto Hex as well."));
}

QString HelpText::buttons() {
    return para(tr("The “Buttons” tab lists the remote's buttons and screen buttons with what they do. The "
                   "same buttons are highlighted on the remote picture; free buttons are dashed. Click a "
                   "button on the picture to select it in the list.")) +
           heading(tr("Assigning")) +
           list({tr("<b>Edit…</b> or double-click: the command the button sends."),
                 tr("<b>Assign button…</b>: a command for a free button of the remote."),
                 tr("<b>Screen button…</b>: a button on the remote's screen with a label or an icon."),
                 tr("<b>Clear assignment</b> frees a remote button; for a screen button the same place "
                    "says <b>Delete</b>.")}) +
           heading(tr("Macros")) +
           para(tr("In the button window choose “Several steps (macro)”: the button runs commands to "
                   "different devices of the activity one after another, with pauses between them.")) +
           heading(tr("Icons on screen buttons")) +
           para(tr("A screen button can show an icon from the remote's own graphics instead of a label. "
                   "Only icons built into the Harmony 900 firmware are offered: the remote cannot draw "
                   "others.")) +
           heading(tr("Copying")) +
           para(tr("“Copy to activities…” copies the selected button or the whole layout to other "
                   "activities. %1, %2 and %3 cut, copy and paste buttons within the configuration.")
                    .arg(keyText(QKeySequence::Cut), keyText(QKeySequence::Copy), keyText(QKeySequence::Paste)));
}

QString HelpText::remote() {
    return heading(tr("Check")) +
           para(tr("“Remote → Check configuration…” (%1) lists what will not work on the remote: buttons that "
                   "send a missing command, two screen buttons in one place, devices an activity uses but "
                   "does not turn on and so on. Many problems fix themselves: “Fix” one by one or “Fix "
                   "all”. Every fix can be undone.")
                    .arg(keyText(QKeySequence(Qt::Key_F7)))) +
           heading(tr("Writing")) +
           para(tr("“Write to remote…” checks the configuration first, then reads the remote and saves its "
                   "current configuration as a backup, shows what will change on the remote and writes only "
                   "after you confirm. Do not disconnect the remote while writing.")) +
           heading(tr("Backups")) +
           para(tr("“Remote → Backups folder” opens the folder with the backups. A backup is an .EZHex file: "
                   "open it like any configuration. “Recover IR data from backup…” brings back the IR "
                   "signals from a backup if they were lost in the open configuration.")) +
           heading(tr("Remote settings and log")) +
           para(tr("“Remote settings…”: key beep, backlight, font, screen theme, clock and child lock. They "
                   "can be saved to the configuration or applied to the connected remote at once. “Remote "
                   "system log…” shows the remote's own log: what it sent and why it rejected a "
                   "configuration.")) +
           note(tr("The RF blaster is paired on the remote itself (Options → RF Receiver Settings). When "
                   "writing, the program keeps the remote's current RF settings."));
}

QString HelpText::versions() {
    return heading(tr("Versions")) +
           para(tr("The program keeps earlier states of the configuration: every three minutes while there "
                   "are unsaved changes, before each save and when closing without saving. “File → "
                   "Versions…” shows them and what restoring one would change; restoring can be undone. If "
                   "the program closed without saving, it offers to bring the changes back next time.")) +
           heading(tr("Comparing configurations")) +
           para(tr("“File → Compare with…” shows how the open configuration differs from another one: "
                   "devices and activities that are only in one of them and what changed in the common "
                   "ones.")) +
           heading(tr("Quick search")) +
           para(tr("%1 finds devices, activities, buttons and commands by a few letters; %2 opens the "
                   "found item.")
                    .arg(keyText(QKeySequence(Qt::CTRL | Qt::Key_K)), keyText(QKeySequence(Qt::Key_Return)))) +
           heading(tr("Journal")) +
           para(tr("“Help → Program journal…” shows what the program did with the remote and the "
                   "configuration and what went wrong. “Copy” puts the records on the clipboard — attach "
                   "them to a bug report. How much is recorded is set in “Settings → General”."));
}

QString HelpText::settings() {
    return list({tr("<b>General</b>: appearance (as in the system, light or dark), language, what to open "
                    "at startup, how much to write to the journal, your own translation."),
                 tr("<b>Remote</b>: the remote's address, the connection check every 10 seconds and how "
                    "long to wait for a signal when learning, the backups folder."),
                 tr("<b>IR database</b>: download the archive files of Logitech's codes and index them. "
                    "The index is built once and takes a few minutes; then the archive files are not "
                    "needed.")}) +
           para(tr("“Settings → Reset settings…” returns the defaults; the list of configurations stays.")) +
           heading(tr("Translations")) +
           para(tr("A translation is a Qt Linguist file harmony_&lt;code&gt;.ts. Put it into the translations "
                   "folder (“Settings → General”) and choose the language; the new language takes effect "
                   "after restarting. “Translation template…” saves a file with all the program's strings. "
                   "Untranslated strings stay in English."));
}

QString HelpText::keys() {
    struct Row {
        QString action;
        QString keys;
    };
    const auto join = [](const QList<QKeySequence>& sequences) {
        QStringList parts;
        for (const QKeySequence& sequence : sequences) {
            if (!sequence.isEmpty()) {
                parts << keyText(sequence);
            }
        }
        return parts.join(QStringLiteral(", "));
    };
    const auto standard = [&join](QKeySequence::StandardKey key) { return join(QKeySequence::keyBindings(key)); };
    const QList<Row> rows = {
        {tr("Help on the open window"), standard(QKeySequence::HelpContents)},
        {tr("New configuration"), standard(QKeySequence::New)},
        {tr("Open configuration"), standard(QKeySequence::Open)},
        {tr("Save"), standard(QKeySequence::Save)},
        {tr("Save as"), standard(QKeySequence::SaveAs)},
        {tr("Close configuration"), standard(QKeySequence::Close)},
        {tr("Undo"), standard(QKeySequence::Undo)},
        {tr("Redo"), join({QKeySequence(Qt::CTRL | Qt::Key_Y), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)})},
        {tr("Cut, copy, paste"), join({QKeySequence(QKeySequence::Cut), QKeySequence(QKeySequence::Copy),
                                       QKeySequence(QKeySequence::Paste)})},
        {tr("Quick search"), join({QKeySequence(Qt::CTRL | Qt::Key_K), QKeySequence(QKeySequence::Find)})},
        {tr("Rename"), join({QKeySequence(Qt::Key_F2)})},
        {tr("Delete"), standard(QKeySequence::Delete)},
        {tr("Open the selected row"), join({QKeySequence(Qt::Key_Return)})},
        {tr("Check configuration"), join({QKeySequence(Qt::Key_F7)})},
        {tr("Remote overview"), join({QKeySequence(Qt::CTRL | Qt::Key_0)})},
        {tr("Settings"), standard(QKeySequence::Preferences)},
        {tr("Exit"), standard(QKeySequence::Quit)},
    };
    QString table = QStringLiteral("<table cellspacing=\"0\">");
    for (const Row& row : rows) {
        if (row.keys.isEmpty()) {
            continue; // в этой системе у действия нет сочетания (например, «Настройки» в Windows)
        }
        table += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(row.action.toHtmlEscaped(), row.keys);
    }
    return table + QStringLiteral("</table>");
}

} // namespace

QString title(Topic topic) {
    return HelpText::title(topic);
}

QString html(Topic topic) {
    return HelpText::body(topic);
}

} // namespace harmony::app::help
