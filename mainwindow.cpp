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
#include <QButtonGroup>

#include <QAction>
#include <QMenu> // If adding to menu
#include <QUndoStack> // Needed for createUndoAction
#include <QKeySequence>

#include <QApplication> // For QApplication::setPalette/palette
#include <QPalette>     // For QPalette
#include <QStyleFactory>// Optional: To force a consistent style
#include <QSettings>    // To save the theme preference

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow) // Create the UI instance
    , m_selectedNodeIndex(-1)
{
    ui->setupUi(this); // Set up the UI defined in the .ui file

    qApp->setStyle(QStyleFactory::create("Fusion"));
    // --- Load Theme Preference ---
    QSettings settings("MyCompany", "CurveMaker");
    bool useDarkMode = settings.value("Appearance/DarkMode", false).toBool();
    ui->actionToggleDarkMode->setChecked(useDarkMode); // This will trigger on_actionToggleDarkMode_toggled if connected

    // Ensure connection exists (auto-connection should work)
    // connect(ui->actionToggleDarkMode, &QAction::toggled, this, &MainWindow::on_actionToggleDarkMode_toggled);

    // Apply initial theme explicitly if setChecked doesn't trigger reliably before show()
    applyTheme(useDarkMode);
    // ---------------------------

    // *** Create Undo/Redo Actions using CurveWidget's Stack ***
    // Ensure curveWidget is valid before accessing its stack
    if (ui->curveWidget) {
        QUndoStack *undoStack = ui->curveWidget->undoStack(); // Get stack via getter

        QAction *undoAction = undoStack->createUndoAction(this, tr("&Undo"));
        undoAction->setShortcut(QKeySequence::Undo); // e.g., Ctrl+Z

        QAction *redoAction = undoStack->createRedoAction(this, tr("&Redo"));
        redoAction->setShortcut(QKeySequence::Redo); // e.g., Ctrl+Y / Ctrl+Shift+Z

        // --- Add actions to an "Edit" menu (Create menu in Designer first!) ---
        // Assuming you created a menu named 'menuEdit' in mainwindow.ui
        if(ui->menuEdit) { // Check if menu exists
            ui->menuEdit->addAction(undoAction);
            ui->menuEdit->addAction(redoAction);
            // Add separators if needed: ui->menuEdit->addSeparator();
        } else {
            qWarning() << "Could not find menu 'menuEdit'. Add it in the UI Designer.";
        }

        // --- Optional: Add actions to a toolbar ---
        // Assuming you created a toolbar named 'mainToolBar' in mainwindow.ui
        // if(ui->mainToolBar) {
        //    ui->mainToolBar->addAction(undoAction);
        //    ui->mainToolBar->addAction(redoAction);
        // }

    } else {
        qCritical() << "CurveWidget instance is null!";
    }

    // --- Populate Bit Depth ComboBox ---
    // Add items: Display Text, UserData (stores the QImage::Format value)
    ui->bitDepthComboBox->addItem("8-bit Grayscale", QVariant::fromValue(QImage::Format_Grayscale8));
    ui->bitDepthComboBox->addItem("16-bit Grayscale", QVariant::fromValue(QImage::Format_Grayscale16));
    // Set 16-bit as the default selection
    ui->bitDepthComboBox->setCurrentIndex(1);
    // ------------------------------------

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

    // // --- Optional: Set up Button Group ---
    // m_alignmentGroup = new QButtonGroup(this);
    // m_alignmentGroup->addButton(ui->freeBtn);
    // m_alignmentGroup->addButton(ui->alignedBtn);
    // m_alignmentGroup->addButton(ui->mirroredBtn);
    // m_alignmentGroup->setExclusive(true); // Only one can be checked
    // //------------------------------------


    // --- Populate LUT Size ComboBox ---
    QList<int> lutSizes = {32, 64, 128, 256, 512}; // Add/remove sizes as needed
    for (int size : lutSizes) {
        // Add item: Display text is the number, UserData holds the integer value
        ui->lutSizeComboBox->addItem(QString::number(size), QVariant(size));
    }
    ui->lutSizeComboBox->setCurrentText("256"); // Set default selection

    // --- Connect Signals to Slots ---


    connect(ui->curveWidget, &CurveWidget::curveChanged,
            this, &MainWindow::updateLUTPreview);

    connect(ui->curveWidget, &CurveWidget::selectionChanged,
            this, &MainWindow::onCurveSelectionChanged);

    applyTheme(useDarkMode);
    updateLUTPreview(); // Show initial preview

    // --- Initial Button State ---
    // Disable buttons initially as nothing is selected
    ui->freeBtn->setEnabled(false);
    ui->alignedBtn->setEnabled(false);
    ui->mirroredBtn->setEnabled(false);
    // --------------------------



}

MainWindow::~MainWindow()
{
    delete ui; // Clean up the UI
}


// --- Slot for Theme Toggle Action ---
void MainWindow::on_actionToggleDarkMode_toggled(bool checked)
{
    applyTheme(checked);

    // --- Save Theme Preference ---
    QSettings settings("MyCompany", "CurveMaker");
    settings.setValue("Appearance/DarkMode", checked);
    // ---------------------------
}
// --- Helper Function to Apply Theme ---
void MainWindow::applyTheme(bool dark)
{
    QString styleSheet = "";
    if (dark) {
        // Load dark stylesheet from resources
        QFile f(":/themes/dark.qss");
        if (f.open(QFile::ReadOnly | QFile::Text)) {
            QTextStream ts(&f);
            styleSheet = ts.readAll();
            f.close();
        } else {
            qWarning() << "Could not load dark theme file :/themes/dark.qss";
        }
    } else {
        // Load light stylesheet (could be empty or contain overrides)
        QFile f(":/themes/light.qss"); // Or just set styleSheet = "";
        if (f.open(QFile::ReadOnly | QFile::Text)) {
            QTextStream ts(&f);
            styleSheet = ts.readAll();
            f.close();
        } else {
            // Okay if light theme is empty/missing, will revert
        }
    }

    // Apply the loaded stylesheet globally
    qApp->setStyleSheet(styleSheet);

    // Also explicitly reset palette when switching to light? Optional.
    // if (!dark) {
    //     qApp->setPalette(qApp->style()->standardPalette());
    // }

    // Update the custom widget explicitly
    if (ui->curveWidget) {
        ui->curveWidget->setDarkMode(dark);
    }
}


void MainWindow::onCurveSelectionChanged(int nodeIndex, CurveWidget::HandleAlignment currentAlignment) {
    m_selectedNodeIndex = nodeIndex; // Store the selected index

    // Determine if an intermediate node is selected (where alignment matters most)
    // Need to ask curveWidget for node count if it changed
    int nodeCount = ui->curveWidget ? ui->curveWidget->getNodes().size() : 0;
    bool intermediateNodeSelected = (nodeIndex > 0 && nodeIndex < nodeCount - 1);

    // Enable/disable buttons based on selection
    // Only enable if an intermediate node is selected
    ui->freeBtn->setEnabled(intermediateNodeSelected);
    ui->alignedBtn->setEnabled(intermediateNodeSelected);
    ui->mirroredBtn->setEnabled(intermediateNodeSelected);

    // Update checked state only if enabled
    if (intermediateNodeSelected) {
        ui->freeBtn->setChecked(currentAlignment == CurveWidget::HandleAlignment::Free);
        ui->alignedBtn->setChecked(currentAlignment == CurveWidget::HandleAlignment::Aligned);
        ui->mirroredBtn->setChecked(currentAlignment == CurveWidget::HandleAlignment::Mirrored);
    } else {
        // Uncheck all if no intermediate node selected
        // (If using QButtonGroup, just disabling might be enough, but explicit uncheck is safer)
        ui->freeBtn->setChecked(false);
        ui->alignedBtn->setChecked(false);
        ui->mirroredBtn->setChecked(false);
    }
}

// Slots for button clicks - call the public slot on curveWidget
void MainWindow::on_freeBtn_clicked() {
    if (m_selectedNodeIndex != -1) { // Check if a node is selected
        ui->curveWidget->setNodeAlignment(m_selectedNodeIndex, CurveWidget::HandleAlignment::Free);
    }
}

void MainWindow::on_alignedBtn_clicked() {
    if (m_selectedNodeIndex != -1) {
        ui->curveWidget->setNodeAlignment(m_selectedNodeIndex, CurveWidget::HandleAlignment::Aligned);
    }
}

void MainWindow::on_mirroredBtn_clicked() {
    if (m_selectedNodeIndex != -1) {
        ui->curveWidget->setNodeAlignment(m_selectedNodeIndex, CurveWidget::HandleAlignment::Mirrored);
    }
}

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
    QImage lutImage = generateLUTImage(previewSize, QImage::Format_Grayscale8);

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
    // *** Get selected format ***
    QImage::Format selectedFormat = ui->bitDepthComboBox->currentData().value<QImage::Format>();

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
    QImage lutImage = generateLUTImage(lutSize, selectedFormat);
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

// Modify the helper function signature and implementation
QImage MainWindow::generateLUTImage(int size, QImage::Format format) // Added format parameter
{
    if (size < 2 || !ui->curveWidget) {
        return QImage(); // Return null image on error
    }
    // Check if format is supported
    if (format != QImage::Format_Grayscale8 && format != QImage::Format_Grayscale16) {
        qWarning() << "Unsupported format requested for LUT generation:" << format;
        return QImage();
    }

    // Create image with the specified format
    QImage image(size, 1, format);
    if (image.isNull()) {
        qWarning() << "Failed to create QImage for LUT with format:" << format;
        return QImage();
    }

    // --- Fill pixel data based on format ---
    if (format == QImage::Format_Grayscale8) {
        uchar *line = image.scanLine(0); // Pointer to 8-bit data
        for (int i = 0; i < size; ++i) {
            qreal x = static_cast<qreal>(i) / (size - 1.0);
            qreal y = ui->curveWidget->sampleCurve(x);
            y = std::max(0.0, std::min(1.0, y)); // Clamp 0-1
            // Convert 0.0-1.0 to 0-255
            line[i] = static_cast<uchar>(std::round(y * 255.0));
        }
    } else { // Must be QImage::Format_Grayscale16
        // scanLine still returns uchar*, but points to 16-bit (quint16) data
        quint16 *line = reinterpret_cast<quint16*>(image.scanLine(0));
        for (int i = 0; i < size; ++i) {
            qreal x = static_cast<qreal>(i) / (size - 1.0);
            qreal y = ui->curveWidget->sampleCurve(x);
            y = std::max(0.0, std::min(1.0, y)); // Clamp 0-1
            // Convert 0.0-1.0 to 0-65535
            line[i] = static_cast<quint16>(std::round(y * 65535.0));
        }
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
