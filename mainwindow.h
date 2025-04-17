#ifndef MAINWINDOW_H
#define MAINWINDOW_H

// Qt Includes
#include <QMainWindow>
#include <QImage>

// Project Includes
#include "curvewidget.h" // Requires CurveWidget::ActiveChannel

// Forward Declarations
namespace Ui {
class MainWindow;
}
class QButtonGroup;
class QAbstractButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_browseButton_clicked();
    void on_exportButton_clicked();
    void on_resetButton_clicked();
    void on_actionToggleDarkMode_toggled(bool checked);
    void on_modeBtn_clicked(bool checked);
    void on_freeBtn_clicked();
    void on_alignedBtn_clicked();
    void on_mirroredBtn_clicked();
    void updateLUTPreview();
    void onCurveSelectionChanged();
    void onChannelButtonClicked(QAbstractButton *button);
    void on_actionPreviewRgb_toggled(bool checked);
    void on_actionInactiveChannels_toggled(bool checked);
    void on_clampHandlesCheckbox_stateChanged(int state);
    void onSaveCurvesActionTriggered();
    void onLoadCurvesActionTriggered();

private:
    // Helper Functions
    void applyTheme(bool dark);
    QImage generateLutImage3D(int size);
    QImage generateCombinedRgbLut1D(int width, int bitDepth = 8);
    QImage generateSingleChannelLut1D(CurveWidget::ActiveChannel channel, int width);

    // Member Variables
    Ui::MainWindow *ui;
    int m_selectedNodeIndex;
    QButtonGroup *m_channelGroup;
    bool m_isPreviewRgbCombined;
};

#endif
