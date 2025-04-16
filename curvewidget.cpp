#include "curvewidget.h"
#include "setcurvestatecommand.h" // Ensure updated command class is included

#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QPainterPath>
#include <QPalette>
#include <QtMath>             // For qSqrt, qAbs, etc.
#include <QtCore/qnumeric.h>  // For qFuzzyCompare
#include <QDebug>
#include <QList>              // For channel list in constructor
#include <QSet>               // Used for m_selectedNodeIndices
#include <QRect>              // Used for m_boxSelectionRect
#include <QVector>            // Used throughout
#include <QMap>               // Used for m_channelNodes
#include <QUndoStack>         // Used for m_undoStack
#include <QKeyEvent>          // Event handlers
#include <QMouseEvent>        // Event handlers
#include <QPaintEvent>        // Event handlers
#include <QResizeEvent>       // Event handlers

#include <algorithm>          // For std::sort, std::max, std::min
#include <cmath>              // For std::max, std::min, std::round, std::abs
#include <limits>             // For std::numeric_limits
#include <functional>         // For std::greater (in keyPressEvent)
#include <stdexcept>          // For std::out_of_range in getActiveNodes


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
    QPointF pointOnCurve; // P_new
    QPointF handle1_Seg1; // P01 (becomes handleOut for node i)
    QPointF handle2_Seg1; // P012 (becomes handleIn for new node)
    QPointF handle1_Seg2; // P123 (becomes handleOut for new node)
    QPointF handle2_Seg2; // P23 (becomes handleIn for node i+1)
};

/**
 * @brief Subdivides a cubic Bézier segment at parameter t using De Casteljau algorithm.
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
 */
QPointF evaluateBezier(const QPointF& p0, const QPointF& p1, const QPointF& p2, const QPointF& p3, qreal t) {
    qreal mt = 1.0 - t;
    qreal mt2 = mt * mt;
    qreal t2 = t * t;
    return p0 * mt * mt2 + p1 * 3.0 * mt2 * t + p2 * 3.0 * mt * t2 + p3 * t * t2;
}

/**
 * @brief Evaluates the X component derivative of a cubic Bézier segment w.r.t. t.
 */
qreal evaluateBezierXDerivative(const QPointF& p0, const QPointF& p1, const QPointF& p2, const QPointF& p3, qreal t) {
    qreal mt = 1.0 - t;
    // Bx'(t) = 3(1-t)^2(P1x-P0x) + 6(1-t)t(P2x-P1x) + 3t^2(P3x-P2x)
    return 3.0 * mt * mt * (p1.x() - p0.x()) +
           6.0 * mt * t  * (p2.x() - p1.x()) +
           3.0 * t * t   * (p3.x() - p2.x());
}

} // End anonymous namespace


// --- CurveNode Implementation ---

CurveWidget::CurveNode::CurveNode(QPointF p)
    : mainPoint(p),
    handleIn(p), // Handles default to coincide with main point
    handleOut(p),
    alignment(HandleAlignment::Aligned) // Default alignment mode
{}

// Equality operator using fuzzy compare for points
bool CurveWidget::CurveNode::operator==(const CurveNode& other) const {
    const qreal epsilon = 1e-9; // Tolerance for floating point comparison
    return qAbs(mainPoint.x() - other.mainPoint.x()) < epsilon &&
           qAbs(mainPoint.y() - other.mainPoint.y()) < epsilon &&
           qAbs(handleIn.x() - other.handleIn.x()) < epsilon &&
           qAbs(handleIn.y() - other.handleIn.y()) < epsilon &&
           qAbs(handleOut.x() - other.handleOut.x()) < epsilon &&
           qAbs(handleOut.y() - other.handleOut.y()) < epsilon &&
           alignment == other.alignment;
}


// --- CurveWidget Constructor ---

CurveWidget::CurveWidget(QWidget *parent)
    : QWidget(parent),
    m_activeChannel(ActiveChannel::RED),     // Default active channel
    m_dragging(false),                   // Not dragging initially
    m_currentDrag({SelectedPart::NONE, -1}), // Nothing being dragged initially
    m_isBoxSelecting(false),             // Not box selecting initially
    m_mainPointRadius(5.0),              // Default visual size
    m_handleRadius(4.0),                 // Default visual size
    m_isDarkMode(false),                 // Default theme
    m_drawInactiveChannels(false)        // Default inactive curve visibility
// Other members like QSet, QMap, QRect, QPoint, QUndoStack are default constructed
{
    // Initialize default curves for R, G, B channels
    QList<ActiveChannel> channels = {ActiveChannel::RED, ActiveChannel::GREEN, ActiveChannel::BLUE};
    for (ActiveChannel channel : channels) {
        QVector<CurveNode> defaultNodes;
        CurveNode node0(QPointF(0.0, 0.0));
        CurveNode node1(QPointF(1.0, 1.0));
        // Initial straight line handles (can be adjusted)
        node0.handleOut = QPointF(1.0/3.0, 0.0);
        node1.handleIn  = QPointF(2.0/3.0, 1.0);
        node0.alignment = HandleAlignment::Free; // Endpoints usually free
        node1.alignment = HandleAlignment::Free;
        defaultNodes << node0 << node1;
        m_channelNodes.insert(channel, defaultNodes);
    }

    // Basic widget setup
    setMinimumSize(200, 200);
    setFocusPolicy(Qt::ClickFocus); // Allow getting focus for key events
    setAutoFillBackground(true);   // Needed for theme background painting
    setMouseTracking(true);        // Enable mouse move events even when no button is pressed (optional)
}


// --- Public Methods ---

/**
 * @brief Gets a copy of the nodes for all channels.
 */
QMap<CurveWidget::ActiveChannel, QVector<CurveWidget::CurveNode>> CurveWidget::getAllChannelNodes() const {
    return m_channelNodes;
}

/**
 * @brief Gets the currently active channel for editing.
 */
CurveWidget::ActiveChannel CurveWidget::getActiveChannel() const {
    return m_activeChannel;
}

/**
 * @brief Samples the curve's Y value for a specific channel at a given X value.
 * Uses iterative solving (Newton-Raphson) to find the Bézier parameter t for the given x.
 */
qreal CurveWidget::sampleCurveChannel(ActiveChannel channel, qreal x) const {
    // Clamp input x to [0, 1]
    x = std::max(0.0, std::min(1.0, x));

    // Get the correct nodes for the requested channel
    auto it = m_channelNodes.find(channel);
    if (it == m_channelNodes.end() || it.value().size() < 2) {
        qWarning() << "CurveWidget::sampleCurveChannel - Channel invalid or < 2 nodes. Returning linear.";
        return x; // Fallback: return linear value (input x)
    }
    const QVector<CurveNode>& nodes = it.value(); // Use const reference

    // --- Accurate Bézier Sampling by X ---
    // 1. Find the correct Bézier segment based on x
    int segmentIndex = -1;
    for (int i = 0; i < nodes.size() - 1; ++i) {
        // Nodes must be sorted by mainPoint.x() for this to work
        if (x >= nodes[i].mainPoint.x() && x <= nodes[i+1].mainPoint.x()) {
            // Handle vertical segments where start/end X are equal (or very close)
            if (qFuzzyCompare(nodes[i].mainPoint.x(), nodes[i+1].mainPoint.x())) {
                // If x matches the vertical line's x, return start Y
                return nodes[i].mainPoint.y();
            }
            segmentIndex = i;
            break;
        }
    }

    // Handle edge cases: x before first node or after last node
    if (segmentIndex == -1) {
        if (x <= nodes.first().mainPoint.x()) {
            return nodes.first().mainPoint.y();
        } else { // x >= last node's x
            return nodes.last().mainPoint.y();
        }
    }

    // 2. Solve for parameter 't' within the segment such that BezierX(t) = x
    const CurveNode& n0 = nodes[segmentIndex];
    const CurveNode& n1 = nodes[segmentIndex + 1];
    const QPointF p0 = n0.mainPoint;
    const QPointF p1 = n0.handleOut;
    const QPointF p2 = n1.handleIn;
    const QPointF p3 = n1.mainPoint;

    // Use an iterative method (Newton-Raphson) to find t
    qreal t_guess = 0.5; // Initial guess
    qreal segmentXRange = p3.x() - p0.x();

    if (std::abs(segmentXRange) > 1e-9) { // Check if segment is not vertical
        t_guess = (x - p0.x()) / segmentXRange;
        t_guess = std::max(0.0, std::min(1.0, t_guess)); // Clamp initial guess
    } else {
        qWarning("Sampling near vertical segment, result might be inaccurate.");
        return p0.y(); // Fallback for vertical segment
    }

    const int MAX_ITERATIONS = 15; // Max iterations for solver
    const qreal TOLERANCE_X = 1e-7; // Tolerance for matching x
    qreal t = t_guess;

    for (int iter = 0; iter < MAX_ITERATIONS; ++iter) {
        qreal currentX = evaluateBezier(p0, p1, p2, p3, t).x();
        qreal error = currentX - x;
        if (std::abs(error) < TOLERANCE_X) break; // Converged

        qreal dXdt = evaluateBezierXDerivative(p0, p1, p2, p3, t);
        if (std::abs(dXdt) < 1e-7) {
            qWarning("Newton's method encountered near-zero derivative during sampling.");
            break; // Derivative too small, stop iterating
        }
        t = t - error / dXdt; // Newton's step
        t = std::max(0.0, std::min(1.0, t)); // Clamp t back to [0, 1]
    }
    t = std::max(0.0, std::min(1.0, t)); // Final safety clamp

    // 3. Evaluate BezierY(t) using the found/approximated t
    qreal resultY = evaluateBezier(p0, p1, p2, p3, t).y();

    // Clamp final output Y to [0, 1]
    return std::max(0.0, std::min(1.0, resultY));
}

/**
 * @brief Resets the *active* curve channel to its default state (straight line). Undoable.
 */
void CurveWidget::resetCurve() {
    m_stateBeforeAction = m_channelNodes; // Capture state BEFORE reset

    QVector<CurveNode>& activeNodes = getActiveNodes();
    activeNodes.clear();

    // Initialize active channel with default nodes
    CurveNode node0(QPointF(0.0, 0.0)); CurveNode node1(QPointF(1.0, 1.0));
    node0.handleOut = QPointF(1.0/3.0, 0.0); node1.handleIn = QPointF(2.0/3.0, 1.0);
    node0.alignment = HandleAlignment::Free; node1.alignment = HandleAlignment::Free;
    activeNodes << node0 << node1;

    QMap<ActiveChannel, QVector<CurveNode>> newState = m_channelNodes; // Capture state AFTER reset
    bool stateChanged = false; // Compare m_stateBeforeAction and newState
    if (m_stateBeforeAction.keys() != newState.keys()) { stateChanged = true; }
    else { for(auto key : m_stateBeforeAction.keys()) { if (m_stateBeforeAction[key] != newState[key]) { stateChanged = true; break; }}}

    if (stateChanged) {
        m_undoStack.push(new SetCurveStateCommand(this, m_stateBeforeAction, newState, "Reset Curve"));
    } else { m_stateBeforeAction.clear(); } // No change, clear captured state

    // Clear selection and interaction states
    m_selectedNodeIndices.clear();
    m_currentDrag = {SelectedPart::NONE, -1};
    m_dragging = false;
    m_isBoxSelecting = false;
    m_boxSelectionRect = QRect();

    update();
    emit curveChanged();
    emit selectionChanged(); // Emit generic signal
}

/**
 * @brief Sets the dark mode flag for drawing colors.
 */
void CurveWidget::setDarkMode(bool dark) {
    if (m_isDarkMode != dark) {
        m_isDarkMode = dark;
        update(); // Trigger repaint
    }
}

/**
 * @brief Provides access to the widget's internal undo stack.
 */
QUndoStack* CurveWidget::undoStack() {
    return &m_undoStack;
}

/**
 * @brief Gets the number of nodes in the currently active channel's curve.
 */
int CurveWidget::getActiveNodeCount() const {
    // Use the existing const helper function to get the nodes
    return getActiveNodes().size();
}

/**
 * @brief Gets the set of currently selected main node indices.
 */
QSet<int> CurveWidget::getSelectedIndices() const {
    return m_selectedNodeIndices;
}

/**
 * @brief Gets the handle alignment mode for a specific node index in the active channel.
 * Returns Free if index is invalid.
 */
CurveWidget::HandleAlignment CurveWidget::getAlignment(int nodeIndex) const {
    const QVector<CurveNode>& activeNodes = getActiveNodes();
    if (nodeIndex >= 0 && nodeIndex < activeNodes.size()) {
        return activeNodes[nodeIndex].alignment;
    }
    qWarning() << "getAlignment: Invalid index" << nodeIndex << "requested for active channel.";
    return HandleAlignment::Free; // Default fallback
}


// --- Public Slots ---

/**
 * @brief Sets the currently active channel for editing and viewing.
 */
void CurveWidget::setActiveChannel(ActiveChannel channel) {
    // Check if the channel exists in our map
    if (!m_channelNodes.contains(channel)) {
        qWarning() << "Attempted to set invalid active channel:" << static_cast<int>(channel);
        return;
    }

    if (m_activeChannel != channel) {
        m_activeChannel = channel;

        // Clear selection and interaction states relevant to the previous channel
        m_selectedNodeIndices.clear();
        m_currentDrag = {SelectedPart::NONE, -1};
        m_dragging = false;
        m_isBoxSelecting = false;
        m_boxSelectionRect = QRect();

        update(); // Redraw to show the new active curve
        emit selectionChanged(); // Signal that selection is now empty
        qDebug() << "Active channel set to:" << static_cast<int>(m_activeChannel);
    }
}

/**
 * @brief Sets the handle alignment mode for a specific node.
 * Only applies if exactly one node is selected and its index matches nodeIndex.
 */
void CurveWidget::setNodeAlignment(int nodeIndex, HandleAlignment mode) {
    // Check if the request makes sense in the current selection context
    if (m_selectedNodeIndices.size() != 1 || !m_selectedNodeIndices.contains(nodeIndex)) {
        qWarning() << "setNodeAlignment ignored: requires exactly one selected node matching the index.";
        return;
    }

    QVector<CurveNode>& activeNodes = getActiveNodes(); // Need non-const ref
    // Check index bounds again for safety
    if (nodeIndex < 0 || nodeIndex >= activeNodes.size()) {
        qWarning() << "setNodeAlignment: Invalid index" << nodeIndex;
        return;
    }

    CurveNode& node = activeNodes[nodeIndex]; // Get reference to the node
    if (node.alignment != mode) {
        m_stateBeforeAction = m_channelNodes; // Capture state BEFORE change
        node.alignment = mode;                // Apply the new mode
        applyAlignmentSnap(nodeIndex, SelectedPart::HANDLE_OUT, true);        // Apply snap logic based on new mode

        QMap<ActiveChannel, QVector<CurveNode>> newState = m_channelNodes; // Capture state AFTER change
        bool stateChanged = false; // Compare states for undo...
        if (m_stateBeforeAction.keys() != newState.keys()) { stateChanged = true; }
        else { for(auto key : m_stateBeforeAction.keys()) { if (m_stateBeforeAction[key] != newState[key]) { stateChanged = true; break; }}}

        if (stateChanged) {
            m_undoStack.push(new SetCurveStateCommand(this, m_stateBeforeAction, newState, "Change Alignment"));
        } else { m_stateBeforeAction.clear(); }

        update();
        emit curveChanged();     // Shape might have changed
        emit selectionChanged(); // Let MainWindow update button state if needed
    }
}

/**
 * @brief Sets whether to draw inactive channels in the background.
 */
void CurveWidget::setDrawInactiveChannels(bool draw) {
    if (m_drawInactiveChannels != draw) {
        m_drawInactiveChannels = draw;
        update(); // Trigger repaint
        qDebug() << "Draw inactive channels set to:" << m_drawInactiveChannels;
    }
}


// --- Event Handlers ---

/**
 * @brief Handles painting the widget. Draws grid, curves (active/inactive),
 * nodes, handles, selection highlights, and box selection rectangle.
 */
void CurveWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // --- Color Definitions ---
    QColor gridColor, borderColor, handleLineColor, handleColor, mainPointColor, outlineColor;
    QColor activeCurveColor;
    QColor selectionColor = Qt::yellow;

    if (m_isDarkMode) {
        gridColor = QColor(80, 80, 80); borderColor = QColor(90, 90, 90);
        handleLineColor = QColor(100, 100, 100); handleColor = QColor(0, 190, 190);
        mainPointColor = QColor(230, 230, 230); outlineColor = Qt::black;
        activeCurveColor = QColor(240, 240, 240);
    } else { // Light Mode
        gridColor = QColor(210, 210, 210); borderColor = QColor(180, 180, 180);
        handleLineColor = QColor(140, 140, 140); handleColor = QColor(0, 100, 100);
        mainPointColor = QColor(10, 10, 10); outlineColor = Qt::darkGray;
        activeCurveColor = QColor(10, 10, 10);
    }

    auto getInactiveChannelColor = [&](ActiveChannel ch) -> QColor {
        QColor base = m_isDarkMode ? QColor(100, 100, 100, 150) : QColor(160, 160, 160, 150);
        switch(ch) {
        case ActiveChannel::RED:   return QColor(255, 80, 80, base.alpha());
        case ActiveChannel::GREEN: return QColor(80, 255, 80, base.alpha());
        case ActiveChannel::BLUE:  return QColor(80, 80, 255, base.alpha());
        default: return base;
        }
    };

    // --- Draw Grid & Border ---
    painter.setPen(QPen(gridColor, 0.5));
    int numGridLines = 10;
    for (int i = 1; i < numGridLines; ++i) {
        qreal ratio = static_cast<qreal>(i) / numGridLines;
        painter.drawLine(mapToWidget(QPointF(ratio, 0.0)), mapToWidget(QPointF(ratio, 1.0)));
        painter.drawLine(mapToWidget(QPointF(0.0, ratio)), mapToWidget(QPointF(1.0, ratio)));
    }
    painter.setPen(QPen(borderColor, 1));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    // --- Draw Inactive Curves (Conditional) ---
    if (m_drawInactiveChannels) {
        painter.save();
        for (auto it = m_channelNodes.constBegin(); it != m_channelNodes.constEnd(); ++it) {
            if (it.key() == m_activeChannel) continue; // Skip active
            const QVector<CurveNode>& nodes = it.value();
            if (nodes.size() < 2) continue;
            QPainterPath inactivePath;
            inactivePath.moveTo(mapToWidget(nodes[0].mainPoint));
            for (int i = 0; i < nodes.size() - 1; ++i) {
                inactivePath.cubicTo(mapToWidget(nodes[i].handleOut), mapToWidget(nodes[i+1].handleIn), mapToWidget(nodes[i+1].mainPoint));
            }
            painter.setPen(QPen(getInactiveChannelColor(it.key()), 1.2, Qt::DotLine));
            painter.drawPath(inactivePath);
        }
        painter.restore();
    }

    // --- Draw Active Curve ---
    const QVector<CurveNode>& activeNodes = getActiveNodes(); // Use const ref
    if (activeNodes.size() >= 2) {
        QPainterPath curvePath;
        curvePath.moveTo(mapToWidget(activeNodes[0].mainPoint));
        for (int i = 0; i < activeNodes.size() - 1; ++i) {
            curvePath.cubicTo(mapToWidget(activeNodes[i].handleOut), mapToWidget(activeNodes[i+1].handleIn), mapToWidget(activeNodes[i+1].mainPoint));
        }
        painter.setPen(QPen(activeCurveColor, 2));
        painter.drawPath(curvePath);
    }

    // --- Draw Nodes and Handles for Active Curve (Highlighting uses new state vars) ---
    for (int i = 0; i < activeNodes.size(); ++i) {
        const CurveNode& node = activeNodes[i];
        QPointF mainWidgetPos = mapToWidget(node.mainPoint);

        // Draw Handle Lines
        painter.setPen(QPen(handleLineColor, 1));
        if (i > 0) painter.drawLine(mainWidgetPos, mapToWidget(node.handleIn));
        if (i < activeNodes.size() - 1) painter.drawLine(mainWidgetPos, mapToWidget(node.handleOut));

        // Draw Handles (Highlight based on m_currentDrag if it's a handle)
        painter.setPen(QPen(outlineColor, 0.5));
        if (i > 0) { // Handle In
            bool isHandleDragging = (m_currentDrag.part == SelectedPart::HANDLE_IN && m_currentDrag.nodeIndex == i);
            painter.setBrush(isHandleDragging ? selectionColor : handleColor);
            painter.drawEllipse(mapToWidget(node.handleIn), m_handleRadius, m_handleRadius);
        }
        if (i < activeNodes.size() - 1) { // Handle Out
            bool isHandleDragging = (m_currentDrag.part == SelectedPart::HANDLE_OUT && m_currentDrag.nodeIndex == i);
            painter.setBrush(isHandleDragging ? selectionColor : handleColor);
            painter.drawEllipse(mapToWidget(node.handleOut), m_handleRadius, m_handleRadius);
        }

        // Draw Main Point (Highlight based on m_selectedNodeIndices)
        painter.setPen(QPen(outlineColor, 1));
        bool isMainSelected = m_selectedNodeIndices.contains(i);
        painter.setBrush(isMainSelected ? selectionColor : mainPointColor);
        painter.drawEllipse(mainWidgetPos, m_mainPointRadius, m_mainPointRadius);
    }

    // --- Draw Box Selection Rectangle (If active) ---
    if (m_isBoxSelecting) {
        painter.save();
        QColor boxColor = m_isDarkMode ? Qt::white : Qt::black;
        painter.setPen(QPen(boxColor, 1, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(m_boxSelectionRect);
        painter.restore();
    }
}

/**
 * @brief Handles mouse press for selecting parts, initiating drags, or starting box selection.
 */
void CurveWidget::mousePressEvent(QMouseEvent *event)
{
    // Reset interaction states
    m_isBoxSelecting = false;
    m_dragging = false;
    m_boxSelectionRect = QRect();
    m_currentDrag = {SelectedPart::NONE, -1}; // Clear drag target initially
    m_stateBeforeAction.clear();              // Clear undo state initially

    QPoint currentPos = event->pos();
    SelectionInfo clickedPart = findNearbyPart(currentPos);
    bool shiftPressed = event->modifiers() & Qt::ShiftModifier;
    bool selectionActuallyChanged = false;

    // --- Right-Click Handling (Delete Node) ---
    if (event->button() == Qt::RightButton && clickedPart.part == SelectedPart::MAIN_POINT)
    {
        int nodeIndex = clickedPart.nodeIndex;
        QVector<CurveNode>& activeNodes = getActiveNodes();
        if (nodeIndex > 0 && nodeIndex < activeNodes.size() - 1) { // Check deletable
            m_stateBeforeAction = m_channelNodes;
            activeNodes.remove(nodeIndex);
            m_undoStack.push(new SetCurveStateCommand(this, m_stateBeforeAction, m_channelNodes, "Delete Node"));

            if(m_selectedNodeIndices.contains(nodeIndex) || !m_selectedNodeIndices.isEmpty()) selectionActuallyChanged = true;
            m_selectedNodeIndices.clear(); // Clear selection after delete
            m_currentDrag = {SelectedPart::NONE, -1}; // Ensure no drag target

            update();
            emit curveChanged();
            if(selectionActuallyChanged) emit selectionChanged();
        }
        return; // Right-click action complete
    }
    // --- Left-Click Handling ---
    else if (event->button() == Qt::LeftButton)
    {
        if (clickedPart.part != SelectedPart::NONE) {
            // --- Clicked on an existing Point or Handle (Left Click) ---
            m_dragging = true; // Start dragging this part
            m_currentDrag = clickedPart; // Store what's being dragged

            if (clickedPart.part == SelectedPart::MAIN_POINT) {
                int clickedIndex = clickedPart.nodeIndex;
                bool alreadySelected = m_selectedNodeIndices.contains(clickedIndex);
                if (!shiftPressed) {
                    if (!alreadySelected) { // Select only this one
                        if (!m_selectedNodeIndices.isEmpty()) selectionActuallyChanged = true;
                        m_selectedNodeIndices.clear();
                        m_selectedNodeIndices.insert(clickedIndex);
                        if (!selectionActuallyChanged) selectionActuallyChanged = true;
                    } // else: Dragging existing selection, no change needed now
                } else { // Shift: Toggle selection
                    if (alreadySelected) m_selectedNodeIndices.remove(clickedIndex); else m_selectedNodeIndices.insert(clickedIndex);
                    selectionActuallyChanged = true;
                }
                // Capture state *before* the drag starts
                m_stateBeforeAction = m_channelNodes;

            } else { // Clicked on Handle
                if (!m_selectedNodeIndices.isEmpty()) { m_selectedNodeIndices.clear(); selectionActuallyChanged = true; }
                // Capture state *before* handle drag starts
                m_stateBeforeAction = m_channelNodes;
            }

        } else {
            // --- Clicked on Empty Space (Left Click) ---
            // Check for NODE ADDITION first
            ClosestSegmentResult hit = findClosestSegment(currentPos);
            const qreal t_tolerance = 0.005; const qreal max_dist_sq_for_add = 20.0 * 20.0;

            if (hit.segmentIndex != -1 && hit.t > t_tolerance && hit.t < (1.0 - t_tolerance) && hit.distanceSq < max_dist_sq_for_add)
            {
                // --- Add New Node ---
                qDebug() << "Adding node on segment" << hit.segmentIndex << "at t=" << hit.t;
                // 1. Capture state BEFORE add for the "Add Node" undo command
                QMap<ActiveChannel, QVector<CurveNode>> stateBeforeAdd = m_channelNodes;

                int i = hit.segmentIndex; qreal t = hit.t;
                QVector<CurveNode>& activeNodes = getActiveNodes();
                if (i < 0 || i >= activeNodes.size() - 1) { /* ... handle error ... */ return; }

                // 2. Perform subdivision and insertion
                const QPointF p0=activeNodes[i].mainPoint, p1=activeNodes[i].handleOut, p2=activeNodes[i+1].handleIn, p3=activeNodes[i+1].mainPoint;
                SubdivisionResult split = subdivideBezier(p0, p1, p2, p3, t);
                CurveNode newNode(split.pointOnCurve);
                newNode.handleIn = split.handle2_Seg1; newNode.handleOut = split.handle1_Seg2;
                newNode.alignment = HandleAlignment::Aligned;
                activeNodes[i].handleOut = split.handle1_Seg1; activeNodes[i+1].handleIn = split.handle2_Seg2;
                int newNodeIndex = i + 1;
                activeNodes.insert(newNodeIndex, newNode); // Node added

                // 3. Capture state AFTER node add
                QMap<ActiveChannel, QVector<CurveNode>> stateAfterAdd = m_channelNodes;

                // 4. Push "Add Node" command
                m_undoStack.push(new SetCurveStateCommand(this, stateBeforeAdd, stateAfterAdd, "Add Node"));
                qDebug() << "Add Node undo command pushed.";

                // 5. Update selection state
                if (!m_selectedNodeIndices.isEmpty()) selectionActuallyChanged = true;
                m_selectedNodeIndices.clear();
                m_selectedNodeIndices.insert(newNodeIndex);
                if (!selectionActuallyChanged) selectionActuallyChanged = true;

                // *** FIX 1: Prepare for immediate drag ***
                m_dragging = true; // Set dragging flag
                m_currentDrag = {SelectedPart::MAIN_POINT, newNodeIndex}; // Set drag target
                m_stateBeforeAction = stateAfterAdd; // Set undo state for the DRAG operation

                emit curveChanged(); // Curve shape changed

            } else {
                // --- Initiate Box Selection ---
                m_isBoxSelecting = true;
                m_boxSelectionStartPoint = currentPos;
                m_boxSelectionRect = QRect(m_boxSelectionStartPoint, QSize());
                if (!shiftPressed) {
                    if (!m_selectedNodeIndices.isEmpty()) { m_selectedNodeIndices.clear(); selectionActuallyChanged = true; }
                } // Else: keep selection for additive box select
            }
        } // End handling empty space click
    } // End handling left click

    // Final updates
    if (selectionActuallyChanged) {
        emit selectionChanged();
    }
    update();
    if (!hasFocus()) setFocus();
}

/**
 * @brief Handles mouse move for dragging selected parts or updating the box selection rectangle.
 */
void CurveWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        // --- Dragging Point(s) or Handle ---
        QVector<CurveNode>& activeNodes = getActiveNodes(); // Need non-const ref
        if (m_currentDrag.nodeIndex < 0 || m_currentDrag.nodeIndex >= activeNodes.size()) {
            qWarning() << "MouseMoveEvent: Invalid drag index" << m_currentDrag.nodeIndex;
            m_dragging = false; m_currentDrag = {SelectedPart::NONE, -1}; return;
        }

        // Use the UNCLAMPED logical position from the mouse for delta calculation
        QPointF logicalPos = mapFromWidget(event->pos());
        CurveNode& primaryNode = activeNodes[m_currentDrag.nodeIndex];
        QPointF deltaLogical; // Calculated change in logical coords

        // --- Calculate Delta based on what's being dragged ---
        if (m_currentDrag.part == SelectedPart::MAIN_POINT) {
            QPointF oldLogicalPos = primaryNode.mainPoint;
            // Calculate target position WITH clamping for the main point being dragged
            qreal newX = logicalPos.x(); qreal newY = logicalPos.y();
            // CLAMP Main Point Y coordinate
            newY = std::max(0.0, std::min(1.0, newY));
            // CLAMP Main Point X coordinate based on neighbors/endpoints
            const qreal epsilon = 1e-9;
            if (m_currentDrag.nodeIndex == 0) newX = 0.0;
            else if (m_currentDrag.nodeIndex == activeNodes.size() - 1) newX = 1.0;
            else {
                qreal minX = (m_currentDrag.nodeIndex > 0) ? activeNodes[m_currentDrag.nodeIndex - 1].mainPoint.x() + epsilon : 0.0;
                qreal maxX = (m_currentDrag.nodeIndex < activeNodes.size() - 1) ? activeNodes[m_currentDrag.nodeIndex + 1].mainPoint.x() - epsilon : 1.0;
                if (minX > maxX) minX = maxX = (minX + maxX) / 2.0;
                newX = std::max(minX, std::min(maxX, newX));
            }
            newX = std::max(0.0, std::min(1.0, newX)); // Final safety clamp
            QPointF newClampedLogicalPos(newX, newY);
            deltaLogical = newClampedLogicalPos - oldLogicalPos; // Delta based on clamped position

        } else if (m_currentDrag.part == SelectedPart::HANDLE_IN || m_currentDrag.part == SelectedPart::HANDLE_OUT) {
            QPointF* handlePtr = (m_currentDrag.part == SelectedPart::HANDLE_IN) ? &primaryNode.handleIn : &primaryNode.handleOut;
            QPointF oldLogicalPos = *handlePtr;
            // Use the raw logical position - NO CLAMPING for handle target
            QPointF newUnclampedLogicalPos = logicalPos;
            deltaLogical = newUnclampedLogicalPos - oldLogicalPos; // Delta based on unclamped position

        } else { return; } // Not dragging a valid part


        // --- Apply Move based on Calculated Delta ---
        bool needsReSort = false;
        if (m_currentDrag.part == SelectedPart::MAIN_POINT && !deltaLogical.isNull()) {
            // Move all selected main points by the same delta
            for (int index : m_selectedNodeIndices) {
                if (index < 0 || index >= activeNodes.size()) continue;
                CurveNode& nodeToMove = activeNodes[index];
                QPointF oldMainPos = nodeToMove.mainPoint;
                const qreal handleCoincidenceThresholdSq = 1e-12;

                // Apply delta to main point
                nodeToMove.mainPoint += deltaLogical;
                // *** CLAMP moved Main Point Y coordinate ***
                nodeToMove.mainPoint.setY(std::max(0.0, std::min(1.0, nodeToMove.mainPoint.y())));

                // Move handles relatively
                if (QPointF::dotProduct(nodeToMove.handleIn - oldMainPos, nodeToMove.handleIn - oldMainPos) > handleCoincidenceThresholdSq) nodeToMove.handleIn += deltaLogical;
                if (QPointF::dotProduct(nodeToMove.handleOut - oldMainPos, nodeToMove.handleOut - oldMainPos) > handleCoincidenceThresholdSq) nodeToMove.handleOut += deltaLogical;

                // --- *** NO CLAMPING applied to relatively moved handles *** ---

                // Apply alignment snap AFTER moving, using HANDLE_OUT as reference
                applyAlignmentSnap(index, SelectedPart::HANDLE_OUT, false);

                if (index > 0 && index < activeNodes.size() - 1) needsReSort = true;
            }
            // Handle sorting (basic version)
            if (needsReSort) { qDebug() << "Multi-drag might require sorting."; /* sortActiveNodes(); */ }

        } else if ((m_currentDrag.part == SelectedPart::HANDLE_IN || m_currentDrag.part == SelectedPart::HANDLE_OUT) && !deltaLogical.isNull()) {
            // Move single handle being dragged
            QPointF* handlePtr = (m_currentDrag.part == SelectedPart::HANDLE_IN) ? &primaryNode.handleIn : &primaryNode.handleOut;
            *handlePtr += deltaLogical; // Apply delta

            // --- *** NO CLAMPING applied DIRECTLY to the moved handle here *** ---

            // Apply alignment constraints (this will adjust the OTHER handle and clamp IT if necessary)
            applyAlignmentSnap(m_currentDrag.nodeIndex, m_currentDrag.part, true);
        }

        // Update UI if movement occurred
        if (!deltaLogical.isNull()) {
            update();
            emit curveChanged();
        }

    } else if (m_isBoxSelecting) {
        // --- Updating Box Selection Rect ---
        m_boxSelectionRect = QRect(m_boxSelectionStartPoint, event->pos()).normalized();
        update(); // Redraw to show the rectangle
    }
}
/**
 * @brief Handles mouse release to finalize drags (with undo) or box selection.
 */
void CurveWidget::mouseReleaseEvent(QMouseEvent *event)
{
    qDebug() << "<<< Release Start: Selection:" << m_selectedNodeIndices << "Dragging:" << m_dragging << "BoxSelect:" << m_isBoxSelecting;

    bool wasDragging = m_dragging;
    // Capture the selection state *before* potentially pushing undo command
    QSet<int> selectionToKeepIndices = m_selectedNodeIndices;
    SelectionInfo dragToKeep = m_currentDrag; // Also keep track of dragged handle if any

    if (wasDragging && event->button() == Qt::LeftButton) {
        m_dragging = false; // Stop drag state

        // --- Undo Logic ---
        bool pushedCommand = false; // Track if command was pushed
        if (!m_stateBeforeAction.isEmpty()) {
            QMap<ActiveChannel, QVector<CurveNode>> currentState = m_channelNodes;
            bool stateChanged = false;
            // Compare states...
            if (m_stateBeforeAction.keys() != currentState.keys()) { stateChanged = true; }
            else { for(auto key : m_stateBeforeAction.keys()) { if (m_stateBeforeAction[key] != currentState[key]) { stateChanged = true; break; }}}

            if (stateChanged) {
                m_undoStack.push(new SetCurveStateCommand(this, m_stateBeforeAction, currentState, "Modify Curve"));
                pushedCommand = true;
                qDebug() << "Modify Curve undo command pushed (drag release).";
            } else {
                qDebug() << "Drag release: State unchanged, no undo command.";
            }
            m_stateBeforeAction.clear();
        }
        // --- End Undo Logic ---

        // *** FIX 3: Re-assert selection state AFTER potential command push ***
        qDebug() << "Re-asserting selection state after drag release.";
        m_selectedNodeIndices = selectionToKeepIndices; // Restore selected indices
        m_currentDrag = dragToKeep; // Restore info about dragged part (esp. for handles)

        // Re-emit selection changed to ensure UI consistency
        // This seemed necessary to prevent visual deselection in some cases
        emit selectionChanged();
        // --- End Fix 3 ---

        // We only clear m_currentDrag when a *new* press happens or box select finishes
        // m_currentDrag = {SelectedPart::NONE, -1}; // DON'T clear here, needed for highlight

        update(); // Ensure final paint reflects correct selection

    } else if (m_isBoxSelecting && event->button() == Qt::LeftButton) {
        // --- Finished Box Selection ---
        m_isBoxSelecting = false;
        bool selectionActuallyChanged = false;
        bool shiftPressed = event->modifiers() & Qt::ShiftModifier;
        QSet<int> originalSelection = m_selectedNodeIndices;

        if (!shiftPressed) { m_selectedNodeIndices.clear(); } // Clear first if not shift

        const QVector<CurveNode>& activeNodes = getActiveNodes();
        for (int i = 0; i < activeNodes.size(); ++i) {
            QPoint widgetPoint = mapToWidget(activeNodes[i].mainPoint).toPoint();
            if (m_boxSelectionRect.contains(widgetPoint)) { m_selectedNodeIndices.insert(i); }
        }

        if (m_selectedNodeIndices != originalSelection) selectionActuallyChanged = true;

        m_boxSelectionRect = QRect(); // Clear visual rect
        m_currentDrag = {SelectedPart::NONE, -1}; // Box selection doesn't drag a specific part
        update();

        if (selectionActuallyChanged) {
            qDebug() << "Box selection completed. Selected indices:" << m_selectedNodeIndices;
            emit selectionChanged();
        } else {
            qDebug() << "Box selection completed. Selection unchanged.";
        }
    } else {
        // Release of other buttons or release without active drag/box - clear transient state
        m_currentDrag = {SelectedPart::NONE, -1};
        m_isBoxSelecting = false; // Ensure box state is cleared
        m_boxSelectionRect = QRect();
        if (m_dragging) m_dragging = false; // Ensure dragging stops on other button release
    }

    qDebug() << ">>> Release End: Selection:" << m_selectedNodeIndices;
    // No base call usually needed
}

/**
 * @brief Handles widget resize events. Triggers repaint.
 */
void CurveWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    update(); // Mapping depends on size, so repaint
}

/**
 * @brief Handles key presses for alignment changes, deletion, undo, redo.
 */
void CurveWidget::keyPressEvent(QKeyEvent *event)
{
    bool keyHandled = false;

    // --- Handle Alignment Keys (F, A, M) ---
    if (m_selectedNodeIndices.size() == 1 &&
        (event->key() == Qt::Key_F || event->key() == Qt::Key_A || event->key() == Qt::Key_M))
    {
        int nodeIndex = *m_selectedNodeIndices.constBegin();
        QVector<CurveNode>& activeNodes = getActiveNodes();
        if (nodeIndex >= 0 && nodeIndex < activeNodes.size()) { // Bounds check
            CurveNode& node = activeNodes[nodeIndex];
            HandleAlignment originalMode = node.alignment;
            HandleAlignment newMode = originalMode;
            switch (event->key()) {
            case Qt::Key_F: newMode = HandleAlignment::Free; break;
            case Qt::Key_A: newMode = HandleAlignment::Aligned; break;
            case Qt::Key_M: newMode = HandleAlignment::Mirrored; break;
            default: break;
            }
            if (newMode != originalMode) {
                // Use setNodeAlignment logic internally (includes undo etc)
                setNodeAlignment(nodeIndex, newMode); // This handles undo, signals, update
                keyHandled = true;
            }
        }
    }
    // --- Handle Delete Key ---
    else if (event->key() == Qt::Key_Delete && !m_selectedNodeIndices.isEmpty())
    {
        m_stateBeforeAction = m_channelNodes;
        QVector<CurveNode>& activeNodes = getActiveNodes();
        bool nodesWereRemoved = false;
        QList<int> indicesToRemove = m_selectedNodeIndices.values();
        std::sort(indicesToRemove.begin(), indicesToRemove.end(), std::greater<int>()); // Sort descending

        for (int index : indicesToRemove) {
            if (index > 0 && index < activeNodes.size() - 1) { // Don't delete endpoints
                activeNodes.remove(index);
                nodesWereRemoved = true;
            }
        }

        if (nodesWereRemoved) {
            m_undoStack.push(new SetCurveStateCommand(this, m_stateBeforeAction, m_channelNodes, "Delete Node(s)"));
            m_selectedNodeIndices.clear(); // Clear selection after deleting
            m_currentDrag = {SelectedPart::NONE, -1};
            m_dragging = false;
            update();
            emit curveChanged();
            emit selectionChanged();
            keyHandled = true;
        } else {
            m_stateBeforeAction.clear(); // No change occurred
        }
    }

    // --- Handle Undo/Redo ---
    if (!keyHandled) {
        if (event->matches(QKeySequence::Undo)) {
            m_undoStack.undo();
            keyHandled = true;
        } else if (event->matches(QKeySequence::Redo)) {
            m_undoStack.redo();
            keyHandled = true;
        }
    }

    // --- Final Event Propagation ---
    if (keyHandled) {
        event->accept();
    } else {
        QWidget::keyPressEvent(event);
    }
}


// --- Protected Method for Undo/Redo ---

/**
 * @brief Restores the internal state of all channel nodes (called by Undo command).
 */
void CurveWidget::restoreAllChannelNodes(const QMap<ActiveChannel, QVector<CurveNode>>& allNodes) {
    // Basic check if state actually changed (could be more thorough)
    if (m_channelNodes == allNodes) {
        qDebug() << "Undo/Redo: State appears unchanged.";
        // Should still reset interaction state? Yes.
    }
    m_channelNodes = allNodes; // Assign the restored state map

    // Reset interaction state completely
    m_selectedNodeIndices.clear();
    m_currentDrag = {SelectedPart::NONE, -1};
    m_dragging = false;
    m_isBoxSelecting = false;
    m_boxSelectionRect = QRect();
    m_stateBeforeAction.clear(); // Clear any pending action state

    update(); // Redraw the widget
    emit curveChanged(); // Signal that curve data has changed
    emit selectionChanged(); // Signal selection is now empty
    qDebug() << "Curve state restored via Undo/Redo.";
}


// --- Private Helper Functions ---

/**
 * @brief Gets a non-const reference to the node vector for the currently active channel.
 * @throws std::out_of_range if the active channel is not found (should not happen).
 */
QVector<CurveWidget::CurveNode>& CurveWidget::getActiveNodes() {
    auto it = m_channelNodes.find(m_activeChannel);
    if (it == m_channelNodes.end()) {
        throw std::out_of_range("Active channel not found in m_channelNodes");
    }
    return it.value(); // Return reference to the QVector
}

/**
 * @brief Gets a const reference to the node vector for the currently active channel.
 * @throws std::out_of_range if the active channel is not found (should not happen).
 */
const QVector<CurveWidget::CurveNode>& CurveWidget::getActiveNodes() const {
    auto it = m_channelNodes.find(m_activeChannel);
    if (it == m_channelNodes.end()) {
        throw std::out_of_range("Active channel not found in m_channelNodes (const)");
    }
    return it.value(); // Return const reference
}

/**
 * @brief Maps a logical point (0-1 range) to widget pixel coordinates.
 */
QPointF CurveWidget::mapToWidget(const QPointF& logicalPoint) const
{
    const qreal margin = m_mainPointRadius + 2.0; // Margin around the drawable area
    qreal usableWidth = std::max(1.0, width() - 2.0 * margin);
    qreal usableHeight = std::max(1.0, height() - 2.0 * margin);

    qreal widgetX = margin + logicalPoint.x() * usableWidth;
    // Y coordinate is inverted: logical 0 = bottom, logical 1 = top
    qreal widgetY = margin + (1.0 - logicalPoint.y()) * usableHeight;

    return QPointF(widgetX, widgetY);
}

/**
 * @brief Maps widget pixel coordinates back to logical (0-1 range). Clamps output.
 */
QPointF CurveWidget::mapFromWidget(const QPoint& widgetPoint) const
{
    const qreal margin = m_mainPointRadius + 2.0;
    qreal usableWidth = width() - 2.0 * margin;
    qreal usableHeight = height() - 2.0 * margin;

    // Avoid division by zero or very small numbers if widget is tiny
    if (usableWidth < 1e-6 || usableHeight < 1e-6) {
        return QPointF(0.0, 0.0); // Default point
    }

    qreal logicalX = (static_cast<qreal>(widgetPoint.x()) - margin) / usableWidth;
    // Y coordinate needs inversion
    qreal logicalY = 1.0 - (static_cast<qreal>(widgetPoint.y()) - margin) / usableHeight;

    // Clamp results to logical [0, 1] range here for safety,
    // although specific uses might re-clamp differently.
    // logicalX = std::max(0.0, std::min(1.0, logicalX));
    // logicalY = std::max(0.0, std::min(1.0, logicalY));
    // Let's NOT clamp here, let the calling function decide based on context

    return QPointF(logicalX, logicalY);
}

/**
 * @brief Finds the closest interactive part (main point or handle) on the *active* curve
 * to a widget position. Used to determine what was clicked.
 */
CurveWidget::SelectionInfo CurveWidget::findNearbyPart(const QPoint& widgetPos, qreal mainRadius /*= 10.0*/, qreal handleRadius /*= 8.0*/)
{
    SelectionInfo closest = {SelectedPart::NONE, -1};
    qreal minDistSq = std::numeric_limits<qreal>::max(); // Use squared distance

    const QVector<CurveNode>& activeNodes = getActiveNodes();
    if (activeNodes.isEmpty()) return closest;

    QPointF widgetPosF = QPointF(widgetPos); // Convert click pos once

    // Iterate through the active channel's nodes
    for (int i = 0; i < activeNodes.size(); ++i) {
        const CurveNode& node = activeNodes[i];

        // Check Handles first (smaller hit radius)
        if (i > 0) { // Check Handle In
            QPointF handleInWidget = mapToWidget(node.handleIn);
            QPointF diffVec = widgetPosF - handleInWidget;
            qreal distSq = QPointF::dotProduct(diffVec, diffVec);
            if (distSq < handleRadius * handleRadius && distSq < minDistSq) {
                minDistSq = distSq;
                closest = {SelectedPart::HANDLE_IN, i};
            }
        }
        if (i < activeNodes.size() - 1) { // Check Handle Out
            QPointF handleOutWidget = mapToWidget(node.handleOut);
            QPointF diffVec = widgetPosF - handleOutWidget;
            qreal distSq = QPointF::dotProduct(diffVec, diffVec);
            if (distSq < handleRadius * handleRadius && distSq < minDistSq) {
                minDistSq = distSq;
                closest = {SelectedPart::HANDLE_OUT, i};
            }
        }

        // Check Main Point last (larger radius, takes precedence if overlapping)
        QPointF mainWidget = mapToWidget(node.mainPoint);
        QPointF diffVec = widgetPosF - mainWidget;
        qreal distSq = QPointF::dotProduct(diffVec, diffVec);
        if (distSq < mainRadius * mainRadius && distSq < minDistSq) {
            minDistSq = distSq;
            closest = {SelectedPart::MAIN_POINT, i};
        }
    }
    return closest;
}

/**
 * @brief Finds the closest point on the *active* curve to a widget position,
 * returning segment index and parameter t. Used for inserting new nodes.
 */
CurveWidget::ClosestSegmentResult CurveWidget::findClosestSegment(const QPoint& widgetPos) const
{
    ClosestSegmentResult bestMatch;
    const QVector<CurveNode>& activeNodes = getActiveNodes();
    if (activeNodes.size() < 2) return bestMatch; // Need at least one segment

    QPointF widgetPosF = QPointF(widgetPos);
    const int stepsPerSegment = 20; // Sampling density for distance check
    qreal minDistanceSq = std::numeric_limits<qreal>::max();

    // Iterate through segments of the active curve
    for (int i = 0; i < activeNodes.size() - 1; ++i) {
        const CurveNode& node0 = activeNodes[i];
        const CurveNode& node1 = activeNodes[i+1];
        const QPointF p0 = node0.mainPoint; const QPointF p1 = node0.handleOut;
        const QPointF p2 = node1.handleIn; const QPointF p3 = node1.mainPoint;

        // Sample points along the current Bézier segment
        for (int j = 0; j <= stepsPerSegment; ++j) {
            qreal t = static_cast<qreal>(j) / stepsPerSegment;
            QPointF pointOnCurveLogical = ::evaluateBezier(p0, p1, p2, p3, t);
            QPointF pointOnCurveWidget = mapToWidget(pointOnCurveLogical);
            QPointF diffVec = widgetPosF - pointOnCurveWidget;
            qreal distSq = QPointF::dotProduct(diffVec, diffVec);

            if (distSq < minDistanceSq) { // Found a closer point
                minDistanceSq = distSq;
                bestMatch.segmentIndex = i; // Index of the segment's start node
                bestMatch.t = t;           // Parameter along the segment
                bestMatch.distanceSq = distSq;
            }
        }
    }
    // Optional: Could refine 't' here for more accuracy if needed
    return bestMatch;
}

/**
 * @brief Applies alignment snap. Calculates TARGET handle based on SOURCE handle.
 * @param nodeIndex Index of the node.
 * @param movedHandlePart The handle considered the 'source' for the calculation.
 * @param clampTarget If true, the calculated target handle position will be clamped to [0,1].
 */
void CurveWidget::applyAlignmentSnap(int nodeIndex, CurveWidget::SelectedPart movedHandlePart, bool clampTarget /*= true*/) {
    QVector<CurveNode>& activeNodes = getActiveNodes();
    if (nodeIndex <= 0 || nodeIndex >= activeNodes.size() - 1) return;
    if (movedHandlePart != SelectedPart::HANDLE_IN && movedHandlePart != SelectedPart::HANDLE_OUT) return;

    CurveNode& node = activeNodes[nodeIndex];
    if (node.alignment == HandleAlignment::Free) return;

    const QPointF& mainPt = node.mainPoint;
    QPointF* hSource = (movedHandlePart == SelectedPart::HANDLE_IN) ? &node.handleIn : &node.handleOut;
    QPointF* hTarget = (movedHandlePart == SelectedPart::HANDLE_IN) ? &node.handleOut : &node.handleIn;

    QPointF vecSource = *hSource - mainPt;
    qreal lenSourceSq = QPointF::dotProduct(vecSource, vecSource);
    QPointF newTargetPos;

    if (lenSourceSq < 1e-12) {
        newTargetPos = mainPt;
    } else {
        qreal lenSource = qSqrt(lenSourceSq);
        QPointF normDirTarget = -vecSource / lenSource; // Direction for Target

        if (node.alignment == HandleAlignment::Aligned) {
            QPointF vecTargetOld = *hTarget - mainPt;
            qreal lenTargetOld = qSqrt(QPointF::dotProduct(vecTargetOld, vecTargetOld));
            if(lenTargetOld < 1e-9) lenTargetOld = 0.0;
            newTargetPos = mainPt + normDirTarget * lenTargetOld;
        } else { // Mirrored
            newTargetPos = mainPt + normDirTarget * lenSource;
        }
    }

    // *** Apply CLAMPING only if requested ***
    if (clampTarget) {
        hTarget->setX(std::max(0.0, std::min(1.0, newTargetPos.x())));
        hTarget->setY(std::max(0.0, std::min(1.0, newTargetPos.y())));
    } else {
        // Set the calculated position directly without clamping
        *hTarget = newTargetPos;
    }
}

/**
 * @brief Sorts the nodes vector of the *active* channel by the main point's X-coordinate.
 * Ensures first and last nodes remain fixed at X=0 and X=1 respectively.
 * WARNING: May invalidate indices stored elsewhere if node order changes.
 */
void CurveWidget::sortActiveNodes() {
    QVector<CurveNode>& activeNodes = getActiveNodes();
    if (activeNodes.size() <= 1) return;

    // Store original endpoints
    CurveNode firstNode = activeNodes.first();
    CurveNode lastNode = activeNodes.last();

    // Sort intermediate nodes (if any)
    if (activeNodes.size() > 2) {
        std::sort(activeNodes.begin() + 1, activeNodes.end() - 1,
                  [](const CurveNode& a, const CurveNode& b) {
                      // Use fuzzy compare for robustness? Maybe not needed if constraints work.
                      return a.mainPoint.x() < b.mainPoint.x();
                  });
    }

    // Restore fixed endpoints and ensure their X values are exact
    activeNodes.first() = firstNode;
    activeNodes.first().mainPoint.setX(0.0);
    activeNodes.last() = lastNode;
    activeNodes.last().mainPoint.setX(1.0);

    // Note: If sorting happens during multi-drag, m_selectedNodeIndices
    // needs to be updated, which is complex. Current implementation might
    // lose track of selection if nodes reorder significantly.
    qDebug() << "Warning: sortActiveNodes called - selection indices may be invalid if order changed.";
}
