#include "setcurvestatecommand.h"
#include "curvewidget.h" // Needed for implementation details

SetCurveStateCommand::SetCurveStateCommand(CurveWidget *widget,
                                           const QVector<CurveNode>& oldState,
                                           const QVector<CurveNode>& newState,
                                           const QString& text,
                                           QUndoCommand *parent)
    : QUndoCommand(parent),
    m_widget(widget),
    m_oldState(oldState),
    m_newState(newState)
{
    // Set the user-visible text for this command in Undo/Redo menus
    setText(text);
}

void SetCurveStateCommand::undo()
{
    if (m_widget) {
        // Call a method in CurveWidget to restore the old state
        m_widget->restoreNodesInternal(m_oldState);
        // Restore selection state here if implemented
    }
}

void SetCurveStateCommand::redo()
{
    if (m_widget) {
        // Call a method in CurveWidget to restore the new state
        m_widget->restoreNodesInternal(m_newState);
            // Restore selection state here if implemented
    }
}

// Optional: Implement mergeWith for smoother dragging undo
/*
int SetCurveStateCommand::id() const {
    // Return an ID indicating this is a node move command
    return 1; // Example ID
}

bool SetCurveStateCommand::mergeWith(const QUndoCommand *command) {
    if (command->id() != id()) {
        return false; // Can't merge different command types
    }
    // Cast the other command
    const SetCurveStateCommand *other = static_cast<const SetCurveStateCommand*>(command);
    // Check if it's the same widget
    if (other->m_widget != m_widget) {
        return false;
    }

    // Merge logic: Keep the OLD state from the *first* command (this one)
    // and the NEW state from the *latest* command (the 'command' parameter).
    m_newState = other->m_newState;

    // Update the command text if desired
    setText("Modify Curve"); // Keep generic text or update based on merged actions

    return true;
}
*/
