#include "setcurvestatecommand.h"
#include "curvewidget.h" // Ensure CurveWidget definition is available
#include <QDebug>

/**
 * @brief Constructor implementation. Stores the widget pointer and copies the state maps.
 */
SetCurveStateCommand::SetCurveStateCommand(CurveWidget *widget,
                                           const CurveStateMap &oldState,
                                           const CurveStateMap &newState,
                                           const QString &text,
                                           QUndoCommand *parent)
    : QUndoCommand(text, parent), // Call base constructor with text
    m_curveWidget(widget),
    m_oldState(oldState),      // Create a copy of the old state map
    m_newState(newState)       // Create a copy of the new state map
{
    // Optional: Store selection info if needed for merging
    // if (widget) {
    //     m_selectionInfo = widget->getCurrentSelectionInfo(); // Need a getter in CurveWidget
    // }
}

/**
 * @brief Executes the "undo" action: restores the widget to the old state.
 */
void SetCurveStateCommand::undo()
{
    if (!m_curveWidget) {
        qWarning() << "SetCurveStateCommand::undo() - CurveWidget pointer is null.";
        return;
    }
    // Call the CurveWidget's method to restore the state using the stored old map
    m_curveWidget->restoreAllChannelNodes(m_oldState);
    // Optional: restore selection state if needed
    // m_curveWidget->setSelection(m_selectionInfo); // Need a setter
    qDebug() << "Command undone:" << text();
}

/**
 * @brief Executes the "redo" action: restores the widget to the new state.
 */
void SetCurveStateCommand::redo()
{
    if (!m_curveWidget) {
        qWarning() << "SetCurveStateCommand::redo() - CurveWidget pointer is null.";
        return;
    }
    // Call the CurveWidget's method to restore the state using the stored new map
    m_curveWidget->restoreAllChannelNodes(m_newState);
    // Optional: restore selection state if needed
    // m_curveWidget->setSelection(m_selectionInfo); // Need a setter
    qDebug() << "Command redone:" << text();

}

// --- Optional: Merging Implementation ---
/*
// Define a unique ID for mergeable commands (e.g., point move)
const int PointMoveCommandId = 1234;

int SetCurveStateCommand::id() const {
    // Return the ID only if this command represents a potentially mergeable action
    // For example, check the command text or add a type member variable.
    if (text().contains("Modify Curve") && m_selectionInfo.part != CurveWidget::SelectedPart::NONE) { // Example condition
        return PointMoveCommandId;
    }
    return -1; // Not mergeable by default
}

bool SetCurveStateCommand::mergeWith(const QUndoCommand *command) {
    const SetCurveStateCommand *other = dynamic_cast<const SetCurveStateCommand*>(command);
    if (!other) return false; // Not the same type of command

    // Check if it's the same type of action (e.g., modify curve)
    // and if it applies to the same widget and the same selected part.
    if (other->id() != PointMoveCommandId ||
        other->m_curveWidget != this->m_curveWidget ||
        other->m_selectionInfo.nodeIndex != this->m_selectionInfo.nodeIndex ||
        other->m_selectionInfo.part != this->m_selectionInfo.part )
    {
        return false;
    }

    // If conditions met, merge the commands:
    // The current command's 'newState' becomes the merged 'newState'.
    // The 'other' command's 'oldState' is discarded.
    // The current command's 'oldState' remains the initial state before the sequence.
    m_newState = other->m_newState; // Take the latest state from the incoming command

    // Update the command text if desired (e.g., "Modify Curve Sequence")
    setText(text() + "+"); // Simple indication of merging

    qDebug() << "Merged curve modify command";
    return true; // Commands were successfully merged
}
*/
