#ifndef CURVEWIDGET_H
#define CURVEWIDGET_H

#include <QWidget>
#include <QVector>
#include <QPointF>
#include <QKeyEvent>    // For key events
#include <QUndoStack>   // For Undo/Redo stack
#include <QMouseEvent>  // Added for clarity
#include <QPaintEvent>  // Added for clarity

// Forward Declarations
class SetCurveStateCommand; // For friend declaration

/**
 * @brief A widget for editing a single-channel cubic Bézier curve.
 *
 * Provides functionality for adding, deleting, and moving nodes and their handles.
 * Supports different handle alignment modes (Free, Aligned, Mirrored).
 * Integrates with QUndoStack for undo/redo capabilities.
 * Adapts drawing based on a dark mode flag.
 */
class CurveWidget : public QWidget
{
    Q_OBJECT // Essential for signals/slots and meta-object features

public:
    // --- Public Enums ---
    /** @brief Defines how handles behave relative to each other around a node. */
    enum class HandleAlignment {
        Free,     ///< Handles move independently.
        Aligned,  ///< Handles stay collinear through the main point. Lengths independent.
        Mirrored  ///< Handles stay collinear and equidistant from the main point.
    };
    Q_ENUM(HandleAlignment) // Make enum known to Qt's meta-object system

    /** @brief (Retained from RGB) Defines curve channels - currently unused but kept for potential future expansion. */
    enum class ActiveChannel { R, G, B };
    Q_ENUM(ActiveChannel)

    // --- Public Nested Struct ---
    /** @brief Represents a node on the Bézier curve, including its handles and alignment mode. */
    struct CurveNode {
        QPointF mainPoint;      ///< Position of the node on the curve (logical 0-1 coords).
        QPointF handleIn;       ///< Control handle for the curve segment entering this node (logical 0-1 coords).
        QPointF handleOut;      ///< Control handle for the curve segment leaving this node (logical 0-1 coords).
        HandleAlignment alignment = HandleAlignment::Aligned; ///< How handles behave.

        CurveNode(QPointF p = QPointF(0,0)); // Constructor declaration

        // Equality operator needed for Undo state comparison
        bool operator==(const CurveNode& other) const;
        bool operator!=(const CurveNode& other) const { return !(*this == other); }
    };

    // --- Public Methods ---
    explicit CurveWidget(QWidget *parent = nullptr);

    /** @brief Gets a copy of the current curve nodes. */
    QVector<CurveNode> getNodes() const;

    /**
     * @brief Samples the curve's Y value at a given X value using cached approximation.
     * @param x - X-coordinate (0.0 to 1.0).
     * @return Approximate Y-coordinate (0.0 to 1.0).
     */
    qreal sampleCurve(qreal x) const;

    /**
     * @brief (Retained from RGB) Samples a specific channel (currently just calls internal sampler on m_nodes).
     * @param channel - The channel to sample (ignored in this version).
     * @param x - X-coordinate (0.0 to 1.0).
     * @return Approximate Y-coordinate (0.0 to 1.0).
     */
    qreal sampleCurveChannel(ActiveChannel channel, qreal x) const;

    /** @brief Resets the curve to its default state (undoable). */
    void resetCurve();

    /** @brief Returns a pointer to the internal QUndoStack for MainWindow integration. */
    QUndoStack* undoStack() { return &m_undoStack; }

    /** @brief Sets the dark mode flag to adjust drawing colors. */
    void setDarkMode(bool dark);

    // --- Public Slots ---
public slots:
    /**
     * @brief Sets the handle alignment mode for the node at the given index (undoable).
     * @param nodeIndex - Index of the node to modify.
     * @param mode - The new HandleAlignment mode.
     */
    void setNodeAlignment(int nodeIndex, CurveWidget::HandleAlignment mode);

    /**
     * @brief (Retained from RGB) Sets the currently active channel (does nothing in single-channel version).
     * @param channel - The channel to activate.
     */
    void setActiveChannel(CurveWidget::ActiveChannel channel);

    // --- Signals ---
signals:
    /** @brief Emitted when the curve data changes (nodes moved, added, deleted, reset). */
    void curveChanged();

    /**
     * @brief Emitted when the selection changes (node/handle clicked or deselected).
     * @param nodeIndex - Index of the selected node (-1 if none).
     * @param currentAlignment - Alignment mode of the selected node.
     */
    void selectionChanged(int nodeIndex, CurveWidget::HandleAlignment currentAlignment);

    /** @brief (Retained from RGB) Emitted when the active channel changes (unused in single-channel version). */
    void activeChannelChanged(CurveWidget::ActiveChannel newChannel);

    // --- Friend declaration for Undo Command ---
    friend class SetCurveStateCommand; // Allow command class to call restoreNodesInternal

    // --- Protected Event Handlers ---
protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

    // --- Protected Method for Undo/Redo ---
protected:
    /** @brief Restores the internal node state from a given vector (called by Undo command). */
    void restoreNodesInternal(const QVector<CurveNode>& nodes);

    // --- Private Helper Functions & Enums ---
private:
    // --- Coordinate Mapping ---
    QPointF mapToWidget(const QPointF& logicalPoint) const;
    QPointF mapFromWidget(const QPoint& widgetPoint) const;

    // --- Selection ---
    /** @brief Defines which part of a node representation is selected. */
    enum class SelectedPart { NONE, MAIN_POINT, HANDLE_IN, HANDLE_OUT };
    /** @brief Stores information about the current selection. */
    struct SelectionInfo {
        SelectedPart part = SelectedPart::NONE;
        int nodeIndex = -1;
    };
    /** @brief Finds the closest interactive part (main point or handle) to a widget position. */
    SelectionInfo findNearbyPart(const QPoint& widgetPos, qreal mainRadius = 10.0, qreal handleRadius = 8.0);

    // --- Curve Modification Helpers ---
    /** @brief Structure to return info about closest point on curve for adding nodes. */
    struct ClosestSegmentResult {
        int segmentIndex = -1; qreal t = 0.0; qreal distanceSq = std::numeric_limits<qreal>::max();
    };
    /** @brief Finds the closest point on the curve to a widget position. */
    ClosestSegmentResult findClosestSegment(const QPoint& widgetPos) const;
    /** @brief Sorts m_nodes by X-coordinate, keeping endpoints fixed. */
    void sortNodes();
    /** @brief Adjusts handles based on alignment mode after a mode change or move. */
    void applyAlignmentSnap(int nodeIndex);

    // --- Sampling & Cache ---
    /** @brief Internal sampling helper (Currently uses linear interpolation - see TODO). */
    qreal sampleCurveInternal(const QVector<CurveNode>& nodes, qreal x) const;
    /** @brief Updates the internal sample cache (m_curveSamples). Must be const for sampleCurve(). */
    void updateCurveSamples() const;
    /** @brief Evaluates the Bezier curve equation. */
    QPointF evaluateBezier(const QPointF& p0, const QPointF& p1, const QPointF& p2, const QPointF& p3, qreal t) const;

    // --- Private Member Variables ---
    QVector<CurveNode> m_nodes;            ///< Stores all nodes defining the curve.
    SelectionInfo m_selection;             ///< Current selection state.
    bool m_dragging = false;               ///< True if mouse is dragging a selection.
    bool m_isDarkMode = false;             ///< Current theme mode for drawing.

    // Visual parameters
    const qreal m_mainPointRadius;         ///< Visual radius of main points.
    const qreal m_handleRadius;            ///< Visual radius of handles.

    // Sampling cache
    const int m_numSamples;                ///< Number of samples for curve approximation cache.
    mutable QVector<QPointF> m_curveSamples; ///< Cached points for fast sampling.
    mutable bool m_samplesDirty = true;    ///< True if cache needs recalculating.

    // Undo/Redo stack
    QUndoStack m_undoStack;                ///< Manages undo/redo commands.
    QVector<CurveNode> m_stateBeforeAction; ///< Stores node state before an undoable action starts.

};

#endif // CURVEWIDGET_H
