#ifndef CURVEWIDGET_H
#define CURVEWIDGET_H

// Qt Includes
#include <QWidget>
#include <QVector>
#include <QPointF>
#include <QMap>
#include <QColor>
#include <QUndoStack>
#include <QSet>
#include <QRect>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>

// Standard Library Includes
#include <limits> // Required for ClosestSegmentResult initialization

// Forward Declarations
class SetCurveStateCommand;

/**
 * @brief A widget for interactively editing Bézier curves,
 * supporting multiple channels (R, G, B) and multiple point selection.
 */
class CurveWidget : public QWidget
{
    Q_OBJECT

public:
    // --- Public Enums and Structs ---

    /**
     * @brief Defines the possible channels for curves.
     */
    enum class ActiveChannel {
        RED,
        GREEN,
        BLUE
    };
    Q_ENUM(ActiveChannel)

    /**
     * @brief Defines how control handles behave relative to each other.
     */
    enum class HandleAlignment {
        Free,
        Aligned,
        Mirrored
    };
    Q_ENUM(HandleAlignment)

    /**
     * @brief Represents a single node on the Bézier curve.
     */
    struct CurveNode {
        QPointF mainPoint;
        QPointF handleIn;
        QPointF handleOut;
        HandleAlignment alignment;

        CurveNode(QPointF p = QPointF());
        bool operator==(const CurveNode& other) const;
        bool operator!=(const CurveNode& other) const { return !(*this == other); }
    };

    // --- Constructor ---
    explicit CurveWidget(QWidget *parent = nullptr);

    // --- Public Methods ---
    qreal sampleCurveChannel(ActiveChannel channel, qreal x) const;
    void resetCurve();
    void setDarkMode(bool dark);
    QMap<ActiveChannel, QVector<CurveNode>> getAllChannelNodes() const;
    ActiveChannel getActiveChannel() const;
    QUndoStack* undoStack();
    int getActiveNodeCount() const;
    QSet<int> getSelectedIndices() const;
    HandleAlignment getAlignment(int nodeIndex) const;

public slots:
    // --- Public Slots ---
    void setActiveChannel(ActiveChannel channel);
    void setNodeAlignment(int nodeIndex, HandleAlignment mode);
    void setDrawInactiveChannels(bool draw);
    void setHandlesClamping(bool clamp);
    void setAllChannelNodes(const QMap<ActiveChannel, QVector<CurveNode>>& allNodes);

signals:
    // --- Signals ---
    /**
     * @brief Emitted when the active curve's shape or data is modified.
     */
    void curveChanged();

    /**
     * @brief Emitted when the set of selected main points changes,
     * or when a handle is selected/deselected.
     */
    void selectionChanged();

protected:
    // --- Event Handlers ---
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

    // --- Protected Methods for Undo/Redo ---
    void restoreAllChannelNodes(const QMap<ActiveChannel, QVector<CurveNode>>& allNodes);

private:
    // --- Private Helper Enums/Structs ---

    /**
     * @brief Identifies which part of a node is selected or targeted for dragging.
     */
    enum class SelectedPart {
        NONE,
        MAIN_POINT,
        HANDLE_IN,
        HANDLE_OUT
    };

    /**
     * @brief Stores information about the currently dragged part (if any).
     */
    struct SelectionInfo {
        SelectedPart part = SelectedPart::NONE;
        int nodeIndex = -1;
    };

    /**
     * @brief Stores results when finding the closest point on a curve segment.
     */
    struct ClosestSegmentResult {
        int segmentIndex = -1;
        qreal t = 0.0;
        qreal distanceSq = std::numeric_limits<qreal>::max();
    };

    // --- Private Helper Functions ---
    QPointF mapToWidget(const QPointF& logicalPoint) const;
    QPointF mapFromWidget(const QPoint& widgetPoint) const;
    SelectionInfo findNearbyPart(const QPoint& widgetPos, qreal mainRadius = 10.0, qreal handleRadius = 8.0);
    ClosestSegmentResult findClosestSegment(const QPoint& widgetPos) const;
    void applyAlignmentSnap(int nodeIndex, SelectedPart movedHandlePart);
    void sortActiveNodes();
    QVector<CurveNode>& getActiveNodes();
    const QVector<CurveNode>& getActiveNodes() const;
    void clampHandlePosition(QPointF& handlePos);

    // --- Private Member Variables ---
    QMap<ActiveChannel, QVector<CurveNode>> m_channelNodes;
    ActiveChannel m_activeChannel;
    QUndoStack m_undoStack;
    QMap<ActiveChannel, QVector<CurveNode>> m_stateBeforeAction;
    bool m_dragging;
    QSet<int> m_selectedNodeIndices;
    SelectionInfo m_currentDrag;
    bool m_isBoxSelecting;
    QPoint m_boxSelectionStartPoint;
    QRect m_boxSelectionRect;
    qreal m_mainPointRadius;
    qreal m_handleRadius;
    bool m_isDarkMode;
    bool m_drawInactiveChannels;
    bool m_clampHandles;

    // --- Friend Declaration ---
    friend class SetCurveStateCommand;
};

#endif
