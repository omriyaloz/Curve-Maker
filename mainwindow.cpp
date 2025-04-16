#include "mainwindow.h"
#include "ui_mainwindow.h" // The header generated from mainwindow.ui
#include "curvewidget.h"   // CurveWidget definition

#include <QFileDialog>
#include <QMessageBox>
#include <QPixmap>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QButtonGroup> // For channel selection
#include <QAbstractButton> // For channel button signal
#include <QRadioButton> // If using radio buttons for channels

#include <QAction>
#include <QMenu>
#include <QUndoStack> // Needed for createUndoAction/curveWidget access
#include <QKeySequence>

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#include <QSettings>
#include <QPainter> // Needed for drawing LUT preview gradient if desired

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow) // Create the UI instance
    , m_selectedNodeIndex(-1)
    , m_channelGroup(nullptr) // Initialize button group pointer
{
    ui->setupUi(this); // Set up the UI defined in the .ui file

    // --- Style and Theme Setup ---
    qApp->setStyle(QStyleFactory::create("Fusion"));
    QSettings settings("MyCompany", "CurveMaker");
    bool useDarkMode = settings.value("Appearance/DarkMode", false).toBool();
    ui->actionToggleDarkMode->setChecked(useDarkMode);
    applyTheme(useDarkMode); // Apply initial theme

    // --- Undo/Redo Setup ---
    if (ui->curveWidget && ui->curveWidget->undoStack()) { // Check both widget and stack
        QUndoStack *undoStack = ui->curveWidget->undoStack();
        QAction *undoAction = undoStack->createUndoAction(this, tr("&Undo"));
        undoAction->setShortcut(QKeySequence::Undo);
        QAction *redoAction = undoStack->createRedoAction(this, tr("&Redo"));
        redoAction->setShortcut(QKeySequence::Redo);

        if(ui->menuEdit) {
            ui->menuEdit->addAction(undoAction);
            ui->menuEdit->addAction(redoAction);
        } else {
            qWarning() << "Could not find menu 'menuEdit'. Add it in the UI Designer.";
        }
    } else {
        qCritical() << "CurveWidget instance or its UndoStack is null!";
    }

    // --- Channel Selection Setup ---
    // Assumes radio buttons channelRedButton, channelGreenButton, channelBlueButton exist in UI
    m_channelGroup = new QButtonGroup(this);
    if (ui->channelRedButton) m_channelGroup->addButton(ui->channelRedButton);
    if (ui->channelGreenButton) m_channelGroup->addButton(ui->channelGreenButton);
    if (ui->channelBlueButton) m_channelGroup->addButton(ui->channelBlueButton);

    // Check if buttons were added successfully
    if (m_channelGroup->buttons().isEmpty()) {
        qWarning() << "No channel selection buttons (channelRedButton, etc.) found or added to group.";
    } else {
        // Set Red as default checked
        if (ui->channelRedButton) ui->channelRedButton->setChecked(true);
        // Connect the group's signal to our slot
        connect(m_channelGroup, &QButtonGroup::buttonClicked, this, &MainWindow::onChannelButtonClicked);
    }


    // --- LUT Width ComboBox Setup ---
    QList<int> lutWidths = {64, 128, 256, 512, 1024}; // Widths for 1D LUT
    for (int width : lutWidths) {
        // Display text is the number, UserData holds the integer value
        ui->lutSizeComboBox->addItem(QString::number(width), QVariant(width));
    }
    ui->lutSizeComboBox->setCurrentText("256"); // Default export width


    // --- Default Export Path Setup ---
    QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    // Suggest more specific filename
    QString defaultFileName = "easing_lut_rgb.png";
    QString defaultFullPath;
    if (!desktopPath.isEmpty()) {
        QDir desktopDir(desktopPath);
        defaultFullPath = desktopDir.filePath(defaultFileName);
    } else {
        qWarning() << "Could not determine Desktop path.";
        defaultFullPath = defaultFileName;
    }
    ui->filePathLineEdit->setText(defaultFullPath);


    // --- Connect Signals to Slots ---
    if (ui->curveWidget) {
        connect(ui->curveWidget, &CurveWidget::curveChanged,
                this, &MainWindow::updateLUTPreview); // Slot implementation updated below

        connect(ui->curveWidget, &CurveWidget::selectionChanged,
                this, &MainWindow::onCurveSelectionChanged);
    }

    // --- Initial State (Unchanged, but preview behavior changes) ---
    updateLUTPreview();
    ui->freeBtn->setEnabled(false);
    ui->alignedBtn->setEnabled(false);
    ui->mirroredBtn->setEnabled(false);
}

MainWindow::~MainWindow()
{
    // No need to delete m_channelGroup if 'this' is parent
    delete ui; // Clean up the UI
}


// --- Slot Implementations ---

void MainWindow::on_actionToggleDarkMode_toggled(bool checked)
{
    applyTheme(checked);
    QSettings settings("MyCompany", "CurveMaker");
    settings.setValue("Appearance/DarkMode", checked);
}

void MainWindow::applyTheme(bool dark)
{
    // Using stylesheets as before
    QString styleSheetPath = dark ? ":/themes/dark.qss" : ":/themes/light.qss";
    QFile f(styleSheetPath);
    QString styleSheet = "";
    if (f.open(QFile::ReadOnly | QFile::Text)) {
        QTextStream ts(&f);
        styleSheet = ts.readAll();
        f.close();
    } else {
        qWarning() << "Could not load theme file:" << styleSheetPath;
    }
    qApp->setStyleSheet(styleSheet);

    // Update custom widget explicitly
    if (ui->curveWidget) {
        ui->curveWidget->setDarkMode(dark);
    }
    // Force preview update as background might change
    updateLUTPreview();
}


// Slot for channel selection button group
void MainWindow::onChannelButtonClicked(QAbstractButton *button)
{
    if (!ui->curveWidget) return;

    CurveWidget::ActiveChannel channel = CurveWidget::ActiveChannel::RED; // Default

    if (button == ui->channelRedButton) {
        channel = CurveWidget::ActiveChannel::RED;
    } else if (button == ui->channelGreenButton) {
        channel = CurveWidget::ActiveChannel::GREEN;
    } else if (button == ui->channelBlueButton) {
        channel = CurveWidget::ActiveChannel::BLUE;
    } else {
        qWarning() << "Unknown button clicked in channel group.";
        return; // Don't change channel if button is unknown
    }

    ui->curveWidget->setActiveChannel(channel);
    // No need to explicitly call updateLUTPreview here, as setActiveChannel triggers
    // a repaint on CurveWidget, which should emit curveChanged if needed,
    // but changing channel itself doesn't change curve data.
    // Let's update the preview explicitly to be sure it reflects the active channel's look.
    // updateLUTPreview(); // Maybe not needed, depends if preview shows active curve only
}


// Slot connected to CurveWidget::selectionChanged
void MainWindow::onCurveSelectionChanged(int nodeIndex, CurveWidget::HandleAlignment currentAlignment) {
    m_selectedNodeIndex = nodeIndex;

    int nodeCount = 0;
    if (ui->curveWidget) {
        // Use the new getter for active node count
        nodeCount = ui->curveWidget->getActiveNodeCount();
    }

    bool intermediateNodeSelected = (nodeIndex > 0 && nodeIndex < nodeCount - 1);

    ui->freeBtn->setEnabled(intermediateNodeSelected);
    ui->alignedBtn->setEnabled(intermediateNodeSelected);
    ui->mirroredBtn->setEnabled(intermediateNodeSelected);

    if (intermediateNodeSelected) {
        // Block signals temporarily to prevent feedback loops if buttons trigger setNodeAlignment
        ui->freeBtn->blockSignals(true);
        ui->alignedBtn->blockSignals(true);
        ui->mirroredBtn->blockSignals(true);

        ui->freeBtn->setChecked(currentAlignment == CurveWidget::HandleAlignment::Free);
        ui->alignedBtn->setChecked(currentAlignment == CurveWidget::HandleAlignment::Aligned);
        ui->mirroredBtn->setChecked(currentAlignment == CurveWidget::HandleAlignment::Mirrored);

        ui->freeBtn->blockSignals(false);
        ui->alignedBtn->blockSignals(false);
        ui->mirroredBtn->blockSignals(false);
    } else {
        ui->freeBtn->setChecked(false);
        ui->alignedBtn->setChecked(false);
        ui->mirroredBtn->setChecked(false);
    }
}

// Slots for alignment button clicks (no change needed)
void MainWindow::on_freeBtn_clicked() {
    if (m_selectedNodeIndex != -1 && ui->curveWidget) {
        ui->curveWidget->setNodeAlignment(m_selectedNodeIndex, CurveWidget::HandleAlignment::Free);
    }
}

void MainWindow::on_alignedBtn_clicked() {
    if (m_selectedNodeIndex != -1 && ui->curveWidget) {
        ui->curveWidget->setNodeAlignment(m_selectedNodeIndex, CurveWidget::HandleAlignment::Aligned);
    }
}

void MainWindow::on_mirroredBtn_clicked() {
    if (m_selectedNodeIndex != -1 && ui->curveWidget) {
        ui->curveWidget->setNodeAlignment(m_selectedNodeIndex, CurveWidget::HandleAlignment::Mirrored);
    }
}

// Browse button (no change needed)
void MainWindow::on_browseButton_clicked()
{
    QString currentSuggestion = ui->filePathLineEdit->text();
    // Suggest png as it's common for LUT images
    QString fileName = QFileDialog::getSaveFileName(this,
                                                    tr("Save 3D LUT Image"),
                                                    currentSuggestion,
                                                    tr("PNG Image (*.png);;All Files (*)"));

    if (!fileName.isEmpty()) {
        // Ensure extension is .png if none provided
        QFileInfo info(fileName);
        if (info.suffix().isEmpty() && info.baseName() == fileName) {
            fileName += ".png";
        }
        ui->filePathLineEdit->setText(fileName);
    }
}

/**
 * @brief Slot to update the LUT preview display. Generates a 1D combined RGB LUT image.
 */
void MainWindow::updateLUTPreview()
{
    // Use a fixed reasonable width for preview, independent of export setting
    const int previewWidth = 256;
    QImage lutImage = generateCombinedRgbLut1D(previewWidth); // Use the new generator

    if (!lutImage.isNull()) {
        QPixmap lutPixmap = QPixmap::fromImage(lutImage);

        // Scale the 1-pixel high pixmap to fill the preview label(s)
        // Ignore aspect ratio to stretch the height for visibility
        ui->lutPreviewLabel->setPixmap(lutPixmap.scaled(ui->lutPreviewLabel->size(),
                                                        Qt::IgnoreAspectRatio, // Stretch vertically
                                                        Qt::SmoothTransformation)); // Use smooth scaling

        if (ui->lutPreviewLabel_3) {
            ui->lutPreviewLabel_3->setPixmap(lutPixmap.scaled(ui->lutPreviewLabel_3->size(),
                                                              Qt::IgnoreAspectRatio,
                                                              Qt::SmoothTransformation));
        }

    } else {
        // Clear preview if generation failed
        ui->lutPreviewLabel->clear();
        if (ui->lutPreviewLabel_3) ui->lutPreviewLabel_3->clear();
        // Optionally display placeholder text/color
        QPixmap errorPixmap(ui->lutPreviewLabel->size());
        errorPixmap.fill(Qt::darkGray);
        QPainter painter(&errorPixmap);
        painter.setPen(Qt::white);
        painter.drawText(errorPixmap.rect(), Qt::AlignCenter, "Preview N/A");
        ui->lutPreviewLabel->setPixmap(errorPixmap);

        if (ui->lutPreviewLabel_3) ui->lutPreviewLabel_3->setPixmap(errorPixmap); // Show in both if exists
    }
}
/**
 * @brief Slot called when the export button is clicked. Generates and saves the 1D Combined RGB LUT.
 */
void MainWindow::on_exportButton_clicked()
{
    // 1. Get Parameters
    QString filePath = ui->filePathLineEdit->text();
    // Get selected width from the combo box (renamed conceptually)
    int lutWidth = ui->lutSizeComboBox->currentData().toInt();

    // 2. Validate Parameters
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, tr("Export Error"), tr("Please specify an export file path."));
        return;
    }
    if (lutWidth < 2) {
        QMessageBox::warning(this, tr("Export Error"), tr("LUT width must be at least 2."));
        return;
    }

    // 3. Generate 1D Combined RGB LUT Image
    QImage lutImage = generateCombinedRgbLut1D(lutWidth);
    if (lutImage.isNull()) {
        QMessageBox::critical(this, tr("Export Error"), tr("Failed to generate LUT image data."));
        return;
    }

    // 4. Save Image (as PNG)
    if (lutImage.save(filePath, "PNG")) {
        QMessageBox::information(this, tr("Export Successful"), tr("Combined RGB LUT image saved to:\n%1").arg(filePath));
    } else {
        QMessageBox::critical(this, tr("Export Error"), tr("Failed to save LUT image to:\n%1\nCheck permissions and path.").arg(filePath));
    }
}

/**
 * @brief Helper function to generate a 1D Combined RGB LUT image.
 * Creates a width x 1 pixel image in RGB888 format.
 * @param width - The desired width (resolution) of the LUT texture.
 * @return The generated QImage, or a null QImage on error.
 */
QImage MainWindow::generateCombinedRgbLut1D(int width)
{
    if (width < 1 || !ui->curveWidget) { // Allow width=1 case
        qWarning() << "generateCombinedRgbLut1D: Invalid width or null curveWidget.";
        return QImage(); // Return null image on error
    }

    // Create the output image: width x 1 pixel, RGB format
    QImage image(width, 1, QImage::Format_RGB888);
    if (image.isNull()) {
        qWarning() << "Failed to create QImage for 1D RGB LUT generation (width:" << width << ")";
        return QImage();
    }

    // --- Fill pixel data by sampling the curves at the same time 't' ---
    for (int i = 0; i < width; ++i) {
        // Calculate normalized time t [0, 1] from the pixel index i
        // Handle width=1 case to avoid division by zero.
        qreal t = (width == 1) ? 0.0 : static_cast<qreal>(i) / (width - 1.0);

        // Sample each channel's curve at this specific time 't'
        qreal yR_norm = ui->curveWidget->sampleCurveChannel(CurveWidget::ActiveChannel::RED, t);
        qreal yG_norm = ui->curveWidget->sampleCurveChannel(CurveWidget::ActiveChannel::GREEN, t);
        qreal yB_norm = ui->curveWidget->sampleCurveChannel(CurveWidget::ActiveChannel::BLUE, t);

        // Clamp results to [0, 1] (should be handled by sampleCurveChannel, but double-check)
        yR_norm = std::max(0.0, std::min(1.0, yR_norm));
        yG_norm = std::max(0.0, std::min(1.0, yG_norm));
        yB_norm = std::max(0.0, std::min(1.0, yB_norm));

        // Convert normalized [0, 1] output to 8-bit [0, 255]
        uchar outR_byte = static_cast<uchar>(std::round(yR_norm * 255.0));
        uchar outG_byte = static_cast<uchar>(std::round(yG_norm * 255.0));
        uchar outB_byte = static_cast<uchar>(std::round(yB_norm * 255.0));

        // Set the pixel color at (i, 0) using the sampled RGB values
        image.setPixel(i, 0, qRgb(outR_byte, outG_byte, outB_byte));
    }

    return image;
}


// Reset button clicked - calls reset on the CurveWidget
void MainWindow::on_resetButton_clicked()
{
    if (ui->curveWidget) {
        ui->curveWidget->resetCurve(); // Resets the *active* curve
    }
}


// --- Helper Function to generate 3D LUT Image ---
// Creates a 2D image representing the 3D LUT mapping (HALD-like structure)
QImage MainWindow::generateLutImage3D(int size)
{
    if (size < 2 || !ui->curveWidget) {
        return QImage(); // Return null image on error
    }

    // Create the output image: size*size width, size height, RGB format
    // Example: size=16 -> 256x16 image
    // Example: size=32 -> 1024x32 image
    QImage image(size * size, size, QImage::Format_RGB888);
    if (image.isNull()) {
        qWarning() << "Failed to create QImage for 3D LUT generation (size:" << size << ")";
        return QImage();
    }
    image.fill(Qt::black); // Start with black background

    // --- Fill pixel data by sampling the curves ---
    for (int b = 0; b < size; ++b) { // Blue axis loop (maps to Y pixel coordinate)
        quint8* line = image.scanLine(b); // Get pointer to the start of row 'b'

        for (int g = 0; g < size; ++g) { // Green axis loop
            for (int r = 0; r < size; ++r) { // Red axis loop

                // Calculate normalized input coordinates [0, 1]
                qreal inputR = static_cast<qreal>(r) / (size - 1.0);
                qreal inputG = static_cast<qreal>(g) / (size - 1.0);
                qreal inputB = static_cast<qreal>(b) / (size - 1.0);

                // Sample each channel's curve using its corresponding input value
                qreal outputR_norm = ui->curveWidget->sampleCurveChannel(CurveWidget::ActiveChannel::RED, inputR);
                qreal outputG_norm = ui->curveWidget->sampleCurveChannel(CurveWidget::ActiveChannel::GREEN, inputG);
                qreal outputB_norm = ui->curveWidget->sampleCurveChannel(CurveWidget::ActiveChannel::BLUE, inputB);

                // Clamp results just in case sampling goes slightly out of bounds
                outputR_norm = std::max(0.0, std::min(1.0, outputR_norm));
                outputG_norm = std::max(0.0, std::min(1.0, outputG_norm));
                outputB_norm = std::max(0.0, std::min(1.0, outputB_norm));

                // Convert normalized [0, 1] output to 8-bit [0, 255]
                uchar outR_byte = static_cast<uchar>(std::round(outputR_norm * 255.0));
                uchar outG_byte = static_cast<uchar>(std::round(outputG_norm * 255.0));
                uchar outB_byte = static_cast<uchar>(std::round(outputB_norm * 255.0));

                // Calculate the X pixel coordinate in the current row (scanline)
                // HALD layout: x = r + g * size
                int px = r + g * size;

                // Calculate the offset within the scanline (3 bytes per pixel for RGB888)
                int offset = px * 3;

                // Write the RGB bytes to the scanline data
                // Order is R, G, B for Format_RGB888
                line[offset + 0] = outR_byte; // Red
                line[offset + 1] = outG_byte; // Green
                line[offset + 2] = outB_byte; // Blue
            }
        }
    }

    return image;
}


// --- Old modeBtn slot (keep if you still have this button) ---
// If modeBtn was just for toggling dark mode, on_actionToggleDarkMode_toggled handles it.
// Remove this if modeBtn is removed or repurposed.
void MainWindow::on_modeBtn_clicked(bool checked)
{
    // Assuming this button is intended to toggle dark mode like the action
    ui->actionToggleDarkMode->setChecked(checked);
    // applyTheme(checked); // applyTheme is called by action's toggled signal
}
