#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QImage>       // Include QImage
#include "curvewidget.h" // Include CurveWidget header

// Forward declarations
namespace Ui {
class MainWindow; // Forward declare the UI class
}
class QButtonGroup; // Forward declare QButtonGroup
class QAbstractButton; // Forward declare QAbstractButton for button group slot

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // Slots for UI elements (auto-connected or manually connected)
    void on_browseButton_clicked();
    void on_exportButton_clicked();
    void on_resetButton_clicked(); // Resets the active curve
    void on_actionToggleDarkMode_toggled(bool checked);
    void on_modeBtn_clicked(bool checked);

    // Slots for alignment buttons (no change needed here)
    void on_freeBtn_clicked();
    void on_alignedBtn_clicked();
    void on_mirroredBtn_clicked();

    // Slots connected to CurveWidget signals
    void updateLUTPreview(); // Updates the 3D LUT preview image
    //void onCurveSelectionChanged(int nodeIndex, CurveWidget::HandleAlignment currentAlignment);
    void onCurveSelectionChanged(); // New signature

    // Slot for channel selection button group
    void onChannelButtonClicked(QAbstractButton *button);
    void on_actionPreviewRgb_toggled(bool checked);
    void on_actionInactiveChannels_toggled(bool checked);

    void on_clampHandlesCheckbox_stateChanged(int state);

    void onSaveCurvesActionTriggered();
    void onLoadCurvesActionTriggered();
private:
    // --- Helper Functions ---
    void applyTheme(bool dark);
    // Renamed and updated function to generate 3D LUT image
    QImage generateLutImage3D(int size);
    QImage generateCombinedRgbLut1D(int width, int bitDepth=8);
    QImage generateSingleChannelLut1D(CurveWidget::ActiveChannel channel, int width);

    // --- Member Variables ---
    Ui::MainWindow *ui;         // Pointer to the UI elements
    int m_selectedNodeIndex;    // Index of the selected node in the *active* curve
    QButtonGroup *m_channelGroup; // Button group for R, G, B channel selection
    bool m_isPreviewRgbCombined;

};

#endif // MAINWINDOW_H
