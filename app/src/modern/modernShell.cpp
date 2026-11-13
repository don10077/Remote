#include "modernShell.h"

#include "../icons.h"
#include "../theme.h"

#include <QApplication>
#include <QDrag>
#include <QDropEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

using harmony::app::ModernShell;
using harmony::app::NavItem;
using harmony::app::NavRow;

namespace {

/** mime type of a dragged sidebar row, one per section */
QString navMime(const QString& section) {
    return QStringLiteral("application/x-h900edit-nav-") + section;
}

/**
 * Container of the rows of one item section. Accepts rows of its own
 * section, shows an insert line while dragging and reports the drop.
 */
class NavDropBox : public QWidget {
public:
    explicit NavDropBox(QWidget* parent) : QWidget(parent) {
        line = new QFrame(this);
        line->setObjectName("navDropLine");
        line->setFixedHeight(2);
        line->hide();
    }

    /** accepted mime type, empty = no drops */
    QString mime;
    /** rows of the section, top to bottom */
    std::function<QList<QWidget*>()> rows;
    /** row with this id was dropped at insert position (0..rows) */
    std::function<void(uint32_t, int)> dropped;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (accepts(event)) {
            event->acceptProposedAction();
        } else {
            event->ignore();
        }
    }

    void dragMoveEvent(QDragMoveEvent* event) override {
        if (!accepts(event)) {
            event->ignore();
            return;
        }
        showLine(insertIndex(event->position().toPoint()));
        event->acceptProposedAction();
    }

    void dragLeaveEvent(QDragLeaveEvent* event) override {
        line->hide();
        QWidget::dragLeaveEvent(event);
    }

    void dropEvent(QDropEvent* event) override {
        line->hide();
        if (!accepts(event)) {
            event->ignore();
            return;
        }
        const uint32_t id = event->mimeData()->data(mime).toUInt();
        const int index = insertIndex(event->position().toPoint());
        event->acceptProposedAction();
        if (dropped) {
            dropped(id, index);
        }
    }

private:
    bool accepts(const QDropEvent* event) const {
        return !mime.isEmpty() && event->mimeData()->hasFormat(mime);
    }

    /** row index a drop at this position would be inserted before */
    int insertIndex(const QPoint& pos) const {
        const QList<QWidget*> list = rows ? rows() : QList<QWidget*>();
        for (int i = 0; i < list.size(); ++i) {
            if (pos.y() < list[i]->geometry().center().y()) {
                return i;
            }
        }
        return static_cast<int>(list.size());
    }

    void showLine(int index) {
        const QList<QWidget*> list = rows ? rows() : QList<QWidget*>();
        if (list.isEmpty()) {
            line->hide();
            return;
        }
        const int y = (index < list.size()) ? list[index]->geometry().top() - 2
                                            : list.last()->geometry().bottom() + 1;
        line->setGeometry(8, std::max(0, y), width() - 16, 2);
        line->show();
        line->raise();
    }

    QFrame* line = nullptr;
};

} // namespace

// ── NavRow ───────────────────────────────────────────────────────────────────

void NavRow::setDragData(const QString& mimeType, const QByteArray& payload) {
    dragMime = mimeType;
    dragPayload = payload;
}

void NavRow::mousePressEvent(QMouseEvent* event) {
    pressPos = event->position().toPoint();
    emit clicked();
    QWidget::mousePressEvent(event);
}

void NavRow::mouseMoveEvent(QMouseEvent* event) {
    if (dragMime.isEmpty() || !(event->buttons() & Qt::LeftButton) ||
        ((event->position().toPoint() - pressPos).manhattanLength() <
         QApplication::startDragDistance())) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    auto* drag = new QDrag(this);
    auto* mime = new QMimeData;
    mime->setData(dragMime, dragPayload);
    drag->setMimeData(mime);
    drag->setPixmap(grab());
    drag->setHotSpot(pressPos);
    drag->exec(Qt::MoveAction);
}

ModernShell::ModernShell(QWidget* parent) : QWidget(parent) {
    rootLayout = new QHBoxLayout(this);
    auto* layout = rootLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Слева — боковая панель, справа — страница. Перегородку можно тянуть:
    // длинные названия устройств и активностей не обязаны обрезаться.
    splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName("mainSplitter");
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(4);

    // sidebar
    sidebar = new QWidget(splitter);
    sidebar->setObjectName("sidebar");
    sidebar->setAttribute(Qt::WA_StyledBackground, true);
    sidebar->setMinimumWidth(210);
    sidebar->setMaximumWidth(640);
    auto* sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(8, 12, 8, 8);
    sideLayout->setSpacing(2);

    // remote / document card on top, opens the card page (connection)
    card = new NavRow(sidebar);
    card->setObjectName("sidebarCard");
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setCursor(Qt::PointingHandCursor);
    auto* cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(10, 8, 10, 8);
    cardLayout->setSpacing(10);
    auto* cardIcon = new QLabel(card);
    cardIcon->setObjectName("navTile");
    cardIcon->setFixedSize(28, 28);
    cardIcon->setAlignment(Qt::AlignCenter);
    cardIcon->setText(QStringLiteral("H"));
    cardLayout->addWidget(cardIcon);
    auto* cardText = new QVBoxLayout();
    cardText->setSpacing(1);
    auto* appTitle = new QLabel(tr("Harmony 900"), card);
    appTitle->setObjectName("appTitle");
    cardText->addWidget(appTitle);
    documentLabel = new QLabel(tr("no configuration"), card);
    documentLabel->setObjectName("appSub");
    cardText->addWidget(documentLabel);
    cardLayout->addLayout(cardText, 1);
    connect(card, &NavRow::clicked, this, [this]() {
        if (!cardPage.isEmpty()) {
            setCurrentPage(cardPage);
        }
    });
    sideLayout->addWidget(card);

    // the rows scroll: with many devices / activities the window could
    // otherwise not get lower than all rows together
    auto* rowsScroll = new QScrollArea(sidebar);
    rowsScroll->setObjectName("sidebarScroll");
    rowsScroll->setFrameShape(QFrame::NoFrame);
    rowsScroll->setWidgetResizable(true);
    rowsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rowsScroll->viewport()->setAutoFillBackground(false);
    auto* rows = new QWidget(rowsScroll);
    rows->setAutoFillBackground(false);
    auto* rowsLayout = new QVBoxLayout(rows);
    rowsLayout->setContentsMargins(0, 0, 0, 0);
    rowsLayout->setSpacing(2);
    rowsScroll->setWidget(rows);
    sideLayout->addWidget(rowsScroll, 1);

    // document items (devices, activities), filled by setItems()
    itemsLayout = new QVBoxLayout();
    itemsLayout->setContentsMargins(0, 0, 0, 0);
    itemsLayout->setSpacing(2);
    rowsLayout->addLayout(itemsLayout);

    rowsLayout->addStretch(1);

    splitter->addWidget(sidebar);

    // page host: header (title + meta) + pages
    pageHost = new QWidget(this);
    pageHost->setObjectName("pageHost");
    pageHost->setAttribute(Qt::WA_StyledBackground, true);
    auto* hostLayout = new QVBoxLayout(pageHost);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(0);

    auto* head = new QWidget(pageHost);
    head->setObjectName("pageHead");
    head->setAttribute(Qt::WA_StyledBackground, true);
    auto* headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(24, 18, 24, 14);
    headLayout->setSpacing(12);
    auto* titles = new QVBoxLayout();
    titles->setSpacing(2);
    // Название и виджет рядом с ним («Активно») — одна строка, чтобы переключатель
    // стоял у названия, а не терялся между кнопками действий.
    auto* titleRow = new QHBoxLayout();
    titleRow->setSpacing(14);
    headerTitle = new QLabel(head);
    headerTitle->setObjectName("pageTitle");
    // переносятся, а не растягивают страницу: место справа нужно кнопкам действий
    headerTitle->setWordWrap(true);
    titleRow->addWidget(headerTitle);
    // растяжка в конце строки: виджет рядом с названием остаётся у текста,
    // а не уезжает к кнопкам действий
    titleRow->addStretch(1);
    titleLayout = titleRow;
    titles->addLayout(titleRow);
    headerMeta = new QLabel(head);
    headerMeta->setObjectName("pageMeta");
    headerMeta->setWordWrap(true);
    titles->addWidget(headerMeta);
    headLayout->addLayout(titles, 1);
    pageHeadLayout = headLayout;
    hostLayout->addWidget(head);

    // the stack is as high as its highest page needs (also hidden pages):
    // in a scroll area the window can get lower, the page then scrolls
    auto* pageScroll = new QScrollArea(pageHost);
    pageScroll->setObjectName("pageScroll");
    pageScroll->setFrameShape(QFrame::NoFrame);
    pageScroll->setWidgetResizable(true);
    pageScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    stack = new QStackedWidget(pageScroll);
    pageScroll->setWidget(stack);
    hostLayout->addWidget(pageScroll, 1);

    splitter->addWidget(pageHost);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({280, 900});
    layout->addWidget(splitter, 1);
}

// ── pages ────────────────────────────────────────────────────────────────────

void ModernShell::addPage(const QString& id, const QString& title, QWidget* page) {
    Entry entry;
    entry.id = id;
    entry.title = title;
    entries.append(entry);
    stack->addWidget(page);

    if (currentId.isEmpty()) {
        setCurrentPage(id);
    }
}

void ModernShell::setCurrentPage(const QString& id) {
    const int index = indexOf(id);
    if (index < 0) {
        return;
    }
    const bool changed = (currentId != id);
    stack->setCurrentIndex(index);
    currentId = id;

    // an item stays highlighted only while its page is shown
    const ItemSection* section = findSection(currentItemSection);
    if ((section != nullptr) && (section->pageId != id)) {
        currentItemSection.clear();
        currentItemId = 0;
    }

    updateChecked();
    updateHeader();
    if (changed) {
        emit currentPageChanged(id);
    }
}

QString ModernShell::currentPageId() const {
    return currentId;
}

void ModernShell::setDocumentName(const QString& name) {
    if (documentLabel != nullptr) {
        documentLabel->setText(name);
    }
}

void ModernShell::setSidePanel(QWidget* panel) {
    if (sidePanel != nullptr) {
        rootLayout->removeWidget(sidePanel);
        sidePanel->hide();
    }
    sidePanel = panel;
    if (sidePanel != nullptr) {
        sidePanel->setParent(this);
        rootLayout->addWidget(sidePanel);
        sidePanel->show();
    }
}

void ModernShell::setHeaderWidget(QWidget* widget) {
    // справа от заголовка, по верхнему краю — рядом с названием объекта
    pageHeadLayout->addWidget(widget, 0, Qt::AlignTop);
}

void ModernShell::setTitleWidget(QWidget* widget) {
    // сразу после названия страницы, до кнопок действий; растяжка в конце
    // строки остаётся последней, чтобы виджет держался у названия
    if (titleLayout == nullptr) {
        return;
    }
    const int at = std::max(0, titleLayout->count() - 1);
    titleLayout->insertWidget(at, widget, 0, Qt::AlignVCenter);
}

void ModernShell::setRemoteName(const QString& name) {
    card->findChild<QLabel*>(QStringLiteral("appTitle"))->setText(name);
}

void ModernShell::setCardPage(const QString& pageId) {
    cardPage = pageId;
    updateChecked();
}

void ModernShell::setPageHeader(const QString& pageId, const QString& title, const QString& meta) {
    const int index = indexOf(pageId);
    if (index < 0) {
        return;
    }
    entries[index].headerTitle = title;
    entries[index].headerMeta = meta;
    if (pageId == currentId) {
        updateHeader();
    }
}

// ── item sections ────────────────────────────────────────────────────────────

void ModernShell::addItemSection(const QString& section, const QString& title,
                                 const QString& pageId, const QString& addText) {
    ItemSection entry;
    entry.id = section;
    entry.title = title;
    entry.pageId = pageId;

    auto* box = new NavDropBox(sidebar);
    // look the section up on every call: the section list may reallocate
    box->rows = [this, section]() {
        QList<QWidget*> list;
        if (const ItemSection* s = findSection(section)) {
            for (const auto& row : s->itemRows) {
                list.append(row.second);
            }
        }
        return list;
    };
    box->dropped = [this, section](uint32_t id, int index) { dropItem(section, id, index); };
    entry.box = box;
    auto* boxLayout = new QVBoxLayout(entry.box);
    boxLayout->setContentsMargins(0, 0, 0, 0);
    boxLayout->setSpacing(2);

    // heading: clickable title + "+" button
    auto* head = new QWidget(entry.box);
    head->setObjectName("navGroupHead");
    auto* headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(0, 12, 4, 4);
    headLayout->setSpacing(4);
    auto* headingRow = new NavRow(head);
    headingRow->setObjectName("navHeading");
    headingRow->setAttribute(Qt::WA_StyledBackground, true);
    headingRow->setCursor(Qt::PointingHandCursor);
    auto* headingLayout = new QHBoxLayout(headingRow);
    headingLayout->setContentsMargins(6, 4, 6, 4);
    entry.heading = new QLabel(title.toUpper(), headingRow);
    entry.heading->setObjectName("sectionHeading");
    // длинное название не должно выдавливать кнопку «+» за край панели
    entry.heading->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    entry.heading->setMinimumWidth(0);
    headingLayout->addWidget(entry.heading);
    headLayout->addWidget(headingRow, 1);
    // «добавить» — компактная кнопка в заголовке раздела, а не широкая
    // пунктирная плашка под списком. Плюс рисуется значком, а не символом «+»:
    // у шрифтов он стоит не по центру и разной толщины.
    auto* add = new QToolButton(head);
    add->setObjectName("navAdd");
    namespace app = harmony::app;
    add->setIcon(app::icons::make(app::icons::Icon::Plus, app::theme::color(app::theme::Color::Accent)));
    add->setIconSize(QSize(14, 14));
    add->setToolButtonStyle(Qt::ToolButtonIconOnly);
    add->setToolTip(addText);
    add->setAccessibleName(addText);
    add->setVisible(!addText.isEmpty());
    add->setCursor(Qt::PointingHandCursor);
    add->setFixedSize(22, 22);
    headLayout->addWidget(add);
    boxLayout->addWidget(head);

    entry.rows = new QVBoxLayout();
    entry.rows->setContentsMargins(0, 0, 0, 0);
    entry.rows->setSpacing(2);
    boxLayout->addLayout(entry.rows);

    const QString id = section;
    connect(headingRow, &NavRow::clicked, this, [this, pageId]() { setCurrentPage(pageId); });
    connect(add, &QToolButton::clicked, this, [this, id]() { emit addRequested(id); });

    itemsLayout->addWidget(entry.box);
    sections.append(entry);
}

void ModernShell::setItems(const QString& sectionId, const QList<NavItem>& items) {
    ItemSection* section = findSection(sectionId);
    if (section == nullptr) {
        return;
    }

    // drop old rows
    while (section->rows->count() > 0) {
        QLayoutItem* item = section->rows->takeAt(0);
        if (item->widget() != nullptr) {
            // строку могут удалять из её же обработчика (перетаскивание) — поэтому
            // deleteLater; но вне раскладки она до удаления висела бы поверх заголовка
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        delete item;
    }
    section->itemRows.clear();
    section->heading->setText(QString("%1 · %2").arg(section->title.toUpper()).arg(items.size()));

    for (const auto& item : items) {
        auto* row = new NavRow(section->box);
        row->setObjectName("navRow");
        row->setAttribute(Qt::WA_StyledBackground, true);
        row->setCursor(Qt::PointingHandCursor);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(10, 6, 10, 6);
        rowLayout->setSpacing(10);

        const QString sid = section->id;
        const QString pageId = section->pageId;
        const uint32_t itemId = item.id;

        auto* tile = new QLabel(row);
        tile->setObjectName("navTile");
        tile->setAlignment(Qt::AlignCenter);
        tile->setToolTip(item.tileTooltip);
        if (!item.icon.isNull()) {
            tile->setPixmap(item.icon.pixmap(item.iconSize, item.iconSize));
        } else {
            tile->setText(item.tile);
        }
        // a big icon (activities) needs a bigger tile
        const int tileSide = (item.iconSize > 18) ? item.iconSize + 8 : 28;
        tile->setFixedSize(tileSide, tileSide);
        if (item.colour.isValid()) {
            // coloured bar at the bottom: same colour as the keys on the remote
            tile->setStyleSheet(
                QString("#navTile { border-bottom: 3px solid %1; }").arg(item.colour.name()));
        }
        rowLayout->addWidget(tile);
        row->setToolTip(item.tooltip);

        auto* column = new QVBoxLayout();
        column->setSpacing(1);
        auto* label = new QLabel(item.label, row);
        label->setObjectName("navLabel");
        // узкая панель: длинное название обрезается, а не растягивает список
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        label->setMinimumWidth(0);
        column->addWidget(label);
        if (!item.meta.isEmpty()) {
            auto* meta = new QLabel(item.meta, row);
            meta->setObjectName("navMeta");
            meta->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            meta->setMinimumWidth(0);
            column->addWidget(meta);
        }
        rowLayout->addLayout(column, 1);

        connect(row, &NavRow::clicked, this, [this, sid, pageId, itemId]() {
            currentItemSection = sid;
            currentItemId = itemId;
            setCurrentPage(pageId);
            updateChecked();
            emit itemActivated(sid, itemId);
        });

        if (section->reorderable) {
            row->setDragData(navMime(sid), QByteArray::number(itemId));
        }
        section->rows->addWidget(row);
        section->itemRows.append(qMakePair(itemId, row));
    }
    updateChecked();
}

void ModernShell::setItemsReorderable(const QString& sectionId, bool on) {
    ItemSection* section = findSection(sectionId);
    if (section == nullptr) {
        return;
    }
    section->reorderable = on;
    auto* box = static_cast<NavDropBox*>(section->box);
    box->mime = on ? navMime(sectionId) : QString();
    box->setAcceptDrops(on);
    for (auto& row : section->itemRows) {
        row.second->setDragData(on ? navMime(sectionId) : QString(), QByteArray::number(row.first));
    }
}

void ModernShell::dropItem(const QString& sectionId, uint32_t id, int insertIndex) {
    const ItemSection* section = findSection(sectionId);
    if (section == nullptr) {
        return;
    }
    int from = -1;
    for (int i = 0; i < section->itemRows.size(); ++i) {
        if (section->itemRows[i].first == id) {
            from = i;
            break;
        }
    }
    if (from < 0) {
        return;
    }
    // insert position counts the row itself; after removing it, positions
    // below it move up by one
    const int to = (insertIndex > from) ? insertIndex - 1 : insertIndex;
    if (to == from) {
        return;
    }
    emit itemMoved(sectionId, id, to);
}

void ModernShell::setCurrentItem(const QString& section, uint32_t id) {
    currentItemSection = (id != 0) ? section : QString();
    currentItemId = id;
    updateChecked();
}

// ── private ──────────────────────────────────────────────────────────────────

void ModernShell::setChecked(QWidget* widget, bool checked) {
    if (widget == nullptr) {
        return;
    }
    if (widget->property("checked").toBool() == checked) {
        return;
    }
    widget->setProperty("checked", checked);
    // dynamic property selectors need a re-polish
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

void ModernShell::updateChecked() {
    for (auto& section : sections) {
        for (auto& row : section.itemRows) {
            setChecked(row.second, (section.id == currentItemSection) &&
                                       (row.first == currentItemId) &&
                                       (section.pageId == currentId));
        }
    }
    setChecked(card, !cardPage.isEmpty() && (cardPage == currentId));
}

void ModernShell::updateHeader() {
    const int index = indexOf(currentId);
    if (index < 0) {
        return;
    }
    const Entry& entry = entries[index];
    const QString title = entry.headerTitle.isEmpty() ? entry.title : entry.headerTitle;
    const QString meta = entry.headerTitle.isEmpty() ? QString() : entry.headerMeta;
    if (headerTitle != nullptr) {
        headerTitle->setText(title);
    }
    if (headerMeta != nullptr) {
        headerMeta->setText(meta);
        headerMeta->setVisible(!meta.isEmpty());
    }
}

int ModernShell::indexOf(const QString& id) const {
    for (int i = 0; i < entries.size(); ++i) {
        if (entries[i].id == id) {
            return i;
        }
    }
    return -1;
}

ModernShell::ItemSection* ModernShell::findSection(const QString& id) {
    for (auto& section : sections) {
        if (section.id == id) {
            return &section;
        }
    }
    return nullptr;
}
