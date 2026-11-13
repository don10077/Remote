#pragma once

#include <QColor>
#include <QSet>
#include <QHash>
#include <QList>
#include <QJsonObject>
#include <QPixmap>
#include <QRectF>
#include <QString>
#include <QWidget>

class QMimeData;
class QPainter;

namespace harmony::app {

/**
 * @brief Фото выбранной модели с зонами кнопок и необязательным экраном из профиля.
 *
 * The widget paints a product photo of the remote and puts two things on top:
 * - a rendered LCD (activity list or soft button page) that replaces the
 *   screen of the photo,
 * - an overlay per hard key that shows its state (assigned / free / broken,
 *   or the colour of the device a key belongs to inside an activity).
 *
 * All key geometry is stored in pixel coordinates of the source photo
 * (profile.json: size) and scaled on paint, so the widget can be any
 * size. Key ids are the names of document::data::HardButtons ("VolumeUp",
 * "Number1", ...), which is also what item::Button::name holds.
 */
class RemoteView : public QWidget {
    Q_OBJECT

public:
    /** how hard keys are decorated */
    enum class Mode {
        Neutral, ///< no decoration (overview pages)
        Device,  ///< key state of one device (assigned / free / broken)
        Activity ///< colour of the device each key of an activity belongs to
    };

    /** key state in Mode::Device */
    enum class KeyState {
        Free,
        Assigned,
        Broken ///< assigned to a command that does not exist (anymore)
    };

    /** content of the LCD */
    enum class Screen {
        Devices,    ///< Список отправляемых устройств
        Activities, ///< "My Activities" list
        Other,
        SoftButtons ///< soft button page of the current device / activity
    };

    /** one row of the "My Activities" screen */
    struct ActivityItem {
        QString label;
        /** тип активности из конфига (напр. "VirtualDvd"), по нему выбирается значок */
        QString type;
        bool selected = false;
    };

    explicit RemoteView(QWidget* parent = nullptr);
    /** Загрузить фото и геометрию модели; пустой id означает 900. */
    void setRemoteProfile(const QString& id);
    /// Снимок текущего экрана без корпуса для редактора зон; пустой при отсутствии экрана.
    QPixmap screenPreview(const QSize& size);

    /** size of the source photo in pixels, all zones are relative to it */
    static constexpr int PHOTO_WIDTH = 400;
    static constexpr int PHOTO_HEIGHT = 1500;
    /** soft buttons per LCD page (same as the remote) */
    static constexpr int SOFT_PER_PAGE = 6;
    /** activities per LCD page */
    static constexpr int ACTIVITIES_PER_PAGE = 3;

    void setMode(Mode mode);
    Mode mode() const { return currentMode; }

    /** key states for Mode::Device. Keys that are not listed are free. */
    void setDeviceKeys(const QHash<QString, KeyState>& states);
    /** key colours for Mode::Activity. Keys that are not listed are dimmed. */
    void setActivityKeys(const QHash<QString, QColor>& colours);
    /** highlight one hard key (empty = none) */
    void setSelectedKey(const QString& id);
    /// Подсветить все аппаратные кнопки команды.
    void setSelectedKeys(const QSet<QString>& ids);

    bool isLandscape() const { return photoSize.width() > photoSize.height(); }
    void flipPage(int delta);
    void setScreen(Screen screen);
    void selectScreenMode(const QString& mode);
    QStringList screenModes() const { return modes.keys(); }
    QString screenMode() const { return activeMode; }
    void setDeviceNames(const QStringList& names);
    Screen screen() const { return currentScreen; }

    /** rows of the activity screen, in remote order */
    void setActivities(const QList<ActivityItem>& items);
    /**
     * @brief soft buttons of the current device / activity
     * @param title  screen title (device or activity name)
     * @param labels label per position (index = position, empty = free slot)
     */
    void setSoftButtons(const QString& title, const QStringList& labels);
    /** highlight one soft button by position (-1 = none) */
    void setSelectedSoft(int position);

    /** flip the LCD to the page that contains this activity / soft position */
    void showActivityPage(int index);
    void showSoftPage(int position);

    /** перетаскивание: кнопка пульта (данные: id кнопки) */
    static constexpr const char* MIME_KEY = "application/x-h900-key";
    /** перетаскивание: экранная кнопка (данные: позиция) */
    static constexpr const char* MIME_SOFT = "application/x-h900-soft";
    /** перетаскивание: команда устройства (данные: "<id устройства>\n<команда>") */
    static constexpr const char* MIME_COMMAND = "application/x-h900-command";
    /** данные для MIME_COMMAND */
    static QByteArray commandPayload(const QString& deviceId, const QString& command);
    /** разобрать MIME_COMMAND, false = не команда */
    static bool parseCommandPayload(const QByteArray& data, QString& deviceId, QString& command);
    /** картинка под курсором при перетаскивании: подпись в «таблетке» экранной кнопки */
    static QPixmap dragPixmap(const QString& text, QFont font);

    /** translated, human readable name of a key id (for tooltips / lists) */
    static QString keyName(const QString& id);
    /** Командные кнопки из зон модели; пустой id означает 900, без зон — пустой список. */
    static QStringList hardKeyIds(const QString& remoteId = {});
    /** Переведённое название режима экрана ("transport" → «Мультимедиа»); неизвестный — как есть. */
    static QString screenModeName(const QString& mode);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    bool hasHeightForWidth() const override { return isLandscape(); }
    int heightForWidth(int width) const override;

signals:
    /** an assignable hard key was clicked */
    void keyClicked(const QString& id);
    /** a system key without editor function was clicked (Off, Help) */
    void systemKeyClicked(const QString& id);
    /** a soft button slot on the LCD was clicked */
    void softButtonClicked(int position);
    /** an activity row on the LCD was clicked (index in remote order) */
    void deviceClicked(int index); ///< Выбрано устройство, индекс в видимом списке
    void activityClicked(int index);
    /** the LCD content was switched from the remote (Activities / Options / Devices keys) */
    void screenChanged(Screen screen);
    /** кнопку перетащили на другую кнопку (copy: с Ctrl — копия) */
    void keyDropped(const QString& from, const QString& to, bool copy);
    /** команду перетащили на кнопку */
    void commandDropped(const QString& key, const QString& deviceId, const QString& command);
    /** экранную кнопку перетащили на другое место экрана */
    void softDropped(int from, int to);
    /** команду перетащили на место экранной кнопки */
    void commandDroppedOnSoft(int position, const QString& deviceId, const QString& command);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool event(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    /** what a click on a zone does */
    enum class ZoneKind {
        Key,        ///< assignable hard key
        System,     ///< Off / Help: reported, no editor function
        ScreenActs, ///< "Activities" key
        ScreenSoft, ///< white keys below the LCD (Options / Devices)
        PagePrev,
        PageNext,
        ScreenMode ///< кнопка на экране, открывающая другой экран (углы, Home)
    };

    struct Zone {
        QString id;
        QRectF rect;  ///< photo coordinates
        qreal radius; ///< photo pixels, < 0: fraction of the shorter side
        ZoneKind kind;
        QString target{}; ///< ScreenMode: режим, который открывает кнопка
    };

    /** hit target on the LCD, in photo coordinates */
    struct ScreenHit {
        QRectF rect;
        int value; ///< activity index or soft button position
    };

    /** физические кнопки модели и кнопки текущего экрана (с "key"/"goto"/"page"/"system") */
    const QList<Zone>& zones() const { return allZones; }
    /** Пересобрать кнопки текущего экрана из layouts.json; вызывается при смене режима. */
    void rebuildScreenZones();
    /** LCD rectangle in photo coordinates */
    QRectF screenRect() const { return modelScreen; }
    /** Перевести координаты отрисовщика LCD в пиксели фото выбранной модели. */
    QRectF screenToPhoto(const QRectF& rect) const;

    QRectF photoTarget() const;
    QRectF toWidget(const QRectF& photo) const;
    qreal scale() const;
    qreal zoneRadius(const Zone& zone) const;
    const Zone* zoneAt(const QPointF& pos) const;
    const ScreenHit* screenHitAt(const QPointF& pos) const;

    void paintZones(QPainter& p) const;
    void paintScreen(QPainter& p);
    /** Отрисовка экрана по раскладке из layouts.json и данным конфигурации (1000/1100/600-серия). */
    void paintModernScreen(QPainter& p);
    /**
     * Экран из картинок remotes/<id>/screens.json (раскладка с "compose"): фон, углы и кнопки —
     * картинки, сверху рисуются плитки и подписи из конфигурации. false — раскладка не собирается.
     */
    bool paintComposedScreen(QPainter& p, const QJsonObject& layout);
    /** Экран 600/650/665/700 (раскладка со стилем "h600"): шапка, 2x2 у боковых кнопок, Devices. */
    void paintCompactScreen(QPainter& p, const QJsonObject& layout);
    void paintClassicScreen(QPainter& p);
    void paintActivityScreen(QPainter& p);
    /// Общая раскладка списка активностей или устройств, с разными данными и действиями.
    bool isListScreen() const { return currentScreen == Screen::Activities || currentScreen == Screen::Devices; }
    const QList<ActivityItem>& listItems() const { return currentScreen == Screen::Devices ? deviceItems : activities; }
    void paintSoftScreen(QPainter& p);
    int slotsPerPage(const QString& mode) const;

    /** цель под курсором при перетаскивании: подсветка, true = можно бросить */
    bool updateDropTarget(const QMimeData* mime, const QPointF& pos);
    /** начать перетаскивание кнопки / экранной кнопки, если мышь ушла далеко */
    void startDragIfMoved(const QPointF& pos);

    int pageCount() const;
    int currentPage() const;


    QPixmap photo;
    QPixmap scaledPhoto; ///< cache for the current widget size
    QSize photoSize{PHOTO_WIDTH, PHOTO_HEIGHT};
    QRectF modelScreen;
    /** основной цвет фона LCD из profile.json (screenTint), может быть невалидным */
    QColor screenTint;
    QList<Zone> modelZones;
    QList<Zone> allZones; ///< modelZones + кнопки текущего экрана
    quint64 resourceRevision = 0; ///< Версия снимка ресурсов.
    QString profileId;
    QString profileDirectory; ///< папка ресурсов модели (profile.json, screens.json)
    QJsonObject modes;
    QHash<QString, QJsonObject> layouts;
    QString activeMode;
    QList<ActivityItem> deviceItems;
    int otherPage = 0;

    Mode currentMode = Mode::Neutral;
    Screen currentScreen = Screen::Activities;
    QHash<QString, KeyState> deviceKeys;
    QHash<QString, QColor> activityKeys;
    QString selectedKey;
    QSet<QString> selectedKeys;
    QString hoverId;

    QList<ActivityItem> activities;
    QString softTitle;
    QStringList softLabels;
    int selectedSoft = -1;
    int activityPage = 0;
    int softPage = 0;

    /** rebuilt on every screen paint, used for hit testing */
    QList<ScreenHit> screenHits;
    int hoverScreenHit = -1;

    /** нажатие левой кнопкой: откуда может начаться перетаскивание */
    QPointF pressPos;
    QString clickKey; ///< Щелчок завершается при отпускании, перетаскивание его отменяет.
    int clickSoft = -1;
    QString pressKey;
    int pressSoft = -1;
    /** зона листания, над которой сейчас тащат (листает один раз при входе) */
    QString dragFlipZone;
};

} // namespace harmony::app
