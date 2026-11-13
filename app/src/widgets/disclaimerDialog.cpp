/**
 * @file disclaimerDialog.cpp
 * @brief Предупреждение о рисках при первом запуске.
 */

#include "disclaimerDialog.h"

#include "../appSettings.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace harmony::app {

DisclaimerDialog::DisclaimerDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Before you start"));
    setMinimumWidth(520);

    auto* title = new QLabel(tr("Please read before using Harmony"), this);
    title->setObjectName("pageTitle");

    const QStringList points = {
        tr("Harmony is an unofficial program. It is not made, endorsed or supported by Logitech."),
        tr("Writing a configuration to a remote changes its memory. An error, an interrupted transfer or an "
           "unsupported model can make the remote stop working until it is restored."),
        tr("Models marked as experimental have not been tested on real remotes. Use them at your own risk."),
        tr("Keep a backup of the remote's configuration. Harmony saves one before writing when the remote "
           "can be read, but not every model allows this — check that the backup exists."),
        tr("The program is provided “as is”, without any warranty, under the GNU General Public License "
           "version 3. The authors are not liable for any damage to remotes, devices or data, or for any "
           "other loss resulting from its use."),
    };
    QString html = QStringLiteral("<ul style=\"margin-left: 0; -qt-list-indent: 1;\">");
    for (const QString& point : points) {
        html += QStringLiteral("<li style=\"margin-bottom: 6px;\">%1</li>").arg(point.toHtmlEscaped());
    }
    html += QStringLiteral("</ul>");
    auto* text = new QLabel(html, this);
    text->setWordWrap(true);
    text->setTextFormat(Qt::RichText);

    auto* agree = new QCheckBox(tr("I understand the risks and accept these terms"), this);
    auto* quit = new QPushButton(tr("Quit"), this);
    auto* proceed = new QPushButton(tr("Continue"), this);
    proceed->setEnabled(false);
    proceed->setDefault(true);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(quit);
    buttons->addWidget(proceed);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(12);
    layout->addWidget(title);
    layout->addWidget(text);
    layout->addWidget(agree);
    layout->addLayout(buttons);

    connect(agree, &QCheckBox::toggled, proceed, &QPushButton::setEnabled);
    connect(proceed, &QPushButton::clicked, this, &QDialog::accept);
    connect(quit, &QPushButton::clicked, this, &QDialog::reject);
}

bool DisclaimerDialog::ensureAccepted() {
    if (settings::acceptedDisclaimer() >= kVersion) return true;
    DisclaimerDialog dialog;
    if (dialog.exec() != QDialog::Accepted) return false;
    settings::setAcceptedDisclaimer(kVersion);
    return true;
}

} // namespace harmony::app
