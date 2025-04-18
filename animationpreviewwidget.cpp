#include "animationpreviewwidget.h"
#include "curvewidget.h" 

// Qt Includes
#include <QDebug>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>      
#include <QPointF>   
#include <QtMath>




AnimationPreviewWidget::AnimationPreviewWidget(QWidget *parent)
    : QWidget(parent),
    m_currentTime(0.0),
    m_loopDurationMs(2000),
    m_curveWidget(nullptr)
{
    connect(&m_timer, &QTimer::timeout, this, &AnimationPreviewWidget::updateAnimation);
    m_timer.setInterval(16);
    m_timer.start();

    setMinimumSize(50, 100);
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
}

void AnimationPreviewWidget::setCurveWidget(CurveWidget *widget) {
    m_curveWidget = widget;
    update();
}

void AnimationPreviewWidget::setLoopDuration(int ms) {
    if (ms > 50) {
        m_loopDurationMs = ms;
    }
}

void AnimationPreviewWidget::updateAnimation() {
    if (m_loopDurationMs <= 0) return;

    qreal increment = static_cast<qreal>(m_timer.interval()) / m_loopDurationMs;
    m_currentTime += increment;

    if (m_currentTime >= 1.0) {
        m_currentTime -= 1.0;
    }

    update();
}

void AnimationPreviewWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.fillRect(rect(), palette().color(QPalette::Base));

    if (!m_curveWidget) {
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(rect(), Qt::AlignCenter, tr("No Curve Set"));
        return;
    }

    int availableWidth = width() - 2 * m_padding;
    int availableHeight = height() - 2 * m_padding;
    if (availableWidth <= 0 || availableHeight <= 0) return;

    painter.setPen(palette().color(QPalette::Mid));
    painter.drawLine(width() / 2, m_padding, width() / 2, height() - m_padding);
    painter.drawLine(width() / 2 - 5, m_padding, width() / 2 + 5, m_padding);
    painter.drawLine(width() / 2 - 5, height() - m_padding, width() / 2 + 5, height() - m_padding);

    CurveWidget::ActiveChannel activeChannel = m_curveWidget->getActiveChannel();
    qreal easedT = m_curveWidget->sampleCurveChannel(activeChannel, m_currentTime);
    easedT = std::max(0.0, std::min(1.0, easedT)); // Use std::max/min

    qreal drawY = (height() - m_padding) - (easedT * availableHeight);
    qreal drawX = width() / 2.0;

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
