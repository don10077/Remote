#pragma once

/**
 * @file zoneCanvas.h
 * @brief Холст редактора зон: фото пульта, зоны слоя поверх, выделение, перетаскивание,
 *        маркеры размера, прилипание к краям соседних зон, рисование новой зоны, масштаб.
 *        Холст двигают левой кнопкой мыши, таща пустую область; Shift/Ctrl — рамка выделения,
 *        щелчок по пустому месту снимает выделение.
 *
 * Координаты зон — в единицах слоя: у корпуса это пиксели фото, у экрана — единицы `ref`
 * раскладки, которые растягиваются на рамку экрана на фото (`frame`), как в RemoteView.
 * Холст меняет свою копию зон во время жеста и сообщает о результате сигналом
 * geometryEdited(); историю правок ведёт владелец.
 */

#include "../zoneEditing.h"

#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QWidget>

#include <optional>

namespace harmony::app {

class ZoneCanvas final : public QWidget {
    Q_OBJECT

public:
    /// Рамка экрана (`screen` профиля) как элемент выделения слоя корпуса.
    static constexpr int SCREEN_ITEM = -2;
    /// Нет элемента.
    static constexpr int NO_ITEM = -1;

    /// Ориентир из другого слоя, в пикселях фото.
    struct Context {
        QRectF rect;
        bool highlight = false; ///< Физическая кнопка выделенной ячейки экрана (поле `button`).
    };

    explicit ZoneCanvas(QWidget* parent = nullptr);

    /// Прямоугольник элемента в наборе зон; пустой — элемента нет.
    static QRectF rectIn(const QJsonArray& zones, const std::optional<QRectF>& screen, int item);
    /// Записать прямоугольник элемента (значения округляются до сотых).
    static void setRectIn(QJsonArray& zones, std::optional<QRectF>& screen, int item, const QRectF& rect);

    /// Подложка: фото (или фото с отрисованным экраном) в пикселях фото `photoSize`.
    void setBackdrop(const QPixmap& pixmap, const QSizeF& photoSize);
    /**
     * @brief Задать систему координат слоя.
     * @param frame Область слоя на фото (всё фото или рамка экрана).
     * @param ref Размер слоя в его единицах.
     * @param screenLayer Слой экрана: фото вокруг рамки затемняется.
     * @param keepView false — вписать слой в окно.
     */
    void setLayer(const QRectF& frame, const QSizeF& ref, bool screenLayer, bool keepView = false);
    /// Зоны слоя и рамка экрана (только у корпуса). Выделение за пределами списка сбрасывается.
    void setZones(const QJsonArray& zones, const std::optional<QRectF>& screen);
    void setContext(const QList<Context>& items);
    /// Замечания проверки по элементам: цвет контура.
    void setMarks(const QHash<int, zoneEditing::Issue::Level>& marks);

    /// Выделение по порядку выбора: первый элемент — опорный для выравнивания.
    void setSelection(const QList<int>& items);
    [[nodiscard]] const QList<int>& selection() const { return selection_; }

    [[nodiscard]] const QJsonArray& zones() const { return zones_; }
    [[nodiscard]] const std::optional<QRectF>& screen() const { return screen_; }
    [[nodiscard]] QRectF itemRect(int item) const { return rectIn(zones_, screen_, item); }
    [[nodiscard]] QSizeF layerSize() const { return ref_; }
    /// Видимая в окне часть слоя, в единицах слоя.
    [[nodiscard]] QRectF visibleLayerRect() const;
    /// Можно ли выводить элемент за пределы слоя: кромки экрана (`role: band`) и уже вышедшие зоны.
    [[nodiscard]] bool mayLeave(int item) const;
    /// Элемент под координатой окна (NO_ITEM, если зоны нет) — для подсказок и тестов.
    [[nodiscard]] int itemAt(const QPointF& pos) const;

    void setDrawMode(bool on);
    [[nodiscard]] bool drawMode() const { return drawMode_; }
    void setSnapping(bool on) { snapping_ = on; }
    void setShowIds(bool on);

    /// Вписать слой в окно.
    void fitView();
    /// Масштаб относительно пикселей фото (1 — 100 %), с центром в середине окна.
    void zoomTo(qreal zoom);
    /// Прокрутить так, чтобы элемент был виден.
    void reveal(int item);

signals:
    void selectionChanged();
    /**
     * @brief Геометрия изменена мышью или клавишами; новые значения — zones() и screen().
     * @param mergeKey Правки с одинаковым непустым ключом подряд — один шаг отмены.
     */
    void geometryEdited(const QString& text, const QString& mergeKey);
    /// Нарисована новая зона (единицы слоя).
    void zoneDrawn(const QRectF& rect);
    void drawModeChanged(bool on);
    /// Двойной щелчок по элементу.
    void itemActivated(int item);
    /// Координаты под курсором и масштаб — для строки состояния.
    void statusChanged(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool event(QEvent* event) override;

private:
    /// Края, которые тянет маркер.
    enum Edge { Left = 1, Top = 2, Right = 4, Bottom = 8 };
    enum class Drag { None, Move, Resize, Band, Draw, Pan };

    QPointF layerToPhoto(const QPointF& point) const;
    QPointF toWidget(const QPointF& point) const;
    QRectF toWidget(const QRectF& rect) const;
    QRectF photoToWidget(const QRectF& rect) const;
    QPointF toLayer(const QPointF& widget) const;
    qreal unitX() const; ///< пикселей окна на единицу слоя по горизонтали
    qreal unitY() const;
    qreal radiusOf(int item, const QRectF& widgetRect) const;

    QList<int> allItems() const;
    /// Края под курсором у единственного выделенного элемента (маркеры и сами края).
    int edgesAt(const QPointF& pos) const;
    QList<QRectF> handleRects(const QRectF& widgetRect) const;
    void setItemRect(int item, const QRectF& rect);
    bool isSelected(int item) const { return selection_.contains(item); }
    /// Вернуть прямоугольник в слой: сдвигом (move) или обрезкой.
    QRectF keepInside(const QRectF& rect, bool move) const;

    /// Ближайшая линия `targets` к одному из значений `moving` в пределах порога: сдвиг и линия.
    static std::optional<qreal> snap(const QList<qreal>& moving, const QList<qreal>& targets, qreal threshold,
                                     qreal& line);
    /// Края и центры невыделенных зон и края слоя.
    void collectTargets(QList<qreal>& xs, QList<qreal>& ys) const;

    void dragMove(const QPointF& pos, Qt::KeyboardModifiers modifiers);
    void dragResize(const QPointF& pos, Qt::KeyboardModifiers modifiers);
    void dragBand(const QPointF& pos);
    void cancelDrag();
    void nudge(int dx, int dy, bool resize);
    void updateCursor(const QPointF& pos);
    void emitStatus(const QPointF& pos);
    void clampView();

    QPixmap backdrop_;
    QSizeF photoSize_{400, 1500};
    QRectF frame_{0, 0, 400, 1500};
    QSizeF ref_{400, 1500};
    bool screenLayer_ = false;

    QJsonArray zones_;
    std::optional<QRectF> screen_;
    QList<Context> context_;
    QHash<int, zoneEditing::Issue::Level> marks_;
    QList<int> selection_;

    qreal zoom_ = 1.0;
    QPointF offset_; ///< положение левого верхнего угла фото в окне
    bool fitted_ = true; ///< масштаб не меняли вручную: при изменении размера окна вписывать заново

    bool drawMode_ = false;
    bool snapping_ = true;
    bool showIds_ = true;

    Drag drag_ = Drag::None;
    bool clickPan_ = false; ///< холст двигают левой кнопкой по пустому месту: щелчок без движения снимает выделение
    bool panMoved_ = false; ///< холст уже сдвинули перетаскиванием, а не щёлкнули
    QPointF pressPos_;
    QPointF lastPos_;
    int pressItem_ = NO_ITEM;
    int resizeEdges_ = 0;
    QHash<int, QRectF> startRects_;
    QRectF band_; ///< рамка выделения или рисуемой зоны, в окне
    QList<int> bandBase_; ///< выделение до начала рамки (Shift/Ctrl — добавить)
    std::optional<qreal> guideX_; ///< линия прилипания, единицы слоя
    std::optional<qreal> guideY_;
    int hover_ = NO_ITEM;
};

} // namespace harmony::app
