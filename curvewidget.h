// curvewidget.h

#ifndef CURVEWIDGET_H
#define CURVEWIDGET_H

#include <QWidget>
#include <QVector>
#include <QPointF>
#include <QMap>
#include <QColor>
#include <QUndoStack> // Include QUndoStack
#include <QSet>      // Include QSet for multi-selection
#include <QRect>     // Include QRect for box selection

// Forward declarations for events are usually not needed if included via QWidget,
// but include specific ones if needed directly (e.g., if using specific event members)
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>


// Forward declaration for the command class
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
    Q_ENUM(ActiveChannel) // Make enum known to Qt's meta-object system

    /**
     * @brief Defines how control handles behave relative to each other.
     */
    enum class HandleAlignment {
        Free,    // Handles move independently.
        Aligned, // Handles stay collinear but can have different lengths.
        Mirrored // Handles stay collinear and equidistant from the main point.
    };
    Q_ENUM(HandleAlignment)

    /**
     * @brief Represents a single node on the Bézier curve.
     */
    struct CurveNode {
        QPointF mainPoint; // The point the curve passes through (anchor)
        QPointF handleIn;  // Control point influencing the incoming segment
        QPointF handleOut; // Control point influencing the outgoing segment
        HandleAlignment alignment; // How handles are linked

        CurveNode(QPointF p = QPointF());
        bool operator==(const CurveNode& other) const; // Needed for state comparison
        bool operator!=(const CurveNode& other) const { return !(*this == other); }
    };

public:
    // --- Constructor ---
    explicit CurveWidget(QWidget *parent = nullptr);

    // --- Public Methods ---

    // Sampling
    qreal sampleCurveChannel(ActiveChannel channel, qreal x) const;

    // State Control
    void resetCurve(); // Resets the *active* channel curve

    // Appearance
    void setDarkMode(bool dark);

    // Getters
    QMap<ActiveChannel, QVector<CurveNode>> getAllChannelNodes() const; // Get all data
    ActiveChannel getActiveChannel() const;
    QUndoStack* undoStack(); // Get the undo stack
    int getActiveNodeCount() const; // Get node count for active channel
    QSet<int> getSelectedIndices() const; // Get set of selected main point indices
    HandleAlignment getAlignment(int nodeIndex) const; // Get alignment for a specific node index

public slots:
    // --- Public Slots ---
    void setActiveChannel(ActiveChannel channel);
    void setNodeAlignment(int nodeIndex, HandleAlignment mode); // Operates on active channel (if single selection)
    void setDrawInactiveChannels(bool draw); // Toggle background curve drawing
    void setHandlesClamping(bool clamp);

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
    // This method MUST be called by the SetCurveStateCommand's undo/redo
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
        int nodeIndex = -1; // Index within the active channel's QVector<CurveNode>
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

    void applyAlignmentSnap(int nodeIndex, SelectedPart movedHandlePart); // Operates on active channel node
    void sortActiveNodes(); // Sorts nodes of the active channel by X

    QVector<CurveNode>& getActiveNodes(); // Non-const access to active channel nodes
    const QVector<CurveNode>& getActiveNodes() const; // Const access
    void clampHandlePosition(QPointF& handlePos);

    // --- Private Member Variables ---

    // Curve Data
    QMap<ActiveChannel, QVector<CurveNode>> m_channelNodes; // Stores nodes for R, G, B
    ActiveChannel m_activeChannel;                       // Currently selected channel for editing

    // Undo/Redo State
    QUndoStack m_undoStack;                              // Handles undo/redo operations
    QMap<ActiveChannel, QVector<CurveNode>> m_stateBeforeAction; // Stores state map before an action

    // Interaction & Selection State
    bool m_dragging;                  // True if dragging a point/handle
    QSet<int> m_selectedNodeIndices;  // Indices of selected main points (multi-select)
    SelectionInfo m_currentDrag;      // Info about the specific part being actively dragged (point OR handle)

    bool m_isBoxSelecting;            // True if dragging a selection rectangle
    QPoint m_boxSelectionStartPoint;  // Start point of box selection (widget coords)
    QRect m_boxSelectionRect;         // Current box rectangle (widget coords)

    // Appearance State
    qreal m_mainPointRadius;          // Visual size of main points
    qreal m_handleRadius;             // Visual size of handles
    bool m_isDarkMode;                // Flag for drawing colors
    bool m_drawInactiveChannels;      // Flag to control drawing inactive curves
    bool m_clampHandles;


    // --- Friend Declaration ---
    // Allow the command class to call the protected restore function
    friend class SetCurveStateCommand;
};

#endif // CURVEWIDGET_H
