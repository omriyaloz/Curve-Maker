#include "setcurvestatecommand.h"
#include "curvewidget.h" // Must include for restoreNodesInternal

SetCurveStateCommand::SetCurveStateCommand(CurveWidget *widget,
                                           const QVector<CurveNode>& oldNodes,
                                           const QVector<CurveNode>& newNodes,
                                           const QString& text,
                                           QUndoCommand *parent)
    : QUndoCommand(parent),
    m_widget(widget),
    m_oldNodes(oldNodes), // Store single vector
    m_newNodes(newNodes)  // Store single vector
{
    setText(text); // Set user-visible undo/redo text
}

void SetCurveStateCommand::undo() {
    if (m_widget) {
        // Call the single-vector version of restoreNodesInternal
        m_widget->restoreNodesInternal(m_oldNodes);
    }
}

void SetCurveStateCommand::redo() {
    if (m_widget) {
        // Call the single-vector version of restoreNodesInternal
        m_widget->restoreNodesInternal(m_newNodes);
    }
}


