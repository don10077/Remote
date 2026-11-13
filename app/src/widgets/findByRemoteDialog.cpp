/**
 * @file findByRemoteDialog.cpp
 * @brief Окно поиска модели по нескольким кнопкам родного пульта.
 */

#include "findByRemoteDialog.h"

#include "../icons.h"
#include "../irDatabaseService.h"
#include "../labels.h"
#include "../remoteSession.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace harmony::app {

namespace {

constexpr int kListMax = 300; ///< Сколько моделей показывать
constexpr int kListenSeconds = 20; ///< Одно ожидание нажатия; потом — снова
constexpr int kRetryMs = 2000; ///< Повтор после ошибки связи
constexpr int kEntryRole = Qt::UserRole + 1;

/// Перекрашивает кнопку как главную или обычную.
void setPrimary(QPushButton* button, bool primary) {
    button->setObjectName(primary ? "primaryButton" : "");
    button->style()->unpolish(button);
    button->style()->polish(button);
}

} // namespace

FindByRemoteDialog::FindByRemoteDialog(RemoteSession* session, IrDatabaseService* irDatabase,
                                       QWidget* parent)
    : QDialog(parent), session_(session), irDatabase_(irDatabase) {
    setWindowTitle(tr("Find the model by its remote"));
    setMinimumSize(640, 600);

    auto* title = new QLabel(tr("Find the model by its remote"), this);
    title->setObjectName("pageTitle");
    auto* hint =
        new QLabel(tr("Point the original remote at the top edge of the Harmony from 5–10 cm and "
                      "press a few different buttons one by one: power, volume, a digit, a menu "
                      "key… The Harmony must be connected via USB."),
                   this);
    hint->setObjectName("pageMeta");
    hint->setWordWrap(true);

    status_ = new QLabel(this);
    QFont big = status_->font();
    big.setPointSizeF(big.pointSizeF() * 1.25);
    big.setBold(true);
    status_->setFont(big);
    status_->setWordWrap(true);
    summary_ = new QLabel(this);
    summary_->setObjectName("pageMeta");
    summary_->setWordWrap(true);

    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText(tr("Manufacturer or model, if you know it (narrows the list)"));
    filter_->setClearButtonEnabled(true);

    list_ = new QTreeWidget(this);
    list_->setColumnCount(3);
    list_->setHeaderLabels({tr("Model"), tr("Type"), tr("Buttons")});
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->setIconSize(QSize(20, 20));
    list_->setAlternatingRowColors(true);
    list_->header()->setStretchLastSection(false);
    list_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    list_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    listen_ = new QPushButton(this);
    reset_ = new QPushButton(tr("Start over"), this);
    auto* cancel = new QPushButton(tr("Cancel"), this);
    use_ = new QPushButton(tr("Add this model"), this);
    use_->setObjectName("primaryButton");
    use_->setDefault(true);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(listen_);
    buttons->addWidget(reset_);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(use_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addWidget(hint);
    layout->addWidget(status_);
    layout->addWidget(summary_);
    layout->addWidget(filter_);
    layout->addWidget(list_, 1);
    layout->addLayout(buttons);

    connect(filter_, &QLineEdit::textChanged, this, &FindByRemoteDialog::updateResults);
    connect(list_, &QTreeWidget::itemSelectionChanged, this, &FindByRemoteDialog::updateState);
    connect(list_, &QTreeWidget::itemDoubleClicked, this, &FindByRemoteDialog::usePicked);
    connect(use_, &QPushButton::clicked, this, &FindByRemoteDialog::usePicked);
    connect(cancel, &QPushButton::clicked, this, &FindByRemoteDialog::reject);
    connect(listen_, &QPushButton::clicked, this, [this] {
        if (listening_) {
            stopListening();
        } else {
            listening_ = true;
            problem_.clear();
            listen();
        }
        updateState();
    });
    connect(reset_, &QPushButton::clicked, this, [this] {
        if (finder_) {
            finder_->reset();
        }
        updateResults();
    });

    // Итоги обучения приходят от сеанса с пультом; окно реагирует, только пока само слушает.
    connect(session_, &RemoteSession::learnFinished, this, [this](const ir::IrSignal& signal) {
        if (!waiting_) {
            return;
        }
        waiting_ = false;
        signalLearned(signal);
        if (listening_) {
            QTimer::singleShot(300, this, &FindByRemoteDialog::listen);
        }
    });
    connect(session_, &RemoteSession::failed, this, [this](const QString& message, bool cancelled) {
        if (!waiting_) {
            return;
        }
        waiting_ = false;
        // Отмена — это и «никто не нажал за отведённое время»: просто слушаем дальше.
        problem_ = cancelled ? QString() : message;
        if (listening_) {
            QTimer::singleShot(cancelled ? 0 : kRetryMs, this, &FindByRemoteDialog::listen);
        }
        updateState();
    });

    // Индекс сигналов — несколько десятков мегабайт: загружается в фоне.
    status_->setText(tr("Loading the signal index…"));
    listen_->setEnabled(false);
    reset_->setEnabled(false);
    use_->setEnabled(false);
    const irdb::IrDatabase* database = irDatabase_->database();
    if (database == nullptr) {
        indexLoaded(irDatabase_->lastError());
        return;
    }
    auto error = std::make_shared<QString>();
    loader_ = QThread::create([this, database, error] {
        auto index = database->signalIndex();
        if (index) {
            index_ = *index;
        } else {
            *error = labels::errorText(index.error());
        }
    });
    connect(loader_, &QThread::finished, this, [this, error] { indexLoaded(*error); });
    loader_->start();
}

FindByRemoteDialog::~FindByRemoteDialog() {
    if (loader_ != nullptr) {
        loader_->wait();
        delete loader_;
    }
    if (waiting_) {
        session_->cancel();
    }
}

void FindByRemoteDialog::reject() {
    stopListening();
    QDialog::reject();
}

void FindByRemoteDialog::indexLoaded(const QString& error) {
    const irdb::IrDatabase* database = irDatabase_->database();
    if (!index_ || database == nullptr) {
        status_->setText(tr("Search is not available"));
        summary_->setText(error);
        return;
    }
    finder_ = std::make_unique<irdb::ModelFinder>(*database, index_);
    listening_ = true;
    updateResults();
    listen();
}

void FindByRemoteDialog::listen() {
    if (!listening_ || waiting_ || !finder_) {
        updateState();
        return;
    }
    if (session_->busy()) {
        // Пульт занят другой операцией (например, проверкой подключения) — чуть позже.
        QTimer::singleShot(500, this, &FindByRemoteDialog::listen);
        return;
    }
    waiting_ = true;
    session_->learn(kListenSeconds);
    updateState();
}

void FindByRemoteDialog::stopListening() {
    listening_ = false;
    if (waiting_) {
        session_->cancel();
    }
}

void FindByRemoteDialog::signalLearned(const ir::IrSignal& signal) {
    if (!finder_) {
        return;
    }
    problem_.clear();
    switch (finder_->add(signal)) {
    case irdb::ModelFinder::Press::Repeated:
        problem_ = tr("That button was pressed already — press another one.");
        break;
    case irdb::ModelFinder::Press::TooShort:
        problem_ = tr("The signal was too short — press the button once more.");
        break;
    case irdb::ModelFinder::Press::Added:
        updateResults();
        break;
    }
    updateState();
}

void FindByRemoteDialog::updateResults() {
    const irdb::IrDatabase* database = irDatabase_->database();
    list_->clear();
    if (!finder_ || database == nullptr) {
        updateState();
        return;
    }
    if (finder_->presses() == 0) {
        summary_->setText(tr("Nothing pressed yet."));
        updateState();
        return;
    }
    const auto results = finder_->results(filter_->text().trimmed().toStdString(), kListMax);
    const auto presses = static_cast<int>(finder_->presses());
    for (const auto& hit : results.hits) {
        const irdb::CatalogueEntry& entry = database->entries()[hit.entry];
        const std::string type = irdb::deviceTypeName(entry.deviceType);
        auto* item = new QTreeWidgetItem(list_);
        item->setIcon(0, icons::deviceType(type));
        item->setText(0, QString::fromStdString(entry.manufacturer + " " + entry.model));
        item->setText(1, labels::deviceType(type));
        item->setText(2, tr("%1 of %2").arg(hit.matched).arg(presses));
        item->setTextAlignment(2, Qt::AlignCenter);
        item->setData(0, kEntryRole, static_cast<qulonglong>(hit.entry));
    }
    if (results.hits.empty()) {
        summary_->setText(
            filter_->text().trimmed().isEmpty()
                ? tr("No model in the database sends these signals. Learn the commands "
                     "instead: “New device…” → “Learn commands…”.")
                : tr("No model matches the filter."));
        updateState();
        return;
    }
    // «Samsung 40, LG 12, Sony 3» — производители лучших.
    QStringList makers;
    for (std::size_t i = 0; i < results.makers.size() && i < 5; ++i) {
        makers << QStringLiteral("%1 %2")
                      .arg(QString::fromStdString(results.makers[i].first))
                      .arg(results.makers[i].second);
    }
    // «Best match: 3 of 3 buttons — 12 models: Samsung 10, LG 2.»
    QString text =
        tr("Best match: %1 of %2 buttons — %3: %4.")
            .arg(results.best)
            .arg(presses)
            .arg(labels::plural(static_cast<qsizetype>(results.bestCount), "model", "models"))
            .arg(makers.join(QStringLiteral(", ")));
    if (results.bestCount > 1) {
        text +=
            QLatin1Char(' ') + tr("Press more buttons or type the manufacturer to narrow the list; "
                                  "models with the same codes work the same.");
    }
    summary_->setText(text);
    list_->setCurrentItem(list_->topLevelItem(0));
    updateState();
}

void FindByRemoteDialog::updateState() {
    if (!finder_) {
        return; // индекс ещё загружается или недоступен — надпись уже стоит
    }
    if (!problem_.isEmpty()) {
        status_->setText(problem_);
    } else if (!listening_) {
        status_->setText(tr("Paused."));
    } else if (!waiting_) {
        status_->setText(tr("Connecting to the remote…"));
    } else if (finder_->presses() == 0) {
        status_->setText(tr("Press a button on the original remote"));
    } else {
        status_->setText(tr("Press another button (%1 so far)").arg(finder_->presses()));
    }
    listen_->setEnabled(true);
    listen_->setText(listening_ ? tr("Pause") : tr("Listen"));
    reset_->setEnabled(finder_->presses() > 0);
    const bool picked = list_->currentItem() != nullptr;
    use_->setEnabled(picked);
    setPrimary(use_, picked);
}

void FindByRemoteDialog::usePicked() {
    const QTreeWidgetItem* item = list_->currentItem();
    if (item == nullptr) {
        return;
    }
    entry_ = static_cast<int>(item->data(0, kEntryRole).toULongLong());
    stopListening();
    accept();
}

} // namespace harmony::app
