/**
 * @file aboutDialog.cpp
 * @brief Окно «О программе».
 */

#include "aboutDialog.h"

#include "../theme.h"

#include "harmony/core/version.h"

#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSysInfo>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>
#include <QtGlobal>

namespace harmony::app {

namespace {

/// Страница проекта.
constexpr const char* kProjectUrl = "https://github.com/don10077/Remote";

/// Адрес для связи с автором.
constexpr const char* kContactEmail = "don10077@mail.ru";

/// Ссылка в HTML.
QString anchor(const QString& url, const QString& text) {
    return QStringLiteral("<a href=\"%1\">%2</a>").arg(url, text.toHtmlEscaped());
}

/// Строка таблицы компонентов: название со ссылкой, назначение, лицензия.
QString component(const QString& name, const QString& url, const QString& role, const QString& license) {
    return QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>")
        .arg(anchor(url, name), role.toHtmlEscaped(), license.toHtmlEscaped());
}

/// Строка таблицы проектов: название со ссылкой и чем проект полезен.
QString project(const QString& name, const QString& url, const QString& role) {
    return QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(anchor(url, name), role.toHtmlEscaped());
}

} // namespace

QString AboutDialog::versionInfo() {
    return QStringLiteral("Harmony %1 · Qt %2 · %3 (%4)")
        .arg(QString::fromUtf8(harmony::kVersion), QString::fromLatin1(qVersion()), QSysInfo::prettyProductName(),
             QSysInfo::currentCpuArchitecture());
}

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("About Harmony"));
    setMinimumSize(560, 520);

    auto* icon = new QLabel(this);
    icon->setPixmap(QPixmap(QStringLiteral(":/res/app/harmony-128.png"))
                        .scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    icon->setAlignment(Qt::AlignTop);

    auto* title = new QLabel(tr("Harmony"), this);
    title->setObjectName("pageTitle");
    auto* version = new QLabel(tr("Version %1").arg(QString::fromUtf8(harmony::kVersion)), this);
    version->setObjectName("pageMeta");
    version->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* description = new QLabel(tr("Configure Logitech Harmony remotes without Logitech's software "
                                      "and cloud."),
                                   this);
    description->setWordWrap(true);
    const QString contactEmail = QString::fromLatin1(kContactEmail);
    auto* links = new QLabel(anchor(QString::fromLatin1(kProjectUrl), tr("Project page")) +
                                 QStringLiteral(" · ") +
                                 anchor(QString::fromLatin1(kProjectUrl) + QStringLiteral("/issues"),
                                        tr("Report a problem")) +
                                 QStringLiteral("<br>") +
                                 anchor(QStringLiteral("mailto:") + contactEmail, contactEmail),
                             this);
    links->setOpenExternalLinks(true);

    auto* heading = new QVBoxLayout;
    heading->setSpacing(4);
    heading->addWidget(title);
    heading->addWidget(version);
    heading->addSpacing(6);
    heading->addWidget(description);
    heading->addWidget(links);
    heading->addStretch(1);
    auto* top = new QHBoxLayout;
    top->setSpacing(18);
    top->addWidget(icon);
    top->addLayout(heading, 1);

    const QString accent = theme::color(theme::Color::Accent).name();
    const QString muted = theme::color(theme::Color::TextMuted).name();
    auto* details = new QTextBrowser(this);
    details->setOpenExternalLinks(true);
    details->document()->setDefaultStyleSheet(
        QStringLiteral("a { color: %1; text-decoration: none; } .note { color: %2; }"
                       "td { padding: 2px 12px 2px 0; } h4 { margin-top: 12px; margin-bottom: 4px; }")
            .arg(accent, muted));
    const QString license =
        tr("Harmony is free software: you can redistribute it and modify it under the terms of the "
           "GNU General Public License version 3 (%1). It comes without any warranty.")
            .arg(anchor(QStringLiteral("https://www.gnu.org/licenses/gpl-3.0.html"), QStringLiteral("GPL-3.0-only")));
    // Поддержка проекта: ссылка и, если в ресурсах есть картинка, QR-код справа.
    const QString donateText =
        QStringLiteral("<p>%1</p><p><b>%2</b></p>")
            .arg(tr("If Harmony is useful to you, you can support the further development of the project.")
                     .toHtmlEscaped(),
                 anchor(QString::fromLatin1(kDonateUrl), tr("Support on DonationAlerts")));
    const QString donate =
        QFile::exists(QString::fromLatin1(kDonateQr))
            ? QStringLiteral("<table cellspacing=\"0\"><tr><td valign=\"middle\">%1</td>"
                             "<td valign=\"middle\"><img src=\"%2\" width=\"120\" height=\"120\"></td></tr></table>")
                  .arg(donateText, QString::fromLatin1(kDonateQr))
            : donateText;
    const QString thanks =
        QStringLiteral("<table cellspacing=\"0\">") +
        project(QStringLiteral("afterglow"), QStringLiteral("https://github.com/harmonic-afterglow/afterglow"),
                tr("IR protocol programs, descriptions of device and activity properties")) +
        project(QStringLiteral("Concordance"), QStringLiteral("https://github.com/jaymzh/concordance"),
                tr("libconcord: configuration transfer over USB")) +
        project(QStringLiteral("logitech-harmony-ir-archive"),
                QStringLiteral("https://github.com/pickysysadmin/logitech-harmony-ir-archive"),
                tr("archive of Logitech's IR codes")) +
        QStringLiteral("</table>");
    const QString related =
        QStringLiteral("<table cellspacing=\"0\">") +
        project(QStringLiteral("harmony900tools"), QStringLiteral("https://github.com/martin-wagner/harmony900tools"),
                tr("Harmony 900 editor")) +
        project(QStringLiteral("RE-HARMONY"), QStringLiteral("https://github.com/Unkn0wn-dx/RE-HARMONY"),
                tr("Harmony One configuration")) +
        QStringLiteral("</table>");
    const QString components =
        QStringLiteral("<table cellspacing=\"0\">") +
        component(QStringLiteral("Qt %1").arg(QString::fromLatin1(qVersion())), QStringLiteral("https://www.qt.io"),
                  tr("interface"), QStringLiteral("LGPL-3.0")) +
        component(QStringLiteral("pugixml"), QStringLiteral("https://pugixml.org"), tr("XML files"),
                  QStringLiteral("MIT")) +
        component(QStringLiteral("zlib"), QStringLiteral("https://zlib.net"), tr(".EZHex archives"),
                  QStringLiteral("zlib")) +
        component(QStringLiteral("nlohmann/json"), QStringLiteral("https://github.com/nlohmann/json"),
                  tr("IR code database"), QStringLiteral("MIT")) +
        component(QStringLiteral("hidapi"), QStringLiteral("https://github.com/libusb/hidapi"),
                  tr("USB HID remotes"), QStringLiteral("BSD-3-Clause")) +
        component(QStringLiteral("Lucide"), QStringLiteral("https://lucide.dev"), tr("icons"),
                  QStringLiteral("ISC")) +
        component(QStringLiteral("Tabler Icons"), QStringLiteral("https://tabler.io/icons"), tr("icons"),
                  QStringLiteral("MIT")) +
        QStringLiteral("</table>");
    details->setHtml(QStringLiteral("<p>%1</p><h4>%2</h4>%3<h4>%4</h4>%5<h4>%6</h4>%7<h4>%8</h4>%9"
                                    "<p class=\"note\">%10</p>")
                         .arg(license, tr("Support the project").toHtmlEscaped(), donate,
                              tr("Thanks").toHtmlEscaped(), thanks, tr("Related projects").toHtmlEscaped(), related,
                              tr("Components").toHtmlEscaped(), components,
                              tr("Logitech and Harmony are trademarks of Logitech. This program is not "
                                 "affiliated with Logitech.")
                                  .toHtmlEscaped()));

    auto* copy = new QPushButton(tr("Copy version info"), this);
    copy->setToolTip(tr("Program, Qt and system versions — for a bug report"));
    auto* close = new QPushButton(tr("Close"), this);
    close->setDefault(true);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(copy);
    buttons->addStretch(1);
    buttons->addWidget(close);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(14);
    layout->addLayout(top);
    layout->addWidget(details, 1);
    layout->addLayout(buttons);

    connect(copy, &QPushButton::clicked, this, [copy] {
        QApplication::clipboard()->setText(versionInfo());
        copy->setText(tr("Copied"));
        QTimer::singleShot(1500, copy, [copy] { copy->setText(tr("Copy version info")); });
    });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    resize(620, 600);
}

} // namespace harmony::app
