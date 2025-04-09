#include "mainwindow.h"
#include "ui_mainwindow.h" // The header generated from mainwindow.ui
#include "curvewidget.h"   // Make sure it's included

#include <QFileDialog>    // For browse dialog
#include <QMessageBox>    // For showing messages
#include <QPixmap>        // For displaying preview
#include <QDebug>         // For debug output

#include <QStandardPaths> // To find standard locations like Desktop
#include <QDir>           // For path manipulation (optional but good practice)
#include <QFileInfo>      // For robust path joining (optional)

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow) // Create the UI instance
{
    ui->setupUi(this); // Set up the UI defined in the .ui file


    // --- Set Default Export Path ---
    QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    QString defaultFileName = "curve_lut.png"; // Your desired default filename
    QString defaultFullPath;

    if (!desktopPath.isEmpty()) {
        // Method 1: Using QDir (robust way to join path and filename)
        QDir desktopDir(desktopPath);
        defaultFullPath = desktopDir.filePath(defaultFileName);

        // Method 2: Simple string concatenation (less robust with separators)
        // defaultFullPath = desktopPath + QDir::separator() + defaultFileName;
    } else {
        // Fallback if Desktop path couldn't be determined (rare)
        qWarning() << "Could not determine Desktop path. Using current directory.";
        defaultFullPath = defaultFileName; // Default to just the filename
    }
    ui->filePathLineEdit->setText(defaultFullPath);

    // --- Populate LUT Size ComboBox ---
    QList<int> lutSizes = {32, 64, 128, 256, 512}; // Add/remove sizes as needed
    for (int size : lutSizes) {
        // Add item: Display text is the number, UserData holds the integer value
        ui->lutSizeComboBox->addItem(QString::number(size), QVariant(size));
    }
    ui->lutSizeComboBox->setCurrentText("256"); // Set default selection

    // --- Connect Signals to Slots ---

    // Auto-connections work for on_<objectName>_<signalName> slots

    // Manually connect the curveChanged signal from our custom widget
    // to the updateLUTPreview slot in this window.
    connect(ui->curveWidget, &CurveWidget::curveChanged,
            this, &MainWindow::updateLUTPreview);


    updateLUTPreview(); // Show initial preview

}

MainWindow::~MainWindow()
{
    delete ui; // Clean up the UI
}

// --- Slot Implementations ---

void MainWindow::on_browseButton_clicked()
{

    QString currentSuggestion = ui->filePathLineEdit->text();
    QString fileName = QFileDialog::getSaveFileName(this,
                                                    tr("Save LUT Texture"),       // Dialog Title
                                                    currentSuggestion,                           // Initial directory (empty means last used or default)
                                                    tr("Image Files (*.png *.bmp *.jpg);;All Files (*)")); // File filters

    if (!fileName.isEmpty()) {
        // Ensure file has a proper extension if user didn't add one (optional but nice)
        QFileInfo info(fileName);
        if (info.suffix().isEmpty()) {
            // Check the selected filter or default to png
            if (info.baseName() == fileName) { // Check if really no extension
                fileName += ".png"; // Default to png
            }
        }
        ui->filePathLineEdit->setText(fileName);
    }
}

void MainWindow::updateLUTPreview()
{
    // Generate a small image for preview (e.g., 256 wide)
    // Use the actual spinbox value for correctness, though scaling helps
    int previewSize = ui->lutSizeComboBox->currentData().toInt(); // Or fixed like 256
    if (previewSize < 2) previewSize = 2;
    QImage lutImage = generateLUTImage(previewSize);

    if (!lutImage.isNull()) {
        // Create a pixmap and scale it to fit the label's width, keeping aspect ratio (sort of, it's 1 pixel high)
        QPixmap lutPixmap = QPixmap::fromImage(lutImage);


        // Scale pixmap to fit the label width, making it taller for visibility
        ui->lutPreviewLabel->setPixmap(lutPixmap.scaled(ui->lutPreviewLabel->width(), ui->lutPreviewLabel->height(),
                                                        Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        ui->lutPreviewLabel_3->setPixmap(lutPixmap.scaled(ui->lutPreviewLabel_3->width(), ui->lutPreviewLabel_3->height(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    } else {
        ui->lutPreviewLabel->clear(); // Clear if image generation failed
        ui->lutPreviewLabel_3->clear();
    }
}

void MainWindow::on_exportButton_clicked()
{
    // 1. Get Parameters
    QString filePath = ui->filePathLineEdit->text();
    //int lutSize = ui->lutSizeSpinBox->value();
    int lutSize = ui->lutSizeComboBox->currentData().toInt();


    // 2. Validate Parameters
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, tr("Export Error"), tr("Please specify an export file path."));
        return;
    }
    if (lutSize < 2) {
        QMessageBox::warning(this, tr("Export Error"), tr("LUT size must be at least 2."));
        return;
    }

    // 3. Generate LUT Image
    QImage lutImage = generateLUTImage(lutSize);
    if (lutImage.isNull()) {
        QMessageBox::critical(this, tr("Export Error"), tr("Failed to generate LUT data."));
        return;
    }

    // 4. Save Image
    if (lutImage.save(filePath)) {
        QMessageBox::information(this, tr("Export Successful"), tr("LUT texture saved to:\n%1").arg(filePath));
    } else {
        QMessageBox::critical(this, tr("Export Error"), tr("Failed to save LUT texture to:\n%1").arg(filePath));
    }
}

// --- Helper Function ---

QImage MainWindow::generateLUTImage(int size)
{
    if (size < 2 || !ui->curveWidget) { // Basic check
        return QImage(); // Return null image
    }

    // Create a 1D Grayscale image
    QImage image(size, 1, QImage::Format_Grayscale8);
    if (image.isNull()) {
        qWarning() << "Failed to create QImage for LUT.";
        return QImage();
    }

    uchar *line = image.scanLine(0); // Get pointer to the single row of pixel data

    for (int i = 0; i < size; ++i) {
        // Normalize x coordinate to 0.0 - 1.0
        qreal x = static_cast<qreal>(i) / (size - 1.0);

        // Sample the curve using the function from CurveWidget
        qreal y = ui->curveWidget->sampleCurve(x); // Access the promoted widget via ui pointer

        // Clamp result just in case sampleCurve goes slightly out of bounds
        y = std::max(0.0, std::min(1.0, y));

        // Convert normalized y (0.0 - 1.0) to grayscale byte (0 - 255)
        line[i] = static_cast<uchar>(std::round(y * 255.0));
    }

    return image;
}

// Add this slot implementation in mainwindow.cpp
void MainWindow::on_resetButton_clicked()
{
    // Check if the curveWidget pointer is valid (it should be)
    if (ui->curveWidget) {
        ui->curveWidget->resetCurve(); // Call the reset function on the widget instance
    }
}
