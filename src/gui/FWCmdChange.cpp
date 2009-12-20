/*

                          Firewall Builder

                 Copyright (C) 2009 NetCitadel, LLC

  Author:  Illiya Yalovoy <yalovoy@gmail.com>

  $Id$

  This program is free software which we release under the GNU General Public
  License. You may redistribute and/or modify this program under the terms
  of that license as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  To get a copy of the GNU General Public License, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#include "global.h"

#include "FWCmdChange.h"
#include "FWWindow.h"
#include "ColDesc.h"
#include "RuleSetView.h"

#include "fwbuilder/FWObjectDatabase.h"
#include "fwbuilder/Rule.h"


#include "events.h"

#include <QObject>
#include <QDebug>

using namespace libfwbuilder;
using namespace std;

/********************************************************
 * FWCmdChange
 ********************************************************/

FWCmdChange::FWCmdChange(ProjectPanel *project,
                         FWObject *obj, QString text, bool _rename_children, QUndoCommand* macro):
    FWCmdBasic(project, macro)
{
    setObject(obj);
    rename_children = _rename_children;

    FWObjectDatabase* db = obj->getRoot();

    if (db == obj)
    {
        this->oldState = new FWObjectDatabase();
        this->newState = new FWObjectDatabase();
    } else
    {
        this->oldState = db->create(obj->getTypeName());
        this->newState = db->create(obj->getTypeName());
    }

    this->oldState->duplicateForUndo(obj);
    this->newState->duplicateForUndo(obj);

    if (text.isEmpty())
    {
        setText(QObject::tr("Edit ") + QString::fromUtf8(obj->getName().c_str()));
    } else
    {
        setText(text);
    }
}

FWCmdChange::~FWCmdChange()
{
    delete (oldState);
    delete (newState);
}

void FWCmdChange::undo()
{
    FWObject* obj = getObject();
    obj->duplicateForUndo(oldState);
    notify();
}

void FWCmdChange::redo()
{
    FWObject* obj = getObject();
    obj->duplicateForUndo(newState);
    notify();
}

void FWCmdChange::notify()
{
//    qDebug() << "FWCmdChange::notify()";
    FWObject* obj = getObject();
    QString filename = QString::fromUtf8(obj->getRoot()->getFileName().c_str());

    if (oldState->getName() != newState->getName())
    {
        // objectNameChangedEvent event triggers actions, such as
        // automatic renaming of child objects. This should only be
        // done once, even if we have the same data file opened in
        // several project panels.
        QCoreApplication::postEvent(
            mw->activeProject(), new objectNameChangedEvent(
                filename, obj->getId(),
                QString::fromUtf8(oldState->getName().c_str()),
                QString::fromUtf8(newState->getName().c_str()),
                rename_children));
    }

    QCoreApplication::postEvent(
        mw, new updateObjectEverywhereEvent(filename, obj->getId()));

    if (mw->isEditorVisible())
    {
        QCoreApplication::postEvent(
            mw, new openObjectInEditorEvent(filename, obj->getId()));
    }

    // QCoreApplication::postEvent(
    //     mw, new showObjectInTreeEvent(filename, obj->getId()));

    QCoreApplication::postEvent(mw, new dataModifiedEvent(filename,
                                                          obj->getId()));
}

/********************************************************
 * FWCmdChangeName
 ********************************************************/

FWCmdChangeName::FWCmdChangeName(ProjectPanel *project, FWObject *obj) :
    FWCmdChange(project, obj, QObject::tr("Rename object"))
{}

/*
 * Command FWCmdChangeName is used in ObjectManipulator::autorename()
 * functions that rename children objects when the name of a firewall
 * or its interface changes. This command should not open object it
 * renamed in the editor. If it does, the object in the editor changes
 * when user renames fw or interface. This is especially unexpecred if
 * renaming was triggered by them changing the name of fw or interface
 * and then hitting Tab. User expectation is that keyboard should
 * switch to the next element in the dialog, even if some side effects
 * do happen. Changing the object in the editor looks confusing.
 */
void FWCmdChangeName::notify()
{
    FWObject* obj = getObject();
    QString filename = QString::fromUtf8(obj->getRoot()->getFileName().c_str());

    // when object's name changes, its position in the tree changes
    // too to keep the tree sorted. Need to update the object
    // everywhere, as well as its parent's subtree.

    QCoreApplication::postEvent(mw, new updateObjectInTreeEvent(filename,
                                                                obj->getId()));
    QCoreApplication::postEvent(mw, new reloadRulesetEvent(filename));
    QCoreApplication::postEvent(mw, new dataModifiedEvent(filename, obj->getId()));
}

/********************************************************
 * FWCmdChangeOptionsObject
 ********************************************************/

FWCmdChangeOptionsObject::FWCmdChangeOptionsObject(ProjectPanel *project, FWObject *obj) :
    FWCmdChange(project, obj, QObject::tr("Edit rule options"))
{}

void FWCmdChangeOptionsObject::notify()
{
    FWObject* obj = getObject();
    QString filename = QString::fromUtf8(obj->getRoot()->getFileName().c_str());
    // obj here is a FWOptions. We need to select its parent in the
    // tree and redraw it in the rule set panel. To do so, using
    // obj->getParent(). Note that parent object hasn't changed, so we
    // send showObjectInTreeEvent rather than
    // updateObjectInTreeEvent.
    QCoreApplication::postEvent(
        mw, new showObjectInTreeEvent(filename, obj->getParent()->getId()));
    QCoreApplication::postEvent(mw, new reloadRulesetEvent(filename));
}


