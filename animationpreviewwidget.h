#ifndef ANIMATIONPREVIEWWIDGET_H
#define ANIMATIONPREVIEWWIDGET_H

// Qt Includes
#include <QWidget>
#include <QTimer>
#include <QPointer>

// Forward Declarations
class CurveWidget;

class AnimationPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AnimationPreviewWidget(QWidget *parent = nullptr);

    void setCurveWidget(CurveWidget *widget);
    void setLoopDuration(int ms);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void updateAnimation();

private:
    QTimer m_timer;
    qreal m_currentTime;
    int m_loopDurationMs;
    QPointer<CurveWidget> m_curveWidget;

    const int m_padding = 10;
    const qreal m_objectRadius = 10.0;
};

#endif
