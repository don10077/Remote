/**
 * @file quickSearch.cpp
 * @brief Окно быстрого поиска по конфигурации.
 */

#include "quickSearch.h"

#include "../icons.h"
#include "../labels.h"
#include "../modern/remoteView.h"

#include <QEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace harmony::app {

namespace {

constexpr int kListMax = 60; ///< Сколько строк показывать
constexpr const char* kHardGroup = "HardButtons";

using Kind = QuickSearch::Hit::Kind;

/// Устройства и активности — первыми, затем кнопки, затем команды.
int kindOrder(Kind kind) {
    switch (kind) {
    case Kind::Device:
    case Kind::Activity:
        return 0;
    case Kind::DeviceKey:
    case Kind::ActivityKey:
    case Kind::DeviceSoft:
    case Kind::ActivitySoft:
        return 1;
    case Kind::Command:
        return 2;
    }
    return 3;
}

/// Команда из действия «<устройство>_<команда>_<модификатор>».
QString commandOf(const std::string& actionId) {
    const auto first = actionId.find('_');
    const auto last = actionId.rfind('_');
    if (first == std::string::npos || last <= first) {
        return QString::fromStdString(actionId);
    }
    return QString::fromStdString(actionId.substr(first + 1, last - first - 1));
}

} // namespace

QuickSearch::QuickSearch(const model::RemoteConfig& config, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Quick search"));
    setObjectName("quickSearch");

    edit_ = new QLineEdit(this);
    edit_->setPlaceholderText(tr("Device, activity, button or command…"));
    edit_->setClearButtonEnabled(true);
    QFont big = edit_->font();
    big.setPointSizeF(big.pointSizeF() * 1.2);
    edit_->setFont(big);
    edit_->installEventFilter(this); // стрелки — по списку, а не по полю

    list_ = new QTreeWidget(this);
    list_->setColumnCount(2);
    list_->setHeaderHidden(true);
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->setIconSize(QSize(20, 20));
    list_->setFocusPolicy(Qt::NoFocus); // печатать можно всегда — фокус остаётся в поле
    list_->header()->setStretchLastSection(false);
    list_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    hint_ = new QLabel(this);
    hint_->setObjectName("pageMeta");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 10);
    layout->setSpacing(8);
    layout->addWidget(edit_);
    layout->addWidget(list_, 1);
    layout->addWidget(hint_);

    connect(edit_, &QLineEdit::textChanged, this, &QuickSearch::update);
    connect(edit_, &QLineEdit::returnPressed, this, &QuickSearch::activateCurrent);
    connect(list_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        list_->setCurrentItem(item);
        activateCurrent();
    });

    collect(config);
    update();
    resize(620, 440);
}

void QuickSearch::collect(const model::RemoteConfig& config) {
    auto add = [this](Hit hit) {
        hit.haystack = (hit.title + QLatin1Char(' ') + hit.where + QLatin1Char(' ') +
                        QString::fromStdString(hit.key + ' ' + hit.command))
                           .toLower();
        all_.push_back(std::move(hit));
    };
    // Кнопки владельца: «Громкость + → VolumeUp», «Субтитры → Subtitle».
    auto addButtons = [&](const std::vector<model::ControlGroup>& groups, const std::string& id,
                          const QString& owner, bool device) {
        for (const auto& group : groups) {
            const bool hard = group.name == kHardGroup;
            for (const auto& button : group.buttons) {
                Hit hit;
                hit.id = id;
                if (hard) {
                    hit.kind = device ? Kind::DeviceKey : Kind::ActivityKey;
                    hit.key = button.name;
                    hit.title = QStringLiteral("%1 → %2").arg(
                        RemoteView::keyName(QString::fromStdString(button.name)),
                        commandOf(button.actionId));
                    hit.where = tr("Key · %1").arg(owner);
                } else {
                    hit.kind = device ? Kind::DeviceSoft : Kind::ActivitySoft;
                    hit.position = button.position;
                    hit.title = QStringLiteral("%1 → %2").arg(labels::button(button),
                                                              commandOf(button.actionId));
                    hit.where = tr("Screen button · %1").arg(owner);
                }
                add(std::move(hit));
            }
        }
    };

    for (const auto& activity : config.activities) {
        if (activity.isPowerOff()) {
            continue;
        }
        const QString name = QString::fromStdString(activity.label);
        Hit hit;
        hit.kind = Kind::Activity;
        hit.id = activity.id;
        hit.title = name;
        hit.where = tr("Activity · %1").arg(labels::activityType(activity.type));
        add(std::move(hit));
        addButtons(activity.controlGroups, activity.id, name, false);
    }
    for (const auto& device : config.devices) {
        const QString name = QString::fromStdString(device.displayName());
        Hit hit;
        hit.kind = Kind::Device;
        hit.id = device.id;
        hit.title = name;
        hit.where =
            tr("Device · %1").arg(QString::fromStdString(device.manufacturer + " " + device.model));
        add(std::move(hit));
        addButtons(device.controlGroups, device.id, name, true);
        for (const auto& command : device.commands) {
            Hit item;
            item.kind = Kind::Command;
            item.id = device.id;
            item.command = command.name;
            item.title = QString::fromStdString(command.name);
            item.where = tr("Command · %1").arg(name);
            add(std::move(item));
        }
    }
}

void QuickSearch::update() {
    list_->clear();
    shown_.clear();
    const QStringList words = edit_->text().toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    std::vector<std::size_t> found;
    for (std::size_t i = 0; i < all_.size(); ++i) {
        const QString& haystack = all_[i].haystack;
        if (std::ranges::all_of(
                words, [&haystack](const QString& word) { return haystack.contains(word); })) {
            found.push_back(i);
        }
    }
    // Порядок: слова — в самом названии (а не только в имени владельца), затем вид
    // (устройства и активности, кнопки, команды), затем название начинается с первого слова.
    const QString first = words.isEmpty() ? QString() : words.front();
    auto inTitle = [&](std::size_t i) {
        const QString title = all_[i].title.toLower();
        return std::ranges::all_of(words,
                                   [&title](const QString& word) { return title.contains(word); });
    };
    auto startsWith = [&](std::size_t i) {
        return !first.isEmpty() && all_[i].title.toLower().startsWith(first);
    };
    std::ranges::stable_sort(found, [&](std::size_t a, std::size_t b) {
        if (inTitle(a) != inTitle(b)) {
            return inTitle(a);
        }
        if (kindOrder(all_[a].kind) != kindOrder(all_[b].kind)) {
            return kindOrder(all_[a].kind) < kindOrder(all_[b].kind);
        }
        return startsWith(a) && !startsWith(b);
    });
    for (std::size_t i = 0; i < found.size() && i < static_cast<std::size_t>(kListMax); ++i) {
        const Hit& hit = all_[found[i]];
        auto* item = new QTreeWidgetItem(list_, {hit.title, hit.where});
        if (hit.kind == Kind::Device || hit.kind == Kind::Command) {
            item->setForeground(1, palette().color(QPalette::PlaceholderText));
        }
        shown_.push_back(found[i]);
    }
    if (list_->topLevelItemCount() > 0) {
        list_->setCurrentItem(list_->topLevelItem(0));
    }
    hint_->setText(
        found.empty() ? tr("Nothing found")
        : found.size() > kListMax
            ? tr("Found %1, showing the first %2 — type more.").arg(found.size()).arg(kListMax)
            : tr("↑↓ — choose, Enter — open, Esc — close"));
}

void QuickSearch::activateCurrent() {
    const int row = list_->indexOfTopLevelItem(list_->currentItem());
    if (row < 0 || static_cast<std::size_t>(row) >= shown_.size()) {
        return;
    }
    chosen_ = all_[shown_[static_cast<std::size_t>(row)]];
    accept();
}

bool QuickSearch::eventFilter(QObject* watched, QEvent* event) {
    if (watched == edit_ && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        const int count = list_->topLevelItemCount();
        int row = list_->indexOfTopLevelItem(list_->currentItem());
        switch (key->key()) {
        case Qt::Key_Down:
            row = std::min(row + 1, count - 1);
            break;
        case Qt::Key_Up:
            row = std::max(row - 1, 0);
            break;
        case Qt::Key_PageDown:
            row = std::min(row + 10, count - 1);
            break;
        case Qt::Key_PageUp:
            row = std::max(row - 10, 0);
            break;
        default:
            return QDialog::eventFilter(watched, event);
        }
        if (count > 0) {
            list_->setCurrentItem(list_->topLevelItem(row));
        }
        return true;
    }
    return QDialog::eventFilter(watched, event);
}

} // namespace harmony::app
