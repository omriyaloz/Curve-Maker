#ifndef ANIMATIONPREVIEWWIDGET_H
#define ANIMATIONPREVIEWWIDGET_H

#include <QWidget>
#include <QTimer>
#include <QPointer> // Use QPointer for safe CurveWidget pointer

// Forward declare CurveWidget to avoid full header include here
class CurveWidget;

class AnimationPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AnimationPreviewWidget(QWidget *parent = nullptr);

    // Call this from MainWindow to link the widgets
    void setCurveWidget(CurveWidget *widget);

    // Set animation duration in milliseconds
    void setLoopDuration(int ms);

protected:
    // Override paint event for custom drawing
    void paintEvent(QPaintEvent *event) override;

private slots:
    // Slot connected to the timer's timeout signal
    void updateAnimation();

private:
    QTimer m_timer;           // Timer to drive the animation
    qreal m_currentTime;      // Current time in the animation loop [0.0, 1.0]
    int m_loopDurationMs;   // Duration of one animation loop in milliseconds

    // Use QPointer for safety - it becomes null if the CurveWidget is deleted
    QPointer<CurveWidget> m_curveWidget;

    // Drawing parameters (can be adjusted)
    const int m_padding = 10; // Padding around the animation area
    const qreal m_objectRadius = 10.0; // Radius of the circle/sphere
};

#endif // ANIMATIONPREVIEWWIDGET_H
