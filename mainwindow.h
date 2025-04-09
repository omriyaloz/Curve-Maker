#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QImage> // For LUT generation/preview
#include "curvewidget.h"
//#include <QButtonGroup>



QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; } // Forward declaration of the UI class
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots: // Declare the functions that will handle UI events
    void on_exportButton_clicked();
    void on_browseButton_clicked();
    void updateLUTPreview();
    void on_resetButton_clicked();

    void on_freeBtn_clicked();
    void on_alignedBtn_clicked();
    void on_mirroredBtn_clicked();
    void onCurveSelectionChanged(int nodeIndex, CurveWidget::HandleAlignment currentAlignment);

private:
    Ui::MainWindow *ui; // Pointer to the UI elements defined in mainwindow.ui
    QImage generateLUTImage(int size, QImage::Format format); // Helper function
    int m_selectedNodeIndex = -1;
    //QButtonGroup *m_alignmentGroup;
};
#endif // MAINWINDOW_H
