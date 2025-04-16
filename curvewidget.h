#ifndef CURVEWIDGET_H
#define CURVEWIDGET_H

#include <QWidget>
#include <QVector>
#include <QPointF>
#include <QMap>
#include <QColor>
#include <QUndoStack>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>

// Forward declaration for the command (adjust path if needed)
class SetCurveStateCommand;

class CurveWidget : public QWidget
{
    Q_OBJECT

public:
    // --- Enums and Structs ---

    /**
     * @brief Defines the possible channels for curves.
     */
    enum class ActiveChannel {
        RED,
        GREEN,
        BLUE
        // Optional: Add MASTER or LUMINANCE if desired later
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
    QUndoStack* undoStack();
    int getActiveNodeCount() const;

private:
    // --- Internal Structs for Interaction ---

    /**
     * @brief Identifies which part of a node is selected or targeted.
     */
    enum class SelectedPart {
        NONE,
        MAIN_POINT,
        HANDLE_IN,
        HANDLE_OUT
    };

    /**
     * @brief Stores information about the currently selected part.
     */
    struct SelectionInfo {
        SelectedPart part = SelectedPart::NONE;
        int nodeIndex = -1; // Index within the active channel's QVector<CurveNode>
    };

    /**
     * @brief Stores results when finding the closest point on a curve segment.
     */
    struct ClosestSegmentResult {
        int segmentIndex = -1; // Index of the first node in the segment
        qreal t = 0.0;         // Parameter along the Bézier segment (0-1)
        qreal distanceSq = std::numeric_limits<qreal>::max(); // Squared distance to the point
    };


public:
    // --- Constructor ---
    explicit CurveWidget(QWidget *parent = nullptr);

    // --- Public Methods ---
    qreal sampleCurveChannel(ActiveChannel channel, qreal x) const;
    void resetCurve(); // Resets the *active* channel curve
    void setDarkMode(bool dark);
    QMap<ActiveChannel, QVector<CurveNode>> getAllChannelNodes() const; // Get all data
    ActiveChannel getActiveChannel() const;

    // --- Public Slots ---
    void setActiveChannel(CurveWidget::ActiveChannel channel);
    void setNodeAlignment(int nodeIndex, HandleAlignment mode); // Operates on active channel

signals:
    // --- Signals ---
    void curveChanged(); // Emitted when any part of the active curve is modified
    void selectionChanged(int nodeIndex, CurveWidget::HandleAlignment alignmentMode); // Emitted when selection changes
    // Optional: void activeChannelChanged(CurveWidget::ActiveChannel newChannel);

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
    // --- Private Helper Functions ---
    QPointF mapToWidget(const QPointF& logicalPoint) const;
    QPointF mapFromWidget(const QPoint& widgetPoint) const;

    SelectionInfo findNearbyPart(const QPoint& widgetPos, qreal mainRadius = 10.0, qreal handleRadius = 8.0);
    ClosestSegmentResult findClosestSegment(const QPoint& widgetPos) const;

    void applyAlignmentSnap(int nodeIndex); // Operates on active channel node
    void sortActiveNodes(); // Sorts nodes of the active channel by X

    QVector<CurveNode>& getActiveNodes(); // Non-const access to active channel nodes
    const QVector<CurveNode>& getActiveNodes() const; // Const access

    // --- Member Variables ---
    QMap<ActiveChannel, QVector<CurveNode>> m_channelNodes; // Stores nodes for R, G, B
    ActiveChannel m_activeChannel;                       // Currently selected channel for editing

    QUndoStack m_undoStack;                              // Handles undo/redo operations
    // Stores the *entire* state map before an action for undo purposes
    QMap<ActiveChannel, QVector<CurveNode>> m_stateBeforeAction;


    bool m_dragging;                                     // True if mouse is dragging a point/handle
    SelectionInfo m_selection;                           // Info about the selected part

    qreal m_mainPointRadius;                             // Visual size of main points
    qreal m_handleRadius;                                // Visual size of handles

    bool m_isDarkMode;                                   // Flag for drawing colors

    // Friend declaration for the command class to call restoreAllChannelNodes
    friend class SetCurveStateCommand; // Make sure command class name matches
};

#endif // CURVEWIDGET_H
