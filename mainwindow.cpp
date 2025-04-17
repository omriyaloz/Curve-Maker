#include "mainwindow.h"
#include "ui_mainwindow.h" // Must be included before usage of ui member
#include "curvewidget.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QIODevice>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QList>
#include <QMapIterator>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QRadioButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTextStream>
#include <QUndoStack>
#include <QVariant>

#include <algorithm> // for std::max/min
#include <cmath>     // for std::round

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_selectedNodeIndex(-1)
    , m_channelGroup(nullptr)
    , m_isPreviewRgbCombined(true)
{
    ui->setupUi(this);

    if (ui->curveWidget && ui->animationPreviewWidget) {
        ui->animationPreviewWidget->setCurveWidget(ui->curveWidget);
    }

    if (ui->curveWidget && ui->animationPreviewWidget) {
        connect(ui->curveWidget, &CurveWidget::curveChanged,
                ui->animationPreviewWidget, QOverload<>::of(&QWidget::update));

        if (m_channelGroup) { // Connect only if group was successfully created
            connect(m_channelGroup, &QButtonGroup::buttonClicked,
                    ui->animationPreviewWidget, QOverload<>::of(&QWidget::update));
        }
    }

    Qt::WindowFlags flags = this->windowFlags();
    flags &= ~Qt::WindowMaximizeButtonHint;
    flags |= Qt::WindowContextHelpButtonHint; // Keep this if intended
    this->setWindowFlags(flags);

    connect(ui->actionSaveCurves, &QAction::triggered, this, &MainWindow::onSaveCurvesActionTriggered);
    connect(ui->actionLoadCurves, &QAction::triggered, this, &MainWindow::onLoadCurvesActionTriggered);

    ui->exportBitDepthComboBox->addItem("8-bit per channel", QVariant(8));
    ui->exportBitDepthComboBox->addItem("16-bit per channel", QVariant(16));
    ui->exportBitDepthComboBox->setCurrentIndex(0);

    m_isPreviewRgbCombined = ui->actionPreviewRgb->isChecked();

    qApp->setStyle(QStyleFactory::create("Fusion"));
    QSettings settings("MyCompany", "CurveMaker");
    bool useDarkMode = settings.value("Appearance/DarkMode", false).toBool();
    ui->actionToggleDarkMode->setChecked(useDarkMode);
    applyTheme(useDarkMode);

    if (ui->curveWidget && ui->curveWidget->undoStack()) {
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

    m_channelGroup = new QButtonGroup(this);
    if (ui->channelRedButton) m_channelGroup->addButton(ui->channelRedButton);
    if (ui->channelGreenButton) m_channelGroup->addButton(ui->channelGreenButton);
    if (ui->channelBlueButton) m_channelGroup->addButton(ui->channelBlueButton);

    if (m_channelGroup->buttons().isEmpty()) {
        qWarning() << "No channel selection buttons (channelRedButton, etc.) found or added to group.";
    } else {
        if (ui->channelRedButton) ui->channelRedButton->setChecked(true);
        connect(m_channelGroup, &QButtonGroup::buttonClicked, this, &MainWindow::onChannelButtonClicked);
    }

    QList<int> lutWidths = {16, 32, 64, 128, 256, 512};
    for (int width : lutWidths) {
        ui->lutSizeComboBox->addItem(QString::number(width), QVariant(width));
    }
    ui->lutSizeComboBox->setCurrentText("128");

    QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
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

    if (ui->curveWidget) {
        connect(ui->curveWidget, &CurveWidget::curveChanged,
                this, &MainWindow::updateLUTPreview);

        connect(ui->curveWidget, &CurveWidget::selectionChanged,
                this, &MainWindow::onCurveSelectionChanged);

        ui->curveWidget->setDrawInactiveChannels(ui->actionInactiveChannels->isChecked());
        ui->curveWidget->setHandlesClamping(ui->clampHandlesCheckbox->isChecked());
    }

    updateLUTPreview();
    ui->freeBtn->setEnabled(false);
    ui->alignedBtn->setEnabled(false);
    ui->mirroredBtn->setEnabled(false);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_actionToggleDarkMode_toggled(bool checked)
{
    applyTheme(checked);
    QSettings settings("MyCompany", "CurveMaker");
    settings.setValue("Appearance/DarkMode", checked);
    ui->modeBtn->setChecked(checked);
}

void MainWindow::applyTheme(bool dark)
{
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

    if (ui->curveWidget) {
        ui->curveWidget->setDarkMode(dark);
    }
    updateLUTPreview();
}

void MainWindow::onChannelButtonClicked(QAbstractButton *button)
{
    if (!ui->curveWidget) {
        qWarning("onChannelButtonClicked: curveWidget is null!");
        return;
    }

    CurveWidget::ActiveChannel channel = CurveWidget::ActiveChannel::RED;
    if (button == ui->channelRedButton) {
        channel = CurveWidget::ActiveChannel::RED;
    } else if (button == ui->channelGreenButton) {
        channel = CurveWidget::ActiveChannel::GREEN;
    } else if (button == ui->channelBlueButton) {
        channel = CurveWidget::ActiveChannel::BLUE;
    } else {
        qWarning("onChannelButtonClicked: Click received from unknown button.");
        return;
    }

    ui->curveWidget->setActiveChannel(channel);

    if (!m_isPreviewRgbCombined) {
        qDebug() << "Active channel changed, updating preview (single channel mode active).";
        updateLUTPreview();
    }
}

void MainWindow::on_actionPreviewRgb_toggled(bool checked)
{
    if (m_isPreviewRgbCombined != checked) {
        m_isPreviewRgbCombined = checked;
        qDebug() << "Preview mode toggled. Is RGB Combined:" << m_isPreviewRgbCombined;
        updateLUTPreview();
    }
}

void MainWindow::onCurveSelectionChanged()
{
    if (!ui->curveWidget) {
        qWarning("onCurveSelectionChanged: curveWidget is null!");
        ui->freeBtn->setEnabled(false);
        ui->alignedBtn->setEnabled(false);
        ui->mirroredBtn->setEnabled(false);
        ui->freeBtn->setChecked(false);
        ui->alignedBtn->setChecked(false);
        ui->mirroredBtn->setChecked(false);
        m_selectedNodeIndex = -1;
        return;
    }

    QSet<int> selectedIndices = ui->curveWidget->getSelectedIndices();
    bool singleNodeSelected = (selectedIndices.size() == 1);
    bool enableAlignmentButtons = false;
    CurveWidget::HandleAlignment currentAlignment = CurveWidget::HandleAlignment::Free;

    if (singleNodeSelected) {
        m_selectedNodeIndex = *selectedIndices.constBegin();

        int nodeCount = ui->curveWidget->getActiveNodeCount();
        if (m_selectedNodeIndex > 0 && m_selectedNodeIndex < nodeCount - 1) {
            enableAlignmentButtons = true;
            currentAlignment = ui->curveWidget->getAlignment(m_selectedNodeIndex);
        } else {
            m_selectedNodeIndex = -1;
            enableAlignmentButtons = false;
        }
    } else {
        m_selectedNodeIndex = -1;
        enableAlignmentButtons = false;
    }

    ui->freeBtn->setEnabled(enableAlignmentButtons);
    ui->alignedBtn->setEnabled(enableAlignmentButtons);
    ui->mirroredBtn->setEnabled(enableAlignmentButtons);

    ui->freeBtn->blockSignals(true);
    ui->alignedBtn->blockSignals(true);
    ui->mirroredBtn->blockSignals(true);

    if (enableAlignmentButtons) {
        ui->freeBtn->setChecked(currentAlignment == CurveWidget::HandleAlignment::Free);
        ui->alignedBtn->setChecked(currentAlignment == CurveWidget::HandleAlignment::Aligned);
        ui->mirroredBtn->setChecked(currentAlignment == CurveWidget::HandleAlignment::Mirrored);
    } else {
        ui->freeBtn->setChecked(false);
        ui->alignedBtn->setChecked(false);
        ui->mirroredBtn->setChecked(false);
    }

    ui->freeBtn->blockSignals(false);
    ui->alignedBtn->blockSignals(false);
    ui->mirroredBtn->blockSignals(false);
}

void MainWindow::on_browseButton_clicked()
{
    QString currentSuggestion = ui->filePathLineEdit->text();
    int currentDepth = ui->exportBitDepthComboBox->currentData().toInt();
    QString filter = tr("PNG Image (*.png)");
    QString defaultSuffix = ".png";

    QString fileName = QFileDialog::getSaveFileName(this,
                                                    tr("Save Combined RGB LUT Image"),
                                                    currentSuggestion,
                                                    filter);

    if (!fileName.isEmpty()) {
        QFileInfo info(fileName);
        if (info.suffix().isEmpty() && info.baseName() == fileName) {
            fileName += defaultSuffix;
        } else if (info.suffix().isEmpty()){
            fileName += defaultSuffix;
        }
        ui->filePathLineEdit->setText(fileName);
    }
}

/**
 * @brief Slot to update the LUT preview display. Generates a 1D combined RGB LUT image.
 */
void MainWindow::updateLUTPreview()
{
    const int previewWidth = 256;
    QImage rgbLutImage;
    QImage secondaryLutImage;

    if (!ui->curveWidget) {
        qWarning("updateLUTPreview: curveWidget is null!");
        QPixmap errorPixmap;
        if(ui->lutPreviewLabel) errorPixmap = QPixmap(ui->lutPreviewLabel->size());
        else errorPixmap = QPixmap(100, 20);
        errorPixmap.fill(Qt::darkGray);
        QPainter painter(&errorPixmap);
        painter.setPen(Qt::white);
        painter.drawText(errorPixmap.rect(), Qt::AlignCenter, "Error: No Curve Widget");
        painter.end();

        if(ui->lutPreviewLabel) ui->lutPreviewLabel->setPixmap(errorPixmap);
        if(ui->lutPreviewLabel_3) ui->lutPreviewLabel_3->setPixmap(errorPixmap.scaled(ui->lutPreviewLabel_3->size(), Qt::KeepAspectRatio));

        return;
    }

    rgbLutImage = generateCombinedRgbLut1D(previewWidth);

    if (!rgbLutImage.isNull()) {
        QPixmap rgbPixmap = QPixmap::fromImage(rgbLutImage);
        ui->lutPreviewLabel->setPixmap(rgbPixmap.scaled(ui->lutPreviewLabel->size(),
                                                        Qt::IgnoreAspectRatio,
                                                        Qt::SmoothTransformation));
    } else {
        qWarning("updateLUTPreview: Failed to generate Combined RGB LUT image.");
        ui->lutPreviewLabel->clear();
        QPixmap errorPixmap(ui->lutPreviewLabel->size());
        errorPixmap.fill(Qt::red);
        QPainter painter(&errorPixmap);
        painter.drawText(errorPixmap.rect(), Qt::AlignCenter, "RGB Gen Error");
        painter.end();
        ui->lutPreviewLabel->setPixmap(errorPixmap);
    }

    if (ui->lutPreviewLabel_3) {
        if (m_isPreviewRgbCombined) {
            if (!rgbLutImage.isNull()) {
                QPixmap rgbPixmap = QPixmap::fromImage(rgbLutImage);
                ui->lutPreviewLabel_3->setPixmap(rgbPixmap.scaled(ui->lutPreviewLabel_3->size(),
                                                                  Qt::IgnoreAspectRatio,
                                                                  Qt::SmoothTransformation));
            } else {
                ui->lutPreviewLabel_3->clear();
            }
        } else {
            CurveWidget::ActiveChannel activeChannel = ui->curveWidget->getActiveChannel();
            secondaryLutImage = generateSingleChannelLut1D(activeChannel, previewWidth);

            if (!secondaryLutImage.isNull()) {
                QPixmap secondaryPixmap = QPixmap::fromImage(secondaryLutImage);
                ui->lutPreviewLabel_3->setPixmap(secondaryPixmap.scaled(ui->lutPreviewLabel_3->size(),
                                                                        Qt::IgnoreAspectRatio,
                                                                        Qt::SmoothTransformation));
            } else {
                qWarning("updateLUTPreview: Failed to generate Single Channel LUT image.");
                ui->lutPreviewLabel_3->clear();
                QPixmap errorPixmap(ui->lutPreviewLabel_3->size());
                errorPixmap.fill(Qt::red);
                QPainter painter(&errorPixmap);
                painter.drawText(errorPixmap.rect(), Qt::AlignCenter, "Grayscale Gen Error");
                painter.end();
                ui->lutPreviewLabel_3->setPixmap(errorPixmap);
            }
        }
    }
}

/**
 * @brief Slot called when the export button is clicked. Generates and saves the 1D Combined RGB LUT.
 */
void MainWindow::on_exportButton_clicked()
{
    QString filePath = ui->filePathLineEdit->text();
    int lutWidth = ui->lutSizeComboBox->currentData().toInt();
    int bitDepth = ui->exportBitDepthComboBox->currentData().toInt();

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

    qDebug() << "Generating" << bitDepth << "-bit LUT image (width:" << lutWidth << ")";
    QImage lutImage = generateCombinedRgbLut1D(lutWidth, bitDepth);
    if (lutImage.isNull()) {
        QMessageBox::critical(this, tr("Export Error"), tr("Failed to generate %1-bit LUT image data.").arg(bitDepth));
        return;
    }
    qDebug() << "Generated image format:" << lutImage.format();

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
QImage MainWindow::generateCombinedRgbLut1D(int width, int bitDepth)
{
    if (width < 1 || !ui->curveWidget || (bitDepth != 8 && bitDepth != 16)) {
        qWarning() << "generateCombinedRgbLut1D: Invalid parameters.";
        return QImage();
    }

    QImage::Format format = (bitDepth == 16) ? QImage::Format_RGBA64 : QImage::Format_RGB888;

    QImage image(width, 1, format);
    if (image.isNull()) {
        qWarning() << "Failed to create QImage for" << bitDepth << "-bit LUT (width:" << width << ")";
        return QImage();
    }

    for (int i = 0; i < width; ++i) {
        qreal t = (width == 1) ? 0.0 : static_cast<qreal>(i) / (width - 1.0);

        qreal yR_norm = ui->curveWidget->sampleCurveChannel(CurveWidget::ActiveChannel::RED, t);
        qreal yG_norm = ui->curveWidget->sampleCurveChannel(CurveWidget::ActiveChannel::GREEN, t);
        qreal yB_norm = ui->curveWidget->sampleCurveChannel(CurveWidget::ActiveChannel::BLUE, t);

        yR_norm = std::max(0.0, std::min(1.0, yR_norm));
        yG_norm = std::max(0.0, std::min(1.0, yG_norm));
        yB_norm = std::max(0.0, std::min(1.0, yB_norm));

        QColor pixelColor = QColor::fromRgbF(yR_norm, yG_norm, yB_norm, 1.0);

        image.setPixelColor(i, 0, pixelColor);
    }

    return image;
}

QImage MainWindow::generateSingleChannelLut1D(CurveWidget::ActiveChannel channel, int width)
{
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

void MainWindow::on_resetButton_clicked()
{
    if (ui->curveWidget) {
        ui->curveWidget->resetCurve();
    }
}

QImage MainWindow::generateLutImage3D(int size)
{
    if (size < 2 || !ui->curveWidget) {
        return QImage();
    }

    QImage image(size * size, size, QImage::Format_RGB888);
    if (image.isNull()) {
        qWarning() << "Failed to create QImage for 3D LUT generation (size:" << size << ")";
        return QImage();
    }
    image.fill(Qt::black);

    for (int b = 0; b < size; ++b) {
        quint8* line = image.scanLine(b);

        for (int g = 0; g < size; ++g) {
            for (int r = 0; r < size; ++r) {

                qreal inputR = static_cast<qreal>(r) / (size - 1.0);
                qreal inputG = static_cast<qreal>(g) / (size - 1.0);
                qreal inputB = static_cast<qreal>(b) / (size - 1.0);

                qreal outputR_norm = ui->curveWidget->sampleCurveChannel(CurveWidget::ActiveChannel::RED, inputR);
                qreal outputG_norm = ui->curveWidget->sampleCurveChannel(CurveWidget::ActiveChannel::GREEN, inputG);
                qreal outputB_norm = ui->curveWidget->sampleCurveChannel(CurveWidget::ActiveChannel::BLUE, inputB);

                outputR_norm = std::max(0.0, std::min(1.0, outputR_norm));
                outputG_norm = std::max(0.0, std::min(1.0, outputG_norm));
                outputB_norm = std::max(0.0, std::min(1.0, outputB_norm));

                uchar outR_byte = static_cast<uchar>(std::round(outputR_norm * 255.0));
                uchar outG_byte = static_cast<uchar>(std::round(outputG_norm * 255.0));
                uchar outB_byte = static_cast<uchar>(std::round(outputB_norm * 255.0));

                int px = r + g * size;
                int offset = px * 3;

                line[offset + 0] = outR_byte;
                line[offset + 1] = outG_byte;
                line[offset + 2] = outB_byte;
            }
        }
    }

    return image;
}

void MainWindow::on_modeBtn_clicked(bool checked)
{
    ui->actionToggleDarkMode->setChecked(checked);
    applyTheme(checked);
}

void MainWindow::on_actionInactiveChannels_toggled(bool checked)
{
    if (ui->curveWidget) {
        ui->curveWidget->setDrawInactiveChannels(checked);
    }
}

/**
 * @brief Slot called when the "Free" alignment button is clicked.
 * Tells the CurveWidget to set the selected node's alignment if exactly one node is selected.
 */
void MainWindow::on_freeBtn_clicked()
{
    if (ui->curveWidget) {
        QSet<int> selectedIndices = ui->curveWidget->getSelectedIndices();
        if (selectedIndices.size() == 1) {
            int index = *selectedIndices.constBegin();
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
    if (ui->curveWidget) {
        QSet<int> selectedIndices = ui->curveWidget->getSelectedIndices();
        if (selectedIndices.size() == 1) {
            int index = *selectedIndices.constBegin();
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
    if (ui->curveWidget) {
        QSet<int> selectedIndices = ui->curveWidget->getSelectedIndices();
        if (selectedIndices.size() == 1) {
            int index = *selectedIndices.constBegin();
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

/**
 * @brief Slot connected to the "Save Curves..." action.
 * Saves the current curve data and relevant UI settings to a JSON file.
 */
void MainWindow::onSaveCurvesActionTriggered() {
    if (!ui->curveWidget) {
        QMessageBox::critical(this, tr("Save Error"), tr("Curve widget is not available."));
        return;
    }

    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString suggestedName = QString("curve_settings_%1w_%2bit.json")
                                .arg(ui->lutSizeComboBox->currentData().toInt())
                                .arg(ui->exportBitDepthComboBox->currentData().toInt());

    QString fileName = QFileDialog::getSaveFileName(this,
                                                    tr("Save Curves and Settings"),
                                                    defaultPath + "/" + suggestedName,
                                                    tr("Curve JSON Files (*.json);;All Files (*)"));

    if (fileName.isEmpty()) {
        return;
    }

    if (!fileName.endsWith(".json", Qt::CaseInsensitive)) {
        fileName += ".json";
    }

    QMap<CurveWidget::ActiveChannel, QVector<CurveWidget::CurveNode>> curveData = ui->curveWidget->getAllChannelNodes();
    int lutWidth = ui->lutSizeComboBox->currentData().toInt();
    int bitDepth = ui->exportBitDepthComboBox->currentData().toInt();
    bool previewRgb = ui->actionPreviewRgb->isChecked();
    bool drawInactive = ui->actionInactiveChannels->isChecked();
    bool clampHandles = ui->clampHandlesCheckbox->isChecked();

    QJsonObject rootObj;
    rootObj["file_format_version"] = "1.1";

    QJsonObject settingsObj;
    settingsObj["lut_width"] = lutWidth;
    settingsObj["export_bit_depth"] = bitDepth;
    settingsObj["preview_rgb_combined"] = previewRgb;
    settingsObj["draw_inactive"] = drawInactive;
    settingsObj["clamp_handles"] = clampHandles;
    rootObj["settings"] = settingsObj;

    QJsonObject channelsObj;
    for (auto it = curveData.constBegin(); it != curveData.constEnd(); ++it) {
        CurveWidget::ActiveChannel channelKey = it.key();
        const QVector<CurveWidget::CurveNode>& nodesVector = it.value();

        QString channelStringKey;
        switch(channelKey) {
        case CurveWidget::ActiveChannel::RED:   channelStringKey = "RED";   break;
        case CurveWidget::ActiveChannel::GREEN: channelStringKey = "GREEN"; break;
        case CurveWidget::ActiveChannel::BLUE:  channelStringKey = "BLUE";  break;
        default:
            qWarning() << "Skipping unknown channel key during save:" << static_cast<int>(channelKey);
            continue;
        }

        QJsonArray nodesArray;
        for (const CurveWidget::CurveNode& node : nodesVector) {
            QJsonObject nodeObj;
            nodeObj["main"] = QJsonArray({node.mainPoint.x(), node.mainPoint.y()});
            nodeObj["in"]   = QJsonArray({node.handleIn.x(), node.handleIn.y()});
            nodeObj["out"]  = QJsonArray({node.handleOut.x(), node.handleOut.y()});
            nodeObj["align"] = static_cast<int>(node.alignment);
            nodesArray.append(nodeObj);
        }
        channelsObj[channelStringKey] = nodesArray;
    }
    rootObj["channels"] = channelsObj;

    QJsonDocument saveDoc(rootObj);
    QFile saveFile(fileName);
    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Couldn't open save file:" << fileName << saveFile.errorString();
        QMessageBox::critical(this, tr("Save Error"), tr("Could not open file for writing:\n%1").arg(fileName));
        return;
    }

    QByteArray jsonData = saveDoc.toJson(QJsonDocument::Indented);
    if (saveFile.write(jsonData) == -1) {
        qWarning() << "Failed to write to save file:" << fileName << saveFile.errorString();
        QMessageBox::critical(this, tr("Save Error"), tr("Failed to write data to file:\n%1").arg(fileName));
        saveFile.close();
        return;
    }

    saveFile.close();
    QMessageBox::information(this, tr("Save Successful"), tr("Curves and settings saved to:\n%1").arg(fileName));
}

/**
 * @brief Slot connected to the "Load Curves..." action.
 * Loads curve data and UI settings from a JSON file.
 */
void MainWindow::onLoadCurvesActionTriggered() {
    if (!ui->curveWidget) {
        QMessageBox::critical(this, tr("Load Error"), tr("Curve widget is not available."));
        return;
    }

    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

    QString fileName = QFileDialog::getOpenFileName(this,
                                                    tr("Load Curves and Settings"),
                                                    defaultPath,
                                                    tr("Curve JSON Files (*.json);;All Files (*)"));

    if (fileName.isEmpty()) {
        return;
    }

    QFile loadFile(fileName);
    if (!loadFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Couldn't open load file:" << fileName << loadFile.errorString();
        QMessageBox::critical(this, tr("Load Error"), tr("Could not open file for reading:\n%1").arg(fileName));
        return;
    }
    QByteArray saveData = loadFile.readAll();
    loadFile.close();

    QJsonParseError parseError;
    QJsonDocument loadDoc = QJsonDocument::fromJson(saveData, &parseError);

    if (loadDoc.isNull()) {
        qWarning() << "Failed to parse JSON:" << parseError.errorString() << "at offset" << parseError.offset;
        QMessageBox::critical(this, tr("Load Error"), tr("Failed to parse curve file:\n%1\nError: %2").arg(fileName).arg(parseError.errorString()));
        return;
    }
    if (!loadDoc.isObject()) {
        QMessageBox::critical(this, tr("Load Error"), tr("Invalid curve file format (Root is not JSON object)."));
        return;
    }

    QJsonObject rootObj = loadDoc.object();

    int loadedLutWidth = 256;
    int loadedBitDepth = 8;
    bool loadedPreviewRgb = true;
    bool loadedDrawInactive = false;
    bool loadedClampHandles = true;
    QString fileVersion = rootObj.value("file_format_version").toString("unknown");
    qDebug() << "Loading file version:" << fileVersion;

    if (rootObj.contains("settings") && rootObj["settings"].isObject()) {
        QJsonObject settingsObj = rootObj["settings"].toObject();

        loadedLutWidth = settingsObj.value("lut_width").toInt(loadedLutWidth);
        loadedBitDepth = settingsObj.value("export_bit_depth").toInt(loadedBitDepth);
        loadedPreviewRgb = settingsObj.value("preview_rgb_combined").toBool(loadedPreviewRgb);
        loadedDrawInactive = settingsObj.value("draw_inactive").toBool(loadedDrawInactive);
        loadedClampHandles = settingsObj.value("clamp_handles").toBool(loadedClampHandles);

        qDebug() << "Loaded Settings: Width" << loadedLutWidth << "Depth" << loadedBitDepth
                 << "PreviewRGB" << loadedPreviewRgb << "DrawInactive" << loadedDrawInactive
                 << "Clamp" << loadedClampHandles;
    } else {
        qWarning() << "Settings object missing in file, using defaults.";
    }

    if (!rootObj.contains("channels") || !rootObj["channels"].isObject()) {
        QMessageBox::critical(this, tr("Load Error"), tr("Invalid curve file format (Missing 'channels' object)."));
        return;
    }
    QJsonObject channelsObj = rootObj["channels"].toObject();
    QMap<CurveWidget::ActiveChannel, QVector<CurveWidget::CurveNode>> loadedChannelNodes;
    bool loadOk = true;

    QList<QPair<QString, CurveWidget::ActiveChannel>> expectedChannels = {
        {"RED", CurveWidget::ActiveChannel::RED},
        {"GREEN", CurveWidget::ActiveChannel::GREEN},
        {"BLUE", CurveWidget::ActiveChannel::BLUE}
    };

    for (const auto& pair : expectedChannels) {
        QString channelStringKey = pair.first;
        CurveWidget::ActiveChannel channelKey = pair.second;

        if (!channelsObj.contains(channelStringKey) || !channelsObj[channelStringKey].isArray()) {
            qWarning() << "Channel data missing or invalid for:" << channelStringKey;
            loadOk = false;
            break;
        }

        QJsonArray nodesArray = channelsObj[channelStringKey].toArray();
        QVector<CurveWidget::CurveNode> nodesVector;
        nodesVector.reserve(nodesArray.size());

        for (const QJsonValue& nodeVal : nodesArray) {
            if (!nodeVal.isObject()) { loadOk = false; qWarning("Node data not object"); break; }
            QJsonObject nodeObj = nodeVal.toObject();

            bool pointsOk = true;
            auto extractPoint = [&](const QString& key, QPointF& point) -> bool {
                if (!nodeObj.contains(key) || !nodeObj[key].isArray()) return false;
                QJsonArray arr = nodeObj[key].toArray();
                if (arr.size() != 2 || !arr[0].isDouble() || !arr[1].isDouble()) return false;
                point.setX(arr[0].toDouble());
                point.setY(arr[1].toDouble());
                return true;
            };

            QPointF pMain, pIn, pOut;
            if (!extractPoint("main", pMain)) pointsOk = false;
            if (!extractPoint("in", pIn)) pointsOk = false;
            if (!extractPoint("out", pOut)) pointsOk = false;

            if (!pointsOk) { loadOk = false; qWarning("Invalid point data"); break; }

            if (!nodeObj.contains("align")|| !nodeObj["align"].isDouble()) {
                loadOk = false; qWarning("Invalid alignment data type"); break;
            }
            int alignInt = nodeObj["align"].toInt(-1);
            if (alignInt < static_cast<int>(CurveWidget::HandleAlignment::Free) ||
                alignInt > static_cast<int>(CurveWidget::HandleAlignment::Mirrored)) {
                qWarning() << "Invalid alignment value in node:" << alignInt;
                loadOk = false; break;
            }
            CurveWidget::HandleAlignment alignment = static_cast<CurveWidget::HandleAlignment>(alignInt);

            CurveWidget::CurveNode node(pMain);
            node.handleIn = pIn;
            node.handleOut = pOut;
            node.alignment = alignment;
            nodesVector.append(node);
        }

        if (!loadOk) break;

        loadedChannelNodes.insert(channelKey, nodesVector);
    }

    if (loadOk && loadedChannelNodes.size() == expectedChannels.size()) {
        qDebug() << "JSON parsing successful, applying state.";

        ui->curveWidget->setAllChannelNodes(loadedChannelNodes);

        bool foundWidth = false;
        for(int i=0; i<ui->lutSizeComboBox->count(); ++i){
            if(ui->lutSizeComboBox->itemData(i).toInt() == loadedLutWidth){
                ui->lutSizeComboBox->setCurrentIndex(i);
                foundWidth = true; break;
            }
        }
        if (!foundWidth) {
            qWarning() << "Loaded LUT Width" << loadedLutWidth << "not found in ComboBox list.";
            ui->lutSizeComboBox->setCurrentText(QString::number(loadedLutWidth));
        }

        bool foundDepth = false;
        for(int i=0; i<ui->exportBitDepthComboBox->count(); ++i){
            if(ui->exportBitDepthComboBox->itemData(i).toInt() == loadedBitDepth){
                ui->exportBitDepthComboBox->setCurrentIndex(i);
                foundDepth = true; break;
            }
        }
        if (!foundDepth) {
            qWarning() << "Loaded Export Bit Depth" << loadedBitDepth << "not found in ComboBox list.";
            ui->exportBitDepthComboBox->setCurrentIndex(0);
        }

        ui->clampHandlesCheckbox->setChecked(loadedClampHandles);
        ui->actionInactiveChannels->setChecked(loadedDrawInactive);
        ui->actionPreviewRgb->setChecked(loadedPreviewRgb);

        QMessageBox::information(this, tr("Load Successful"), tr("Curves and settings loaded from:\n%1").arg(fileName));

    } else {
        QMessageBox::critical(this, tr("Load Error"), tr("Failed to load curves/settings due to file format error or missing data in:\n%1").arg(fileName));
    }
}
