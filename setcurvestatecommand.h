#ifndef SETCURVESTATECOMMAND_H
#define SETCURVESTATECOMMAND_H

#include <QUndoCommand>
#include <QMap>
#include <QVector>
#include "curvewidget.h" // Include CurveWidget header for types

// Forward declaration not sufficient here as we need the nested types like
// CurveWidget::ActiveChannel and CurveWidget::CurveNode for the QMap definition.
// class CurveWidget;

/**
 * @brief An undo command for storing and restoring the complete state
 * of all curve channels in a CurveWidget.
 */
class SetCurveStateCommand : public QUndoCommand
{
public:
    // Define the specific type for the state map for clarity
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

    // --- QUndoCommand Overrides ---
    void undo() override;
    void redo() override;

    // Optional: Implement mergeWith() if you want consecutive moves
    // of the same point to merge into a single undo step.
    // bool mergeWith(const QUndoCommand *command) override;
    // int id() const override; // Required for mergeWith

private:
    CurveWidget* m_curveWidget; // Pointer to the widget to modify
    CurveStateMap m_oldState;   // Copy of the state *before* the command
    CurveStateMap m_newState;   // Copy of the state *after* the command

    // Optional: Store info needed for merging
    // CurveWidget::SelectionInfo m_selectionInfo; // If merging depends on selected part
};

#endif // SETCURVESTATECOMMAND_H
