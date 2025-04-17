#include "animationpreviewwidget.h"
#include "curvewidget.h" // Include full header here for enum/methods
#include <QPainter>
#include <QPaintEvent>
#include <QDebug>
#include <QtMath>     // For qreal calculations

AnimationPreviewWidget::AnimationPreviewWidget(QWidget *parent)
    : QWidget(parent),
    m_currentTime(0.0),
    m_loopDurationMs(2000), // Default loop duration: 2 seconds
    m_curveWidget(nullptr)  // Initialize pointer to null
{
    // Timer setup
    connect(&m_timer, &QTimer::timeout, this, &AnimationPreviewWidget::updateAnimation);
    m_timer.setInterval(16); // Target ~60 FPS update rate (1000ms / 60fps ~= 16.6ms)
    m_timer.start();

    // Set a reasonable minimum size
    setMinimumSize(50, 100);
    // Expand vertically, fixed horizontally (or adjust as needed)
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
}

void AnimationPreviewWidget::setCurveWidget(CurveWidget *widget) {
    m_curveWidget = widget; // Store the pointer
    update(); // Update display when widget is set
}

void AnimationPreviewWidget::setLoopDuration(int ms) {
    if (ms > 50) { // Basic validation
        m_loopDurationMs = ms;
    }
}

void AnimationPreviewWidget::updateAnimation() {
    if (m_loopDurationMs <= 0) return; // Avoid division by zero

    // Calculate time increment based on timer interval and loop duration
    qreal increment = static_cast<qreal>(m_timer.interval()) / m_loopDurationMs;
    m_currentTime += increment;

    // Wrap time around 0.0 to 1.0
    if (m_currentTime >= 1.0) {
        m_currentTime -= 1.0;
        // Small correction if increment overshoots significantly (optional)
        // m_currentTime = qMax(0.0, m_currentTime);
    }

    // Schedule a repaint
    update();
}

void AnimationPreviewWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // --- Basic Background ---
    painter.fillRect(rect(), palette().color(QPalette::Base)); // Use theme background

    // --- Check if CurveWidget is set ---
    if (!m_curveWidget) {
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(rect(), Qt::AlignCenter, tr("No Curve Set"));
        return;
    }

    // --- Calculate Drawing Area ---
    int availableWidth = width() - 2 * m_padding;
    int availableHeight = height() - 2 * m_padding;
    if (availableWidth <= 0 || availableHeight <= 0) return; // Nothing to draw

    // --- Draw Frame/Track (Optional) ---
    painter.setPen(palette().color(QPalette::Mid)); // Mid-gray color
    painter.drawLine(width() / 2, m_padding, width() / 2, height() - m_padding); // Vertical line
    painter.drawLine(width() / 2 - 5, m_padding, width() / 2 + 5, m_padding); // Top marker
    painter.drawLine(width() / 2 - 5, height() - m_padding, width() / 2 + 5, height() - m_padding); // Bottom marker

    // --- Get Eased Position ---
    CurveWidget::ActiveChannel activeChannel = m_curveWidget->getActiveChannel();
    // Use m_currentTime (0->1) as the input 't' for the curve sampling
    qreal easedT = m_curveWidget->sampleCurveChannel(activeChannel, m_currentTime);
    easedT = std::max(0.0, std::min(1.0, easedT)); // Ensure result is clamped [0,1]

    // --- Calculate Object Position ---
    // Map easedT [0,1] to the vertical pixel space within padding
    // easedT = 0.0 -> bottom ; easedT = 1.0 -> top
    qreal drawY = (height() - m_padding) - (easedT * availableHeight);
    qreal drawX = width() / 2.0; // Center horizontally

    // --- Draw Object ---
    //painter.setPen(Qt::white); //Circle outline

    // Use a color based on the active channel?
    QColor objectColor;
    switch(activeChannel) {
    case CurveWidget::ActiveChannel::RED:   objectColor = Qt::red;   break;
    case CurveWidget::ActiveChannel::GREEN: objectColor = Qt::green; break;
    case CurveWidget::ActiveChannel::BLUE:  objectColor = Qt::blue;  break;
    default: objectColor = palette().color(QPalette::Highlight); break;
    }
    painter.setBrush(objectColor);
    QPen outlinePen;
    outlinePen.setColor(palette().color(QPalette::WindowText));
    outlinePen.setWidthF(1.5);
    outlinePen.setStyle(Qt::SolidLine);
    painter.setPen(outlinePen);

    painter.drawEllipse(QPointF(drawX, drawY), m_objectRadius, m_objectRadius);
}
