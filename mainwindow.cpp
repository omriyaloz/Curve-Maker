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
    , m_isPreviewRgbCombined(true)
{
    ui->setupUi(this); // Set up the UI defined in the .ui file

    // --- Populate Export Bit Depth ComboBox ---
    // Add items: Display Text, UserData (stores the bit depth integer: 8 or 16)
    ui->exportBitDepthComboBox->addItem("8-bit per channel", QVariant(8));
    ui->exportBitDepthComboBox->addItem("16-bit per channel", QVariant(16));
    ui->exportBitDepthComboBox->setCurrentIndex(0); // Default to 8-bit

    m_isPreviewRgbCombined = ui->actionPreviewRgb->isChecked();

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
    QList<int> lutWidths = {16, 32, 64, 128, 256, 512}; // Widths for 1D LUT
    for (int width : lutWidths) {
        // Display text is the number, UserData holds the integer value
        ui->lutSizeComboBox->addItem(QString::number(width), QVariant(width));
    }
    ui->lutSizeComboBox->setCurrentText("128"); // Default export width


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
                this, &MainWindow::updateLUTPreview);

        connect(ui->curveWidget, &CurveWidget::selectionChanged,
                this, &MainWindow::onCurveSelectionChanged);
    }
    if (ui->curveWidget) {
        ui->curveWidget->setDrawInactiveChannels(ui->actionInactiveChannels->isChecked());
    }
    if (ui->curveWidget) {
        ui->curveWidget->setHandlesClamping(ui->clampHandlesCheckbox->isChecked());
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
    ui->modeBtn->setChecked(checked);
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
    if (!ui->curveWidget) {
        qWarning("onChannelButtonClicked: curveWidget is null!");
        return;
    }

    CurveWidget::ActiveChannel channel = CurveWidget::ActiveChannel::RED; // Determine channel from button
    if (button == ui->channelRedButton) {
        channel = CurveWidget::ActiveChannel::RED;
    } else if (button == ui->channelGreenButton) {
        channel = CurveWidget::ActiveChannel::GREEN;
    } else if (button == ui->channelBlueButton) {
        channel = CurveWidget::ActiveChannel::BLUE;
    } else {
        qWarning("onChannelButtonClicked: Click received from unknown button.");
        return; // Exit if button is not one of the expected channel buttons
    }

    // Set the active channel in the curve widget for editing
    ui->curveWidget->setActiveChannel(channel);

    // Update preview ONLY IF the secondary preview (lutPreviewLabel_3)
    // might need to change because it's showing the active single channel.
    if (!m_isPreviewRgbCombined) {
        qDebug() << "Active channel changed, updating preview (single channel mode active).";
        updateLUTPreview(); // This will regenerate/display the correct single channel in lutPreviewLabel_3
    }
    // No need to update preview otherwise, as lutPreviewLabel always shows RGB
    // and lutPreviewLabel_3 already shows RGB if m_isPreviewRgbCombined is true.
}

void MainWindow::on_actionPreviewRgb_toggled(bool checked)
{
    if (m_isPreviewRgbCombined != checked) {
        m_isPreviewRgbCombined = checked;
        qDebug() << "Preview mode toggled. Is RGB Combined:" << m_isPreviewRgbCombined;
        updateLUTPreview(); // Refresh the preview with the new mode
    }
}


// Slot connected to CurveWidget::selectionChanged
void MainWindow::onCurveSelectionChanged()
{
    // Check if curveWidget exists
    if (!ui->curveWidget) {
        qWarning("onCurveSelectionChanged: curveWidget is null!");
        // Ensure buttons are disabled if widget is gone
        ui->freeBtn->setEnabled(false);
        ui->alignedBtn->setEnabled(false);
        ui->mirroredBtn->setEnabled(false);
        ui->freeBtn->setChecked(false);
        ui->alignedBtn->setChecked(false);
        ui->mirroredBtn->setChecked(false);
        m_selectedNodeIndex = -1; // Clear stored index
        return;
    }

    // Get the current selection state directly from the widget
    QSet<int> selectedIndices = ui->curveWidget->getSelectedIndices();
    bool singleNodeSelected = (selectedIndices.size() == 1);
    bool enableAlignmentButtons = false; // Assume disabled by default
    CurveWidget::HandleAlignment currentAlignment = CurveWidget::HandleAlignment::Free; // Default alignment

    if (singleNodeSelected) {
        // --- Exactly one node is selected ---
        m_selectedNodeIndex = *selectedIndices.constBegin(); // Get the index

        // Check if it's an intermediate node (not first or last)
        int nodeCount = ui->curveWidget->getActiveNodeCount();
        if (m_selectedNodeIndex > 0 && m_selectedNodeIndex < nodeCount - 1) {
            // It's an intermediate node, so alignment applies
            enableAlignmentButtons = true;
            // Get the actual alignment mode for this specific node
            currentAlignment = ui->curveWidget->getAlignment(m_selectedNodeIndex);
        } else {
            // It's an endpoint (0 or size-1), alignment doesn't apply
            m_selectedNodeIndex = -1; // Clear stored index for alignment logic
            enableAlignmentButtons = false;
        }
    } else {
        // --- 0 or >1 nodes selected ---
        m_selectedNodeIndex = -1; // Clear stored index
        enableAlignmentButtons = false; // Alignment buttons disabled
    }

    // --- Update UI ---
    // Update button enabled state
    ui->freeBtn->setEnabled(enableAlignmentButtons);
    ui->alignedBtn->setEnabled(enableAlignmentButtons);
    ui->mirroredBtn->setEnabled(enableAlignmentButtons);

    // Update button checked state (block signals temporarily to prevent feedback loops)
    ui->freeBtn->blockSignals(true);
    ui->alignedBtn->blockSignals(true);
    ui->mirroredBtn->blockSignals(true);

    if (enableAlignmentButtons) {
        // Set checks based on the actual alignment of the single selected intermediate node
        ui->freeBtn->setChecked(currentAlignment == CurveWidget::HandleAlignment::Free);
        ui->alignedBtn->setChecked(currentAlignment == CurveWidget::HandleAlignment::Aligned);
        ui->mirroredBtn->setChecked(currentAlignment == CurveWidget::HandleAlignment::Mirrored);
    } else {
        // Uncheck all if alignment doesn't apply (no selection or endpoints)
        ui->freeBtn->setChecked(false);
        ui->alignedBtn->setChecked(false);
        ui->mirroredBtn->setChecked(false);
    }

    // Re-enable signals
    ui->freeBtn->blockSignals(false);
    ui->alignedBtn->blockSignals(false);
    ui->mirroredBtn->blockSignals(false);
}
// Browse button (no change needed)
void MainWindow::on_browseButton_clicked()
{
    QString currentSuggestion = ui->filePathLineEdit->text();
    // Suggest png as it's common for LUT images

    int currentDepth = ui->exportBitDepthComboBox->currentData().toInt();
    QString filter = tr("PNG Image (*.png)");
    QString defaultSuffix = ".png";

    QString fileName = QFileDialog::getSaveFileName(this,
                                                    tr("Save Combined RGB LUT Image"),
                                                    currentSuggestion,
                                                    filter);

    if (!fileName.isEmpty()) {
        // Ensure extension if none provided (use .png as default)
        QFileInfo info(fileName);
        if (info.suffix().isEmpty() && info.baseName() == fileName) {
            // Add default extension based on filter or just png
            if (filter.contains(info.suffix().toLower()) || filter.contains(info.suffix().toUpper())) {
                // Keep suffix if user selected via filter type? Might be complex.
                // Let's just default to png for simplicity.
                fileName += defaultSuffix;
            } else if (info.suffix().isEmpty()){
                fileName += defaultSuffix;
            }
        }
        ui->filePathLineEdit->setText(fileName);
    }
}

/**
 * @brief Slot to update the LUT preview display. Generates a 1D combined RGB LUT image.
 */

void MainWindow::updateLUTPreview()
{
    // Use a fixed reasonable width for preview generation
    const int previewWidth = 256;
    QImage rgbLutImage;
    QImage secondaryLutImage; // For lutPreviewLabel_3

    // --- Safety check for curve widget ---
    if (!ui->curveWidget) {
        qWarning("updateLUTPreview: curveWidget is null!");
        // Prepare error placeholder
        QPixmap errorPixmap;
        if(ui->lutPreviewLabel) errorPixmap = QPixmap(ui->lutPreviewLabel->size());
        else errorPixmap = QPixmap(100, 20); // Fallback size
        errorPixmap.fill(Qt::darkGray);
        QPainter painter(&errorPixmap);
        painter.setPen(Qt::white);
        painter.drawText(errorPixmap.rect(), Qt::AlignCenter, "Error: No Curve Widget");
        painter.end(); // End painting

        // Apply error placeholder to labels if they exist
        if(ui->lutPreviewLabel) ui->lutPreviewLabel->setPixmap(errorPixmap);
        if(ui->lutPreviewLabel_3) ui->lutPreviewLabel_3->setPixmap(errorPixmap.scaled(ui->lutPreviewLabel_3->size(), Qt::KeepAspectRatio)); // Scale error pixmap too

        return; // Exit if no curve widget
    }

    // --- 1. Generate and Display Combined RGB in lutPreviewLabel ---
    rgbLutImage = generateCombinedRgbLut1D(previewWidth);

    if (!rgbLutImage.isNull()) {
        QPixmap rgbPixmap = QPixmap::fromImage(rgbLutImage);
        // Scale the 1-pixel high pixmap to fill the preview label vertically
        ui->lutPreviewLabel->setPixmap(rgbPixmap.scaled(ui->lutPreviewLabel->size(),
                                                        Qt::IgnoreAspectRatio, // Stretch vertically
                                                        Qt::SmoothTransformation));
    } else {
        qWarning("updateLUTPreview: Failed to generate Combined RGB LUT image.");
        // Clear or show error placeholder if generation failed
        ui->lutPreviewLabel->clear();
        // You could draw a specific error placeholder here too
        QPixmap errorPixmap(ui->lutPreviewLabel->size());
        errorPixmap.fill(Qt::red);
        QPainter painter(&errorPixmap);
        painter.drawText(errorPixmap.rect(), Qt::AlignCenter, "RGB Gen Error");
        painter.end();
        ui->lutPreviewLabel->setPixmap(errorPixmap);
    }


    // --- 2. Generate and Display for lutPreviewLabel_3 (if it exists) based on mode ---
    if (ui->lutPreviewLabel_3) { // Only proceed if the second label exists in the UI
        if (m_isPreviewRgbCombined) {
            // Mode is Combined RGB: Use the already generated image/pixmap
            if (!rgbLutImage.isNull()) { // Check if RGB gen was successful
                // Re-use the pixmap, scaled for the second label
                QPixmap rgbPixmap = QPixmap::fromImage(rgbLutImage); // Regenerate pixmap if needed, or store earlier
                ui->lutPreviewLabel_3->setPixmap(rgbPixmap.scaled(ui->lutPreviewLabel_3->size(),
                                                                  Qt::IgnoreAspectRatio,
                                                                  Qt::SmoothTransformation));
            } else {
                // If RGB failed, clear the secondary label too or show error
                ui->lutPreviewLabel_3->clear();
                // ... (optional: draw error placeholder) ...
            }
        } else {
            // Mode is Active Single Channel: Generate grayscale LUT
            CurveWidget::ActiveChannel activeChannel = ui->curveWidget->getActiveChannel();
            secondaryLutImage = generateSingleChannelLut1D(activeChannel, previewWidth);

            // Display the secondary (single channel) image
            if (!secondaryLutImage.isNull()) {
                QPixmap secondaryPixmap = QPixmap::fromImage(secondaryLutImage); // Handles grayscale ok
                ui->lutPreviewLabel_3->setPixmap(secondaryPixmap.scaled(ui->lutPreviewLabel_3->size(),
                                                                        Qt::IgnoreAspectRatio,
                                                                        Qt::SmoothTransformation));
            } else {
                qWarning("updateLUTPreview: Failed to generate Single Channel LUT image.");
                // Clear or show error placeholder if secondary generation failed
                ui->lutPreviewLabel_3->clear();
                // ... (optional: draw error placeholder) ...
                QPixmap errorPixmap(ui->lutPreviewLabel_3->size());
                errorPixmap.fill(Qt::red);
                QPainter painter(&errorPixmap);
                painter.drawText(errorPixmap.rect(), Qt::AlignCenter, "Grayscale Gen Error");
                painter.end();
                ui->lutPreviewLabel_3->setPixmap(errorPixmap);
            }
        }
    } // endif ui->lutPreviewLabel_3 exists
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
    int bitDepth = ui->exportBitDepthComboBox->currentData().toInt();

    // 2. Validate Parameters
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, tr("Export Error"), tr("Please specify an export file path."));
        return;
    }
    if (lutWidth < 2) {
        QMessageBox::warning(this, tr("Export Error"), tr("LUT width must be at least 2."));
        return;
    }

    if (bitDepth != 8 && bitDepth != 16) {
        QMessageBox::critical(this, tr("Export Error"), tr("Invalid bit depth selected."));
        return;
    }

    // 3. Generate LUT Image with selected bit depth
    qDebug() << "Generating" << bitDepth << "-bit LUT image (width:" << lutWidth << ")";
    QImage lutImage = generateCombinedRgbLut1D(lutWidth, bitDepth); // Pass bit depth
    if (lutImage.isNull()) {
        QMessageBox::critical(this, tr("Export Error"), tr("Failed to generate %1-bit LUT image data.").arg(bitDepth));
        return;
    }
    qDebug() << "Generated image format:" << lutImage.format();


    // 4. Save Image (as PNG - supports both 8/16 bit)
    // PNG format hint is usually unnecessary, Qt saves based on QImage format
    if (lutImage.save(filePath, "PNG")) {
        QMessageBox::information(this, tr("Export Successful"), tr("%1-bit Combined RGB LUT image saved to:\n%2").arg(bitDepth).arg(filePath));
    } else {
        QMessageBox::critical(this, tr("Export Error"), tr("Failed to save %1-bit LUT image to:\n%2\nCheck permissions and path.").arg(bitDepth).arg(filePath));
    }
}

/**
 * @brief Helper function to generate a 1D Combined RGB LUT image.
 * Creates a width x 1 pixel image in appropriate format based on bitDepth.
 * @param width - The desired width (resolution) of the LUT texture.
 * @param bitDepth - The desired bits per channel (8 or 16).
 * @return The generated QImage, or a null QImage on error.
 */
QImage MainWindow::generateCombinedRgbLut1D(int width, int bitDepth) // Add bitDepth param
{
    if (width < 1 || !ui->curveWidget || (bitDepth != 8 && bitDepth != 16)) {
        qWarning() << "generateCombinedRgbLut1D: Invalid parameters.";
        return QImage();
    }

    // Choose the QImage format based on requested bit depth
    // Format_RGBA64 uses 16 bits per channel (R, G, B, A)
    QImage::Format format = (bitDepth == 16) ? QImage::Format_RGBA64 : QImage::Format_RGB888;

    QImage image(width, 1, format);
    if (image.isNull()) {
        qWarning() << "Failed to create QImage for" << bitDepth << "-bit LUT (width:" << width << ")";
        return QImage();
    }
    // Optional: Fill with black/opaque default
    // image.fill(Qt::black); // For RGB888
    // if (format == QImage::Format_RGBA64) image.fill(qRgba64(0,0,0,0xFFFF)); // For RGBA64

    // --- Fill pixel data ---
    for (int i = 0; i < width; ++i) {
        qreal t = (width == 1) ? 0.0 : static_cast<qreal>(i) / (width - 1.0);

        // Sample curves (returns 0.0-1.0)
        qreal yR_norm = ui->curveWidget->sampleCurveChannel(CurveWidget::ActiveChannel::RED, t);
        qreal yG_norm = ui->curveWidget->sampleCurveChannel(CurveWidget::ActiveChannel::GREEN, t);
        qreal yB_norm = ui->curveWidget->sampleCurveChannel(CurveWidget::ActiveChannel::BLUE, t);

        // Clamp results (should be redundant if sampleCurveChannel clamps, but safe)
        yR_norm = std::max(0.0, std::min(1.0, yR_norm));
        yG_norm = std::max(0.0, std::min(1.0, yG_norm));
        yB_norm = std::max(0.0, std::min(1.0, yB_norm));

        // --- Use QColor and setPixelColor for simplicity and format independence ---
        // QColor handles the conversion from float (0.0-1.0) to the image's underlying format (8 or 16 bit)
        QColor pixelColor = QColor::fromRgbF(yR_norm, yG_norm, yB_norm, 1.0); // Use fromRgbF, alpha=1.0

        image.setPixelColor(i, 0, pixelColor); // Set pixel using QColor

        /* // Alternative: Manual conversion (more complex, less recommended)
        if (bitDepth == 16) {
             quint16 r16 = static_cast<quint16>(std::round(yR_norm * 65535.0));
             quint16 g16 = static_cast<quint16>(std::round(yG_norm * 65535.0));
             quint16 b16 = static_cast<quint16>(std::round(yB_norm * 65535.0));
             quint16 a16 = 0xFFFF; // Opaque alpha
             // Need to write to image data buffer correctly for RGBA64 format
             quint64* pixelPtr = reinterpret_cast<quint64*>(image.scanLine(0)) + i;
             // Check documentation for correct packing order of qRgba64 or use QRgba64 helpers
             *pixelPtr = qRgba64(r16, g16, b16, a16); // Assuming qRgba64 helper packs correctly
        } else { // bitDepth == 8
             uchar r8 = static_cast<uchar>(std::round(yR_norm * 255.0));
             uchar g8 = static_cast<uchar>(std::round(yG_norm * 255.0));
             uchar b8 = static_cast<uchar>(std::round(yB_norm * 255.0));
             image.setPixel(i, 0, qRgb(r8, g8, b8));
        }
        */
    }

    return image;
}

QImage MainWindow::generateSingleChannelLut1D(CurveWidget::ActiveChannel channel, int width)
{
    // ... (Keep exact implementation from the previous step) ...
    if (width < 1 || !ui->curveWidget) return QImage();
    QImage image(width, 1, QImage::Format_Grayscale8);
    if (image.isNull()) return QImage();
    uchar *line = image.scanLine(0);
    for (int i = 0; i < width; ++i) {
        qreal t = (width == 1) ? 0.0 : static_cast<qreal>(i) / (width - 1.0);
        qreal y_norm = ui->curveWidget->sampleCurveChannel(channel, t);
        y_norm = std::max(0.0, std::min(1.0, y_norm));
        uchar gray_byte = static_cast<uchar>(std::round(y_norm * 255.0));
        line[i] = gray_byte;
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




void MainWindow::on_modeBtn_clicked(bool checked)
{
    // Assuming this button is intended to toggle dark mode like the action
    ui->actionToggleDarkMode->setChecked(checked);
    applyTheme(checked); // applyTheme is called by action's toggled signal
}

void MainWindow::on_actionInactiveChannels_toggled(bool checked)
{
    if (ui->curveWidget) {
        // Pass the checked state directly to the CurveWidget's slot
        ui->curveWidget->setDrawInactiveChannels(checked);
    }
}



/**
 * @brief Slot called when the "Free" alignment button is clicked.
 * Tells the CurveWidget to set the selected node's alignment if exactly one node is selected.
 */
void MainWindow::on_freeBtn_clicked()
{
    // Check if curveWidget exists and if exactly one node is selected
    if (ui->curveWidget) {
        QSet<int> selectedIndices = ui->curveWidget->getSelectedIndices();
        if (selectedIndices.size() == 1) {
            int index = *selectedIndices.constBegin(); // Get the single selected index
            // CurveWidget::setNodeAlignment will perform further checks (e.g., if it's an endpoint)
            ui->curveWidget->setNodeAlignment(index, CurveWidget::HandleAlignment::Free);
        } else {
            qDebug() << "Free button clicked, but selection size is not 1.";
        }
    }
}

/**
 * @brief Slot called when the "Aligned" alignment button is clicked.
 * Tells the CurveWidget to set the selected node's alignment if exactly one node is selected.
 */
void MainWindow::on_alignedBtn_clicked()
{
    // Check if curveWidget exists and if exactly one node is selected
    if (ui->curveWidget) {
        QSet<int> selectedIndices = ui->curveWidget->getSelectedIndices();
        if (selectedIndices.size() == 1) {
            int index = *selectedIndices.constBegin(); // Get the single selected index
            ui->curveWidget->setNodeAlignment(index, CurveWidget::HandleAlignment::Aligned);
        } else {
            qDebug() << "Aligned button clicked, but selection size is not 1.";
        }
    }
}

/**
 * @brief Slot called when the "Mirrored" alignment button is clicked.
 * Tells the CurveWidget to set the selected node's alignment if exactly one node is selected.
 */
void MainWindow::on_mirroredBtn_clicked()
{
    // Check if curveWidget exists and if exactly one node is selected
    if (ui->curveWidget) {
        QSet<int> selectedIndices = ui->curveWidget->getSelectedIndices();
        if (selectedIndices.size() == 1) {
            int index = *selectedIndices.constBegin(); // Get the single selected index
            ui->curveWidget->setNodeAlignment(index, CurveWidget::HandleAlignment::Mirrored);
        } else {
            qDebug() << "Mirrored button clicked, but selection size is not 1.";
        }
    }
}

void MainWindow::on_clampHandlesCheckbox_stateChanged(int state)
{
    if (ui->curveWidget) {
        ui->curveWidget->setHandlesClamping(state == Qt::Checked);
    }
}

