#include "curvewidget.h"
#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QPainterPath>
#include <QtCore/qnumeric.h> // For qFuzzyCompare
#include <algorithm>       // For std::sort, std::max, std::min
#include <cmath>           // For std::max, std::min, std::pow
#include <limits> // Required for std::numeric_limits
#include <QKeyEvent>
#include <QtMath>


CurveWidget::CurveNode::CurveNode(QPointF p) // <<< Use CurveWidget::CurveNode::
    : mainPoint(p),
    handleIn(p),
    handleOut(p),
    // Use unqualified name here - we are effectively inside CurveWidget scope
    alignment(HandleAlignment::Aligned)
{
    // Empty body
}

// Constructor
CurveWidget::CurveWidget(QWidget *parent)
    : QWidget(parent), m_dragging(false)
{

    // Initialize with a default straight line using two nodes
    CurveNode node0(QPointF(0.0, 0.0));
    CurveNode node1(QPointF(1.0, 1.0));

    // Place handles to create an initial straight line (1/3rd along segment)
    // Note: handleIn for node0 and handleOut for node1 are not used for drawing
    // but we initialize them relative to their main point.
    node0.handleOut = QPointF(1.0/3.0, 1.0/3.0); // Outgoing handle for segment 0
    node1.handleIn  = QPointF(2.0/3.0, 2.0/3.0); // Incoming handle for segment 0

    m_nodes.append(node0);
    m_nodes.append(node1);

    m_samplesDirty = true; // Mark samples needing update

    setMinimumSize(200, 200);
    setFocusPolicy(Qt::ClickFocus);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::darkGray);
    setPalette(pal);

    setFocusPolicy(Qt::ClickFocus);
}

// --- Public Accessors ---

// Linear interpolation between two points
inline QPointF lerp(const QPointF& a, const QPointF& b, qreal t) {
    return a * (1.0 - t) + b * t;
}



// Structure to hold the results of De Casteljau subdivision
struct SubdivisionResult {
    QPointF pointOnCurve; // P_new (main point for the new node)
    QPointF handle1_Seg1; // P01   (becomes handleOut for node i)
    QPointF handle2_Seg1; // P012  (becomes handleIn for new node)
    QPointF handle1_Seg2; // P123  (becomes handleOut for new node)
    QPointF handle2_Seg2; // P23   (becomes handleIn for node i+1)
};

// De Casteljau subdivision function
SubdivisionResult subdivideBezier(const QPointF& p0, const QPointF& p1, const QPointF& p2, const QPointF& p3, qreal t) {
    SubdivisionResult result;
    QPointF p01 = lerp(p0, p1, t);
    QPointF p12 = lerp(p1, p2, t);
    QPointF p23 = lerp(p2, p3, t);
    result.handle1_Seg1 = p01; // = H1_new0
    result.handle2_Seg2 = p23; // = H2_new1
    result.handle2_Seg1 = lerp(p01, p12, t); // = H2_new0 = newNode.handleIn
    result.handle1_Seg2 = lerp(p12, p23, t); // = H1_new1 = newNode.handleOut
    result.pointOnCurve = lerp(result.handle2_Seg1, result.handle1_Seg2, t); // = P_new
    return result;
}

QVector<CurveWidget::CurveNode> CurveWidget::getNodes() const {
    return m_nodes; // Return a copy
}

// Sample the curve at X using pre-calculated samples and linear interpolation
qreal CurveWidget::sampleCurve(qreal x) const {
    if (m_samplesDirty) {
        updateCurveSamples();
    }

    if (m_curveSamples.isEmpty()) return 0.0; // Should not happen if initialized

    // Clamp x to the valid range of samples
    x = std::max(m_curveSamples.first().x(), std::min(m_curveSamples.last().x(), x));

    // Find the segment in the sample table where x lies
    // (Using std::lower_bound assumes samples are sorted by x, which they should be)
    auto it = std::lower_bound(m_curveSamples.constBegin(), m_curveSamples.constEnd(), x,
                               [](const QPointF& point, qreal xVal) {
                                   return point.x() < xVal;
                               });

    // Handle edge cases or if lower_bound returns end()
    if (it == m_curveSamples.constBegin()) {
        return m_curveSamples.first().y();
    }
    if (it == m_curveSamples.constEnd()) {
        return m_curveSamples.last().y();
    }

    // Linear interpolation between the two samples surrounding x
    const QPointF& p2 = *it;
    const QPointF& p1 = *(it - 1);

    if (qFuzzyIsNull(p2.x() - p1.x())) { // Avoid division by zero if x values are identical
        return p1.y();
    }

    qreal t = (x - p1.x()) / (p2.x() - p1.x());
    return p1.y() * (1.0 - t) + p2.y() * t;
}

// --- Event Handlers ---

void CurveWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // --- Optional Grid --- (same as before)
    painter.setPen(QPen(Qt::gray, 0.5));
    int numGridLines = 10;
    for (int i = 1; i < numGridLines; ++i) {
        qreal ratio = static_cast<qreal>(i) / numGridLines;
        painter.drawLine(mapToWidget(QPointF(ratio, 1.0)), mapToWidget(QPointF(ratio, 0.0)));
        painter.drawLine(mapToWidget(QPointF(0.0, ratio)), mapToWidget(QPointF(1.0, ratio)));
    }
    painter.setPen(QPen(Qt::lightGray, 1));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    if (m_nodes.size() < 2) return; // Need at least two nodes to draw a curve

    // --- Draw Bézier Curve Segments ---
    QPainterPath curvePath;
    curvePath.moveTo(mapToWidget(m_nodes[0].mainPoint));
    for (int i = 0; i < m_nodes.size() - 1; ++i) {
        const CurveNode& node0 = m_nodes[i];
        const CurveNode& node1 = m_nodes[i+1];
        QPointF p0_widget = mapToWidget(node0.mainPoint);
        QPointF p1_widget = mapToWidget(node0.handleOut); // Outgoing handle from start node
        QPointF p2_widget = mapToWidget(node1.handleIn);  // Incoming handle to end node
        QPointF p3_widget = mapToWidget(node1.mainPoint);
        curvePath.cubicTo(p1_widget, p2_widget, p3_widget);
    }
    painter.setPen(QPen(Qt::white, 2));
    painter.drawPath(curvePath);

    // --- Draw Nodes and Handles ---
    for (int i = 0; i < m_nodes.size(); ++i) {
        const CurveNode& node = m_nodes[i];
        QPointF mainWidgetPos = mapToWidget(node.mainPoint);
        QPointF handleInWidgetPos = mapToWidget(node.handleIn);
        QPointF handleOutWidgetPos = mapToWidget(node.handleOut);

        // Draw Handles and lines first (so main point is on top)
        painter.setPen(QPen(Qt::cyan, 1)); // Handle lines
        if (i > 0) { // Node 0 has no incoming segment/handle to draw
            painter.setBrush( (m_selection.part == SelectedPart::HANDLE_IN && m_selection.nodeIndex == i) ? Qt::yellow : Qt::cyan);
            painter.drawEllipse(handleInWidgetPos, m_handleRadius, m_handleRadius);
            painter.drawLine(mainWidgetPos, handleInWidgetPos);
        }
        if (i < m_nodes.size() - 1) { // Last node has no outgoing segment/handle to draw
            painter.setBrush( (m_selection.part == SelectedPart::HANDLE_OUT && m_selection.nodeIndex == i) ? Qt::yellow : Qt::cyan);
            painter.drawEllipse(handleOutWidgetPos, m_handleRadius, m_handleRadius);
            painter.drawLine(mainWidgetPos, handleOutWidgetPos);
        }

        // Draw Main Point
        painter.setPen(QPen(Qt::black, 1));
        painter.setBrush( (m_selection.part == SelectedPart::MAIN_POINT && m_selection.nodeIndex == i) ? Qt::yellow : Qt::white);
        painter.drawEllipse(mainWidgetPos, m_mainPointRadius, m_mainPointRadius);
    }
}

void CurveWidget::mousePressEvent(QMouseEvent *event)
{
    m_selection = findNearbyPart(event->pos());
    m_dragging = (m_selection.part != SelectedPart::NONE);

    if (event->button() == Qt::LeftButton) {
        if (m_dragging) {
            // Point selected, ready for drag (no change needed here)
            //qDebug() << "Selected existing part:" << static_cast<int>(m_selection.part) << "Node:" << m_selection.nodeIndex;
        } else {
            // --- Add New Node using Curve Splitting ---
            ClosestSegmentResult hit = findClosestSegment(event->pos());

            // Add a small tolerance - don't insert if t is practically 0 or 1
            const qreal t_tolerance = 0.001;

            if (hit.segmentIndex != -1 && hit.t > t_tolerance && hit.t < (1.0 - t_tolerance)) {
                // Found a segment to split
                int i = hit.segmentIndex; // Index of the node *before* the new one
                qreal t = hit.t;

                // Get the original points of the segment being split
                const QPointF p0 = m_nodes[i].mainPoint;
                const QPointF p1 = m_nodes[i].handleOut;
                const QPointF p2 = m_nodes[i+1].handleIn;
                const QPointF p3 = m_nodes[i+1].mainPoint;

                // Perform the subdivision
                SubdivisionResult split = subdivideBezier(p0, p1, p2, p3, t);

                // Create the new node
                CurveNode newNode;
                newNode.mainPoint = split.pointOnCurve;
                newNode.handleIn = split.handle2_Seg1;  // Handle arriving at new node
                newNode.handleOut = split.handle1_Seg2; // Handle leaving new node

                // Update the handles of the surrounding nodes
                m_nodes[i].handleOut = split.handle1_Seg1;   // Update outgoing handle of node i
                m_nodes[i+1].handleIn = split.handle2_Seg2; // Update incoming handle of node i+1

                // Insert the new node into the vector
                m_nodes.insert(i + 1, newNode);

                m_samplesDirty = true; // Curve has changed
                m_selection = {SelectedPart::MAIN_POINT, i + 1}; // Select the newly added node's main point
                m_dragging = true; // Allow immediate dragging
                // qDebug() << "Inserted new node at index" << (i + 1) << "on segment" << i << "at t=" << t;

            } else {
                // Clicked too far from curve, or too close to an existing node
                // Do nothing, or provide feedback?
                // qDebug() << "Click too far from curve or too close to existing node.";
            }
            // --- End Add New Node ---
        }
    } else if (event->button() == Qt::RightButton) {
        // Delete node/handle (no change from previous version needed here)
        if (m_selection.part == SelectedPart::MAIN_POINT &&
            m_selection.nodeIndex > 0 && m_selection.nodeIndex < m_nodes.size() - 1)
        {
            m_nodes.remove(m_selection.nodeIndex);
            m_selection.part = SelectedPart::NONE;
            m_dragging = false;
            // No need to sort after removal if order was correct before
            m_samplesDirty = true;
        }
    }

    update(); // Trigger repaint
    emit curveChanged();
}
void CurveWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging || m_selection.part == SelectedPart::NONE) return;

    QPointF logicalPos = mapFromWidget(event->pos());
    CurveNode& node = m_nodes[m_selection.nodeIndex];
    QPointF* pointToMove = nullptr;
    QPointF* oppositeHandle = nullptr;
    SelectedPart movedPart = m_selection.part;


    // Clamp position differently based on what's selected
    qreal newX = logicalPos.x();
    qreal newY = logicalPos.y();

    // Clamp Y universally for now
    newY = std::max(0.0, std::min(1.0, newY));

    // Get pointer to the point being moved
    // Determine pointToMove and oppositeHandle (same as before)
    switch(movedPart) {
    // ... (cases for MAIN_POINT, HANDLE_IN, HANDLE_OUT setting pointToMove and oppositeHandle) ...
    case SelectedPart::MAIN_POINT:
        pointToMove = &node.mainPoint;
        { /* ... X clamping for main point ... */
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



    // --- Actually move the point ---
    QPointF oldPos = *pointToMove;
    pointToMove->setX(newX);
    pointToMove->setY(newY);
    QPointF delta = *pointToMove - oldPos; // Calculate how much it moved


    // --- Handle Constraints/Auto-movement (VERY BASIC EXAMPLE) ---
    // If moving a main point, move handles with it (relative positioning)
    if (m_selection.part == SelectedPart::MAIN_POINT) {
        // Only move handles if they weren't already coincident (avoids NaNs etc.)
        if (node.handleIn != oldPos) node.handleIn += delta;
        if (node.handleOut != oldPos) node.handleOut += delta;
        // Clamp handles after moving with main point
        node.handleIn.setY(std::max(0.0, std::min(1.0, node.handleIn.y())));
        node.handleOut.setY(std::max(0.0, std::min(1.0, node.handleOut.y())));
        node.handleIn.setX(std::max(0.0, std::min(1.0, node.handleIn.x()))); // Simple clamp for now
        node.handleOut.setX(std::max(0.0, std::min(1.0, node.handleOut.x())));// Simple clamp for now

    }

    // ***vvv START OF UPDATED CONSTRAINT LOGIC vvv***
    else if (oppositeHandle != nullptr && node.alignment != HandleAlignment::Free) {
        // Apply ALIGNED or MIRRORED constraints when moving a handle
        const QPointF& mainPt = node.mainPoint;
        QPointF vecMoved = *pointToMove - mainPt; // Vector from main to moved handle's NEW position

        qreal lenMovedSq = QPointF::dotProduct(vecMoved, vecMoved);
        QPointF newOppositePos; // Calculated new position for the opposite handle

        // Check if the moved handle is coincident with the main point
        if (lenMovedSq < 1e-12) // Use squared epsilon for comparison
        {
            // If coincident, make the opposite handle coincident too
            newOppositePos = mainPt;
        } else {
            // Calculate based on mode if moved handle is not coincident
            qreal lenMoved = qSqrt(lenMovedSq);
            QPointF normDirOpposite = -vecMoved / lenMoved; // Normalized vector in opposite direction

            if (node.alignment == HandleAlignment::Aligned) {
                // Keep opposite handle's ORIGINAL distance from main point
                QPointF vecOppositeOld = *oppositeHandle - mainPt;
                qreal lenOppositeOld = qSqrt(QPointF::dotProduct(vecOppositeOld, vecOppositeOld));
                newOppositePos = mainPt + normDirOpposite * lenOppositeOld;
            } else { // Mirrored
                // Use moved handle's NEW distance for the opposite handle
                newOppositePos = mainPt + normDirOpposite * lenMoved;
            }
        }

        // Update the opposite handle and clamp its position (using basic 0-1 clamp)
        oppositeHandle->setX(std::max(0.0, std::min(1.0, newOppositePos.x())));
        oppositeHandle->setY(std::max(0.0, std::min(1.0, newOppositePos.y())));

    } // ***^^^ END OF UPDATED CONSTRAINT LOGIC ^^^***

    // --- Re-sort if a main point's X changed order ---
    if (m_selection.part == SelectedPart::MAIN_POINT) {
        bool needsReSort = (m_selection.nodeIndex > 0 && m_selection.nodeIndex < m_nodes.size() - 1);
        if (needsReSort) {
            // Store position to re-select after sorting
            QPointF mainPointToFind = node.mainPoint;
            sortNodes();
            // Re-find index (fuzzy compare recommended but simple == might work)
            m_selection.nodeIndex = -1;
            for(int i=0; i<m_nodes.size(); ++i) {
                if(m_nodes[i].mainPoint == mainPointToFind) {
                    m_selection.nodeIndex = i;
                    break;
                }
            }
            if (m_selection.nodeIndex == -1) m_dragging = false; // Lost the point
        }
    }

    m_samplesDirty = true; // Curve shape changed
    update();
    emit curveChanged();
}

void CurveWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        // Optional: keep selection active?
        // m_selection.part = SelectedPart::NONE;
        // update();
    }
}

void CurveWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Mapping changes, but curve itself doesn't, only repaint needed
    update();
}

// --- Private Helper Functions ---

// Find nearby node part
CurveWidget::SelectionInfo CurveWidget::findNearbyPart(const QPoint& widgetPos, qreal mainRadius, qreal handleRadius)
{
    SelectionInfo closest = {SelectedPart::NONE, -1};
    qreal minDistSq = 1e10; // Use squared distance for efficiency

    // Convert widgetPos (QPoint) to QPointF once for calculations
    QPointF widgetPosF = QPointF(widgetPos);

    for (int i = 0; i < m_nodes.size(); ++i) {
        const CurveNode& node = m_nodes[i]; // Get reference to current node

        // Check Handles first (usually smaller target)
        if (i > 0) { // Check HandleIn for node i (controls segment i-1 ending at i)
            QPointF handleInWidget = mapToWidget(node.handleIn);
            QPointF diffVec = widgetPosF - handleInWidget; // Vector from handle to click
            qreal distSq = QPointF::dotProduct(diffVec, diffVec); // Calculate squared length
            if (distSq < handleRadius * handleRadius && distSq < minDistSq) {
                minDistSq = distSq;
                closest = {SelectedPart::HANDLE_IN, i};
            }
        }
        if (i < m_nodes.size() - 1) { // Check HandleOut for node i (controls segment i starting at i)
            QPointF handleOutWidget = mapToWidget(node.handleOut);
            QPointF diffVec = widgetPosF - handleOutWidget; // Vector from handle to click
            qreal distSq = QPointF::dotProduct(diffVec, diffVec); // Calculate squared length
            if (distSq < handleRadius * handleRadius && distSq < minDistSq) {
                minDistSq = distSq;
                closest = {SelectedPart::HANDLE_OUT, i};
            }
        }

        // Check Main Point for node i
        QPointF mainWidget = mapToWidget(node.mainPoint);
        QPointF diffVec = widgetPosF - mainWidget; // Vector from main point to click
        qreal distSq = QPointF::dotProduct(diffVec, diffVec); // Calculate squared length
        if (distSq < mainRadius * mainRadius && distSq < minDistSq) {
            // Update minimum distance even if we previously found a handle closer than mainRadius,
            // because main point takes precedence if within its radius.
            minDistSq = distSq;
            closest = {SelectedPart::MAIN_POINT, i};
        }
    }
    return closest;
}

// Sort nodes by main point X, ensuring endpoints are fixed at 0 and 1
void CurveWidget::sortNodes()
{
    if (m_nodes.size() <= 1) return;

    // Store original endpoints if needed (though they should be fixed)
    // QPointF firstMain = m_nodes.first().mainPoint;
    // QPointF lastMain = m_nodes.last().mainPoint;

    // Sort all points except the fixed first and last based on mainPoint.x
    if (m_nodes.size() > 2) {
        std::sort(m_nodes.begin() + 1, m_nodes.end() - 1,
                  [](const CurveNode& a, const CurveNode& b) {
                      return a.mainPoint.x() < b.mainPoint.x();
                  });
    }

    // Force endpoints X coordinate - handles should ideally be adjusted too,
    // but we rely on move logic to keep them reasonable for now.
    m_nodes.first().mainPoint.setX(0.0);
    m_nodes.last().mainPoint.setX(1.0);

    m_samplesDirty = true; // Sorting changes curve sampling
}


// --- Coordinate Mapping (Ensure using qreal) ---
QPointF CurveWidget::mapToWidget(const QPointF& logicalPoint) const
{
    const qreal margin = m_mainPointRadius + 2.0; // Use main radius for margin
    qreal usableWidth = width() - 2.0 * margin;
    qreal usableHeight = height() - 2.0 * margin;

    qreal widgetX = margin + logicalPoint.x() * usableWidth;
    qreal widgetY = margin + (1.0 - logicalPoint.y()) * usableHeight; // Y is inverted

    return QPointF(widgetX, widgetY);
}

QPointF CurveWidget::mapFromWidget(const QPoint& widgetPoint) const
{
    const qreal margin = m_mainPointRadius + 2.0;
    qreal usableWidth = width() - 2.0 * margin;
    qreal usableHeight = height() - 2.0 * margin;

    if (usableWidth <= 1e-6 || usableHeight <= 1e-6) {
        return QPointF(0.0, 0.0);
    }

    qreal logicalX = (static_cast<qreal>(widgetPoint.x()) - margin) / usableWidth;
    qreal logicalY = 1.0 - (static_cast<qreal>(widgetPoint.y()) - margin) / usableHeight;

    // Clamp results - move logic should handle constraints, but good safety
    logicalX = std::max(0.0, std::min(1.0, logicalX));
    logicalY = std::max(0.0, std::min(1.0, logicalY));

    return QPointF(logicalX, logicalY);
}


// --- Curve Sampling Approximation ---

// Function to evaluate a single cubic bezier segment at parameter t
inline QPointF evaluateBezier(const QPointF& p0, const QPointF& p1, const QPointF& p2, const QPointF& p3, qreal t) {
    qreal mt = 1.0 - t;
    qreal mt2 = mt * mt;
    qreal t2 = t * t;

    return p0 * mt * mt2 +       // (1-t)^3 * P0
           p1 * 3.0 * mt2 * t +  // 3 * (1-t)^2 * t * P1
           p2 * 3.0 * mt * t2 +  // 3 * (1-t) * t^2 * P2
           p3 * t * t2;          // t^3 * P3
}

// Re-calculate the dense lookup table for sampleCurve(x)
void CurveWidget::updateCurveSamples() const {
    m_curveSamples.clear();
    if (m_nodes.size() < 2) {
        m_samplesDirty = false;
        return;
    }

    m_curveSamples.reserve(m_numSamples + 1);

    int numSegments = m_nodes.size() - 1;
    qreal totalLengthApproximation = 0; // Could use this for adaptive sampling, but fixed steps for now

    for (int i = 0; i < numSegments; ++i) {
        // Estimate segment length? For now, divide samples evenly per segment
        int samplesPerSegment = m_numSamples / numSegments;
        if (i == 0) samplesPerSegment = m_numSamples - (numSegments - 1) * samplesPerSegment; // Give remainder to first

        const CurveNode& node0 = m_nodes[i];
        const CurveNode& node1 = m_nodes[i+1];

        // Add start point of segment (unless it's the very first point overall)
        if (i == 0) {
            m_curveSamples.append(node0.mainPoint);
        }

        // Sample along the segment (parameter t from >0 to 1)
        for (int j = 1; j <= samplesPerSegment; ++j) {
            qreal t = static_cast<qreal>(j) / samplesPerSegment;
            QPointF sample = evaluateBezier(node0.mainPoint, node0.handleOut, node1.handleIn, node1.mainPoint, t);
            // Ensure samples are monotonic in X for lower_bound to work correctly
            // (This might slightly distort the curve if handles make X non-monotonic)
            if (!m_curveSamples.isEmpty() && sample.x() < m_curveSamples.last().x()) {
                sample.setX(m_curveSamples.last().x()); // Clamp X if needed
            }
            // Clamp Y just in case
            sample.setY(std::max(0.0, std::min(1.0, sample.y())));
            m_curveSamples.append(sample);
        }
    }
    // Ensure last point is exactly the last node's main point
    if (!m_curveSamples.isEmpty() && m_curveSamples.last() != m_nodes.last().mainPoint) {
        if (m_curveSamples.last().x() <= m_nodes.last().mainPoint.x()){ // Only overwrite if X is not beyond
            m_curveSamples.append(m_nodes.last().mainPoint);
        } else {
            m_curveSamples.last() = m_nodes.last().mainPoint; // Overwrite last sample
        }
    }


    m_samplesDirty = false;
    // qDebug() << "Updated curve samples:" << m_curveSamples.size();
}

ClosestSegmentResult CurveWidget::findClosestSegment(const QPoint& widgetPos) const {
    ClosestSegmentResult bestMatch;
    if (m_nodes.size() < 2) return bestMatch; // No segments

    QPointF widgetPosF = QPointF(widgetPos);
    const int stepsPerSegment = 20; // Density for checking distance
    qreal minDistanceSq = std::numeric_limits<qreal>::max();

    // Iterate through each curve segment
    for (int i = 0; i < m_nodes.size() - 1; ++i) {
        const CurveNode& node0 = m_nodes[i];
        const CurveNode& node1 = m_nodes[i + 1];
        const QPointF p0 = node0.mainPoint;
        const QPointF p1 = node0.handleOut;
        const QPointF p2 = node1.handleIn;
        const QPointF p3 = node1.mainPoint;

        // Check distance at sample points along this segment
        for (int j = 0; j <= stepsPerSegment; ++j) {
            qreal t = static_cast<qreal>(j) / stepsPerSegment;
            QPointF pointOnCurve = evaluateBezier(p0, p1, p2, p3, t);
            QPointF pointWidget = mapToWidget(pointOnCurve);
            QPointF diffVec = widgetPosF - pointWidget;
            qreal distSq = QPointF::dotProduct(diffVec, diffVec);

            if (distSq < minDistanceSq) {
                minDistanceSq = distSq;
                bestMatch.segmentIndex = i;
                bestMatch.t = t;
                bestMatch.distanceSq = distSq;
            }
        }
    }

    // Optional: Add a maximum distance threshold?
    // const qreal maxDistThreshold = 30.0; // pixels
    // if (bestMatch.distanceSq > maxDistThreshold * maxDistThreshold) {
    //     bestMatch.segmentIndex = -1; // Too far away
    // }

    return bestMatch;
}


// Add this function implementation in curvewidget.cpp
void CurveWidget::resetCurve()
{
    m_nodes.clear(); // Remove all existing nodes

    // Re-initialize with the default straight line (same logic as constructor)
    CurveNode node0(QPointF(0.0, 0.0));
    CurveNode node1(QPointF(1.0, 1.0));


    // Place handles to create an initial straight line (e.g., 1/3rd along segment)
    // Make sure this matches your constructor's initial handle placement!
    node0.handleOut = QPointF(1.0/3.0, 1.0/3.0);
    node1.handleIn  = QPointF(2.0/3.0, 2.0/3.0);
    // If you initialized handles coincident with points, use that instead:
    // node0.handleOut = node0.mainPoint;
    // node1.handleIn = node1.mainPoint;


    m_nodes.append(node0);
    m_nodes.append(node1);

    // Reset state
    m_samplesDirty = true; // Mark samples needing update
    m_selection = {SelectedPart::NONE, -1}; // Clear any active selection
    m_dragging = false; // Ensure dragging state is off

    // Notify UI and update display
    update(); // Repaint the widget
    emit curveChanged(); // Notify MainWindow (e.g., to update preview)
}




void CurveWidget::keyPressEvent(QKeyEvent *event)
{
    // Check if a main point is selected and it's not an endpoint (where alignment doesn't apply)
    if (m_selection.part == SelectedPart::MAIN_POINT &&
        m_selection.nodeIndex > 0 && m_selection.nodeIndex < m_nodes.size() - 1)
    {
        bool modeChanged = false;
        CurveNode& node = m_nodes[m_selection.nodeIndex]; // Get reference
        HandleAlignment originalMode = node.alignment; // Store original mode
        HandleAlignment newMode = originalMode;       // Initialize new mode

        // Determine new mode based on key press
        switch (event->key()) {
        case Qt::Key_F: newMode = HandleAlignment::Free; break;
        case Qt::Key_A: newMode = HandleAlignment::Aligned; break;
        case Qt::Key_M: newMode = HandleAlignment::Mirrored; break;
        default:
            QWidget::keyPressEvent(event); // Pass unhandled keys
            return;
        }

        // Check if mode actually changed
        if (newMode != originalMode) {
            node.alignment = newMode; // Apply the new mode
            modeChanged = true;
            qDebug() << "Node" << m_selection.nodeIndex << "set to" << static_cast<int>(newMode);

            // --- IMMEDIATE SNAP LOGIC ---
            // If switched TO Aligned or Mirrored, snap handleIn based on handleOut
            if (newMode == HandleAlignment::Aligned || newMode == HandleAlignment::Mirrored) {
                const QPointF& mainPt = node.mainPoint;
                QPointF* hIn = &node.handleIn;  // Pointer to handleIn
                QPointF* hOut = &node.handleOut; // Pointer to handleOut

                QPointF vecOut = *hOut - mainPt; // Vector from main to handleOut
                qreal lenOutSq = QPointF::dotProduct(vecOut, vecOut);
                QPointF newInPos; // Calculated new position for handleIn

                if (lenOutSq < 1e-12) {
                    // If handleOut is coincident, make handleIn coincident too
                    newInPos = mainPt;
                } else {
                    qreal lenOut = qSqrt(lenOutSq);
                    QPointF normDirIn = -vecOut / lenOut; // Normalized opposite direction

                    if (newMode == HandleAlignment::Aligned) {
                        // Keep handleIn's original distance
                        QPointF vecInOld = *hIn - mainPt;
                        qreal lenInOld = qSqrt(QPointF::dotProduct(vecInOld, vecInOld));
                        newInPos = mainPt + normDirIn * lenInOld;
                    } else { // Mirrored
                        // Use handleOut's distance for handleIn
                        newInPos = mainPt + normDirIn * lenOut;
                    }
                }
                // Update handleIn and clamp its position
                hIn->setX(std::max(0.0, std::min(1.0, newInPos.x())));
                hIn->setY(std::max(0.0, std::min(1.0, newInPos.y())));
            }
            // --- END IMMEDIATE SNAP LOGIC ---

            m_samplesDirty = true; // Curve shape might have changed due to snap
            update();           // Repaint to show potential snap
            emit curveChanged(); // Emit signal
        } // end if(modeChanged)

        event->accept(); // Indicate we handled the key press

    } else { // If no valid main point selected, or it's an endpoint
        QWidget::keyPressEvent(event); // Pass key press to base class
    }
}
