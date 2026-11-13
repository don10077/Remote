/**
 * @file startDialog.cpp
 * @brief Реализация окна выбора конфигурации.
 */

#include "startDialog.h"

#include "../configLibrary.h"
#include "../icons.h"
#include "../labels.h"
#include "../theme.h"
#include "harmony/core/configDocument.h"

#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace harmony::app {

namespace {

constexpr int kFolderRole = Qt::UserRole + 1;   ///< Папка конфигурации (QString)
constexpr int kDetailRole = Qt::UserRole + 2;   ///< Вторая строка: состав и дата (QString)
constexpr int kMissingRole = Qt::UserRole + 3;  ///< Папки больше нет (bool)

/**
 * @brief Строка списка: значок пульта, имя, состав и путь мелким шрифтом.
 */
class ConfigDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        const QRect row = option.rect.adjusted(4, 2, -4, -2);
        if (option.state & QStyle::State_Selected) {
            painter->setPen(QPen(theme::color(theme::Color::Accent), 1.2));
            painter->setBrush(theme::color(theme::Color::AccentSoft));
            painter->drawRoundedRect(QRectF(row).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
        } else if (option.state & QStyle::State_MouseOver) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(theme::color(theme::Color::SurfaceAlt));
            painter->drawRoundedRect(row, 8, 8);
        }

        const bool missing = index.data(kMissingRole).toBool();
        const QColor muted = theme::color(theme::Color::TextMuted);
        icons::make(icons::Icon::Remote, missing ? muted : theme::color(theme::Color::Accent))
            .paint(painter, QRect(row.left() + 12, row.top() + (row.height() - 28) / 2, 28, 28));

        const int textLeft = row.left() + 54;
        const int width = row.right() - 12 - textLeft;
        QFont nameFont = option.font;
        nameFont.setWeight(QFont::DemiBold);
        nameFont.setPointSizeF(nameFont.pointSizeF() + 1.0);
        QFont smallFont = option.font;
        smallFont.setPointSizeF(smallFont.pointSizeF() - 0.5);
        const QFontMetrics nameMetrics(nameFont);
        const QFontMetrics smallMetrics(smallFont);

        int top = row.top() + 9;
        painter->setFont(nameFont);
        painter->setPen(missing ? muted : theme::color(theme::Color::Text));
        painter->drawText(QRect(textLeft, top, width, nameMetrics.height()), Qt::AlignLeft,
                          nameMetrics.elidedText(index.data(Qt::DisplayRole).toString(),
                                                 Qt::ElideRight, width));
        top += nameMetrics.height() + 1;
        painter->setFont(smallFont);
        painter->setPen(missing ? theme::color(theme::Color::Danger) : muted);
        painter->drawText(QRect(textLeft, top, width, smallMetrics.height()), Qt::AlignLeft,
                          smallMetrics.elidedText(index.data(kDetailRole).toString(),
                                                  Qt::ElideRight, width));
        top += smallMetrics.height() + 1;
        painter->setPen(muted);
        painter->drawText(QRect(textLeft, top, width, smallMetrics.height()), Qt::AlignLeft,
                          smallMetrics.elidedText(
                              QDir::toNativeSeparators(index.data(kFolderRole).toString()),
                              Qt::ElideMiddle, width));
        painter->restore();
    }

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& /*index*/) const override {
        QFont nameFont = option.font;
        nameFont.setPointSizeF(nameFont.pointSizeF() + 1.0);
        QFont smallFont = option.font;
        smallFont.setPointSizeF(smallFont.pointSizeF() - 0.5);
        return {option.rect.width(),
                QFontMetrics(nameFont).height() + 2 * QFontMetrics(smallFont).height() + 24};
    }
};

/// «20240802 154354» → «02.08.2024».
QString formatDate(const std::string& stamp) {
    const QString text = QString::fromStdString(stamp);
    return text.size() >= 8 ? QString("%1.%2.%3").arg(text.mid(6, 2), text.mid(4, 2), text.left(4))
                            : QString();
}

/// Кнопка действия под списком.
QPushButton* makeButton(icons::Icon icon, const QString& text, QWidget* parent) {
    auto* button = new QPushButton(icons::make(icon), text, parent);
    button->setIconSize(QSize(18, 18));
    return button;
}

} // namespace

StartDialog::StartDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Configurations"));
    setMinimumSize(620, 480);

    auto* title = new QLabel(tr("Choose a configuration"), this);
    title->setObjectName("pageTitle");
    auto* subtitle = new QLabel(
        tr("A configuration is a single EZHex file. Create one from scratch, open a backup or look at the example."),
        this);
    subtitle->setObjectName("pageMeta");
    subtitle->setWordWrap(true);

    list_ = new QListWidget(this);
    list_->setItemDelegate(new ConfigDelegate(list_));
    list_->setMouseTracking(true);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setSpacing(1);

    startup_ = new QCheckBox(tr("Open the selected configuration at startup"), this);
    remove_ = new QPushButton(tr("Remove from list"), this);
    remove_->setToolTip(tr("The configuration on disk is not deleted"));

    auto* create = makeButton(icons::Icon::Plus, tr("Create new…"), this);
    auto* browse = makeButton(icons::Icon::Folder, tr("Open…"), this);
    auto* demo = makeButton(icons::Icon::Play, tr("Example"), this);
    open_ = new QPushButton(tr("Open"), this);
    open_->setObjectName("primaryButton");
    open_->setDefault(true);

    auto* listRow = new QHBoxLayout;
    listRow->addWidget(startup_, 1);
    listRow->addWidget(remove_);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addWidget(create);
    buttons->addWidget(browse);
    buttons->addWidget(demo);
    buttons->addStretch(1);
    buttons->addWidget(open_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    // Окно не может стать меньше содержимого (иначе поля и подписи налезают друг на друга).
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    layout->setSpacing(8);
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addSpacing(8);
    layout->addWidget(list_, 1);
    layout->addLayout(listRow);
    layout->addSpacing(6);
    layout->addLayout(buttons);

    connect(list_, &QListWidget::currentItemChanged, this, [this] { updateButtons(); });
    connect(list_, &QListWidget::itemActivated, this, [this] { openSelected(); });
    connect(open_, &QPushButton::clicked, this, [this] { openSelected(); });
    connect(create, &QPushButton::clicked, this, [this] { finish(Choice::Create); });
    connect(browse, &QPushButton::clicked, this, [this] { finish(Choice::Browse); });
    connect(demo, &QPushButton::clicked, this, [this] { finish(Choice::Demo); });
    connect(remove_, &QPushButton::clicked, this, [this] {
        if (QListWidgetItem* item = list_->currentItem()) {
            library::forget(item->data(kFolderRole).toString());
            delete item;
            updateButtons();
        }
    });
    // Галочка относится к выбранной строке: снимаем — при запуске снова покажется это окно.
    connect(startup_, &QCheckBox::toggled, this, [this](bool on) {
        const QListWidgetItem* item = list_->currentItem();
        if (item == nullptr) {
            return;
        }
        const QString folder = item->data(kFolderRole).toString();
        if (on) {
            library::setStartupFolder(folder);
        } else if (library::startupFolder() == folder) {
            library::setStartupFolder({});
        }
    });

    fillList();
}

void StartDialog::fillList() {
    const QString startup = library::startupFolder();
    QListWidgetItem* select = nullptr;
    for (const QString& folder : library::recent()) {
        auto* item = new QListWidgetItem(list_);
        item->setData(kFolderRole, folder);

        // Читаем конфигурацию, чтобы показать её состав (файлы небольшие, это быстро).
        const auto document = ConfigDocument::open(folder.toStdU16String());
        if (document) {
            const auto& model = document->model();
            item->setText(QString::fromStdString(document->name()));
            QString detail =
                labels::plural(static_cast<qsizetype>(model.devices.size()), "device", "devices") +
                " · " +
                labels::plural(static_cast<qsizetype>(model.activities.size()), "activity", "activities");
            const QString date = formatDate(model.property("LastUpdated"));
            if (!date.isEmpty()) {
                detail += tr(" · updated %1").arg(date);
            }
            item->setData(kDetailRole, detail);
        } else {
            item->setText(QFileInfo(folder).fileName());
            item->setData(kDetailRole, document.error().code == ErrorCode::NotFound
                                           ? tr("configuration not found")
                                           : tr("could not read the configuration"));
            item->setData(kMissingRole, true);
        }
        if (folder == startup || select == nullptr) {
            select = item;
        }
    }

    if (list_->count() == 0) {
        auto* empty = new QListWidgetItem(tr("No configurations yet"), list_);
        empty->setData(kDetailRole, tr("Create a new one, open a remote backup or look at the example"));
        empty->setFlags(Qt::NoItemFlags);
        empty->setData(kMissingRole, true);
    } else {
        list_->setCurrentItem(select);
    }
    updateButtons();
}

void StartDialog::updateButtons() {
    const QListWidgetItem* item = list_->currentItem();
    const bool selectable = item != nullptr && (item->flags() & Qt::ItemIsEnabled);
    const bool openable = selectable && !item->data(kMissingRole).toBool();
    open_->setEnabled(openable);
    remove_->setEnabled(selectable);
    startup_->setEnabled(openable);
    const QSignalBlocker blocker(startup_);
    startup_->setChecked(openable && library::startupFolder() == item->data(kFolderRole).toString());
}

void StartDialog::openSelected() {
    const QListWidgetItem* item = list_->currentItem();
    if (item == nullptr || item->data(kMissingRole).toBool()) {
        return;
    }
    folder_ = item->data(kFolderRole).toString();
    finish(Choice::Open);
}

void StartDialog::finish(Choice choice) {
    choice_ = choice;
    accept();
}

} // namespace harmony::app
