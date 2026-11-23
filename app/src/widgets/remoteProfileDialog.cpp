#include "remoteProfileDialog.h"
#include "aboutDialog.h"
#include "../assignmentGate.h"
#include "../modelKeysQt.h"
#include "../remoteResources.h"
#include "harmony/core/remoteCatalog.h"
#include "harmony/core/remoteProfile.h"
#include "../modern/remoteView.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace harmony::app {

RemoteProfileDialog::RemoteProfileDialog(const QString& current, QWidget* parent,
                                         const model::RemoteConfig* config) : QDialog(parent) {
    setWindowTitle(tr("Target remote"));
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(24);
    auto* image = new QLabel(this);
    image->setFixedSize(300, 420);
    image->setAlignment(Qt::AlignCenter);
    image->setWordWrap(true);
    layout->addWidget(image);
    auto* fields = new QVBoxLayout;
    // Строка статуса удалена по указанию владельца. Минимальный размер окна и QR-строки
    // нужен, чтобы QR-код не обрезался.
    setMinimumSize(720, 540);
    models_ = new QComboBox(this);
    // Модели каталога: у модели без профиля нет ни фото, ни зон, но выбрать её можно.
    models_->addItem(tr("No model"), QString());
    for (const auto& model : remoteCatalog()) {
        QString text = QString::fromUtf8(model.name.data(), static_cast<qsizetype>(model.name.size()));
        if (remoteProfile(model.id) == nullptr) text += " — " + tr("no image");
        models_->addItem(text, QString::fromUtf8(model.id.data(), static_cast<qsizetype>(model.id.size())));
    }
    // Свои пульты: профиль есть, записи в каталоге нет.
    for (const auto& profile : remoteProfiles) {
        if (catalogModel(profile.id) != nullptr) continue;
        models_->addItem(QString::fromUtf8(profile.name.data(), static_cast<qsizetype>(profile.name.size())),
                         QString::fromUtf8(profile.id.data(), static_cast<qsizetype>(profile.id.size())));
    }
    // Пульты с ошибкой в конфигурации: выбрать нельзя, но ошибка видна сразу.
    for (const QString& id : remoteResources::skippedIds())
        models_->addItem(tr("%1 (configuration error)").arg(id), id);
    models_->setCurrentIndex(models_->findData(current));
    if (models_->currentIndex() < 0) models_->setCurrentIndex(0);
    // Про запись говорит решение ядра (canWriteTo), но строка статуса в этом окне
    // удалена по указанию владельца.
    auto* notice = new QLabel(tr("Devices, activities and IR codes are kept. Changing the target does not convert the remote's binary format."), this);
    notice->setWordWrap(true);
    fields->addWidget(new QLabel(tr("Remote model:"), this));
    fields->addWidget(models_);
    fields->addSpacing(12);
    fields->addWidget(notice);
    auto* summary = new QLabel(this);
    summary->setObjectName("modelChangeSummary");
    summary->setWordWrap(true);
    fields->addWidget(summary);
    fields->addStretch();
    // Поддержка проекта: ссылка и, если в ресурсах есть картинка, QR-код.
    auto* donate = new QLabel(QStringLiteral("<a href=\"%1\">%2</a>")
                                  .arg(QString::fromLatin1(AboutDialog::kDonateUrl),
                                       tr("Support the further development of the project").toHtmlEscaped()),
                              this);
    donate->setWordWrap(true);
    donate->setMinimumWidth(240);
    donate->setOpenExternalLinks(true);
    donate->setToolTip(QString::fromLatin1(AboutDialog::kDonateUrl));
    auto* donateRow = new QHBoxLayout;
    donateRow->setSpacing(12);
    donateRow->setAlignment(Qt::AlignTop);
    if (QFile::exists(QString::fromLatin1(AboutDialog::kDonateQr))) {
        auto* qr = new QLabel(this);
        qr->setPixmap(QPixmap(QString::fromLatin1(AboutDialog::kDonateQr))
                          .scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        qr->setMinimumSize(96, 96);
        donateRow->addWidget(qr);
    }
    donateRow->addWidget(donate, 1);
    fields->addLayout(donateRow);
    fields->addSpacing(8);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    fields->addWidget(buttons);
    layout->addLayout(fields);
    auto refresh = [this, image, buttons, summary, config, current] {
        const auto id = selectedRemote();
        summary->setVisible(config && id != current);
        if (config && id != current) {
            const auto keys = modelKeysFor(id, config);
            summary->setText(keys.complete
                ? tr("Assignments on keys not available on the selected model: %1. "
                     "Assignments are kept and will appear as warnings. Press OK to change the target.")
                    .arg(static_cast<qulonglong>(outsideModelAssignmentCount(keys, *config)))
                : tr("The key list for the selected model is not complete; unavailable keys cannot be determined. "
                     "Assignments are kept. Press OK to change the target."));
        }
        const auto modelId = id.toStdString();
        const auto* profile = remoteProfile(modelId);
        const auto* model = catalogModel(modelId);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(id.isEmpty() || profile != nullptr || model != nullptr);
        if (id.isEmpty()) {
            image->clear();
            return;
        }
        if (const QString problem = remoteResources::modelError(id); !profile && !problem.isEmpty()) {
            image->setText(tr("This remote has a configuration error. Open the zone editor for this remote and fix it.\n\n%1").arg(problem));
            return;
        }
        if (!profile) {
            if (!model) {
                image->setText(tr("This remote model is unknown."));
                return;
            }
            // Модель каталога без профиля: фото и зон нет, кнопки назначаются в таблице.
            image->setText(tr("No remote image: buttons are assigned in the table. You can add an image in the zone editor."));
            return;
        }
        QPixmap photo;
        photo.loadFromData(remoteResources::data(QString::fromUtf8(profile->image.data())));
        image->setPixmap(photo.scaled(image->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    };
    connect(models_, &QComboBox::currentIndexChanged, this, refresh);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    refresh();
}

QString RemoteProfileDialog::selectedRemote() const { return models_->currentData().toString(); }

} // namespace harmony::app
