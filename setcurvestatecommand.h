#ifndef SETCURVESTATECOMMAND_H
#define SETCURVESTATECOMMAND_H

#include <QUndoCommand>
#include <QVector>
#include "curvewidget.h"



class SetCurveStateCommand : public QUndoCommand
{
public:
    // Use the nested type from CurveWidget
    using CurveNode = CurveWidget::CurveNode;

    // Constructor takes the single old/new node vectors
    SetCurveStateCommand(CurveWidget *widget,
                         const QVector<CurveNode>& oldNodes,
                         const QVector<CurveNode>& newNodes,
                         const QString& text = "Modify Curve",
                         QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;



private:
    CurveWidget *m_widget;
    QVector<CurveNode> m_oldNodes; // Store single vector state
    QVector<CurveNode> m_newNodes; // Store single vector state
};

#endif // SETCURVESTATECOMMAND_H
