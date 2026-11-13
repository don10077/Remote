/**
 * @file newConfigDialog.cpp
 * @brief Реализация окна создания конфигурации.
 */

#include "newConfigDialog.h"

#include "../configLibrary.h"
#include "harmony/core/configDocument.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace harmony::app {

NewConfigDialog::NewConfigDialog(const QString& initialName, QWidget* parent)
    : QDialog(parent), base_(library::defaultLocation()) {
    const QString caption = tr("New configuration");
    setWindowTitle(caption);
    setMinimumWidth(520);

    auto* title = new QLabel(caption, this);
    title->setObjectName("pageTitle");
    auto* hint = new QLabel(tr("Create an EZHex configuration from scratch or as a copy of an existing one. No remote needed."), this);
    hint->setObjectName("pageMeta");
    hint->setWordWrap(true);

    name_ = new QLineEdit(this);
    name_->setPlaceholderText(tr("For example, Living room"));
    name_->setText(initialName.isEmpty() ? tr("My configuration") : initialName);
    name_->selectAll();

    location_ = new QLabel(this);
    location_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    location_->setWordWrap(true); // длинный путь переносится, а не растягивает окно
    auto* change = new QPushButton(tr("Edit…"), this);
    auto* locationRow = new QHBoxLayout;
    locationRow->addWidget(location_, 1);
    locationRow->addWidget(change);

    result_ = new QLabel(this);
    result_->setObjectName("pageMeta");
    result_->setWordWrap(true);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    {
        // Основа: пустая, встроенный пример или копия одной из недавних конфигураций.
        source_ = new QComboBox(this);
        source_->addItem(tr("Empty configuration"), QString());
        source_->addItem(tr("Built-in example"), QString::fromLatin1(kDemoSource));
        for (const QString& folder : library::recent()) {
            if (QFileInfo(folder).isFile() || QFileInfo::exists(QDir(folder).filePath(QStringLiteral("UserConfiguration.xml")))) {
                source_->addItem(tr("Copy: %1").arg(QFileInfo(folder).fileName()), folder);
                source_->setItemData(source_->count() - 1, QDir::toNativeSeparators(folder),
                                     Qt::ToolTipRole);
            }
        }
        auto* pick = new QPushButton(tr("Choose file…"), this);
        auto* sourceRow = new QHBoxLayout;
        sourceRow->addWidget(source_, 1);
        sourceRow->addWidget(pick);
        form->addRow(tr("Base:"), sourceRow);

        connect(source_, &QComboBox::currentIndexChanged, this, [this] {
            suggestName();
            validate();
        });
        connect(pick, &QPushButton::clicked, this, [this] {
            const QString path = QFileDialog::getOpenFileName(
                this, tr("Base configuration"), library::lastDialogDir(),
                tr("Harmony configuration (*.EZHex *.ezhex);;Legacy configuration (UserConfiguration.xml)"));
            if (!path.isEmpty()) {
                library::setLastDialogDir(QFileInfo(path).absolutePath());
                addSource(path);
            }
        });
    }
    form->addRow(tr("Name:"), name_);
    form->addRow(tr("Location:"), locationRow);

    auto* cancel = new QPushButton(tr("Cancel"), this);
    create_ = new QPushButton(tr("Create"), this);
    create_->setObjectName("primaryButton");
    create_->setDefault(true);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(create_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    // Окно не может стать меньше содержимого (иначе поля и подписи налезают друг на друга).
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    layout->setSpacing(8);
    layout->addWidget(title);
    layout->addWidget(hint);
    layout->addSpacing(8);
    layout->addLayout(form);
    layout->addWidget(result_);
    layout->addSpacing(8);
    layout->addLayout(buttons);

    connect(name_, &QLineEdit::textEdited, this, [this] { nameEdited_ = true; });
    connect(name_, &QLineEdit::textChanged, this, [this] { validate(); });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(create_, &QPushButton::clicked, this, &QDialog::accept);
    connect(change, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Where to store the configuration"), base_);
        if (!dir.isEmpty()) {
            base_ = dir;
            validate();
        }
    });
    validate();
}

QString NewConfigDialog::source() const {
    return source_ != nullptr ? source_->currentData().toString() : QString();
}

void NewConfigDialog::addSource(const QString& folder) {
    const QString path = library::normalized(folder);
    int index = source_->findData(path);
    if (index < 0) {
        source_->addItem(tr("Copy: %1").arg(QFileInfo(path).fileName()), path);
        index = source_->count() - 1;
        source_->setItemData(index, QDir::toNativeSeparators(path), Qt::ToolTipRole);
    }
    source_->setCurrentIndex(index);
}

void NewConfigDialog::suggestName() {
    if (nameEdited_) {
        return;
    }
    const QString from = source();
    if (from.isEmpty()) {
        name_->setText(tr("My configuration"));
    } else if (from == QLatin1String(kDemoSource)) {
        name_->setText(tr("Example"));
    } else {
        name_->setText(tr("%1 (copy)").arg(QFileInfo(from).isDir() ? QFileInfo(from).fileName() : QFileInfo(from).completeBaseName()));
    }
}

QString NewConfigDialog::name() const {
    return name_->text().trimmed();
}

QString NewConfigDialog::filePath() const {
    const QString fileName = name().endsWith(".EZHex", Qt::CaseInsensitive) ? name() : name() + ".EZHex";
    return QDir(base_).filePath(fileName);
}

void NewConfigDialog::validate() {
    location_->setText(QDir::toNativeSeparators(base_));

    // Имя станет именем файла: запрещаем символы, недопустимые в Windows.
    static const QRegularExpression forbidden(QStringLiteral(R"([<>:"/\\|?*])"));
    QString problem;
    if (name().isEmpty()) {
        problem = tr("Enter a name.");
    } else if (name().contains(forbidden)) {
        problem = tr("The name cannot contain < > : \" / \\ | ? *");
    } else if (QFileInfo::exists(filePath())) {
        problem = tr("A configuration with this name already exists here.");
    } else if (!source().isEmpty() && source() != QLatin1String(kDemoSource) &&
               library::normalized(source()) == library::normalized(filePath())) {
        problem = tr("The copy must use a different file from the base.");
    }

    create_->setEnabled(problem.isEmpty());
    QString text = tr("EZHex file: %1").arg(QDir::toNativeSeparators(filePath()));
    const QString from = source();
    if (!from.isEmpty() && from != QLatin1String(kDemoSource)) {
        text += tr("\nThe configuration from %1 will be copied into this archive.").arg(QDir::toNativeSeparators(from));
    }
    result_->setText(problem.isEmpty() ? text : problem);
}

} // namespace harmony::app
