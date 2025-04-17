#ifndef SETCURVESTATECOMMAND_H
#define SETCURVESTATECOMMAND_H

// Qt Includes
#include <QUndoCommand>
#include <QMap>
#include <QVector>
#include <QString> // Needed for constructor text parameter

// Project Includes
#include "curvewidget.h" // Required for CurveWidget::ActiveChannel, CurveWidget::CurveNode

/**
 * @brief An undo command for storing and restoring the complete state
 * of all curve channels in a CurveWidget.
 */
class SetCurveStateCommand : public QUndoCommand
{
public:
    using CurveStateMap = QMap<CurveWidget::ActiveChannel, QVector<CurveWidget::CurveNode>>;

    /**
     * @brief Constructor for the command.
     * @param widget - Pointer to the CurveWidget whose state is being managed.
     * @param oldState - The state map *before* the change (will be copied).
     * @param newState - The state map *after* the change (will be copied).
     * @param text - Optional description for the undo/redo action (e.g., "Modify Curve").
     * @param parent - Optional parent command (usually nullptr).
     */
    SetCurveStateCommand(CurveWidget *widget,
                         const CurveStateMap &oldState,
                         const CurveStateMap &newState,
                         const QString &text = "Set Curve State",
                         QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

private:
    CurveWidget* m_curveWidget;
    CurveStateMap m_oldState;
    CurveStateMap m_newState;
};

#endif
