#include "curvewidget.h"
#include "setcurvestatecommand.h" // Include the command header (ensure it's for single state)

#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QPainterPath>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPalette>        // Included for paintEvent palette access
#include <QtMath>          // For qSqrt
#include <QtCore/qnumeric.h> // For qFuzzyCompare
#include <algorithm>       // For std::sort, std::max, std::min
#include <cmath>           // For std::max, std::min
#include <limits>          // For std::numeric_limits
#include <QDebug>          // For logging

// --- Anonymous Namespace for Local File Helpers ---
namespace {

/**
 * @brief Linearly interpolates between two points.
 * @param a - Start point.
 * @param b - End point.
 * @param t - Interpolation factor (0.0 to 1.0).
 * @return Interpolated point.
 */
inline QPointF lerp(const QPointF& a, const QPointF& b, qreal t) {
    return a * (1.0 - t) + b * t;
}

/**
 * @brief Structure to hold the results of De Casteljau subdivision.
 */
struct SubdivisionResult {
    QPointF pointOnCurve; // P_new (main point for the new node)
    QPointF handle1_Seg1; // P01   (becomes handleOut for node i)
    QPointF handle2_Seg1; // P012  (becomes handleIn for new node)
    QPointF handle1_Seg2; // P123  (becomes handleOut for new node)
    QPointF handle2_Seg2; // P23   (becomes handleIn for node i+1)
};

/**
 * @brief Subdivides a cubic Bézier segment at parameter t using De Casteljau algorithm.
 * @param p0 - Start point of the segment.
 * @param p1 - First control point (outgoing handle of start).
 * @param p2 - Second control point (incoming handle of end).
 * @param p3 - End point of the segment.
 * @param t - Parameter value (0.0 to 1.0) at which to subdivide.
 * @return SubdivisionResult containing the new point and handles.
 */
SubdivisionResult subdivideBezier(const QPointF& p0, const QPointF& p1, const QPointF& p2, const QPointF& p3, qreal t) {
    SubdivisionResult result;
    QPointF p01 = lerp(p0, p1, t);
    QPointF p12 = lerp(p1, p2, t);
    QPointF p23 = lerp(p2, p3, t);
    result.handle1_Seg1 = p01;
    result.handle2_Seg2 = p23;
    result.handle2_Seg1 = lerp(p01, p12, t);
    result.handle1_Seg2 = lerp(p12, p23, t);
    result.pointOnCurve = lerp(result.handle2_Seg1, result.handle1_Seg2, t);
    return result;
}

/**
 * @brief Evaluates a cubic Bézier segment at parameter t.
 * @param p0 - Start point.
 * @param p1 - Control point 1.
 * @param p2 - Control point 2.
 * @param p3 - End point.
 * @param t - Parameter (0.0 to 1.0).
 * @return Point on the curve at parameter t.
 */
QPointF evaluateBezier(const QPointF& p0, const QPointF& p1, const QPointF& p2, const QPointF& p3, qreal t) {
    qreal mt = 1.0 - t;
    qreal mt2 = mt * mt;
    qreal t2 = t * t;
    return p0 * mt * mt2 + p1 * 3.0 * mt2 * t + p2 * 3.0 * mt * t2 + p3 * t * t2;
}

} // End anonymous namespace


// --- CurveNode Implementation ---

/**
 * @brief Constructor for CurveNode. Initializes handles coincident with the main point.
 * @param p - Initial position for the main point.
 */
CurveWidget::CurveNode::CurveNode(QPointF p)
    : mainPoint(p),
    handleIn(p),
    handleOut(p),
    alignment(HandleAlignment::Aligned) // Default alignment mode
{}

/**
 * @brief Equality operator for comparing CurveNode states (used by Undo).
 * @param other - The CurveNode to compare against.
 * @return True if all members are equal, false otherwise.
 */
bool CurveWidget::CurveNode::operator==(const CurveNode& other) const {
    return mainPoint == other.mainPoint &&
           handleIn == other.handleIn &&
           handleOut == other.handleOut &&
           alignment == other.alignment;
}


// --- CurveWidget Constructor ---

/**
 * @brief Constructor for CurveWidget.
 * @param parent - Parent widget.
 */
CurveWidget::CurveWidget(QWidget *parent)
    : QWidget(parent),
    m_dragging(false),
    m_isDarkMode(false), // Default to light mode unless set otherwise
    m_selection({SelectedPart::NONE, -1}), // Initialize selection
    m_samplesDirty(true),
    m_mainPointRadius(5.0), // Default radii - consider making these configurable
    m_handleRadius(4.0),
    m_numSamples(256)
{
    // Initialize with a default straight line curve
    CurveNode node0(QPointF(0.0, 0.0));
    CurveNode node1(QPointF(1.0, 1.0));
    node0.handleOut = QPointF(1.0/3.0, 1.0/3.0); // Initial straight line handles
    node1.handleIn  = QPointF(2.0/3.0, 2.0/3.0);
    m_nodes << node0 << node1; // Use the single m_nodes vector

    setMinimumSize(200, 200);
    setFocusPolicy(Qt::ClickFocus); // Allow widget to receive keyboard focus on click
    setAutoFillBackground(true);   // Required for QSS/Palette background to work reliably
    // NOTE: Theme/Palette should be controlled by MainWindow/QApplication
}


// --- Public Methods ---

/**
 * @brief Gets a copy of the current curve nodes.
 * @return QVector containing the curve nodes.
 */
QVector<CurveWidget::CurveNode> CurveWidget::getNodes() const {
    return m_nodes;
}

/**
 * @brief Samples the curve's Y value at a given X value using cached approximation.
 * @param x - The X-coordinate (0.0 to 1.0) to sample.
 * @return Approximate Y-coordinate (0.0 to 1.0).
 */
qreal CurveWidget::sampleCurve(qreal x) const {
    if (m_samplesDirty) {
        updateCurveSamples(); // Update cache if needed
    }
    if (m_curveSamples.isEmpty()) return 0.0;

    // Clamp x to the valid range of *cached* samples X values
    // Note: Cache X values might not perfectly align with node X values due to Bezier nature
    x = std::max((m_curveSamples.isEmpty() ? 0.0 : m_curveSamples.first().x()),
                 std::min((m_curveSamples.isEmpty() ? 1.0 : m_curveSamples.last().x()), x));


    // Find segment in sample table using lower_bound
    auto it = std::lower_bound(m_curveSamples.constBegin(), m_curveSamples.constEnd(), x,
                               [](const QPointF& point, qreal xVal) {
                                   return point.x() < xVal;
                               });

    if (it == m_curveSamples.constBegin()) return m_curveSamples.first().y();
    // lower_bound returns iterator to first element NOT LESS than x.
    // If x is >= last element, it returns end().
    if (it == m_curveSamples.constEnd()) return m_curveSamples.last().y();

    // Linear interpolation between the two cache samples surrounding x
    const QPointF& p2 = *it;     // The sample at or after x
    const QPointF& p1 = *(it - 1); // The sample before x

    // Avoid division by zero if cached points have same x
    if (qFuzzyCompare(p1.x(), p2.x())) {
        return p1.y();
    }

    qreal t = (x - p1.x()) / (p2.x() - p1.x());
    // Clamp t just in case due to floating point inaccuracies near ends
    t = std::max(0.0, std::min(1.0, t));

    return lerp(p1, p2, t).y(); // Use global helper
}

/**
 * @brief (Retained from RGB) Samples a specific channel (now just calls internal sampler on m_nodes).
 * @param channel - The channel to sample (ignored in this version).
 * @param x - X-coordinate (0.0 to 1.0).
 * @return Approximate Y-coordinate (0.0 to 1.0).
 */
qreal CurveWidget::sampleCurveChannel(ActiveChannel channel, qreal x) const {
    Q_UNUSED(channel); // Mark as unused in this single-channel version
    // Uses the internal helper which currently does linear interpolation.
    return sampleCurveInternal(m_nodes, x);
}

/**
 * @brief Resets the curve to its default state (straight line 0,0 to 1,1).
 * This action is undoable.
 */
void CurveWidget::resetCurve() {
    // Capture state BEFORE reset for Undo
    m_stateBeforeAction = m_nodes; // Capture single vector state

    m_nodes.clear(); // Clear the single node vector

    // Re-initialize with default nodes
    CurveNode node0(QPointF(0.0, 0.0));
    CurveNode node1(QPointF(1.0, 1.0));
    node0.handleOut = QPointF(1.0/3.0, 1.0/3.0);
    node1.handleIn  = QPointF(2.0/3.0, 2.0/3.0);
    m_nodes << node0 << node1;

    // Push command only if state actually changed
    if (m_stateBeforeAction != m_nodes) {
        // Push command with single old/new state vectors
        m_undoStack.push(new SetCurveStateCommand(this, m_stateBeforeAction, m_nodes, "Reset Curve"));
    } else {
        m_stateBeforeAction.clear();
    }

    // Update internal state and UI
    m_samplesDirty = true;
    m_selection = {SelectedPart::NONE, -1};
    m_dragging = false;
    update();
    emit curveChanged();
    emit selectionChanged(-1, HandleAlignment::Free); // Emit deselection
}

/**
 * @brief Sets the dark mode flag to adjust drawing colors.
 * @param dark - True to enable dark mode colors, false for light mode.
 */
void CurveWidget::setDarkMode(bool dark) {
    if (m_isDarkMode != dark) {
        m_isDarkMode = dark;
        update(); // Trigger repaint to use new colors
    }
}

// --- Public Slots ---

/**
 * @brief Sets the handle alignment mode for a specific node. Undoable.
 * @param nodeIndex - Index of the node to modify.
 * @param mode - The new HandleAlignment mode.
 */
void CurveWidget::setNodeAlignment(int nodeIndex, CurveWidget::HandleAlignment mode) {
    // Check bounds using the single m_nodes vector
    if (nodeIndex < 0 || nodeIndex >= m_nodes.size()) {
        qWarning() << "setNodeAlignment: Invalid index" << nodeIndex;
        return;
    }

    CurveNode& node = m_nodes[nodeIndex]; // Get reference from m_nodes
    if (node.alignment != mode) {
        // Action will happen, prepare for Undo
        m_stateBeforeAction = m_nodes; // Capture state BEFORE change

        node.alignment = mode; // Apply the new mode
        applyAlignmentSnap(nodeIndex); // Apply snap logic AFTER setting mode

        // Push command only if state actually changed
        if (m_stateBeforeAction != m_nodes) {
            // Push command with single old/new state vectors
            m_undoStack.push(new SetCurveStateCommand(this, m_stateBeforeAction, m_nodes, "Change Alignment"));
        } else {
            m_stateBeforeAction.clear();
        }

        // Update state and UI
        m_samplesDirty = true;
        update();
        emit curveChanged();
        emit selectionChanged(nodeIndex, mode); // Update button states in MainWindow
    }
}

/**
 * @brief (Retained from RGB) Sets the currently active channel (does nothing now).
 * @param channel - The channel to activate.
 */
void CurveWidget::setActiveChannel(CurveWidget::ActiveChannel channel) {
    Q_UNUSED(channel);
    // In single channel mode, this function has no effect.
    // qDebug() << "setActiveChannel called, but widget is in single-channel mode.";
}


// --- Protected Event Handlers ---

/**
 * @brief Handles painting the widget. Draws grid, curve, nodes, and handles.
 * Uses m_isDarkMode to select appropriate colors.
 * @param event - Paint event details.
 */
void CurveWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Define Colors based on Dark/Light Mode
    QColor gridColor, borderColor, handleLineColor, handleColor, mainPointColor, outlineColor;
    QColor curveColor;
    QColor selectionColor = Qt::yellow;

    if (m_isDarkMode) {
        gridColor = QColor(80, 80, 80); borderColor = QColor(90, 90, 90);
        handleLineColor = QColor(100, 100, 100); handleColor = QColor(0, 190, 190);
        mainPointColor = QColor(230, 230, 230); outlineColor = Qt::black;
        curveColor = QColor(240, 240, 240);
    } else { // Light Mode
        gridColor = QColor(210, 210, 210); borderColor = QColor(180, 180, 180);
        handleLineColor = QColor(140, 140, 140); handleColor = QColor(0, 100, 100);
        mainPointColor = QColor(10, 10, 10); outlineColor = Qt::darkGray;
        curveColor = QColor(10, 10, 10);
    }

    // Draw Grid & Border
    painter.setPen(QPen(gridColor, 0.5));
    int numGridLines = 10;
    for (int i = 1; i < numGridLines; ++i) {
        qreal ratio = static_cast<qreal>(i) / numGridLines;
        painter.drawLine(mapToWidget(QPointF(ratio, 1.0)), mapToWidget(QPointF(ratio, 0.0)));
        painter.drawLine(mapToWidget(QPointF(0.0, ratio)), mapToWidget(QPointF(1.0, ratio)));
    }
    painter.setPen(QPen(borderColor, 1));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    // Draw Curve
    if (m_nodes.size() < 2) return;
    QPainterPath curvePath;
    curvePath.moveTo(mapToWidget(m_nodes[0].mainPoint));
    for (int i = 0; i < m_nodes.size() - 1; ++i) {
        curvePath.cubicTo(mapToWidget(m_nodes[i].handleOut),
                          mapToWidget(m_nodes[i+1].handleIn),
                          mapToWidget(m_nodes[i+1].mainPoint));
    }
    painter.setPen(QPen(curveColor, 2));
    painter.drawPath(curvePath);

    // Draw Nodes and Handles
    for (int i = 0; i < m_nodes.size(); ++i) {
        const CurveNode& node = m_nodes[i];
        QPointF mainWidgetPos = mapToWidget(node.mainPoint);
        // Draw Handle Lines
        painter.setPen(QPen(handleLineColor, 1));
        if (i > 0) painter.drawLine(mainWidgetPos, mapToWidget(node.handleIn));
        if (i < m_nodes.size() - 1) painter.drawLine(mainWidgetPos, mapToWidget(node.handleOut));
        // Draw Handles
        painter.setPen(QPen(outlineColor, 0.5));
        if (i > 0) { // Handle In
            bool isSelected = (m_selection.part == SelectedPart::HANDLE_IN && m_selection.nodeIndex == i);
            painter.setBrush(isSelected ? selectionColor : handleColor);
            painter.drawEllipse(mapToWidget(node.handleIn), m_handleRadius, m_handleRadius);
        }
        if (i < m_nodes.size() - 1) { // Handle Out
            bool isSelected = (m_selection.part == SelectedPart::HANDLE_OUT && m_selection.nodeIndex == i);
            painter.setBrush(isSelected ? selectionColor : handleColor);
            painter.drawEllipse(mapToWidget(node.handleOut), m_handleRadius, m_handleRadius);
        }
        // Draw Main Point
        painter.setPen(QPen(outlineColor, 1));
        bool isMainSelected = (m_selection.part == SelectedPart::MAIN_POINT && m_selection.nodeIndex == i);
        painter.setBrush(isMainSelected ? selectionColor : mainPointColor);
        painter.drawEllipse(mainWidgetPos, m_mainPointRadius, m_mainPointRadius);
    }
}

/**
 * @brief Handles mouse button press events. Selects points/handles,
 * initiates dragging, adds nodes, or deletes nodes. Manages state capture for Undo.
 * @param event - Mouse event details.
 */
void CurveWidget::mousePressEvent(QMouseEvent *event)
{
    SelectionInfo oldSelection = m_selection;
    m_selection = findNearbyPart(event->pos()); // Uses m_nodes internally
    m_dragging = (m_selection.part != SelectedPart::NONE);
    m_stateBeforeAction.clear(); // Clear previous stored state

    // Emit selection change signal if necessary
    if (m_selection.nodeIndex != oldSelection.nodeIndex || m_selection.part != oldSelection.part) {
        if (m_selection.part != SelectedPart::NONE) {
            if (m_selection.nodeIndex >= 0 && m_selection.nodeIndex < m_nodes.size()) { // Check m_nodes size
                emit selectionChanged(m_selection.nodeIndex, m_nodes[m_selection.nodeIndex].alignment);
            } else { m_selection = {SelectedPart::NONE, -1}; m_dragging = false; emit selectionChanged(-1, HandleAlignment::Free); }
        } else { emit selectionChanged(-1, HandleAlignment::Free); }
    }

    // Handle Left Button
    if (event->button() == Qt::LeftButton) {
        if (m_dragging) { // Started drag on existing part
            m_stateBeforeAction = m_nodes; // Store state BEFORE drag
        } else { // Add New Node
            m_stateBeforeAction = m_nodes; // Store state BEFORE add attempt
            ClosestSegmentResult hit = findClosestSegment(event->pos()); // Uses m_nodes
            const qreal t_tolerance = 0.001;
            if (hit.segmentIndex != -1 && hit.t > t_tolerance && hit.t < (1.0 - t_tolerance)) {
                int i = hit.segmentIndex; qreal t = hit.t;
                if (i < 0 || i >= m_nodes.size() - 1) { m_stateBeforeAction.clear(); return; } // Bounds check
                const QPointF p0 = m_nodes[i].mainPoint; const QPointF p1 = m_nodes[i].handleOut;
                const QPointF p2 = m_nodes[i+1].handleIn; const QPointF p3 = m_nodes[i+1].mainPoint;
                SubdivisionResult split = subdivideBezier(p0, p1, p2, p3, t); // Use helper
                CurveNode newNode(split.pointOnCurve);
                newNode.handleIn = split.handle2_Seg1; newNode.handleOut = split.handle1_Seg2;
                m_nodes[i].handleOut = split.handle1_Seg1; m_nodes[i+1].handleIn = split.handle2_Seg2;
                m_nodes.insert(i + 1, newNode); // Modify m_nodes directly

                if (m_stateBeforeAction != m_nodes) { // Push Undo command if state changed
                    m_undoStack.push(new SetCurveStateCommand(this, m_stateBeforeAction, m_nodes, "Add Node"));
                }
                m_samplesDirty = true; m_selection = {SelectedPart::MAIN_POINT, i + 1}; m_dragging = true; // Don't start drag
                update(); emit curveChanged(); emit selectionChanged(m_selection.nodeIndex, newNode.alignment);
            } else { m_stateBeforeAction.clear(); } // No add action occurred
        }
        // Handle Right Button (Delete)
    } else if (event->button() == Qt::RightButton) {
        if (m_selection.part == SelectedPart::MAIN_POINT && m_selection.nodeIndex > 0 && m_selection.nodeIndex < m_nodes.size() - 1) { // Use m_nodes size
            m_stateBeforeAction = m_nodes; // Store state BEFORE delete
            int deletedIndex = m_selection.nodeIndex;
            m_nodes.remove(deletedIndex); // Modify m_nodes directly

            if (m_stateBeforeAction != m_nodes) { // Push Undo command
                m_undoStack.push(new SetCurveStateCommand(this, m_stateBeforeAction, m_nodes, "Delete Node"));
            }
            m_selection = {SelectedPart::NONE, -1}; m_dragging = false; m_samplesDirty = true;
            update(); emit curveChanged(); emit selectionChanged(-1, HandleAlignment::Free);
        } else { m_stateBeforeAction.clear(); } // No delete action occurred
    } else { // Other mouse button
        m_stateBeforeAction.clear();
    }
}

/**
 * @brief Handles mouse move events. Updates position of dragged point/handle
 * and applies alignment constraints. This action is part of an undoable sequence.
 * @param event - Mouse event details.
 */
void CurveWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging || m_selection.part == SelectedPart::NONE) return;
    if (m_selection.nodeIndex < 0 || m_selection.nodeIndex >= m_nodes.size()) return; // Check bounds

    QPointF logicalPos = mapFromWidget(event->pos());
    CurveNode& node = m_nodes[m_selection.nodeIndex]; // Operate on single m_nodes vector
    QPointF* pointToMove = nullptr;
    QPointF* oppositeHandle = nullptr;
    SelectedPart movedPart = m_selection.part;
    qreal newX = logicalPos.x(); qreal newY = logicalPos.y();
    newY = std::max(0.0, std::min(1.0, newY)); // Clamp Y

    // Determine pointToMove and oppositeHandle
    switch(movedPart) {
    case SelectedPart::MAIN_POINT:
        pointToMove = &node.mainPoint;
        { // X clamping for main point
            const qreal epsilon = 1e-6;
            if (m_selection.nodeIndex == 0) newX = 0.0;
            else if (m_selection.nodeIndex == m_nodes.size() - 1) newX = 1.0;
            else {
                qreal minX = m_nodes[m_selection.nodeIndex - 1].mainPoint.x() + epsilon;
                qreal maxX = m_nodes[m_selection.nodeIndex + 1].mainPoint.x() - epsilon;
                if (minX > maxX) minX = maxX = (minX + maxX) / 2.0;
                newX = std::max(minX, std::min(maxX, newX));
            }
            newX = std::max(0.0, std::min(1.0, newX));
        }
        break;
    case SelectedPart::HANDLE_IN:
        if (m_selection.nodeIndex == 0) return;
        pointToMove = &node.handleIn;
        if (m_selection.nodeIndex < m_nodes.size() - 1) oppositeHandle = &node.handleOut;
        newX = std::max(0.0, std::min(1.0, newX));
        break;
    case SelectedPart::HANDLE_OUT:
        if (m_selection.nodeIndex == m_nodes.size() - 1) return;
        pointToMove = &node.handleOut;
        if (m_selection.nodeIndex > 0) oppositeHandle = &node.handleIn;
        newX = std::max(0.0, std::min(1.0, newX));
        break;
    case SelectedPart::NONE: return;
    }

    QPointF oldPos = *pointToMove;
    pointToMove->setX(newX); pointToMove->setY(newY);
    QPointF delta = *pointToMove - oldPos;

    // Move related handles if main point moved
    if (movedPart == SelectedPart::MAIN_POINT) {
        if (node.handleIn != oldPos) node.handleIn += delta;
        if (node.handleOut != oldPos) node.handleOut += delta;
        // Clamp handles
        node.handleIn.setY(std::max(0.0, std::min(1.0, node.handleIn.y())));
        node.handleOut.setY(std::max(0.0, std::min(1.0, node.handleOut.y())));
        node.handleIn.setX(std::max(0.0, std::min(1.0, node.handleIn.x())));
        node.handleOut.setX(std::max(0.0, std::min(1.0, node.handleOut.x())));
    }
    // Apply alignment constraints if handle moved
    else if (oppositeHandle != nullptr && node.alignment != HandleAlignment::Free) {
        const QPointF& mainPt = node.mainPoint; QPointF vecMoved = *pointToMove - mainPt;
        qreal lenMovedSq = QPointF::dotProduct(vecMoved, vecMoved); QPointF newOppositePos;
        if (lenMovedSq < 1e-12) { newOppositePos = mainPt; }
        else {
            qreal lenMoved = qSqrt(lenMovedSq); QPointF normDirOpposite = -vecMoved / lenMoved;
            if (node.alignment == HandleAlignment::Aligned) {
                QPointF vecOppositeOld = *oppositeHandle - mainPt;
                qreal lenOppositeOld = qSqrt(QPointF::dotProduct(vecOppositeOld, vecOppositeOld));
                newOppositePos = mainPt + normDirOpposite * lenOppositeOld;
            } else { /* Mirrored */ newOppositePos = mainPt + normDirOpposite * lenMoved; }
        }
        oppositeHandle->setX(std::max(0.0, std::min(1.0, newOppositePos.x())));
        oppositeHandle->setY(std::max(0.0, std::min(1.0, newOppositePos.y())));
    }

    // Re-sort if a main point's X changed order
    if (movedPart == SelectedPart::MAIN_POINT) {
        bool needsReSort = (m_selection.nodeIndex > 0 && m_selection.nodeIndex < m_nodes.size() - 1);
        if (needsReSort) {
            QPointF mainPointToFind = node.mainPoint;
            sortNodes(); // Sort m_nodes directly
            // Re-find index in m_nodes
            m_selection.nodeIndex = -1;
            for(int i=0; i<m_nodes.size(); ++i){
                // Use fuzzy compare for robustness with floating point positions
                if(qFuzzyCompare(m_nodes[i].mainPoint.x(), mainPointToFind.x()) && qFuzzyCompare(m_nodes[i].mainPoint.y(), mainPointToFind.y())){
                    m_selection.nodeIndex = i; break;
                }
            }
            if (m_selection.nodeIndex == -1) { m_dragging = false; qWarning("Lost node after sort!"); } // Lost point
        }
    }
    m_samplesDirty = true; update(); emit curveChanged();
}

/**
 * @brief Handles mouse button release events. Completes dragging operations
 * and pushes the final state change to the undo stack.
 * @param event - Mouse event details.
 */
void CurveWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging && event->button() == Qt::LeftButton) {
        m_dragging = false; // End drag state

        // Check if state was actually captured (should be if m_dragging was true)
        // and if the state actually changed during the drag.
        if (m_stateBeforeAction.size() > 0 && m_stateBeforeAction != m_nodes)
        {
            // Push command with single vector state
            m_undoStack.push(new SetCurveStateCommand(this, m_stateBeforeAction, m_nodes, "Modify Curve"));
            qDebug() << "Modify Curve undo command pushed via mouse release.";
        } else {
            // No state change or state wasn't captured correctly.
            qDebug() << "Mouse Release: State unchanged or not captured; no command pushed.";
        }
        m_stateBeforeAction.clear(); // Clear stored state, action sequence complete
    }
    QWidget::mouseReleaseEvent(event); // Call base implementation
}

/**
 * @brief Handles widget resize events. Just triggers a repaint.
 * @param event - Resize event details.
 */
void CurveWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    update(); // Repaint needed as mapping depends on size
}

/**
 * @brief Handles key press events. Used here to change handle alignment mode (F, A, M).
 * This action is undoable.
 * @param event - Key event details.
 */
void CurveWidget::keyPressEvent(QKeyEvent *event)
{
    bool selectionValid = m_selection.part != SelectedPart::NONE &&
                          m_selection.nodeIndex >= 0 &&
                          m_selection.nodeIndex < m_nodes.size(); // Use m_nodes size

    bool keyHandled = false; // Flag if F, A, or M was processed

    if (selectionValid) {
        CurveNode& node = m_nodes[m_selection.nodeIndex]; // Use m_nodes
        HandleAlignment originalMode = node.alignment;
        HandleAlignment newMode = originalMode;
        bool modeChangeKeyPressed = true;

        switch (event->key()) {
        case Qt::Key_F: newMode = HandleAlignment::Free; break;
        case Qt::Key_A: newMode = HandleAlignment::Aligned; break;
        case Qt::Key_M: newMode = HandleAlignment::Mirrored; break;
        default: modeChangeKeyPressed = false; break; // Not a mode change key
        }

        if (modeChangeKeyPressed && newMode != originalMode) {
            m_stateBeforeAction = m_nodes; // Capture state BEFORE
            node.alignment = newMode;      // Apply mode change
            applyAlignmentSnap(m_selection.nodeIndex); // Apply snap logic

            // Push command only if state actually changed after snap
            if (m_stateBeforeAction != m_nodes) {
                // Push command with single old/new state vectors
                m_undoStack.push(new SetCurveStateCommand(this, m_stateBeforeAction, m_nodes, "Change Alignment"));
            } else {
                m_stateBeforeAction.clear(); // Clear state if no effective change
            }

            // Update UI and signal listeners
            m_samplesDirty = true; update(); emit curveChanged();
            emit selectionChanged(m_selection.nodeIndex, newMode); // Update MainWindow
            keyHandled = true; // Mark key as handled
        }
    }

    // Final event handling
    if (keyHandled) {
        event->accept(); // Consume the event
    } else {
        QWidget::keyPressEvent(event); // Pass to base class for other keys
    }
}


// --- Protected Method for Undo/Redo ---

/**
 * @brief Restores the internal node state from a given vector (called by Undo command).
 * @param nodes - The node state to restore.
 */
void CurveWidget::restoreNodesInternal(const QVector<CurveNode>& nodes)
{
    // Check if state actually changed to avoid unnecessary updates/signal emissions
    if (m_nodes != nodes) { // Requires CurveNode::operator==
        m_nodes = nodes; // Assign the restored state
        m_samplesDirty = true;
        m_selection = {SelectedPart::NONE, -1}; // Reset selection on undo/redo
        m_dragging = false;
        update();
        emit curveChanged();
        // Emit deselection signal to update MainWindow UI
        emit selectionChanged(-1, HandleAlignment::Free);
    }
}


// --- Private Helper Functions ---

/**
 * @brief Sorts the internal m_nodes vector by the main point's X-coordinate.
 * Ensures first and last nodes remain at X=0 and X=1 respectively.
 */
void CurveWidget::sortNodes() {
    if (m_nodes.size() <= 1) return;
    // Sort intermediate nodes
    if (m_nodes.size() > 2) {
        std::sort(m_nodes.begin() + 1, m_nodes.end() - 1,
                  [](const CurveNode& a, const CurveNode& b) {
                      return a.mainPoint.x() < b.mainPoint.x();
                  });
    }
    // Ensure endpoints are fixed
    m_nodes.first().mainPoint.setX(0.0);
    m_nodes.last().mainPoint.setX(1.0);
    m_samplesDirty = true; // Sorting affects curve sampling order
}

/**
 * @brief Private helper to apply alignment snapping to a specific node's handles.
 * @param nodeIndex - Index of the node whose handles should be snapped (in m_nodes).
 */
void CurveWidget::applyAlignmentSnap(int nodeIndex) {
    // Check if it's an intermediate node
    if (nodeIndex <= 0 || nodeIndex >= m_nodes.size() - 1) return;

    CurveNode& node = m_nodes[nodeIndex]; // Get reference

    // Snap only if mode requires it
    if (node.alignment == HandleAlignment::Aligned || node.alignment == HandleAlignment::Mirrored) {
        const QPointF& mainPt = node.mainPoint; QPointF* hIn = &node.handleIn; QPointF* hOut = &node.handleOut;
        QPointF vecOut = *hOut - mainPt; qreal lenOutSq = QPointF::dotProduct(vecOut, vecOut); QPointF newInPos;
        if (lenOutSq < 1e-12) { newInPos = mainPt; } // Handles coincident
        else {
            qreal lenOut = qSqrt(lenOutSq); QPointF normDirIn = -vecOut / lenOut;
            if (node.alignment == HandleAlignment::Aligned) {
                QPointF vecInOld = *hIn - mainPt; qreal lenInOld = qSqrt(QPointF::dotProduct(vecInOld, vecInOld));
                newInPos = mainPt + normDirIn * lenInOld;
            } else { // Mirrored
                newInPos = mainPt + normDirIn * lenOut;
            }
        }
        // Update handleIn and clamp its position
        hIn->setX(std::max(0.0, std::min(1.0, newInPos.x())));
        hIn->setY(std::max(0.0, std::min(1.0, newInPos.y())));
    }
}

/**
 * @brief Finds the closest interactive part (main point or handle) to a widget position.
 * @param widgetPos - Position in widget coordinates.
 * @param mainRadius - Click radius for main points (Defaults from header if not provided).
 * @param handleRadius - Click radius for handles (Defaults from header if not provided).
 * @return SelectionInfo indicating the closest part and its node index.
 */
CurveWidget::SelectionInfo CurveWidget::findNearbyPart(const QPoint& widgetPos, qreal mainRadius /*= 10.0*/, qreal handleRadius /*= 8.0*/)
{
    // Using default arguments from header declaration if not passed.
    SelectionInfo closest = {SelectedPart::NONE, -1};
    qreal minDistSq = std::numeric_limits<qreal>::max(); // Use squared distance
    if (m_nodes.isEmpty()) return closest;

    QPointF widgetPosF = QPointF(widgetPos); // Convert click pos once

    // Iterate through the single m_nodes vector
    for (int i = 0; i < m_nodes.size(); ++i) {
        const CurveNode& node = m_nodes[i];
        // Check Handles (In/Out based on index bounds)
        if (i > 0) {
            QPointF handleInWidget = mapToWidget(node.handleIn);
            QPointF diffVec = widgetPosF - handleInWidget;
            qreal distSq = QPointF::dotProduct(diffVec, diffVec);
            if (distSq < handleRadius * handleRadius && distSq < minDistSq) {
                minDistSq = distSq; closest = {SelectedPart::HANDLE_IN, i};
            }
        }
        if (i < m_nodes.size() - 1) {
            QPointF handleOutWidget = mapToWidget(node.handleOut);
            QPointF diffVec = widgetPosF - handleOutWidget;
            qreal distSq = QPointF::dotProduct(diffVec, diffVec);
            if (distSq < handleRadius * handleRadius && distSq < minDistSq) {
                minDistSq = distSq; closest = {SelectedPart::HANDLE_OUT, i};
            }
        }
        // Check Main Point (takes precedence if within its radius and closer than any handle)
        QPointF mainWidget = mapToWidget(node.mainPoint);
        QPointF diffVec = widgetPosF - mainWidget;
        qreal distSq = QPointF::dotProduct(diffVec, diffVec);
        if (distSq < mainRadius * mainRadius && distSq < minDistSq) {
            minDistSq = distSq; closest = {SelectedPart::MAIN_POINT, i};
        }
    }
    return closest;
}

/**
 * @brief Finds the closest point on the curve to a widget position, returning segment index and parameter t.
 * Used for inserting new nodes. Operates on the single m_nodes curve.
 * @param widgetPos - Position in widget coordinates.
 * @return ClosestSegmentResult indicating segment index, parameter t, and distance squared.
 */
CurveWidget::ClosestSegmentResult CurveWidget::findClosestSegment(const QPoint& widgetPos) const
{
    ClosestSegmentResult bestMatch;
    if (m_nodes.size() < 2) return bestMatch;

    QPointF widgetPosF = QPointF(widgetPos);
    const int stepsPerSegment = 20; // Sampling density for distance check
    qreal minDistanceSq = std::numeric_limits<qreal>::max();

    for (int i = 0; i < m_nodes.size() - 1; ++i) { // Iterate through segments
        const CurveNode& node0 = m_nodes[i]; const CurveNode& node1 = m_nodes[i+1];
        const QPointF p0 = node0.mainPoint; const QPointF p1 = node0.handleOut;
        const QPointF p2 = node1.handleIn; const QPointF p3 = node1.mainPoint;

        for (int j = 0; j <= stepsPerSegment; ++j) { // Sample points on segment
            qreal t = static_cast<qreal>(j) / stepsPerSegment;
            QPointF pointOnCurve = ::evaluateBezier(p0, p1, p2, p3, t); // Use helper from namespace
            QPointF pointWidget = mapToWidget(pointOnCurve);
            QPointF diffVec = widgetPosF - pointWidget;
            qreal distSq = QPointF::dotProduct(diffVec, diffVec);
            if (distSq < minDistanceSq) { // Found a closer point
                minDistanceSq = distSq; bestMatch.segmentIndex = i;
                bestMatch.t = t; bestMatch.distanceSq = distSq;
            }
        }
    }
    // Optional: Add max distance threshold if desired
    // const qreal maxDistThresholdSq = 30.0 * 30.0;
    // if (bestMatch.distanceSq > maxDistThresholdSq) {
    //    bestMatch.segmentIndex = -1; // Mark as too far
    // }
    return bestMatch;
}


/**
 * @brief Internal helper to sample the curve represented by the given nodes at x.
 * IMPORTANT: Currently uses linear interpolation between main points for simplicity.
 * Replace with proper Bezier sampling by X if higher accuracy is needed.
 * @param nodes - The vector of nodes representing the curve.
 * @param x - The X-coordinate to sample.
 * @return The linearly interpolated Y-coordinate.
 */
qreal CurveWidget::sampleCurveInternal(const QVector<CurveNode>& nodes, qreal x) const {
    // TODO: Implement proper Bezier sampling by X (solve cubic or use iteration)
    if (nodes.size() < 2) return (nodes.isEmpty() ? 0.0 : nodes.first().mainPoint.y());
    // Assume nodes are sorted by x (sortNodes should be called after relevant modifications)
    if (x <= nodes.first().mainPoint.x()) return nodes.first().mainPoint.y();
    if (x >= nodes.last().mainPoint.x()) return nodes.last().mainPoint.y();
    for (int i = 0; i < nodes.size() - 1; ++i) {
        const CurveNode& n1 = nodes[i]; const CurveNode& n2 = nodes[i+1];
        if (x >= n1.mainPoint.x() && x <= n2.mainPoint.x()) {
            qreal range = n2.mainPoint.x() - n1.mainPoint.x();
            if (std::fabs(range) < 1e-9) return n1.mainPoint.y(); // Avoid division by zero
            qreal t_lin = (x - n1.mainPoint.x()) / range;
            return lerp(n1.mainPoint, n2.mainPoint, t_lin).y(); // Use global helper
        }
    }
    return nodes.last().mainPoint.y(); // Fallback
}

/**
 * @brief Updates the internal cache of sampled points (m_curveSamples) for the curve.
 * Uses the m_nodes vector and evaluates the Bézier segments. Must be const for sampleCurve().
 */
void CurveWidget::updateCurveSamples() const {
    m_curveSamples.clear();
    if (m_nodes.size() < 2) { m_samplesDirty = false; return; } // Use m_nodes
    m_curveSamples.reserve(m_numSamples + 1);
    int numSegments = m_nodes.size() - 1;
    for (int i = 0; i < numSegments; ++i) {
        const CurveNode& node0 = m_nodes[i]; const CurveNode& node1 = m_nodes[i+1];
        // Approximate samples per segment
        int samplesPerSegment = m_numSamples / numSegments;
        if (i == numSegments - 1) samplesPerSegment = m_numSamples - i * (m_numSamples / numSegments); // Remainder
        samplesPerSegment = std::max(1, samplesPerSegment); // Ensure at least 1
        // Add start point
        if (i == 0) m_curveSamples.append(node0.mainPoint);
        // Sample segment
        for (int j = 1; j <= samplesPerSegment; ++j) {
            qreal t = static_cast<qreal>(j) / samplesPerSegment;
            QPointF sample = ::evaluateBezier(node0.mainPoint, node0.handleOut, node1.handleIn, node1.mainPoint, t); // Use helper
            // Ensure monotonic X
            if (!m_curveSamples.isEmpty() && sample.x() < m_curveSamples.last().x()) {
                sample.setX(m_curveSamples.last().x());
            }
            sample.setY(std::max(0.0, std::min(1.0, sample.y()))); // Clamp Y
            m_curveSamples.append(sample);
        }
    }
    // Ensure last point is exactly the last node's main point if possible
    if (!m_curveSamples.isEmpty() && m_curveSamples.last() != m_nodes.last().mainPoint) {
        if (m_curveSamples.last().x() <= m_nodes.last().mainPoint.x()){ m_curveSamples.append(m_nodes.last().mainPoint); }
        else { m_curveSamples.last() = m_nodes.last().mainPoint; }
    }
    m_samplesDirty = false; // Cache is now clean
}


// --- Coordinate Mapping ---
QPointF CurveWidget::mapToWidget(const QPointF& logicalPoint) const
{
    const qreal margin = m_mainPointRadius + 2.0;
    qreal usableWidth = std::max(1.0, width() - 2.0 * margin);   // Prevent non-positive size
    qreal usableHeight = std::max(1.0, height() - 2.0 * margin); // Prevent non-positive size
    qreal widgetX = margin + logicalPoint.x() * usableWidth;
    qreal widgetY = margin + (1.0 - logicalPoint.y()) * usableHeight; // Y is inverted
    return QPointF(widgetX, widgetY);
}

QPointF CurveWidget::mapFromWidget(const QPoint& widgetPoint) const
{
    const qreal margin = m_mainPointRadius + 2.0;
    qreal usableWidth = width() - 2.0 * margin;
    qreal usableHeight = height() - 2.0 * margin;
    if (usableWidth <= 1e-6 || usableHeight <= 1e-6) { return QPointF(0.0, 0.0); } // Avoid division by zero
    qreal logicalX = (static_cast<qreal>(widgetPoint.x()) - margin) / usableWidth;
    qreal logicalY = 1.0 - (static_cast<qreal>(widgetPoint.y()) - margin) / usableHeight; // Y is inverted
    // No clamping here - let caller handle it
    return QPointF(logicalX, logicalY);
}
