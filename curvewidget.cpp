#include "curvewidget.h"
#include "setcurvestatecommand.h"

#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QPainterPath>
#include <QPalette>
#include <QtMath>
#include <QtCore/qnumeric.h>
#include <QDebug>
#include <QList>
#include <QSet>
#include <QRect>
#include <QVector>
#include <QMap>
#include <QUndoStack>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <functional>
#include <stdexcept>


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
    QPointF pointOnCurve;
    QPointF handle1_Seg1;
    QPointF handle2_Seg1;
    QPointF handle1_Seg2;
    QPointF handle2_Seg2;
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
    return 3.0 * mt * mt * (p1.x() - p0.x()) +
           6.0 * mt * t  * (p2.x() - p1.x()) +
           3.0 * t * t   * (p3.x() - p2.x());
}

}


// --- CurveNode Implementation ---

CurveWidget::CurveNode::CurveNode(QPointF p)
    : mainPoint(p),
    handleIn(p),
    handleOut(p),
    alignment(HandleAlignment::Aligned)
{}

bool CurveWidget::CurveNode::operator==(const CurveNode& other) const {
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
    m_activeChannel(ActiveChannel::RED),
    m_dragging(false),
    m_currentDrag({SelectedPart::NONE, -1}),
    m_isBoxSelecting(false),
    m_mainPointRadius(5.0),
    m_handleRadius(4.0),
    m_isDarkMode(false),
    m_drawInactiveChannels(false),
    m_clampHandles(true)

{
    QList<ActiveChannel> channels = {ActiveChannel::RED, ActiveChannel::GREEN, ActiveChannel::BLUE};
    for (ActiveChannel channel : channels) {
        QVector<CurveNode> defaultNodes;
        CurveNode node0(QPointF(0.0, 0.0));
        CurveNode node1(QPointF(1.0, 1.0));
        node0.handleOut = QPointF(1.0/3.0, 0.0);
        node1.handleIn  = QPointF(2.0/3.0, 1.0);
        node0.alignment = HandleAlignment::Free;
        node1.alignment = HandleAlignment::Free;
        defaultNodes << node0 << node1;
        m_channelNodes.insert(channel, defaultNodes);
    }

    setMinimumSize(200, 200);
    setFocusPolicy(Qt::ClickFocus);
    setAutoFillBackground(true);
    setMouseTracking(true);
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
    x = std::max(0.0, std::min(1.0, x));

    auto it = m_channelNodes.find(channel);
    if (it == m_channelNodes.end() || it.value().size() < 2) {
        qWarning() << "CurveWidget::sampleCurveChannel - Channel invalid or < 2 nodes. Returning linear.";
        return x;
    }
    const QVector<CurveNode>& nodes = it.value();

    int segmentIndex = -1;
    for (int i = 0; i < nodes.size() - 1; ++i) {
        if (x >= nodes[i].mainPoint.x() && x <= nodes[i+1].mainPoint.x()) {
            if (qFuzzyCompare(nodes[i].mainPoint.x(), nodes[i+1].mainPoint.x())) {
                return nodes[i].mainPoint.y();
            }
            segmentIndex = i;
            break;
        }
    }

    if (segmentIndex == -1) {
        if (x <= nodes.first().mainPoint.x()) {
            return nodes.first().mainPoint.y();
        } else {
            return nodes.last().mainPoint.y();
        }
    }

    const CurveNode& n0 = nodes[segmentIndex];
    const CurveNode& n1 = nodes[segmentIndex + 1];
    const QPointF p0 = n0.mainPoint;
    const QPointF p1 = n0.handleOut;
    const QPointF p2 = n1.handleIn;
    const QPointF p3 = n1.mainPoint;

    qreal t_guess = 0.5;
    qreal segmentXRange = p3.x() - p0.x();

    if (std::abs(segmentXRange) > 1e-9) {
        t_guess = (x - p0.x()) / segmentXRange;
        t_guess = std::max(0.0, std::min(1.0, t_guess));
    } else {
        qWarning("Sampling near vertical segment, result might be inaccurate.");
        return p0.y();
    }

    const int MAX_ITERATIONS = 15;
    const qreal TOLERANCE_X = 1e-7;
    qreal t = t_guess;

    for (int iter = 0; iter < MAX_ITERATIONS; ++iter) {
        qreal currentX = evaluateBezier(p0, p1, p2, p3, t).x();
        qreal error = currentX - x;
        if (std::abs(error) < TOLERANCE_X) break;

        qreal dXdt = evaluateBezierXDerivative(p0, p1, p2, p3, t);
        if (std::abs(dXdt) < 1e-7) {
            qWarning("Newton's method encountered near-zero derivative during sampling.");
            break;
        }
        t = t - error / dXdt;
        t = std::max(0.0, std::min(1.0, t));
    }
    t = std::max(0.0, std::min(1.0, t));

    qreal resultY = evaluateBezier(p0, p1, p2, p3, t).y();

    return std::max(0.0, std::min(1.0, resultY));
}

/**
 * @brief Resets the *active* curve channel to its default state (straight line). Undoable.
 */
void CurveWidget::resetCurve() {
    m_stateBeforeAction = m_channelNodes;

    QVector<CurveNode>& activeNodes = getActiveNodes();
    activeNodes.clear();

    CurveNode node0(QPointF(0.0, 0.0)); CurveNode node1(QPointF(1.0, 1.0));
    node0.handleOut = QPointF(1.0/3.0, 0.0); node1.handleIn = QPointF(2.0/3.0, 1.0);
    node0.alignment = HandleAlignment::Free; node1.alignment = HandleAlignment::Free;
    activeNodes << node0 << node1;

    QMap<ActiveChannel, QVector<CurveNode>> newState = m_channelNodes;
    bool stateChanged = false;
    if (m_stateBeforeAction.keys() != newState.keys()) { stateChanged = true; }
    else { for(auto key : m_stateBeforeAction.keys()) { if (m_stateBeforeAction[key] != newState[key]) { stateChanged = true; break; }}}

    if (stateChanged) {
        m_undoStack.push(new SetCurveStateCommand(this, m_stateBeforeAction, newState, "Reset Curve"));
    } else { m_stateBeforeAction.clear(); }

    m_selectedNodeIndices.clear();
    m_currentDrag = {SelectedPart::NONE, -1};
    m_dragging = false;
    m_isBoxSelecting = false;
    m_boxSelectionRect = QRect();

    update();
    emit curveChanged();
    emit selectionChanged();
}

/**
 * @brief Sets the dark mode flag for drawing colors.
 */
void CurveWidget::setDarkMode(bool dark) {
    if (m_isDarkMode != dark) {
        m_isDarkMode = dark;
        update();
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
    return HandleAlignment::Free;
}


// --- Public Slots ---

/**
 * @brief Sets the currently active channel for editing and viewing.
 */
void CurveWidget::setActiveChannel(ActiveChannel channel) {
    if (!m_channelNodes.contains(channel)) {
        qWarning() << "Attempted to set invalid active channel:" << static_cast<int>(channel);
        return;
    }

    if (m_activeChannel != channel) {
        m_activeChannel = channel;

        m_selectedNodeIndices.clear();
        m_currentDrag = {SelectedPart::NONE, -1};
        m_dragging = false;
        m_isBoxSelecting = false;
        m_boxSelectionRect = QRect();

        update();
        emit selectionChanged();
        qDebug() << "Active channel set to:" << static_cast<int>(m_activeChannel);
    }
}

/**
 * @brief Sets the handle alignment mode for a specific node.
 * Only applies if exactly one node is selected and its index matches nodeIndex.
 */
void CurveWidget::setNodeAlignment(int nodeIndex, HandleAlignment mode) {
    if (m_selectedNodeIndices.size() != 1 || !m_selectedNodeIndices.contains(nodeIndex)) {
        qWarning() << "setNodeAlignment ignored: requires exactly one selected node matching the index.";
        return;
    }

    QVector<CurveNode>& activeNodes = getActiveNodes();
    if (nodeIndex < 0 || nodeIndex >= activeNodes.size()) {
        qWarning() << "setNodeAlignment: Invalid index" << nodeIndex;
        return;
    }

    CurveNode& node = activeNodes[nodeIndex];
    if (node.alignment != mode) {
        m_stateBeforeAction = m_channelNodes;
        node.alignment = mode;
        applyAlignmentSnap(nodeIndex, SelectedPart::HANDLE_OUT);

        QMap<ActiveChannel, QVector<CurveNode>> newState = m_channelNodes;
        bool stateChanged = false;
        if (m_stateBeforeAction.keys() != newState.keys()) { stateChanged = true; }
        else { for(auto key : m_stateBeforeAction.keys()) { if (m_stateBeforeAction[key] != newState[key]) { stateChanged = true; break; }}}

        if (stateChanged) {
            m_undoStack.push(new SetCurveStateCommand(this, m_stateBeforeAction, newState, "Change Alignment"));
        } else { m_stateBeforeAction.clear(); }

        update();
        emit curveChanged();
        emit selectionChanged();
    }
}

/**
 * @brief Sets whether to draw inactive channels in the background.
 */
void CurveWidget::setDrawInactiveChannels(bool draw) {
    if (m_drawInactiveChannels != draw) {
        m_drawInactiveChannels = draw;
        update();
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

    QColor gridColor, borderColor, handleLineColor, handleColor, mainPointColor, outlineColor;
    QColor activeCurveColor;
    QColor selectionColor = Qt::yellow;

    if (m_isDarkMode) {
        gridColor = QColor(80, 80, 80); borderColor = QColor(90, 90, 90);
        handleLineColor = QColor(100, 100, 100); handleColor = QColor(0, 190, 190);
        mainPointColor = QColor(230, 230, 230); outlineColor = Qt::black;
        activeCurveColor = QColor(240, 240, 240);
    } else {
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

    painter.setPen(QPen(gridColor, 0.5));
    int numGridLines = 10;
    for (int i = 1; i < numGridLines; ++i) {
        qreal ratio = static_cast<qreal>(i) / numGridLines;
        painter.drawLine(mapToWidget(QPointF(ratio, 0.0)), mapToWidget(QPointF(ratio, 1.0)));
        painter.drawLine(mapToWidget(QPointF(0.0, ratio)), mapToWidget(QPointF(1.0, ratio)));
    }
    painter.setPen(QPen(borderColor, 1));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    if (m_drawInactiveChannels) {
        painter.save();
        for (auto it = m_channelNodes.constBegin(); it != m_channelNodes.constEnd(); ++it) {
            if (it.key() == m_activeChannel) continue;
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

    const QVector<CurveNode>& activeNodes = getActiveNodes();
    if (activeNodes.size() >= 2) {
        QPainterPath curvePath;
        curvePath.moveTo(mapToWidget(activeNodes[0].mainPoint));
        for (int i = 0; i < activeNodes.size() - 1; ++i) {
            curvePath.cubicTo(mapToWidget(activeNodes[i].handleOut), mapToWidget(activeNodes[i+1].handleIn), mapToWidget(activeNodes[i+1].mainPoint));
        }
        painter.setPen(QPen(activeCurveColor, 2));
        painter.drawPath(curvePath);
    }

    for (int i = 0; i < activeNodes.size(); ++i) {
        const CurveNode& node = activeNodes[i];
        QPointF mainWidgetPos = mapToWidget(node.mainPoint);

        painter.setPen(QPen(handleLineColor, 1));
        if (i > 0) painter.drawLine(mainWidgetPos, mapToWidget(node.handleIn));
        if (i < activeNodes.size() - 1) painter.drawLine(mainWidgetPos, mapToWidget(node.handleOut));

        painter.setPen(QPen(outlineColor, 0.5));
        if (i > 0) {
            bool isHandleDragging = (m_currentDrag.part == SelectedPart::HANDLE_IN && m_currentDrag.nodeIndex == i);
            painter.setBrush(isHandleDragging ? selectionColor : handleColor);
            painter.drawEllipse(mapToWidget(node.handleIn), m_handleRadius, m_handleRadius);
        }
        if (i < activeNodes.size() - 1) {
            bool isHandleDragging = (m_currentDrag.part == SelectedPart::HANDLE_OUT && m_currentDrag.nodeIndex == i);
            painter.setBrush(isHandleDragging ? selectionColor : handleColor);
            painter.drawEllipse(mapToWidget(node.handleOut), m_handleRadius, m_handleRadius);
        }

        painter.setPen(QPen(outlineColor, 1));
        bool isMainSelected = m_selectedNodeIndices.contains(i);
        painter.setBrush(isMainSelected ? selectionColor : mainPointColor);
        painter.drawEllipse(mainWidgetPos, m_mainPointRadius, m_mainPointRadius);
    }

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
    m_isBoxSelecting = false;
    m_dragging = false;
    m_boxSelectionRect = QRect();
    m_currentDrag = {SelectedPart::NONE, -1};
    m_stateBeforeAction.clear();

    QPoint currentPos = event->pos();
    SelectionInfo clickedPart = findNearbyPart(currentPos);
    bool shiftPressed = event->modifiers() & Qt::ShiftModifier;
    bool selectionActuallyChanged = false;

    if (event->button() == Qt::RightButton && clickedPart.part == SelectedPart::MAIN_POINT)
    {
        int nodeIndex = clickedPart.nodeIndex;
        QVector<CurveNode>& activeNodes = getActiveNodes();
        if (nodeIndex > 0 && nodeIndex < activeNodes.size() - 1) {
            m_stateBeforeAction = m_channelNodes;
            activeNodes.remove(nodeIndex);
            m_undoStack.push(new SetCurveStateCommand(this, m_stateBeforeAction, m_channelNodes, "Delete Node"));

            if(m_selectedNodeIndices.contains(nodeIndex) || !m_selectedNodeIndices.isEmpty()) selectionActuallyChanged = true;
            m_selectedNodeIndices.clear();
            m_currentDrag = {SelectedPart::NONE, -1};

            update();
            emit curveChanged();
            if(selectionActuallyChanged) emit selectionChanged();
        }
        return;
    }
    else if (event->button() == Qt::LeftButton)
    {
        if (clickedPart.part != SelectedPart::NONE) {
            m_dragging = true;
            m_currentDrag = clickedPart;

            if (clickedPart.part == SelectedPart::MAIN_POINT) {
                int clickedIndex = clickedPart.nodeIndex;
                bool alreadySelected = m_selectedNodeIndices.contains(clickedIndex);
                if (!shiftPressed) {
                    if (!alreadySelected) {
                        if (!m_selectedNodeIndices.isEmpty()) selectionActuallyChanged = true;
                        m_selectedNodeIndices.clear();
                        m_selectedNodeIndices.insert(clickedIndex);
                        if (!selectionActuallyChanged) selectionActuallyChanged = true;
                    }
                } else {
                    if (alreadySelected) m_selectedNodeIndices.remove(clickedIndex); else m_selectedNodeIndices.insert(clickedIndex);
                    selectionActuallyChanged = true;
                }
                m_stateBeforeAction = m_channelNodes;

            } else {
                if (!m_selectedNodeIndices.isEmpty()) { m_selectedNodeIndices.clear(); selectionActuallyChanged = true; }
                m_stateBeforeAction = m_channelNodes;
            }

        } else {
            ClosestSegmentResult hit = findClosestSegment(currentPos);
            const qreal t_tolerance = 0.005; const qreal max_dist_sq_for_add = 20.0 * 20.0;

            if (hit.segmentIndex != -1 && hit.t > t_tolerance && hit.t < (1.0 - t_tolerance) && hit.distanceSq < max_dist_sq_for_add)
            {
                qDebug() << "Adding node on segment" << hit.segmentIndex << "at t=" << hit.t;
                QMap<ActiveChannel, QVector<CurveNode>> stateBeforeAdd = m_channelNodes;

                int i = hit.segmentIndex; qreal t = hit.t;
                QVector<CurveNode>& activeNodes = getActiveNodes();
                if (i < 0 || i >= activeNodes.size() - 1) { return; }

                const QPointF p0=activeNodes[i].mainPoint, p1=activeNodes[i].handleOut, p2=activeNodes[i+1].handleIn, p3=activeNodes[i+1].mainPoint;
                SubdivisionResult split = subdivideBezier(p0, p1, p2, p3, t);
                CurveNode newNode(split.pointOnCurve);
                newNode.handleIn = split.handle2_Seg1; newNode.handleOut = split.handle1_Seg2;
                newNode.alignment = HandleAlignment::Aligned;
                activeNodes[i].handleOut = split.handle1_Seg1; activeNodes[i+1].handleIn = split.handle2_Seg2;
                int newNodeIndex = i + 1;
                activeNodes.insert(newNodeIndex, newNode);

                QMap<ActiveChannel, QVector<CurveNode>> stateAfterAdd = m_channelNodes;

                m_undoStack.push(new SetCurveStateCommand(this, stateBeforeAdd, stateAfterAdd, "Add Node"));
                qDebug() << "Add Node undo command pushed.";

                if (!m_selectedNodeIndices.isEmpty()) selectionActuallyChanged = true;
                m_selectedNodeIndices.clear();
                m_selectedNodeIndices.insert(newNodeIndex);
                if (!selectionActuallyChanged) selectionActuallyChanged = true;

                m_dragging = true;
                m_currentDrag = {SelectedPart::MAIN_POINT, newNodeIndex};
                m_stateBeforeAction = stateAfterAdd;

                emit curveChanged();

            } else {
                m_isBoxSelecting = true;
                m_boxSelectionStartPoint = currentPos;
                m_boxSelectionRect = QRect(m_boxSelectionStartPoint, QSize());
                if (!shiftPressed) {
                    if (!m_selectedNodeIndices.isEmpty()) { m_selectedNodeIndices.clear(); selectionActuallyChanged = true; }
                }
            }
        }
    }

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
        QVector<CurveNode>& activeNodes = getActiveNodes();
        if (m_currentDrag.nodeIndex < 0 || m_currentDrag.nodeIndex >= activeNodes.size()) {
            qWarning() << "MouseMoveEvent: Invalid drag index" << m_currentDrag.nodeIndex;
            m_dragging = false; m_currentDrag = {SelectedPart::NONE, -1}; return;
        }

        QPointF logicalPos = mapFromWidget(event->pos());
        CurveNode& primaryNode = activeNodes[m_currentDrag.nodeIndex];
        QPointF deltaLogical;

        if (m_currentDrag.part == SelectedPart::MAIN_POINT) {
            QPointF oldLogicalPos = primaryNode.mainPoint;
            qreal newX = logicalPos.x(); qreal newY = logicalPos.y();
            newY = std::max(0.0, std::min(1.0, newY));
            const qreal epsilon = 1e-9;
            if (m_currentDrag.nodeIndex == 0) newX = 0.0;
            else if (m_currentDrag.nodeIndex == activeNodes.size() - 1) newX = 1.0;
            else {
                qreal minX = (m_currentDrag.nodeIndex > 0) ? activeNodes[m_currentDrag.nodeIndex - 1].mainPoint.x() + epsilon : 0.0;
                qreal maxX = (m_currentDrag.nodeIndex < activeNodes.size() - 1) ? activeNodes[m_currentDrag.nodeIndex + 1].mainPoint.x() - epsilon : 1.0;
                if (minX > maxX) minX = maxX = (minX + maxX) / 2.0;
                newX = std::max(minX, std::min(maxX, newX));
            }
            newX = std::max(0.0, std::min(1.0, newX));
            QPointF newClampedLogicalPos(newX, newY);
            deltaLogical = newClampedLogicalPos - oldLogicalPos;

        } else if (m_currentDrag.part == SelectedPart::HANDLE_IN || m_currentDrag.part == SelectedPart::HANDLE_OUT) {
            QPointF* handlePtr = (m_currentDrag.part == SelectedPart::HANDLE_IN) ? &primaryNode.handleIn : &primaryNode.handleOut;
            QPointF oldLogicalPos = *handlePtr;
            QPointF newUnclampedLogicalPos = logicalPos;
            deltaLogical = newUnclampedLogicalPos - oldLogicalPos;

        } else { return; }


        bool needsReSort = false;
        if (m_currentDrag.part == SelectedPart::MAIN_POINT && !deltaLogical.isNull()) {
            for (int index : m_selectedNodeIndices) {
                if (index < 0 || index >= activeNodes.size()) continue;
                CurveNode& nodeToMove = activeNodes[index];
                QPointF oldMainPos = nodeToMove.mainPoint;
                const qreal handleCoincidenceThresholdSq = 1e-12;

                nodeToMove.mainPoint += deltaLogical;
                nodeToMove.mainPoint.setY(std::max(0.0, std::min(1.0, nodeToMove.mainPoint.y())));

                if (QPointF::dotProduct(nodeToMove.handleIn - oldMainPos, nodeToMove.handleIn - oldMainPos) > handleCoincidenceThresholdSq) nodeToMove.handleIn += deltaLogical;
                if (QPointF::dotProduct(nodeToMove.handleOut - oldMainPos, nodeToMove.handleOut - oldMainPos) > handleCoincidenceThresholdSq) nodeToMove.handleOut += deltaLogical;

                clampHandlePosition(nodeToMove.handleIn);
                clampHandlePosition(nodeToMove.handleOut);

                applyAlignmentSnap(index, SelectedPart::HANDLE_OUT);

                if (index > 0 && index < activeNodes.size() - 1) needsReSort = true;
            }
            if (needsReSort) { qDebug() << "Multi-drag might require sorting."; }

        } else if ((m_currentDrag.part == SelectedPart::HANDLE_IN || m_currentDrag.part == SelectedPart::HANDLE_OUT) && !deltaLogical.isNull()) {
            QPointF* handlePtr = (m_currentDrag.part == SelectedPart::HANDLE_IN) ? &primaryNode.handleIn : &primaryNode.handleOut;
            *handlePtr += deltaLogical;

            clampHandlePosition(*handlePtr);

            applyAlignmentSnap(m_currentDrag.nodeIndex, m_currentDrag.part);
        }

        if (!deltaLogical.isNull()) {
            update();
            emit curveChanged();
        }

    } else if (m_isBoxSelecting) {
        m_boxSelectionRect = QRect(m_boxSelectionStartPoint, event->pos()).normalized();
        update();
    }
}
/**
 * @brief Handles mouse release to finalize drags (with undo) or box selection.
 */
void CurveWidget::mouseReleaseEvent(QMouseEvent *event)
{
    qDebug() << "<<< Release Start: Selection:" << m_selectedNodeIndices << "Dragging:" << m_dragging << "BoxSelect:" << m_isBoxSelecting;

    bool wasDragging = m_dragging;
    QSet<int> selectionToKeepIndices = m_selectedNodeIndices;
    SelectionInfo dragToKeep = m_currentDrag;

    if (wasDragging && event->button() == Qt::LeftButton) {
        m_dragging = false;

        bool pushedCommand = false;
        if (!m_stateBeforeAction.isEmpty()) {
            QMap<ActiveChannel, QVector<CurveNode>> currentState = m_channelNodes;
            bool stateChanged = false;
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

        qDebug() << "Re-asserting selection state after drag release.";
        m_selectedNodeIndices = selectionToKeepIndices;
        m_currentDrag = dragToKeep;

        emit selectionChanged();

        update();

    } else if (m_isBoxSelecting && event->button() == Qt::LeftButton) {
        m_isBoxSelecting = false;
        bool selectionActuallyChanged = false;
        bool shiftPressed = event->modifiers() & Qt::ShiftModifier;
        QSet<int> originalSelection = m_selectedNodeIndices;

        if (!shiftPressed) { m_selectedNodeIndices.clear(); }

        const QVector<CurveNode>& activeNodes = getActiveNodes();
        for (int i = 0; i < activeNodes.size(); ++i) {
            QPoint widgetPoint = mapToWidget(activeNodes[i].mainPoint).toPoint();
            if (m_boxSelectionRect.contains(widgetPoint)) { m_selectedNodeIndices.insert(i); }
        }

        if (m_selectedNodeIndices != originalSelection) selectionActuallyChanged = true;

        m_boxSelectionRect = QRect();
        m_currentDrag = {SelectedPart::NONE, -1};
        update();

        if (selectionActuallyChanged) {
            qDebug() << "Box selection completed. Selected indices:" << m_selectedNodeIndices;
            emit selectionChanged();
        } else {
            qDebug() << "Box selection completed. Selection unchanged.";
        }
    } else {
        m_currentDrag = {SelectedPart::NONE, -1};
        m_isBoxSelecting = false;
        m_boxSelectionRect = QRect();
        if (m_dragging) m_dragging = false;
    }

    qDebug() << ">>> Release End: Selection:" << m_selectedNodeIndices;
}

/**
 * @brief Handles widget resize events. Triggers repaint.
 */
void CurveWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    update();
}

/**
 * @brief Handles key presses for alignment changes, deletion, undo, redo.
 */
void CurveWidget::keyPressEvent(QKeyEvent *event)
{
    bool keyHandled = false;

    if (m_selectedNodeIndices.size() == 1 &&
        (event->key() == Qt::Key_F || event->key() == Qt::Key_A || event->key() == Qt::Key_M))
    {
        int nodeIndex = *m_selectedNodeIndices.constBegin();
        QVector<CurveNode>& activeNodes = getActiveNodes();
        if (nodeIndex >= 0 && nodeIndex < activeNodes.size()) {
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
                setNodeAlignment(nodeIndex, newMode);
                keyHandled = true;
            }
        }
    }
    else if (event->key() == Qt::Key_Delete && !m_selectedNodeIndices.isEmpty())
    {
        m_stateBeforeAction = m_channelNodes;
        QVector<CurveNode>& activeNodes = getActiveNodes();
        bool nodesWereRemoved = false;
        QList<int> indicesToRemove = m_selectedNodeIndices.values();
        std::sort(indicesToRemove.begin(), indicesToRemove.end(), std::greater<int>());

        for (int index : indicesToRemove) {
            if (index > 0 && index < activeNodes.size() - 1) {
                activeNodes.remove(index);
                nodesWereRemoved = true;
            }
        }

        if (nodesWereRemoved) {
            m_undoStack.push(new SetCurveStateCommand(this, m_stateBeforeAction, m_channelNodes, "Delete Node(s)"));
            m_selectedNodeIndices.clear();
            m_currentDrag = {SelectedPart::NONE, -1};
            m_dragging = false;
            update();
            emit curveChanged();
            emit selectionChanged();
            keyHandled = true;
        } else {
            m_stateBeforeAction.clear();
        }
    }

    if (!keyHandled) {
        if (event->matches(QKeySequence::Undo)) {
            m_undoStack.undo();
            keyHandled = true;
        } else if (event->matches(QKeySequence::Redo)) {
            m_undoStack.redo();
            keyHandled = true;
        }
    }

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
    if (m_channelNodes == allNodes) {
        qDebug() << "Undo/Redo: State appears unchanged.";
    }
    m_channelNodes = allNodes;

    m_selectedNodeIndices.clear();
    m_currentDrag = {SelectedPart::NONE, -1};
    m_dragging = false;
    m_isBoxSelecting = false;
    m_boxSelectionRect = QRect();
    m_stateBeforeAction.clear();

    update();
    emit curveChanged();
    emit selectionChanged();
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
    return it.value();
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
    return it.value();
}

/**
 * @brief Maps a logical point (0-1 range) to widget pixel coordinates.
 */
QPointF CurveWidget::mapToWidget(const QPointF& logicalPoint) const
{
    const qreal margin = m_mainPointRadius + 2.0;
    qreal usableWidth = std::max(1.0, width() - 2.0 * margin);
    qreal usableHeight = std::max(1.0, height() - 2.0 * margin);

    qreal widgetX = margin + logicalPoint.x() * usableWidth;
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

    if (usableWidth < 1e-6 || usableHeight < 1e-6) {
        return QPointF(0.0, 0.0);
    }

    qreal logicalX = (static_cast<qreal>(widgetPoint.x()) - margin) / usableWidth;
    qreal logicalY = 1.0 - (static_cast<qreal>(widgetPoint.y()) - margin) / usableHeight;


    return QPointF(logicalX, logicalY);
}

/**
 * @brief Finds the closest interactive part (main point or handle) on the *active* curve
 * to a widget position. Used to determine what was clicked.
 */
CurveWidget::SelectionInfo CurveWidget::findNearbyPart(const QPoint& widgetPos, qreal mainRadius /*= 10.0*/, qreal handleRadius /*= 8.0*/)
{
    SelectionInfo closest = {SelectedPart::NONE, -1};
    qreal minDistSq = std::numeric_limits<qreal>::max();

    const QVector<CurveNode>& activeNodes = getActiveNodes();
    if (activeNodes.isEmpty()) return closest;

    QPointF widgetPosF = QPointF(widgetPos);

    for (int i = 0; i < activeNodes.size(); ++i) {
        const CurveNode& node = activeNodes[i];

        if (i > 0) {
            QPointF handleInWidget = mapToWidget(node.handleIn);
            QPointF diffVec = widgetPosF - handleInWidget;
            qreal distSq = QPointF::dotProduct(diffVec, diffVec);
            if (distSq < handleRadius * handleRadius && distSq < minDistSq) {
                minDistSq = distSq;
                closest = {SelectedPart::HANDLE_IN, i};
            }
        }
        if (i < activeNodes.size() - 1) {
            QPointF handleOutWidget = mapToWidget(node.handleOut);
            QPointF diffVec = widgetPosF - handleOutWidget;
            qreal distSq = QPointF::dotProduct(diffVec, diffVec);
            if (distSq < handleRadius * handleRadius && distSq < minDistSq) {
                minDistSq = distSq;
                closest = {SelectedPart::HANDLE_OUT, i};
            }
        }

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
    if (activeNodes.size() < 2) return bestMatch;

    QPointF widgetPosF = QPointF(widgetPos);
    const int stepsPerSegment = 20;
    qreal minDistanceSq = std::numeric_limits<qreal>::max();

    for (int i = 0; i < activeNodes.size() - 1; ++i) {
        const CurveNode& node0 = activeNodes[i];
        const CurveNode& node1 = activeNodes[i+1];
        const QPointF p0 = node0.mainPoint; const QPointF p1 = node0.handleOut;
        const QPointF p2 = node1.handleIn; const QPointF p3 = node1.mainPoint;

        for (int j = 0; j <= stepsPerSegment; ++j) {
            qreal t = static_cast<qreal>(j) / stepsPerSegment;
            QPointF pointOnCurveLogical = ::evaluateBezier(p0, p1, p2, p3, t);
            QPointF pointOnCurveWidget = mapToWidget(pointOnCurveLogical);
            QPointF diffVec = widgetPosF - pointOnCurveWidget;
            qreal distSq = QPointF::dotProduct(diffVec, diffVec);

            if (distSq < minDistanceSq) {
                minDistanceSq = distSq;
                bestMatch.segmentIndex = i;
                bestMatch.t = t;
                bestMatch.distanceSq = distSq;
            }
        }
    }
    return bestMatch;
}

/**
 * @brief Applies alignment snap. Calculates TARGET handle based on SOURCE handle.
 * Uses clampHandlePosition helper to conditionally clamp the target position.
 */
void CurveWidget::applyAlignmentSnap(int nodeIndex, CurveWidget::SelectedPart movedHandlePart) {
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
        QPointF normDirTarget = -vecSource / lenSource;
        if (node.alignment == HandleAlignment::Aligned) {
            QPointF vecTargetOld = *hTarget - mainPt;
            qreal lenTargetOld = qSqrt(QPointF::dotProduct(vecTargetOld, vecTargetOld));
            if(lenTargetOld < 1e-9) lenTargetOld = 0.0;
            newTargetPos = mainPt + normDirTarget * lenTargetOld;
        } else {
            newTargetPos = mainPt + normDirTarget * lenSource;
        }
    }

    clampHandlePosition(newTargetPos);

    *hTarget = newTargetPos;
}

/**
 * @brief Sorts the nodes vector of the *active* channel by the main point's X-coordinate.
 * Ensures first and last nodes remain fixed at X=0 and X=1 respectively.
 * WARNING: May invalidate indices stored elsewhere if node order changes.
 */
void CurveWidget::sortActiveNodes() {
    QVector<CurveNode>& activeNodes = getActiveNodes();
    if (activeNodes.size() <= 1) return;

    CurveNode firstNode = activeNodes.first();
    CurveNode lastNode = activeNodes.last();

    if (activeNodes.size() > 2) {
        std::sort(activeNodes.begin() + 1, activeNodes.end() - 1,
                  [](const CurveNode& a, const CurveNode& b) {
                      return a.mainPoint.x() < b.mainPoint.x();
                  });
    }

    activeNodes.first() = firstNode;
    activeNodes.first().mainPoint.setX(0.0);
    activeNodes.last() = lastNode;
    activeNodes.last().mainPoint.setX(1.0);

    qDebug() << "Warning: sortActiveNodes called - selection indices may be invalid if order changed.";
}

void CurveWidget::setHandlesClamping(bool clamp) {
    if (m_clampHandles != clamp) {
        m_clampHandles = clamp;
        qDebug() << "Handle clamping set to:" << m_clampHandles;
    }
}

void CurveWidget::clampHandlePosition(QPointF& handlePos) {
    if (m_clampHandles) {
        handlePos.setX(std::max(0.0, std::min(1.0, handlePos.x())));
        handlePos.setY(std::max(0.0, std::min(1.0, handlePos.y())));
    }
}

/**
 * @brief Replaces the entire curve state with the provided data.
 * Clears selection, interaction states, and the undo stack.
 * @param allNodes - The map containing the complete node data for all channels.
 */
void CurveWidget::setAllChannelNodes(const QMap<ActiveChannel, QVector<CurveNode>>& allNodes) {

    m_channelNodes = allNodes;

    m_selectedNodeIndices.clear();
    m_currentDrag = {SelectedPart::NONE, -1};
    m_dragging = false;
    m_isBoxSelecting = false;
    m_boxSelectionRect = QRect();
    m_stateBeforeAction.clear();

    m_undoStack.clear();
    qDebug() << "Curve data loaded, undo stack cleared.";

    if (!m_channelNodes.contains(m_activeChannel)) {
        m_activeChannel = ActiveChannel::RED;
    }

    update();
    emit curveChanged();
    emit selectionChanged();
}
