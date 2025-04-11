#ifndef CURVEWIDGET_H
#define CURVEWIDGET_H

#include <QWidget>
#include <QVector>
#include <QPointF>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath> // Needed for cubicTo
#include <QKeyEvent>
#include <QDebug> // For debugging output
#include <QUndoStack>


//class CurveWidget;


// Structure to return segment finding result
struct ClosestSegmentResult {
    int segmentIndex = -1; // Index of the segment (starts at 0)
    qreal t = 0.0;         // Parameter (0-1) along the segment
    qreal distanceSq = std::numeric_limits<qreal>::max(); // Min distance found
};

class CurveWidget : public QWidget
{
    Q_OBJECT

public:
    // Structure to hold a main point and its associated control handles
    enum class HandleAlignment {
        Free, // Handles move independently
        Aligned, // Handles stay collinear with main point, opposite directions, independent length
        Mirrored // Aligned + equidistant from main point
    };

    struct CurveNode {
        QPointF mainPoint;
        QPointF handleIn;
        QPointF handleOut;
        // Use unqualified name now, as it's in the same class scope
        HandleAlignment alignment = HandleAlignment::Aligned; // Default

        // Declaration only - Define implementation in .cpp
        CurveNode(QPointF p = QPointF(0,0));


        bool operator==(const CurveNode& other) const {
            return mainPoint == other.mainPoint &&
                   handleIn == other.handleIn &&
                   handleOut == other.handleOut &&
                   alignment == other.alignment;
        }
    };

    explicit CurveWidget(QWidget *parent = nullptr);
    QVector<CurveNode> getNodes() const;
    qreal sampleCurve(qreal x) const;
    void resetCurve();

    QUndoStack* undoStack() { return &m_undoStack; }
    friend class SetCurveStateCommand;
    void setDarkMode(bool dark);

public slots:
    void setNodeAlignment(int nodeIndex, CurveWidget::HandleAlignment mode);


signals:
    void curveChanged(); // Signal emitted when the curve data is modified
    // Emits node index (-1 if none) and its current alignment when selection changes
    void selectionChanged(int nodeIndex, CurveWidget::HandleAlignment currentAlignment);

protected:
    // --- Event handlers ---
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void restoreNodesInternal(const QVector<CurveNode>& nodes);

private:
    // --- Helper functions ---
    QPointF mapToWidget(const QPointF& logicalPoint) const;
    QPointF mapFromWidget(const QPoint& widgetPoint) const;



    ClosestSegmentResult findClosestSegment(const QPoint& widgetPos) const;

    // Find which part (main point, handle) is near the click
    enum class SelectedPart { NONE, MAIN_POINT, HANDLE_IN, HANDLE_OUT };
    struct SelectionInfo {
        SelectedPart part = SelectedPart::NONE;
        int nodeIndex = -1;
    };

    SelectionInfo findNearbyPart(const QPoint& widgetPos, qreal mainRadius = 10.0, qreal handleRadius = 8.0);

    void sortNodes(); // Ensure nodes are sorted by mainPoint.x()
    void updateCurveSamples() const;

    void applyAlignmentSnap(int nodeIndex);

    // --- Member variables ---
private:
    QVector<CurveNode> m_nodes; // Stores the curve nodes and handles

    // Selection state
    SelectionInfo m_selection;
    bool m_dragging;

    const qreal m_mainPointRadius = 5.0; // Visual radius of main points
    const qreal m_handleRadius = 4.0;    // Visual radius of handles

    // Cache for approximated curve points (for sampleCurve)
    mutable QVector<QPointF> m_curveSamples;
    mutable bool m_samplesDirty = true; // Flag to recalculate samples when curve changes
     // Recalculate the lookup table
    const int m_numSamples = 256; // Number of samples for approximation LUT
    // *** Add Undo Stack member ***
    QUndoStack m_undoStack;

    // *** Add member to store state before an action starts ***
    QVector<CurveNode> m_stateBeforeAction;
    bool m_isDarkMode = false;

};



#endif // CURVEWIDGET_H
