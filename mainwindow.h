#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QImage> // For LUT generation/preview

// Forward declaration is sufficient here if ui file includes curvewidget.h
// Or include the header directly if needed

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

private:
    Ui::MainWindow *ui; // Pointer to the UI elements defined in mainwindow.ui
    QImage generateLUTImage(int size); // Helper function
};
#endif // MAINWINDOW_H
