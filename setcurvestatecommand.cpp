#include "setcurvestatecommand.h"
#include "curvewidget.h"
#include <QDebug>

/**
 * @brief Constructor implementation. Stores the widget pointer and copies the state maps.
 */
SetCurveStateCommand::SetCurveStateCommand(CurveWidget *widget,
                                           const CurveStateMap &oldState,
                                           const CurveStateMap &newState,
                                           const QString &text,
                                           QUndoCommand *parent)
    : QUndoCommand(text, parent),
    m_curveWidget(widget),
    m_oldState(oldState),
    m_newState(newState)
{
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
    m_curveWidget->restoreAllChannelNodes(m_oldState);
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
    m_curveWidget->restoreAllChannelNodes(m_newState);
    qDebug() << "Command redone:" << text();
}
