#pragma once

/**
 * @file zoneEditorDialog.h
 * @brief Редактор зон пультов в remote.json: зоны корпуса (`hardware.zones`), ячейки экранов
 *        (`screens[].cells`), рамка экрана (`display.rect`).
 *
 * Слева холст (ZoneCanvas) с фото модели, справа список зон, свойства выделенной зоны
 * и замечания проверки. Правки идут через общую историю отмены модели; сохранение
 * заменяет в файлах папки ресурсов только изменённые фрагменты (zoneEditing) и просит
 * владельца перечитать ресурсы сигналом resourcesSaved().
 */

#include "zoneCanvas.h"

#include <QDialog>
#include <QHash>
#include <QLabel>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPixmap>
#include <QRectF>
#include <QSet>
#include <QSizeF>
#include <QStringList>
#include <QUndoStack>

#include <functional>
#include <optional>

class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLineEdit;
class QListWidget;
class QProcess;
class QPushButton;
class QSpinBox;

namespace harmony::app {

/**
 * @brief Подпись в одну строку: длинный текст сокращается многоточием.
 *
 * Нужна для строки состояния редактора: высота окна не меняется от длины
 * текста, а полный текст остаётся во всплывающей подсказке.
 */
class ElidedLabel final : public QLabel {
    Q_OBJECT

public:
    explicit ElidedLabel(QWidget* parent = nullptr);

    /// Задать полный текст; на экране он сокращается по ширине подписи.
    void setFullText(const QString& text);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    /// Пересчитать видимый текст под текущую ширину.
    void elide();

    QString full_;
};

class ZoneEditorDialog final : public QDialog {
    Q_OBJECT

public:
    /// Содержимое слоя: зоны и рамка экрана (рамка есть только у корпуса).
    struct State {
        QJsonArray zones;
        std::optional<QRectF> screen; ///< корпус: область экрана на фото; экран: зона плиток (`grid.area`)
        qreal gap = zoneEditing::kDefaultGap; ///< зазор плиток экрана (`grid.gap`); у корпуса не используется
        bool ownArea = false; ///< у экрана своя зона плиток (`grid.area`), иначе общая зона пульта (`tiles.area`)
        bool operator==(const State&) const = default;
    };

    /**
     * @brief Открыть редактор на модели.
     * @param model id модели; неизвестная — первая из списка.
     */
    explicit ZoneEditorDialog(const QString& model, QWidget* parent = nullptr);
    /// Останавливает незавершённое распознавание авто-зон.
    ~ZoneEditorDialog() override;

    /**
     * @brief Поставить слою состояние из истории правок и показать этот слой.
     * @param layerKey Ключ слоя (Layer::key).
     */
    void applyState(const QString& layerKey, const State& state);

signals:
    /// Файлы ресурсов записаны: их нужно перечитать.
    void resourcesSaved();

public slots:
    /// Закрытие (Esc, крестик, «Закрыть») с вопросом о несохранённых правках.
    void reject() override;

private:
    /// Слой разметки: корпус либо одна раскладка экрана модели.
    struct Layer {
        QString key;       ///< "body" или id раскладки при загрузке; не меняется до перезагрузки модели
        QString layout;    ///< id экрана в remote.json (у черновика — его временный ключ); пусто у корпуса
        QStringList modes; ///< режимы экрана модели, показывающие эту раскладку
        QString caption;
        QString role;       ///< роль экрана из remote.json
        /// Экран из remote.json без `cells` (ячейки — в current); у черновика — оформление образца.
        QJsonObject screen;
        bool draft = false; ///< новый экран, ещё не записанный в remote.json (живёт только в редакторе)
        QSizeF ref;        ///< единицы слоя: размер фото или `ref` раскладки
        State original;    ///< как в файле
        QList<QByteArray> originalText; ///< исходный текст зон `original` (пусто — не разобран)
        State current;
        [[nodiscard]] bool body() const { return key == QLatin1String("body"); }
    };
    enum class Align { Left, Right, Top, Bottom, CentreX, CentreY, Width, Height };

    /// Прочитать модель (remoteResources::readRemote); false — модель не прочитана, прежнее состояние сохранено.
    bool loadModel(const QString& model);
    /**
     * @brief Перечитать снимок ресурсов и модель, показать слой.
     * @param target Слой (Layer::key), который показать; не найден — корпус.
     * @param keepDrafts Вернуть черновик нового экрана (он живёт только в редакторе).
     */
    void reloadModel(QString target, bool keepDrafts = true);
    /// Текст файла папки ресурсов, а если его нет — из загруженного снимка.
    static QByteArray readResource(const QString& relative);
    /// `remotes/<id>/remote.json` открытой модели, путь от корня папки ресурсов.
    [[nodiscard]] QString remotePath() const;

    [[nodiscard]] bool hasLayer() const { return current_ >= 0 && current_ < layers_.size(); }
    Layer& layer() { return layers_[current_]; }
    const Layer& layer() const { return layers_[current_]; }
    [[nodiscard]] QJsonObject zoneAt(int item) const;
    [[nodiscard]] QString uniqueId(const QString& base, const QSet<QString>& taken) const;
    [[nodiscard]] QSet<QString> idsOf(const QJsonArray& zones) const;
    [[nodiscard]] QList<zoneEditing::Issue> issuesOf(const Layer& layer) const;
    /// Строки зон для записи: неизменённые зоны — исходным текстом.
    [[nodiscard]] static QList<QByteArray> linesOf(const Layer& layer);
    [[nodiscard]] bool isDirty() const;

    void showLayer(int index, bool keepView = false);
    /// Перерисовать подложку холста реальным видом экрана с несохранёнными правками зон.
    void updateScreenBackdrop();
    void refreshAll();
    void refreshList();
    void syncListSelection();
    void refreshProperties();
    void refreshContext();
    void refreshCheck();
    /// Обновить вкладку «Пульт»: название, подключение, формат, касания, плитки.
    void refreshRemote();
    void updateTitle();
    void note(const QString& text);

    /// Записать правку в историю: before — текущее состояние слоя.
    void edit(const QString& text, const State& after, const QString& mergeKey = {});
    void onCanvasGeometry(const QString& text, const QString& mergeKey);
    void onListSelection();
    void onModelChosen(int index);

    void addZone(const std::optional<QRectF>& drawn);
    /**
     * @brief Распознать кнопки на фото (tools/autoZones.py) и добавить зоны в слой корпуса.
     *
     * Скрипт запускается асинхронно: окно не блокируется, повторное нажатие кнопки отменяет
     * распознавание. Если распознавание уже идёт, вызов его останавливает.
     */
    void runAutoZones();
    /// Запустить следующий интерпретатор из autoAttempts_; список пуст — сообщить, что Python не найден.
    void startAutoZones();
    /// Остановить распознавание: процесс завершается, его результат не применяется.
    void stopAutoZones();
    /// Вернуть кнопке «Авто-зоны» обычный вид после завершения или отмены.
    void resetAutoAction();
    /**
     * @brief Добавить в корпус зоны из вывода скрипта.
     * @param output JSON-массив рамок (stdout `autoZones.py --json`).
     */
    void applyAutoZones(const QByteArray& output);
    /// Выделить все зоны текущего слоя (Ctrl+A).
    void selectAll();
    void duplicateZones();
    void removeZones();
    void copyZones();
    void pasteZones();
    void align(Align how);
    void setScreenArea(bool on);
    void applyRect();
    void applyId();
    void applyKind(int index);
    void applyRadius(double value);
    /// Применить роль подложки `ScreenPanel` (band, oval, side) из списка.
    void applyPanelRole(int index);
    /// Записать цвет выбранной кнопки или плитки экрана (поле `color` ячейки); пусто — убрать.
    void applyZoneColor();
    /// Применить действие экранной зоны из обычных полей формы.
    void applyAction();

    /// Сохранить все изменённые слои модели; false — отменено или ошибка (показана).
    bool save();
    /// Обновить вкладку «Экраны»: список экранов модели и параметры открытого экрана.
    void refreshScreens();
    /// Правка текста remote.json: текущий текст (в текущей схеме) → новый; nullopt — структура не распознана.
    using TextChange = std::function<std::optional<QByteArray>(const QByteArray&)>;
    /**
     * @brief Сразу изменить remote.json и перечитать модель.
     *
     * Несохранённые правки зон — сначала вопрос (сохранить, отбросить, отмена). Правка
     * применяется к тексту файла на диске в текущей схеме (remoteResources::currentSchema),
     * поэтому внешние изменения не теряются, а файл старой схемы записывается в новой.
     * @param change Правка текста.
     * @param target Слой (Layer::key), который показать после перезагрузки; пусто — корпус.
     * @param keepDrafts Вернуть черновик нового экрана после перезагрузки.
     * @return false — отменено или ошибка (показана).
     */
    bool modifyRemote(const TextChange& change, QString target, bool keepDrafts = true);
    /**
     * @brief Сразу записать одно значение remote.json (zoneEditing::patchValue).
     * @param screenId Экран, внутри которого путь; пусто — от корня файла.
     * @param keys Путь ключей, например `{"tiles", "color"}`.
     * @param value Новое значение; `QJsonValue::Undefined` — удалить ключ.
     * @param target Слой, который показать после перезагрузки.
     * @return false — запись не удалась или отменена (ошибка показана).
     */
    bool writeField(const QString& screenId, const QStringList& keys, const QJsonValue& value, QString target);
    /// Значение поля пульта: сразу в файл.
    void setRemoteField(const QStringList& keys, const QJsonValue& value);
    /// Значение поля открытого экрана: у черновика — в памяти, иначе сразу в файл.
    void setScreenField(const QStringList& keys, const QJsonValue& value);
    /**
     * @brief Высота шапки (`header`) или подвала (`footer`) фона открытого экрана.
     * @param zone `header` или `footer`.
     * @param height Единицы `ref`; 0 и меньше — зона убирается целиком вместе с цветом.
     */
    void setBackgroundZoneHeight(const QString& zone, qreal height);
    /// Показать на экране вводимый цвет поля экрана, не записывая файл; некорректный цвет не показывается.
    void previewScreenField(const QStringList& keys, const QString& text);
    /// Показать на экране вводимый цвет плиток пульта (`tiles.<key>`), не записывая файл.
    void previewTiles(const QString& key, const QString& text);
    /// Значение поля цвета: пусто — Undefined (удалить ключ); nullopt — цвет некорректен (замечание показано).
    std::optional<QJsonValue> colourValue(QLineEdit* input);
    /// Значение поля картинки: путь от папки ресурсов; nullopt — файла нет (замечание показано).
    std::optional<QJsonValue> imageValue(QLineEdit* input);
    /// Выбрать картинку в папке ресурсов и записать её путь в поле; false — отменено или файл вне папки.
    bool browseImage(QLineEdit* input);
    /// Разложить плитки открытого экрана сеткой из полей «колонки × ряды» и «зазор».
    void applyGrid();
    /**
     * @brief Разложить плитки открытого экрана сеткой одной отменяемой правкой.
     * @param gap Зазор между плитками и от краёв области плиток, в единицах `ref`.
     */
    void arrange(int cols, int rows, qreal gap, const QString& text, const QString& mergeKey);
    /**
     * @brief Состояние экрана с плитками, разложенными сеткой внутри зоны.
     * @param area Зона плиток; плитки отступают от её краёв и друг от друга на `gap`.
     * @return Новое состояние: лишние плитки убраны, недостающие добавлены, служебные зоны не тронуты.
     */
    [[nodiscard]] State laidOut(const State& state, int cols, int rows, qreal gap, const QRectF& area) const;
    /**
     * @brief Показать или убрать служебный элемент открытого экрана (заголовок, страница, часы, батарея).
     * @param index Номер элемента в списке флажков; правка отменяемая и сохраняется вместе с зонами.
     */
    void setService(int index, bool on);
    /// Своя зона плиток у открытого экрана (on) либо общая зона пульта; отменяемая правка.
    void setOwnArea(bool on);
    /**
     * @brief Общая зона плиток пульта: зона первого экрана без своей зоны (черновики не в счёт).
     * @param original Как в файле (true) или с правками.
     * @param skip Экран, который не учитывать.
     */
    [[nodiscard]] std::optional<QRectF> commonArea(bool original, int skip = -1) const;
    /// Состояние после правки: если зона плиток сдвинута, плитки разложены в ней той же сеткой.
    [[nodiscard]] State followArea(const Layer& layer, State next) const;
    /// Начать новый экран: черновик сразу показывается на холсте, параметры правятся вживую.
    void beginNewScreen();
    /// Оставить в списке типов только экраны, которых у пульта ещё нет.
    void updateRoleChoices();
    /// Записать черновик нового экрана в remote.json.
    void createNewScreen();
    /// Отбросить черновик нового экрана.
    void cancelNewScreen();
    /// Удалить экран, который сейчас открыт в редакторе.
    void removeScreen();
    /// Нет несохранённых правок либо пользователь их сохранил или отбросил.
    bool confirmDiscard();

    QString model_;
    QString modelName_;
    QPixmap photo_;
    QSizeF photoSize_;
    QSet<QString> modes_;     ///< режимы экрана модели (для `goto`)
    QList<Layer> layers_;     ///< [0] — корпус, далее экраны
    int current_ = -1;
    /// Тексты файлов на момент загрузки/сохранения: для обнаружения правки извне.
    QHash<QString, QByteArray> files_;
    QUndoStack undo_;

    ZoneCanvas* canvas_ = nullptr;
    QComboBox* modelBox_ = nullptr;
    QComboBox* layerBox_ = nullptr;
    std::optional<QRectF> previousScreen_; ///< Рамка до отключения экрана.
    QCheckBox* screenCheck_ = nullptr;
    QListWidget* screenList_ = nullptr;      ///< экраны модели (вкладка «Экраны»)
    QComboBox* roleBox_ = nullptr;           ///< роль нового экрана
    QPushButton* addScreenButton_ = nullptr;
    QWidget* draftPanel_ = nullptr;          ///< кнопки «Создать»/«Отмена» черновика
    int draftSerial_ = 0;                    ///< номер для уникального ключа черновика (история правок ссылается на ключ)
    QSpinBox* arrangeColsBox_ = nullptr;     ///< колонок плиток открытого экрана
    QSpinBox* arrangeRowsBox_ = nullptr;     ///< рядов плиток открытого экрана
    QPushButton* removeScreenButton_ = nullptr;
    QDoubleSpinBox* gapBox_ = nullptr;          ///< grid.gap открытого экрана
    QLineEdit* backgroundColorEdit_ = nullptr;  ///< background.color
    QLineEdit* backgroundImageEdit_ = nullptr;  ///< background.image
    QDoubleSpinBox* headerHeightBox_ = nullptr; ///< background.header.height; 0 — шапки нет
    QLineEdit* headerColorEdit_ = nullptr;      ///< background.header.color; пусто — прозрачная
    QDoubleSpinBox* footerHeightBox_ = nullptr; ///< background.footer.height; 0 — подвала нет
    QLineEdit* footerColorEdit_ = nullptr;      ///< background.footer.color; пусто — прозрачный
    QLineEdit* cellImageEdit_ = nullptr;        ///< cell.image
    QLineEdit* panelColorEdit_ = nullptr;       ///< panel.color
    QLineEdit* panelTextEdit_ = nullptr;        ///< panel.text
    QCheckBox* centerLastRowCheck_ = nullptr;   ///< centerLastRow
    QCheckBox* ownAreaCheck_ = nullptr;         ///< своя зона плиток у открытого экрана
    QList<QCheckBox*> serviceChecks_;           ///< служебные элементы экрана: заголовок, страница, часы, батарея
    QLabel* screenHint_ = nullptr;              ///< что из оформления экрана перекрыто картинками
    QList<QWidget*> screenWidgets_;             ///< поля экрана: доступны, пока на холсте экран
    // Вкладка «Пульт»: поля remote.json, общие для пульта.
    QJsonObject remote_;                        ///< описание открытой модели (readRemote)
    std::optional<QJsonObject> liveScreen_;     ///< открытый экран с вводимым, ещё не записанным цветом
    std::optional<QJsonObject> liveTiles_;      ///< блок tiles с вводимым, ещё не записанным цветом
    QLineEdit* nameEdit_ = nullptr;             ///< name
    QComboBox* driverBox_ = nullptr;            ///< connection.driver
    QComboBox* formatBox_ = nullptr;            ///< connection.format
    QLabel* connectionHint_ = nullptr;          ///< что программа умеет с этой парой
    QCheckBox* touchCheck_ = nullptr;           ///< capabilities.touch
    QLineEdit* tileColorEdit_ = nullptr;        ///< tiles.color
    QLineEdit* tileBorderEdit_ = nullptr;       ///< tiles.border
    QLabel* tilesHint_ = nullptr;               ///< на каких экранах цвет плиток не виден
    bool writing_ = false;                      ///< идёт запись remote.json (повторный editingFinished не пишет)
    QListWidget* zoneList_ = nullptr;
    QLineEdit* idEdit_ = nullptr;
    QComboBox* kindBox_ = nullptr;
    QDoubleSpinBox* xBox_ = nullptr;
    QDoubleSpinBox* yBox_ = nullptr;
    QDoubleSpinBox* wBox_ = nullptr;
    QDoubleSpinBox* hBox_ = nullptr;
    QDoubleSpinBox* radiusBox_ = nullptr;
    QComboBox* panelRoleBox_ = nullptr;         ///< role подложки ScreenPanel
    QFormLayout* propsForm_ = nullptr;          ///< форма свойств зоны (строки прячутся по виду зоны)
    QWidget* actionPanel_ = nullptr;
    QComboBox* actionBox_ = nullptr;
    QComboBox* commandBox_ = nullptr;
    QComboBox* targetBox_ = nullptr;
    QComboBox* buttonBox_ = nullptr;
    QLineEdit* zoneColorEdit_ = nullptr; ///< цвет заливки выбранной кнопки или плитки экрана (поле `color` ячейки)
    QLabel* selectionHint_ = nullptr;
    QLabel* checkLabel_ = nullptr;
    QListWidget* checkList_ = nullptr;
    ElidedLabel* statusLabel_ = nullptr;
    ElidedLabel* noteLabel_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    QAction* drawAction_ = nullptr;
    QAction* autoAction_ = nullptr;
    /// Запуск интерпретатора для авто-зон: программа и аргументы.
    struct AutoAttempt {
        QString program;
        QStringList arguments;
    };
    QList<AutoAttempt> autoAttempts_;           ///< интерпретаторы, которые ещё не пробовали
    QProcess* autoProcess_ = nullptr;           ///< идущее распознавание; nullptr — не идёт
    QAction* selectAllAction_ = nullptr;
    QAction* duplicateAction_ = nullptr;
    QAction* deleteAction_ = nullptr;
    QAction* copyAction_ = nullptr;
    QList<QAction*> alignActions_;
};

} // namespace harmony::app
