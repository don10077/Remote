#pragma once

#include <QByteArray>
#include <QColor>
#include <QIcon>
#include <QList>
#include <QPair>
#include <QString>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QMouseEvent;
class QSplitter;
class QStackedWidget;
class QVBoxLayout;

namespace harmony::app {

/** one clickable row in the sidebar (icon tile + title + meta line) */
class NavRow : public QWidget {
    Q_OBJECT

public:
    explicit NavRow(QWidget* parent = nullptr) : QWidget(parent) {}

    /** make the row draggable: mime type + payload of the drag (empty = off) */
    void setDragData(const QString& mimeType, const QByteArray& payload);

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    QString dragMime;
    QByteArray dragPayload;
    QPoint pressPos;
};

/** one document item in the sidebar (a device, an activity) */
struct NavItem {
    uint32_t id = 0;
    QString label;
    QString meta;
    /** short text in the tile (abbreviation, number) */
    QString tile;
    /** accent of the tile, invalid = none */
    QColor colour;
    /** icon in the tile (replaces the tile text) */
    QIcon icon;
    /** size of the icon in pixels; more than 18 makes the tile bigger too */
    int iconSize = 18;
    /** tooltip of the whole row */
    QString tooltip;
    QString tileTooltip;
};

/**
 * Navigation shell of the modern interface:
 * a sidebar on the left, a page header (title + meta), the pages in the
 * middle and an optional fixed panel on the right.
 *
 * The sidebar holds, top to bottom:
 * - the remote card (opens the "card page"),
 * - item sections: one row per document item (devices, activities),
 * - (pages are opened by the item rows and the card; they have no rows of their own).
 */
class ModernShell : public QWidget {
    Q_OBJECT

public:
    explicit ModernShell(QWidget* parent = nullptr);

    void addPage(const QString& id, const QString& title, QWidget* page);

    void setCurrentPage(const QString& id);
    QString currentPageId() const;

    void setDocumentName(const QString& name);
    /// Обновляет название выбранной модели на карточке пульта слева.
    void setRemoteName(const QString& name);

    /** fixed panel right of the pages (remote preview). nullptr = none */
    void setSidePanel(QWidget* panel);
    /** виджет справа в заголовке страницы (кнопки действий над выбранным объектом) */
    void setHeaderWidget(QWidget* widget);
    /** виджет вплотную к названию страницы, слева от кнопок действий */
    void setTitleWidget(QWidget* widget);

    /** page opened by a click on the remote card at the top of the sidebar */
    void setCardPage(const QString& pageId);
    /** override title / meta of the page header (e.g. name of the selected item) */
    void setPageHeader(const QString& pageId, const QString& title, const QString& meta);

    /**
     * @brief add an item section to the sidebar
     * @param section  section id, reported by the signals
     * @param title    section heading
     * @param pageId   page that shows the items of this section
     */
    void addItemSection(const QString& section, const QString& title, const QString& pageId,
                        const QString& addText = QString());
    /** replace the rows of an item section */
    void setItems(const QString& section, const QList<NavItem>& items);
    /** highlight one item row (id 0 = none) */
    void setCurrentItem(const QString& section, uint32_t id);
    /** allow reordering the rows of a section by drag and drop (reports itemMoved) */
    void setItemsReorderable(const QString& section, bool on);

signals:
    void currentPageChanged(const QString& id);
    /** an item row was clicked */
    void itemActivated(const QString& section, uint32_t id);
    /** the "+" of a section was clicked */
    void addRequested(const QString& section);
    /** a row was dropped at a new position (index in the list after the move) */
    void itemMoved(const QString& section, uint32_t id, int newIndex);

private:
    struct Entry {
        QString id;
        QString title;
        QString headerTitle; ///< overrides title in the page header
        QString headerMeta;  ///< overrides meta in the page header
    };

    struct ItemSection {
        QString id;
        QString title;
        QString pageId;
        QWidget* box = nullptr;
        QVBoxLayout* rows = nullptr;
        QLabel* heading = nullptr;
        QList<QPair<uint32_t, NavRow*>> itemRows;
        bool reorderable = false;
    };

    /** drop at insert position (0..rows) in a section */
    void dropItem(const QString& section, uint32_t id, int insertIndex);

    void updateHeader();
    void updateChecked();
    int indexOf(const QString& id) const;
    ItemSection* findSection(const QString& id);
    static void setChecked(QWidget* widget, bool checked);

    QList<Entry> entries;
    QList<ItemSection> sections;
    QStackedWidget* stack = nullptr;
    QHBoxLayout* rootLayout = nullptr;
    QSplitter* splitter = nullptr; ///< боковая панель и страницы: ширину можно менять
    QWidget* sidePanel = nullptr;
    QWidget* pageHost = nullptr;
    QWidget* sidebar = nullptr;
    QVBoxLayout* itemsLayout = nullptr;
    NavRow* card = nullptr;
    QLabel* documentLabel = nullptr;
    QLabel* headerTitle = nullptr;
    QLabel* headerMeta = nullptr;
    QHBoxLayout* pageHeadLayout = nullptr; ///< заголовок страницы: названия + виджет действий
    QHBoxLayout* titleLayout = nullptr;    ///< строка названия: название + виджет рядом с ним
    QString currentId;
    QString cardPage;
    QString currentItemSection;
    uint32_t currentItemId = 0;
};

} // namespace harmony::app
