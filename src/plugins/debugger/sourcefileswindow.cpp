/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2012 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
**
** GNU Lesser General Public License Usage
**
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this file.
** Please review the following information to ensure the GNU Lesser General
** Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** Other Usage
**
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
** If you have questions regarding the use of this file, please contact
** Nokia at qt-info@nokia.com.
**
**************************************************************************/

#include "sourcefileswindow.h"

#include "debuggeractions.h"
#include "debuggerconstants.h"
#include "debuggercore.h"
#include "debuggerengine.h"

#include <utils/qtcassert.h>
#include <utils/savedaction.h>

#include <QDebug>
#include <QFileInfo>

#include <QContextMenuEvent>
#include <QMenu>


//////////////////////////////////////////////////////////////////
//
// SourceFilesWindow
//
//////////////////////////////////////////////////////////////////

namespace Debugger {
namespace Internal {

SourceFilesTreeView::SourceFilesTreeView(QWidget *parent)
    : BaseTreeView(parent)
{
    setSortingEnabled(true);
}

void SourceFilesTreeView::rowActivated(const QModelIndex &index)
{
    DebuggerEngine *engine = debuggerCore()->currentEngine();
    QTC_ASSERT(engine, return);
    engine->gotoLocation(index.data().toString());
}

void SourceFilesTreeView::contextMenuEvent(QContextMenuEvent *ev)
{
    DebuggerEngine *engine = debuggerCore()->currentEngine();
    QTC_ASSERT(engine, return);
    QModelIndex index = indexAt(ev->pos());
    index = index.sibling(index.row(), 0);
    QString name = index.data().toString();
    bool engineActionsEnabled = engine->debuggerActionsEnabled();

    QMenu menu;
    QAction *act1 = new QAction(tr("Reload Data"), &menu);

    act1->setEnabled(engineActionsEnabled);
    //act1->setCheckable(true);
    QAction *act2 = 0;
    if (name.isEmpty()) {
        act2 = new QAction(tr("Open File"), &menu);
        act2->setEnabled(false);
    } else {
        act2 = new QAction(tr("Open File \"%1\"'").arg(name), &menu);
        act2->setEnabled(true);
    }

    menu.addAction(act1);
    menu.addAction(act2);
    addBaseContextActions(&menu);

    QAction *act = menu.exec(ev->globalPos());

    if (act == act1)
        engine->reloadSourceFiles();
    else if (act == act2)
        engine->gotoLocation(name);
    else
        handleBaseContextAction(act);
}

SourceFilesWindow::SourceFilesWindow()
    : BaseWindow(new SourceFilesTreeView)
{
    setWindowTitle(tr("Source Files"));
}

} // namespace Internal
} // namespace Debugger

