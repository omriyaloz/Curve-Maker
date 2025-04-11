#ifndef SETCURVESTATECOMMAND_H
#define SETCURVESTATECOMMAND_H

#include <QUndoCommand>
#include <QVector>
// We need the full definition of CurveNode, which is inside CurveWidget
#include "curvewidget.h" // Include the widget header

// Forward declare CurveWidget to avoid circular includes if possible,
// but we need the full definition for CurveNode here.
// class CurveWidget;

class SetCurveStateCommand : public QUndoCommand
{
public:
    // Use the nested CurveNode type name
    using CurveNode = CurveWidget::CurveNode;

    SetCurveStateCommand(CurveWidget *widget,
                         const QVector<CurveNode>& oldState,
                         const QVector<CurveNode>& newState,
                         const QString& text = "Modify Curve", // Allow setting command text
                         QUndoCommand *parent = nullptr);

    // Reimplement undo/redo functions
    void undo() override;
    void redo() override;

    // Optional: Implement merging for consecutive moves
    // int id() const override;
    // bool mergeWith(const QUndoCommand *command) override;

private:
    CurveWidget *m_widget; // Pointer back to the widget to modify
    QVector<CurveNode> m_oldState;
    QVector<CurveNode> m_newState;
    // Store the selection state too? Optional, adds complexity.
    // int m_oldSelectionIndex;
    // int m_newSelectionIndex;
};

#endif // SETCURVESTATECOMMAND_H
