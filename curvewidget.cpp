#include "curvewidget.h"
#include "setcurvestatecommand.h" // <-- Needs update to handle QMap state!

#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QPainterPath>
#include <QPalette>
#include <QtMath>
#include <QtCore/qnumeric.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <QDebug>
#include <QList> // For initializer list iteration

// --- Anonymous Namespace for Local File Helpers ---
namespace {

/**
 * @brief Linearly interpolates between two points.
 */
inline QPointF lerp(const QPointF& a, const QPointF& b, qreal t) {
    return a * (1.0 - t) + b * t;
}

/**
 * @brief Structure to hold the results of De Casteljau subdivision.
 */
struct SubdivisionResult {
    QPointF pointOnCurve; // P_new (main point for the new node)
    QPointF handle1_Seg1; // P01    (becomes handleOut for node i)
    QPointF handle2_Seg1; // P012   (becomes handleIn for new node)
    QPointF handle1_Seg2; // P123   (becomes handleOut for new node)
    QPointF handle2_Seg2; // P23    (becomes handleIn for node i+1)
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
    // Using Horner's method for potentially better numerical stability / efficiency:
    // P(t) = (1-t)^3*P0 + 3*(1-t)^2*t*P1 + 3*(1-t)*t^2*P2 + t^3*P3
    return p0 * mt * mt2 + p1 * 3.0 * mt2 * t + p2 * 3.0 * mt * t2 + p3 * t * t2;
}

// Function to get the X derivative of a Bezier curve w.r.t. t
qreal evaluateBezierXDerivative(const QPointF& p0, const QPointF& p1, const QPointF& p2, const QPointF& p3, qreal t) {
    qreal mt = 1.0 - t;
    // B'(t) = 3(1-t)^2(P1-P0) + 6(1-t)t(P2-P1) + 3t^2(P3-P2)
    return 3.0 * mt * mt * (p1.x() - p0.x()) +
           6.0 * mt * t * (p2.x() - p1.x()) +
           3.0 * t * t * (p3.x() - p2.x());
}


} // End anonymous namespace


// --- CurveNode Implementation ---

CurveWidget::CurveNode::CurveNode(QPointF p)
    : mainPoint(p),
    handleIn(p),
    handleOut(p),
    alignment(HandleAlignment::Aligned) // Default alignment
{}

bool CurveWidget::CurveNode::operator==(const CurveNode& other) const {
    // Use fuzzy compare for points due to potential floating point inaccuracies
    const qreal epsilon = 1e-9;
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
    m_activeChannel(ActiveChannel::RED), // Default to Red
    m_dragging(false),
    m_selection({SelectedPart::NONE, -1}),
    m_mainPointRadius(5.0),
    m_handleRadius(4.0),
    m_isDarkMode(false) // Default to light mode
{
    // Initialize default curves for R, G, B
    QList<ActiveChannel> channels = {ActiveChannel::RED, ActiveChannel::GREEN, ActiveChannel::BLUE};
    for (ActiveChannel channel : channels) {
        QVector<CurveNode> defaultNodes;
        CurveNode node0(QPointF(0.0, 0.0));
        CurveNode node1(QPointF(1.0, 1.0));
        // Initial straight line handles (adjust if needed)
        node0.handleOut = QPointF(1.0/3.0, 0.0);
        node1.handleIn  = QPointF(2.0/3.0, 1.0);

        // Ensure endpoints are not aligned/mirrored initially if they have coincident handles
        node0.alignment = HandleAlignment::Free;
        node1.alignment = HandleAlignment::Free;

        defaultNodes << node0 << node1;
        m_channelNodes.insert(channel, defaultNodes);
    }

    setMinimumSize(200, 200);
    setFocusPolicy(Qt::ClickFocus);
    setAutoFillBackground(true);
}


// --- Public Methods ---

/**
 * @brief Gets a copy of the nodes for all channels.
 * @return QMap containing the curve nodes for each channel.
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
 * Uses iterative solving to find the Bézier parameter t for the given x.
 * @param channel - The channel (R, G, B) to sample.
 * @param x - The X-coordinate (0.0 to 1.0) to sample.
 * @return Approximate Y-coordinate (0.0 to 1.0) on the specified channel's curve.
 */
qreal CurveWidget::sampleCurveChannel(ActiveChannel channel, qreal x) const {
    // Clamp input x to [0, 1]
    x = std::max(0.0, std::min(1.0, x));

    // Get the correct nodes for the requested channel
    auto it = m_channelNodes.find(channel);
    if (it == m_channelNodes.end() || it.value().size() < 2) {
        qWarning() << "CurveWidget::sampleCurveChannel - Channel not found or insufficient nodes. Returning linear.";
        return x; // Fallback: return linear value (input x)
    }
    const QVector<CurveNode>& nodes = it.value(); // Use const reference

    // --- Accurate Bézier Sampling by X ---
    // 1. Find the correct Bézier segment based on x
    int segmentIndex = -1;
    for (int i = 0; i < nodes.size() - 1; ++i) {
        // Check assumption: nodes MUST be sorted by mainPoint.x()
        if (x >= nodes[i].mainPoint.x() && x <= nodes[i+1].mainPoint.x()) {
            // Handle vertical segments where start/end X are equal (or very close)
            if (qFuzzyCompare(nodes[i].mainPoint.x(), nodes[i+1].mainPoint.x())) {
                // If x matches the vertical line's x, interpolate or choose endpoint Y
                if (qFuzzyCompare(x, nodes[i].mainPoint.x())) {
                    // Linearly interpolate Y based on assumed t (0..1 -> Y0..Y1)
                    // This is ambiguous. A simpler approach is to return the lower Y value?
                    // Or average? Let's return start Y for now if exactly at start X.
                    // Consider a more robust strategy if vertical segments are expected.
                    // Let's linearly interpolate between the Y values if X matches
                    qreal y_lerp = lerp(nodes[i].mainPoint, nodes[i+1].mainPoint, 0.5).y(); // Arbitrarily pick middle
                    // Maybe better: find t corresponding to X closest to node i/i+1 handles?
                    // For now, let's just pick node i's y if X matches segment X
                    return nodes[i].mainPoint.y();
                }
                // If x is theoretically on this vertical line but not exactly matching
                // the node x due to float issues, we might fall through.
            }
            segmentIndex = i;
            break;
        }
    }

    // Handle edge cases: x before first node or after last node (or not found within any segment)
    if (segmentIndex == -1) {
        if (x <= nodes.first().mainPoint.x()) { // Includes qFuzzyCompare check implicitly
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

    // Improve initial guess if segment isn't vertical
    if (std::abs(segmentXRange) > 1e-9) {
        t_guess = (x - p0.x()) / segmentXRange;
        t_guess = std::max(0.0, std::min(1.0, t_guess)); // Clamp initial guess
    } else {
        // If segment is effectively vertical, t is not well-defined by X.
        // We likely returned earlier if x matched p0.x exactly.
        // If we are here, it implies x is within the bounds but the segment is vertical.
        // Fallback: linearly interpolate Y based on X (which is constant). Difficult.
        // Let's just return the starting node's Y as a fallback here.
        qWarning("Sampling near vertical segment, result might be inaccurate.");
        return p0.y();
    }


    const int MAX_ITERATIONS = 15; // Max iterations for solver
    const qreal TOLERANCE_X = 1e-7; // Tolerance for matching x
    qreal t = t_guess;

    for (int iter = 0; iter < MAX_ITERATIONS; ++iter) {
        // Calculate the curve's X at the current t
        qreal currentX = evaluateBezier(p0, p1, p2, p3, t).x();
        qreal error = currentX - x;

        // Check for convergence
        if (std::abs(error) < TOLERANCE_X) {
            break;
        }

        // Calculate the derivative of the X component with respect to t
        qreal dXdt = evaluateBezierXDerivative(p0, p1, p2, p3, t);

        // Avoid division by zero or very small numbers (Newton's method instability)
        if (std::abs(dXdt) < 1e-7) {
            // Derivative is too small, Newton step won't work well.
            // Could switch to bisection, but for now, let's just break.
            // The result might be less accurate if convergence wasn't reached.
            qWarning("Newton's method encountered near-zero derivative during sampling.");
            break;
        }

        // Perform Newton-Raphson step
        t = t - error / dXdt;

        // Clamp t to the valid range [0, 1] after each step
        t = std::max(0.0, std::min(1.0, t));
    }
    // Safety clamp t one last time
    t = std::max(0.0, std::min(1.0, t));

    // 3. Evaluate BezierY(t) using the found/approximated t
    qreal resultY = evaluateBezier(p0, p1, p2, p3, t).y();

    // Clamp final output Y to [0, 1]
    return std::max(0.0, std::min(1.0, resultY));
}

/**
 * @brief Resets the *active* curve to its default state (straight line 0,0 to 1,1).
 * This action is undoable.
 */
void CurveWidget::resetCurve() {
    // Capture state of ALL channels BEFORE reset for Undo
    m_stateBeforeAction = m_channelNodes;

    // Get reference to the active channel's node vector
    QVector<CurveNode>& activeNodes = getActiveNodes();
    activeNodes.clear(); // Clear the active node vector

    // Re-initialize active channel with default nodes
    CurveNode node0(QPointF(0.0, 0.0));
    CurveNode node1(QPointF(1.0, 1.0));
    node0.handleOut = QPointF(1.0/3.0, 0.0);
    node1.handleIn  = QPointF(2.0/3.0, 1.0);
    node0.alignment = HandleAlignment::Free;
    node1.alignment = HandleAlignment::Free;
    activeNodes << node0 << node1;

    // Create the new state map AFTER modification
    QMap<ActiveChannel, QVector<CurveNode>> newState = m_channelNodes;

    // Push command only if state actually changed
    // Need a reliable way to compare QMaps of QVectors of CurveNodes (operator== for CurveNode helps)
    bool stateChanged = false;
    if (m_stateBeforeAction.keys() != newState.keys()) {
        stateChanged = true;
    } else {
        for(auto key : m_stateBeforeAction.keys()) {
            if (m_stateBeforeAction[key] != newState[key]) { // Relies on QVector::operator== and CurveNode::operator==
                stateChanged = true;
                break;
            }
        }
    }


    if (stateChanged) {
        // Push command with the complete old/new state maps
        m_undoStack.push(new SetCurveStateCommand(this, m_stateBeforeAction, newState, "Reset Curve"));
    } else {
        m_stateBeforeAction.clear(); // No change, clear captured state
    }

    // Update internal state and UI
    m_selection = {SelectedPart::NONE, -1};
    m_dragging = false;
    update();
    emit curveChanged();
    emit selectionChanged(-1, HandleAlignment::Free); // Emit deselection
}

/**
 * @brief Sets the dark mode flag to adjust drawing colors.
 */
void CurveWidget::setDarkMode(bool dark) {
    if (m_isDarkMode != dark) {
        m_isDarkMode = dark;
        update(); // Trigger repaint to use new colors
    }
}

// --- Public Slots ---

/**
 * @brief Sets the handle alignment mode for a specific node in the *active* channel. Undoable.
 * @param nodeIndex - Index of the node to modify within the active channel's node list.
 * @param mode - The new HandleAlignment mode.
 */
void CurveWidget::setNodeAlignment(int nodeIndex, CurveWidget::HandleAlignment mode) {
    QVector<CurveNode>& activeNodes = getActiveNodes(); // Get ref

    // Check bounds for the active channel
    if (nodeIndex < 0 || nodeIndex >= activeNodes.size()) {
        qWarning() << "setNodeAlignment: Invalid index" << nodeIndex << "for active channel.";
        return;
    }

    CurveNode& node = activeNodes[nodeIndex]; // Get reference to the node
    if (node.alignment != mode) {
        // Action will happen, prepare for Undo by capturing state of ALL channels
        m_stateBeforeAction = m_channelNodes;

        node.alignment = mode; // Apply the new mode
        applyAlignmentSnap(nodeIndex); // Apply snap logic AFTER setting mode (operates on activeNodes)

        QMap<ActiveChannel, QVector<CurveNode>> newState = m_channelNodes; // Capture state AFTER change

        // Push command only if state actually changed
        bool stateChanged = false; // Recalculate state change, snap might have changed other nodes indirectly
        if (m_stateBeforeAction.keys() != newState.keys()) { stateChanged = true; }
        else {
            for(auto key : m_stateBeforeAction.keys()) {
                if (m_stateBeforeAction[key] != newState[key]) { stateChanged = true; break; }
            }
        }

        if (stateChanged) {
            m_undoStack.push(new SetCurveStateCommand(this, m_stateBeforeAction, newState, "Change Alignment"));
        } else {
            m_stateBeforeAction.clear();
        }

        // Update UI
        update();
        emit curveChanged(); // Curve shape might have changed due to snap
        emit selectionChanged(nodeIndex, mode); // Update button states in MainWindow
    }
}

/**
 * @brief Sets the currently active channel for editing and viewing.
 * @param channel - The channel to activate (RED, GREEN, or BLUE).
 */
void CurveWidget::setActiveChannel(CurveWidget::ActiveChannel channel) {
    // Check if the channel exists in our map (it always should with current setup)
    if (!m_channelNodes.contains(channel)) {
        qWarning() << "Attempted to set invalid active channel:" << static_cast<int>(channel);
        return;
    }

    if (m_activeChannel != channel) {
        m_activeChannel = channel;
        m_selection = {SelectedPart::NONE, -1}; // Clear selection when changing channel
        m_dragging = false; // Stop any dragging action
        update(); // Redraw to show the new active curve and potentially change colors
        emit selectionChanged(-1, HandleAlignment::Free); // Signal deselection
        // Optional: emit activeChannelChanged(m_activeChannel);
        qDebug() << "Active channel set to:" << static_cast<int>(m_activeChannel);
    }
}


// --- Protected Event Handlers ---

/**
 * @brief Handles painting the widget. Draws grid, active curve, nodes, handles,
 * and optionally inactive curves. Uses m_isDarkMode to select appropriate colors.
 */
void CurveWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // --- Define Colors ---
    QColor gridColor, borderColor, handleLineColor, handleColor, mainPointColor, outlineColor;
    QColor activeCurveColor, inactiveCurveColorBase;
    QColor selectionColor = Qt::yellow; // Color for selected parts

    if (m_isDarkMode) {
        gridColor = QColor(80, 80, 80); borderColor = QColor(90, 90, 90);
        handleLineColor = QColor(100, 100, 100); handleColor = QColor(0, 190, 190); // Tealish handle
        mainPointColor = QColor(230, 230, 230); outlineColor = Qt::black;
        activeCurveColor = QColor(240, 240, 240); // White/Light gray for active curve
        inactiveCurveColorBase = QColor(100, 100, 100, 150); // Semi-transparent gray base for inactive
    } else { // Light Mode
        gridColor = QColor(210, 210, 210); borderColor = QColor(180, 180, 180);
        handleLineColor = QColor(140, 140, 140); handleColor = QColor(0, 100, 100); // Darker tealish
        mainPointColor = QColor(10, 10, 10); outlineColor = Qt::darkGray;
        activeCurveColor = QColor(10, 10, 10); // Black/Dark gray for active curve
        inactiveCurveColorBase = QColor(160, 160, 160, 150); // Semi-transparent gray base for inactive
    }

    // Map channel to color for inactive curves
    auto getChannelColor = [&](ActiveChannel ch) -> QColor {
        switch(ch) {
        case ActiveChannel::RED:   return QColor(255, 80, 80, inactiveCurveColorBase.alpha()); // Dim Red
        case ActiveChannel::GREEN: return QColor(80, 255, 80, inactiveCurveColorBase.alpha()); // Dim Green
        case ActiveChannel::BLUE:  return QColor(80, 80, 255, inactiveCurveColorBase.alpha()); // Dim Blue
        default: return inactiveCurveColorBase; // Fallback
        }
    };

    // --- Draw Grid & Border ---
    painter.setPen(QPen(gridColor, 0.5));
    int numGridLines = 10;
    for (int i = 1; i < numGridLines; ++i) {
        qreal ratio = static_cast<qreal>(i) / numGridLines;
        painter.drawLine(mapToWidget(QPointF(ratio, 1.0)), mapToWidget(QPointF(ratio, 0.0)));
        painter.drawLine(mapToWidget(QPointF(0.0, ratio)), mapToWidget(QPointF(1.0, ratio)));
    }
    painter.setPen(QPen(borderColor, 1));
    painter.drawRect(rect().adjusted(0, 0, -1, -1)); // Draw border slightly inside

    // --- Draw Inactive Curves (Optional) ---
    /* // Uncomment this block to draw inactive curves
    painter.save();
    painter.setPen(QPen(Qt::NoPen)); // No outline for inactive usually
    for (auto it = m_channelNodes.constBegin(); it != m_channelNodes.constEnd(); ++it) {
        if (it.key() == m_activeChannel) continue; // Skip active channel

        const QVector<CurveNode>& nodes = it.value();
        if (nodes.size() < 2) continue;

        QPainterPath inactivePath;
        inactivePath.moveTo(mapToWidget(nodes[0].mainPoint));
        for (int i = 0; i < nodes.size() - 1; ++i) {
            inactivePath.cubicTo(mapToWidget(nodes[i].handleOut),
                                 mapToWidget(nodes[i+1].handleIn),
                                 mapToWidget(nodes[i+1].mainPoint));
        }
        painter.setPen(QPen(getChannelColor(it.key()), 1.5, Qt::DotLine)); // Thinner, dotted line
        painter.drawPath(inactivePath);
    }
    painter.restore();
    */

    // --- Draw Active Curve ---
    const QVector<CurveNode>& activeNodes = getActiveNodes(); // Get const ref
    if (activeNodes.size() < 2) return; // Cannot draw curve with < 2 nodes

    QPainterPath curvePath;
    curvePath.moveTo(mapToWidget(activeNodes[0].mainPoint));
    for (int i = 0; i < activeNodes.size() - 1; ++i) {
        curvePath.cubicTo(mapToWidget(activeNodes[i].handleOut),
                          mapToWidget(activeNodes[i+1].handleIn),
                          mapToWidget(activeNodes[i+1].mainPoint));
    }
    // Use activeCurveColor, potentially tint it slightly by channel if desired?
    // QColor currentCurveColor = activeCurveColor; // Keep it simple for now
    painter.setPen(QPen(activeCurveColor, 2)); // Thicker line for active curve
    painter.drawPath(curvePath);

    // --- Draw Nodes and Handles for Active Curve ---
    for (int i = 0; i < activeNodes.size(); ++i) {
        const CurveNode& node = activeNodes[i];
        QPointF mainWidgetPos = mapToWidget(node.mainPoint);

        // Draw Handle Lines (only for active curve)
        painter.setPen(QPen(handleLineColor, 1));
        if (i > 0) painter.drawLine(mainWidgetPos, mapToWidget(node.handleIn));
        if (i < activeNodes.size() - 1) painter.drawLine(mainWidgetPos, mapToWidget(node.handleOut));

        // Draw Handles (only for active curve)
        painter.setPen(QPen(outlineColor, 0.5));
        if (i > 0) { // Handle In exists only for nodes > 0
            bool isSelected = (m_selection.part == SelectedPart::HANDLE_IN && m_selection.nodeIndex == i);
            painter.setBrush(isSelected ? selectionColor : handleColor);
            painter.drawEllipse(mapToWidget(node.handleIn), m_handleRadius, m_handleRadius);
        }
        if (i < activeNodes.size() - 1) { // Handle Out exists only for nodes < size-1
            bool isSelected = (m_selection.part == SelectedPart::HANDLE_OUT && m_selection.nodeIndex == i);
            painter.setBrush(isSelected ? selectionColor : handleColor);
            painter.drawEllipse(mapToWidget(node.handleOut), m_handleRadius, m_handleRadius);
        }

        // Draw Main Point (for active curve)
        painter.setPen(QPen(outlineColor, 1));
        bool isMainSelected = (m_selection.part == SelectedPart::MAIN_POINT && m_selection.nodeIndex == i);
        painter.setBrush(isMainSelected ? selectionColor : mainPointColor);
        painter.drawEllipse(mainWidgetPos, m_mainPointRadius, m_mainPointRadius);
    }
}

/**
 * @brief Handles mouse button press events. Selects parts on the active curve,
 * initiates dragging, adds nodes to the active curve, or deletes nodes from it.
 * Manages state capture for Undo (captures state of all channels).
 */
void CurveWidget::mousePressEvent(QMouseEvent *event)
{
    SelectionInfo oldSelection = m_selection;
    // findNearbyPart operates on the active channel via getActiveNodes() called inside it
    m_selection = findNearbyPart(event->pos());
    m_dragging = (m_selection.part != SelectedPart::NONE);
    m_stateBeforeAction.clear(); // Clear previous stored state before potential action

    const QVector<CurveNode>& activeNodesConst = getActiveNodes(); // Need const version for checks

    // Emit selection change signal if necessary
    if (m_selection.nodeIndex != oldSelection.nodeIndex || m_selection.part != oldSelection.part) {
        if (m_selection.part != SelectedPart::NONE) {
            // Bounds check against activeNodes size
            if (m_selection.nodeIndex >= 0 && m_selection.nodeIndex < activeNodesConst.size()) {
                emit selectionChanged(m_selection.nodeIndex, activeNodesConst[m_selection.nodeIndex].alignment);
            } else {
                // This case indicates an internal error if findNearbyPart returned an invalid index
                qWarning() << "MousePressEvent: Invalid selection index found.";
                m_selection = {SelectedPart::NONE, -1}; m_dragging = false;
                emit selectionChanged(-1, HandleAlignment::Free);
            }
        } else {
            emit selectionChanged(-1, HandleAlignment::Free); // Deselected
        }
    }

    // --- Handle Left Button Press ---
    if (event->button() == Qt::LeftButton) {
        if (m_dragging) { // Started drag on existing part of the active curve
            m_stateBeforeAction = m_channelNodes; // Store state of ALL channels BEFORE drag
            qDebug() << "Drag started, captured state.";
        } else { // Attempt to Add New Node to the active curve
            // findClosestSegment operates on the active channel
            ClosestSegmentResult hit = findClosestSegment(event->pos());
            const qreal t_tolerance = 0.005; // Avoid adding nodes too close to existing ones
            const qreal max_dist_sq_for_add = 20.0 * 20.0; // Max distance from curve to add node

            if (hit.segmentIndex != -1 && hit.t > t_tolerance && hit.t < (1.0 - t_tolerance) && hit.distanceSq < max_dist_sq_for_add)
            {
                // Capture state BEFORE add attempt
                m_stateBeforeAction = m_channelNodes;

                int i = hit.segmentIndex; qreal t = hit.t;
                // Get non-const ref to modify
                QVector<CurveNode>& activeNodes = getActiveNodes();

                if (i < 0 || i >= activeNodes.size() - 1) { // Safety check
                    m_stateBeforeAction.clear(); return;
                }
                const QPointF p0 = activeNodes[i].mainPoint; const QPointF p1 = activeNodes[i].handleOut;
                const QPointF p2 = activeNodes[i+1].handleIn; const QPointF p3 = activeNodes[i+1].mainPoint;

                SubdivisionResult split = subdivideBezier(p0, p1, p2, p3, t); // Use helper

                CurveNode newNode(split.pointOnCurve);
                newNode.handleIn = split.handle2_Seg1; newNode.handleOut = split.handle1_Seg2;
                // New node usually starts as Aligned, or perhaps Free? Let's use Aligned.
                newNode.alignment = HandleAlignment::Aligned;

                // Update existing nodes' handles
                activeNodes[i].handleOut = split.handle1_Seg1;
                activeNodes[i+1].handleIn = split.handle2_Seg2;

                // Insert the new node into the active channel's vector
                activeNodes.insert(i + 1, newNode);

                QMap<ActiveChannel, QVector<CurveNode>> newState = m_channelNodes; // Capture state AFTER add

                // Push Undo command if state changed (should always change on add)
                bool stateChanged = true; // Assume changed on add
                // Optionally add comparison check here if needed

                if (stateChanged) {
                    m_undoStack.push(new SetCurveStateCommand(this, m_stateBeforeAction, newState, "Add Node"));
                    qDebug() << "Add Node undo command pushed.";
                } else { m_stateBeforeAction.clear(); }

                // Update internal state: Select the new node, and start dragging it immediately
                m_selection = {SelectedPart::MAIN_POINT, i + 1};
                m_dragging = true; // Explicitly false, don't drag immediately after add
                update();
                emit curveChanged();
                emit selectionChanged(m_selection.nodeIndex, newNode.alignment);
            } else {
                m_stateBeforeAction.clear(); // No add action occurred
                // If clicked on empty space, deselect
                if (m_selection.part == SelectedPart::NONE && oldSelection.part != SelectedPart::NONE) {
                    emit selectionChanged(-1, HandleAlignment::Free);
                }
            }
        }
        // --- Handle Right Button Press (Delete) ---
    } else if (event->button() == Qt::RightButton) {
        // Check if clicked on a deletable main point (not first or last) of the active curve
        if (m_selection.part == SelectedPart::MAIN_POINT &&
            m_selection.nodeIndex > 0 &&
            m_selection.nodeIndex < activeNodesConst.size() - 1) // Check bounds
        {
            // Capture state BEFORE delete
            m_stateBeforeAction = m_channelNodes;
            int deletedIndex = m_selection.nodeIndex;

            // Get non-const ref to modify
            QVector<CurveNode>& activeNodes = getActiveNodes();
            activeNodes.remove(deletedIndex); // Remove from active channel vector

            QMap<ActiveChannel, QVector<CurveNode>> newState = m_channelNodes; // Capture state AFTER delete

            // Push Undo command (state always changes on delete)
            m_undoStack.push(new SetCurveStateCommand(this, m_stateBeforeAction, newState, "Delete Node"));
            qDebug() << "Delete Node undo command pushed.";

            // Update state
            m_selection = {SelectedPart::NONE, -1};
            m_dragging = false;
            update();
            emit curveChanged();
            emit selectionChanged(-1, HandleAlignment::Free); // Deselect
        } else {
            m_stateBeforeAction.clear(); // No delete action occurred
        }
    } else { // Other mouse button
        m_stateBeforeAction.clear();
    }

    // Ensure widget has focus if clicked
    if (!hasFocus()) {
        setFocus();
    }
    QWidget::mousePressEvent(event); // Call base potentially? Maybe not needed.
}


/**
 * @brief Handles mouse move events. Updates position of dragged part on the active curve
 * and applies alignment constraints. This action is part of an undoable sequence
 * started in mousePressEvent.
 */
void CurveWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging || m_selection.part == SelectedPart::NONE) return;

    QVector<CurveNode>& activeNodes = getActiveNodes(); // Get ref

    // Bounds check for safety
    if (m_selection.nodeIndex < 0 || m_selection.nodeIndex >= activeNodes.size()) {
        qWarning() << "MouseMoveEvent: Invalid selection index.";
        m_dragging = false; // Stop dragging if index is bad
        return;
    }

    // Get logical coordinates [0, 1] from widget coordinates
    QPointF logicalPos = mapFromWidget(event->pos());

    CurveNode& node = activeNodes[m_selection.nodeIndex]; // Get ref to the node being modified
    QPointF* pointToMove = nullptr;
    QPointF* oppositeHandle = nullptr; // Only relevant if a handle is moved
    SelectedPart movedPart = m_selection.part;

    // Clamp position Y to [0, 1]
    qreal newY = std::max(0.0, std::min(1.0, logicalPos.y()));
    // Clamp position X differently based on part type
    qreal newX = logicalPos.x();


    // Determine which point to move and apply X constraints
    switch(movedPart) {
    case SelectedPart::MAIN_POINT:
        pointToMove = &node.mainPoint;
        { // Apply X clamping specific to main points
            const qreal epsilon = 1e-6; // Small buffer to prevent exact overlap issues
            if (m_selection.nodeIndex == 0) {
                newX = 0.0; // First node must be at x=0
            } else if (m_selection.nodeIndex == activeNodes.size() - 1) {
                newX = 1.0; // Last node must be at x=1
            } else {
                // Intermediate nodes must stay between neighbors
                qreal minX = activeNodes[m_selection.nodeIndex - 1].mainPoint.x() + epsilon;
                qreal maxX = activeNodes[m_selection.nodeIndex + 1].mainPoint.x() - epsilon;
                // Prevent minX > maxX if neighbors are very close
                if (minX > maxX) minX = maxX = (minX + maxX) / 2.0;
                newX = std::max(minX, std::min(maxX, newX));
            }
            // Final clamp to [0, 1] just in case
            newX = std::max(0.0, std::min(1.0, newX));
        }
        break;

    case SelectedPart::HANDLE_IN:
        if (m_selection.nodeIndex == 0) return; // First node has no handle_in visually/functionally
        pointToMove = &node.handleIn;
        // Opposite handle for alignment logic (only if node is not last)
        if (m_selection.nodeIndex < activeNodes.size() - 1) {
            oppositeHandle = &node.handleOut;
        }
        // Clamp handle X to [0, 1]
        newX = std::max(0.0, std::min(1.0, newX));
        break;

    case SelectedPart::HANDLE_OUT:
        if (m_selection.nodeIndex == activeNodes.size() - 1) return; // Last node has no handle_out
        pointToMove = &node.handleOut;
        // Opposite handle for alignment logic (only if node is not first)
        if (m_selection.nodeIndex > 0) {
            oppositeHandle = &node.handleIn;
        }
        // Clamp handle X to [0, 1]
        newX = std::max(0.0, std::min(1.0, newX));
        break;

    case SelectedPart::NONE:
    default:
        return; // Should not happen if m_dragging is true
    }

    // --- Apply the move ---
    QPointF oldPos = *pointToMove; // Store old position for delta calculation
    pointToMove->setX(newX);
    pointToMove->setY(newY);
    QPointF delta = *pointToMove - oldPos; // Calculate change in position

    // --- Apply secondary effects ---

    // A. If MAIN_POINT moved, move its handles with it
    if (movedPart == SelectedPart::MAIN_POINT) {
        // Move handles only if they weren't coincident with the main point *before* the move
        // (avoids snapping coincident handles apart unintentionally)
        const qreal handleCoincidenceThresholdSq = 1e-12;
        if (QPointF::dotProduct(node.handleIn - oldPos, node.handleIn - oldPos) > handleCoincidenceThresholdSq) {
            node.handleIn += delta;
            // Clamp handle position after moving
            node.handleIn.setX(std::max(0.0, std::min(1.0, node.handleIn.x())));
            node.handleIn.setY(std::max(0.0, std::min(1.0, node.handleIn.y())));
        }
        if (QPointF::dotProduct(node.handleOut - oldPos, node.handleOut - oldPos) > handleCoincidenceThresholdSq) {
            node.handleOut += delta;
            // Clamp handle position after moving
            node.handleOut.setX(std::max(0.0, std::min(1.0, node.handleOut.x())));
            node.handleOut.setY(std::max(0.0, std::min(1.0, node.handleOut.y())));
        }
    }
    // B. If a HANDLE moved, apply alignment constraints to the opposite handle
    else if (oppositeHandle != nullptr && node.alignment != HandleAlignment::Free) {
        const QPointF& mainPt = node.mainPoint;
        QPointF vecMoved = *pointToMove - mainPt; // Vector from main point to the handle that was just moved
        qreal lenMovedSq = QPointF::dotProduct(vecMoved, vecMoved);
        QPointF newOppositePos;

        if (lenMovedSq < 1e-12) { // Handle moved onto the main point
            newOppositePos = mainPt; // Snap opposite handle to main point too
        } else {
            qreal lenMoved = qSqrt(lenMovedSq);
            QPointF normDirOpposite = -vecMoved / lenMoved; // Normalized direction for the opposite handle

            if (node.alignment == HandleAlignment::Aligned) {
                // Keep original length of the opposite handle
                QPointF vecOppositeOld = *oppositeHandle - mainPt;
                qreal lenOppositeOld = qSqrt(QPointF::dotProduct(vecOppositeOld, vecOppositeOld));
                newOppositePos = mainPt + normDirOpposite * lenOppositeOld;
            } else { // Mirrored
                // Match the length of the handle that was moved
                newOppositePos = mainPt + normDirOpposite * lenMoved;
            }
        }
        // Apply the calculated position to the opposite handle and clamp
        oppositeHandle->setX(std::max(0.0, std::min(1.0, newOppositePos.x())));
        oppositeHandle->setY(std::max(0.0, std::min(1.0, newOppositePos.y())));
    }

    // --- Post-Move Updates ---

    // Re-sort active nodes if a main point's X changed order (should only happen if clamping fails)
    if (movedPart == SelectedPart::MAIN_POINT) {
        // Basic check: is the moved node potentially out of order?
        bool needsReSortCheck = (m_selection.nodeIndex > 0 && activeNodes[m_selection.nodeIndex].mainPoint.x() < activeNodes[m_selection.nodeIndex - 1].mainPoint.x()) ||
                                (m_selection.nodeIndex < activeNodes.size() - 1 && activeNodes[m_selection.nodeIndex].mainPoint.x() > activeNodes[m_selection.nodeIndex + 1].mainPoint.x());

        if (needsReSortCheck) {
            qWarning("Node order possibly violated during drag, attempting resort.");
            QPointF mainPointToFind = node.mainPoint; // Store current position to find index after sort
            sortActiveNodes(); // Sorts activeNodes in place

            // Re-find the index of the node being dragged after the sort
            m_selection.nodeIndex = -1;
            for(int i = 0; i < activeNodes.size(); ++i) {
                // Use fuzzy compare for robustness with floating point positions
                if(qFuzzyCompare(activeNodes[i].mainPoint.x(), mainPointToFind.x()) &&
                    qFuzzyCompare(activeNodes[i].mainPoint.y(), mainPointToFind.y()))
                {
                    m_selection.nodeIndex = i;
                    break;
                }
            }
            if (m_selection.nodeIndex == -1) {
                // This should not happen if the node is still in the list!
                qCritical("Lost track of dragged node after sorting!");
                m_dragging = false; // Stop dragging if we lost the node
            }
        }
    }

    // Update UI and signal changes
    update();
    emit curveChanged();
}


/**
 * @brief Handles mouse button release events. Completes dragging operations
 * and pushes the final state change (of all channels) to the undo stack if changes occurred.
 */
void CurveWidget::mouseReleaseEvent(QMouseEvent *event)
{

    bool wasDragging = m_dragging;
    SelectionInfo selectionToKeep = m_selection; // Capture the selection we want to maintain

    if (wasDragging && event->button() == Qt::LeftButton) {
        m_dragging = false;

        bool pushedCommand = false; // Track if command was pushed
        if (!m_stateBeforeAction.isEmpty()) {
            QMap<ActiveChannel, QVector<CurveNode>> currentState = m_channelNodes;
            bool stateChanged = false;
            // ... state comparison logic ...
            if (m_stateBeforeAction.keys() != currentState.keys()) { stateChanged = true; }
            else { /* ... compare vectors ... */
                for(auto key : m_stateBeforeAction.keys()) {
                    if (m_stateBeforeAction[key] != currentState[key]) {
                        stateChanged = true; break;
                    }
                }
            }

            if (stateChanged) {
                // --- Push command (the potential problem point) ---
                m_undoStack.push(new SetCurveStateCommand(this, m_stateBeforeAction, currentState, "Modify Curve"));
                pushedCommand = true; // Mark that we pushed
                qDebug() << "Modify Curve undo command pushed.";
            } else {
                qDebug() << "Mouse Release: Drag occurred but state unchanged; no command pushed.";
            }
            m_stateBeforeAction.clear();
        } else {
            qWarning() << "Mouse Release: Drag finished but no initial state was captured.";
        }

        // *** FIX: Re-assert selection state AFTER potentially pushing command ***
        // If a command was pushed, it might have indirectly caused deselection. Restore it.
        // Also useful even if command wasn't pushed to ensure consistency.
        qDebug() << "Re-asserting selection state after potential command push.";

        // Set m_selection back explicitly (in case it was cleared)
        m_selection = selectionToKeep;

        // Re-emit signal to update UI based on the restored selection
        if (m_selection.part != SelectedPart::NONE && m_selection.nodeIndex != -1) {
            const QVector<CurveNode>& activeNodes = getActiveNodes(); // Use const ref
            if (m_selection.nodeIndex < activeNodes.size()) { // Bounds check
                HandleAlignment currentAlignment = activeNodes[m_selection.nodeIndex].alignment;
                emit selectionChanged(m_selection.nodeIndex, currentAlignment);
                qDebug() << "--- Emitted selectionChanged(" << m_selection.nodeIndex << ") after push.";
            } else {
                qWarning() << "Release: Index invalid after push.";
                m_selection = {SelectedPart::NONE, -1}; // Clear selection if index bad
                emit selectionChanged(-1, HandleAlignment::Free);
                qDebug() << "--- Emitted selectionChanged(-1) after push (bad index).";
            }
        } else {
            // If selectionToKeep was already NONE, ensure that's signalled too
            // (though this shouldn't happen if wasDragging was true)
            emit selectionChanged(-1, HandleAlignment::Free);
            qDebug() << "--- Emitted selectionChanged(-1) after push (selection was already none?).";
        }
        // --- End Fix ---

        // Ensure repaint happens to reflect final state
        update();

    }


    QWidget::mouseReleaseEvent(event);
}

/**
 * @brief Handles widget resize events. Just triggers a repaint as mapping depends on size.
 */
void CurveWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    update();
}

/**
 * @brief Handles key press events. Used here to change handle alignment mode (F, A, M)
 * for the selected node in the active channel. This action is undoable.
 */
void CurveWidget::keyPressEvent(QKeyEvent *event)
{
    // Check if a node is currently selected in the active channel
    bool selectionValid = m_selection.part != SelectedPart::NONE &&
                          m_selection.nodeIndex >= 0; // Index check done below

    bool keyHandled = false; // Flag if F, A, or M was processed

    if (selectionValid) {
        QVector<CurveNode>& activeNodes = getActiveNodes(); // Get ref

        // Check index validity again now we have the vector size
        if (m_selection.nodeIndex >= activeNodes.size()) {
            qWarning() << "KeyPressEvent: Selection index out of bounds.";
            QWidget::keyPressEvent(event); // Pass to base if invalid
            return;
        }

        CurveNode& node = activeNodes[m_selection.nodeIndex]; // Use ref to active node
        HandleAlignment originalMode = node.alignment;
        HandleAlignment newMode = originalMode;
        bool modeChangeKeyPressed = true;

        // Determine new mode based on key press
        switch (event->key()) {
        case Qt::Key_F: newMode = HandleAlignment::Free; break;
        case Qt::Key_A: newMode = HandleAlignment::Aligned; break;
        case Qt::Key_M: newMode = HandleAlignment::Mirrored; break;
        default: modeChangeKeyPressed = false; break; // Not a mode change key
        }

        // If a mode change key was pressed AND it results in a different mode
        if (modeChangeKeyPressed && newMode != originalMode) {
            // Capture state of ALL channels BEFORE change
            m_stateBeforeAction = m_channelNodes;

            node.alignment = newMode; // Apply mode change to the node
            applyAlignmentSnap(m_selection.nodeIndex); // Apply snap logic (might change handles)

            QMap<ActiveChannel, QVector<CurveNode>> newState = m_channelNodes; // Capture state AFTER snap

            // Push command only if state actually changed after potential snap
            bool stateChanged = false;
            if (m_stateBeforeAction.keys() != newState.keys()) { stateChanged = true; }
            else {
                for(auto key : m_stateBeforeAction.keys()) {
                    if (m_stateBeforeAction[key] != newState[key]) { stateChanged = true; break; }
                }
            }

            if (stateChanged) {
                m_undoStack.push(new SetCurveStateCommand(this, m_stateBeforeAction, newState, "Change Alignment"));
                qDebug() << "Change Alignment undo command pushed via key press.";
            } else {
                m_stateBeforeAction.clear(); // Clear state if no effective change
                qDebug() << "Alignment key pressed but state unchanged after snap.";
            }

            // Update UI and signal listeners
            update();
            emit curveChanged(); // Curve shape might have changed
            emit selectionChanged(m_selection.nodeIndex, newMode); // Update MainWindow button states etc.
            keyHandled = true; // Mark key as handled
        }
    }

    // Final event handling
    if (keyHandled) {
        event->accept(); // Consume the event if we handled it
    } else {
        // TODO: Implement Undo (Ctrl+Z) / Redo (Ctrl+Y/Shift+Ctrl+Z)
        if (event->matches(QKeySequence::Undo)) {
            m_undoStack.undo();
            qDebug() << "Undo action triggered.";
            event->accept();
        } else if (event->matches(QKeySequence::Redo)) {
            m_undoStack.redo();
            qDebug() << "Redo action triggered.";
            event->accept();
        } else {
            QWidget::keyPressEvent(event); // Pass to base class for other keys
        }
    }
}


// --- Protected Method for Undo/Redo ---

/**
 * @brief Restores the internal state of all channel nodes (called by Undo command).
 * @param allNodes - The complete map of channel nodes to restore.
 */
void CurveWidget::restoreAllChannelNodes(const QMap<ActiveChannel, QVector<CurveNode>>& allNodes)
{
    // Check if state actually changed to avoid unnecessary updates/signal emissions
    // Simple check: if the map pointers are different. For value check, need map comparison.
    // Let's assume if called via undo/redo, a change is intended.
    bool changed = true; // Assume changed for now
    // Add more robust comparison if needed:
    /*
    if (m_channelNodes.keys() == allNodes.keys()) {
        changed = false;
        for(auto key : m_channelNodes.keys()) {
            if (m_channelNodes[key] != allNodes[key]) {
                 changed = true; break;
            }
        }
    } else { changed = true; }
    */

    if (changed) {
        m_channelNodes = allNodes; // Assign the restored state map

        // Reset interaction state
        m_selection = {SelectedPart::NONE, -1};
        m_dragging = false;
        m_stateBeforeAction.clear(); // Clear any pending action state

        update(); // Redraw the widget
        emit curveChanged(); // Signal that curve data has changed
        emit selectionChanged(-1, HandleAlignment::Free); // Signal deselection
        qDebug() << "Curve state restored via Undo/Redo.";
    } else {
        qDebug() << "Undo/Redo called but state appears unchanged.";
    }
}


// --- Private Helper Functions ---

/**
 * @brief Sorts the nodes vector of the *active* channel by the main point's X-coordinate.
 * Ensures first and last nodes remain at X=0 and X=1 respectively.
 */
void CurveWidget::sortActiveNodes() {
    QVector<CurveNode>& activeNodes = getActiveNodes(); // Get ref
    if (activeNodes.size() <= 1) return;

    // Store the original first and last nodes as they shouldn't be sorted
    CurveNode firstNode = activeNodes.first();
    CurveNode lastNode = activeNodes.last();

    // Sort intermediate nodes if they exist
    if (activeNodes.size() > 2) {
        std::sort(activeNodes.begin() + 1, activeNodes.end() - 1,
                  [](const CurveNode& a, const CurveNode& b) {
                      return a.mainPoint.x() < b.mainPoint.x();
                  });
    }

    // Restore fixed endpoints (ensure their X is exactly 0 and 1)
    activeNodes.first() = firstNode;
    activeNodes.first().mainPoint.setX(0.0);
    activeNodes.last() = lastNode;
    activeNodes.last().mainPoint.setX(1.0);


}

/**
 * @brief Private helper to apply alignment snapping to a specific node's handles
 * within the *active* channel's node list.
 * @param nodeIndex - Index of the node whose handles should be snapped.
 */
void CurveWidget::applyAlignmentSnap(int nodeIndex) {
    QVector<CurveNode>& activeNodes = getActiveNodes(); // Get ref

    // Check if it's an intermediate node that *can* have alignment applied
    // (Endpoints typically don't have both handles active for alignment)
    if (nodeIndex <= 0 || nodeIndex >= activeNodes.size() - 1) return;

    CurveNode& node = activeNodes[nodeIndex]; // Get reference to the node

    // Snap only if mode requires it (Aligned or Mirrored)
    if (node.alignment == HandleAlignment::Aligned || node.alignment == HandleAlignment::Mirrored) {
        const QPointF& mainPt = node.mainPoint;
        QPointF* hIn = &node.handleIn; // Pointer to incoming handle
        QPointF* hOut = &node.handleOut; // Pointer to outgoing handle

        // Determine which handle dictates the direction (arbitrarily choose Out handle)
        // Could be improved by checking which handle was moved last if needed.
        QPointF vecOut = *hOut - mainPt;
        qreal lenOutSq = QPointF::dotProduct(vecOut, vecOut);
        QPointF newInPos; // Calculate the new position for the In handle

        if (lenOutSq < 1e-12) { // If Out handle is coincident with main point
            newInPos = mainPt; // Snap In handle to main point too
        } else {
            qreal lenOut = qSqrt(lenOutSq);
            QPointF normDirIn = -vecOut / lenOut; // Normalized direction for In handle (opposite to Out)

            if (node.alignment == HandleAlignment::Aligned) {
                // Keep the original length of the In handle
                QPointF vecInOld = *hIn - mainPt;
                qreal lenInOld = qSqrt(QPointF::dotProduct(vecInOld, vecInOld));
                newInPos = mainPt + normDirIn * lenInOld;
            } else { // Mirrored
                // Match the length of the Out handle
                newInPos = mainPt + normDirIn * lenOut;
            }
        }
        // Update handleIn position and clamp its coordinates
        hIn->setX(std::max(0.0, std::min(1.0, newInPos.x())));
        hIn->setY(std::max(0.0, std::min(1.0, newInPos.y())));
    }
}

/**
 * @brief Finds the closest interactive part (main point or handle) on the *active* curve
 * to a widget position.
 * @param widgetPos - Position in widget coordinates.
 * @param mainRadius - Click radius for main points (widget coordinates).
 * @param handleRadius - Click radius for handles (widget coordinates).
 * @return SelectionInfo indicating the closest part and its node index in the active channel's list.
 */
CurveWidget::SelectionInfo CurveWidget::findNearbyPart(const QPoint& widgetPos, qreal mainRadius /*= 10.0*/, qreal handleRadius /*= 8.0*/)
{
    SelectionInfo closest = {SelectedPart::NONE, -1};
    qreal minDistSq = std::numeric_limits<qreal>::max(); // Use squared distance for efficiency

    const QVector<CurveNode>& activeNodes = getActiveNodes(); // Get const ref
    if (activeNodes.isEmpty()) return closest;

    QPointF widgetPosF = QPointF(widgetPos); // Convert click pos once

    // Iterate through the active channel's nodes
    for (int i = 0; i < activeNodes.size(); ++i) {
        const CurveNode& node = activeNodes[i];

        // Check Handles (In/Out based on index bounds)
        // Handles have smaller hit radius and are checked first
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

        // Check Main Point (larger hit radius, checked last to take precedence if overlapping)
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
 * @param widgetPos - Position in widget coordinates.
 * @return ClosestSegmentResult indicating segment index, parameter t, and distance squared.
 */
CurveWidget::ClosestSegmentResult CurveWidget::findClosestSegment(const QPoint& widgetPos) const
{
    ClosestSegmentResult bestMatch;
    const QVector<CurveNode>& activeNodes = getActiveNodes(); // Get const ref
    if (activeNodes.size() < 2) return bestMatch; // Need at least one segment

    QPointF widgetPosF = QPointF(widgetPos);
    const int stepsPerSegment = 20; // Sampling density for initial distance check
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
            QPointF pointOnCurveLogical = ::evaluateBezier(p0, p1, p2, p3, t); // Use helper
            QPointF pointOnCurveWidget = mapToWidget(pointOnCurveLogical);
            QPointF diffVec = widgetPosF - pointOnCurveWidget;
            qreal distSq = QPointF::dotProduct(diffVec, diffVec);

            // If this point is closer than the best found so far
            if (distSq < minDistanceSq) {
                minDistanceSq = distSq;
                bestMatch.segmentIndex = i; // Store index of the start node of the segment
                bestMatch.t = t;           // Store parameter t along the segment
                bestMatch.distanceSq = distSq; // Store distance squared
            }
        }
    }

    // Optional: Implement refinement step here using the best guess 't'
    // to find a more accurate closest point, potentially using calculus.
    // For node insertion, the sampled approach is often sufficient.

    // Optional: Add max distance threshold check if node insertion should only happen
    // if the click is very close to the curve.
    // const qreal maxDistThresholdSq = 20.0 * 20.0; // Example threshold
    // if (bestMatch.distanceSq > maxDistThresholdSq) {
    //    bestMatch.segmentIndex = -1; // Mark as too far
    // }

    return bestMatch;
}


/**
 * @brief Gets a non-const reference to the node vector for the currently active channel.
 * @throws std::out_of_range if the active channel is not found (should not happen).
 */
QVector<CurveWidget::CurveNode>& CurveWidget::getActiveNodes() {
    auto it = m_channelNodes.find(m_activeChannel);
    if (it == m_channelNodes.end()) {
        // This indicates a critical internal error
        throw std::out_of_range("Active channel not found in m_channelNodes");
        // Or use qFatal("Active channel nodes not found!");
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


// --- Coordinate Mapping ---

/**
 * @brief Maps a logical point (0-1 range) to widget pixel coordinates.
 */
QPointF CurveWidget::mapToWidget(const QPointF& logicalPoint) const
{
    // Add a small margin around the edge for points/handles
    const qreal margin = m_mainPointRadius + 2.0;
    qreal usableWidth = std::max(1.0, width() - 2.0 * margin);   // Prevent non-positive size
    qreal usableHeight = std::max(1.0, height() - 2.0 * margin); // Prevent non-positive size

    qreal widgetX = margin + logicalPoint.x() * usableWidth;
    // Y coordinate is inverted: logical 0 = bottom, logical 1 = top
    qreal widgetY = margin + (1.0 - logicalPoint.y()) * usableHeight;

    return QPointF(widgetX, widgetY);
}

/**
 * @brief Maps widget pixel coordinates back to logical (0-1 range).
 */
QPointF CurveWidget::mapFromWidget(const QPoint& widgetPoint) const
{
    const qreal margin = m_mainPointRadius + 2.0;
    qreal usableWidth = width() - 2.0 * margin;
    qreal usableHeight = height() - 2.0 * margin;

    // Avoid division by zero or very small numbers if widget is too small
    if (usableWidth < 1e-6 || usableHeight < 1e-6) {
        return QPointF(0.0, 0.0); // Return default point
    }

    qreal logicalX = (static_cast<qreal>(widgetPoint.x()) - margin) / usableWidth;
    // Y coordinate needs inversion
    qreal logicalY = 1.0 - (static_cast<qreal>(widgetPoint.y()) - margin) / usableHeight;

    // Note: This function does NOT clamp the output to [0, 1].
    // Clamping is handled by the calling logic (e.g., mouseMoveEvent) as needed.
    return QPointF(logicalX, logicalY);
}

QUndoStack* CurveWidget::undoStack() {
    return &m_undoStack; // Return the address of the private member
}

/**
 * @brief Gets the number of nodes in the currently active channel's curve.
 * @return The count of nodes. Const method.
 */
int CurveWidget::getActiveNodeCount() const {
    // Use the existing const helper function to get the nodes
    // then return the size of the vector.
    return getActiveNodes().size();
}
// ----------------------------
