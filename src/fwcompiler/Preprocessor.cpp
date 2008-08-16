/* 

                          Firewall Builder

                 Copyright (C) 2006 NetCitadel, LLC

  Author:  Vadim Kurland     vadim@vk.crocodile.org

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


#include "Preprocessor.h"

#include "fwbuilder/MultiAddress.h"
#include "fwbuilder/Rule.h"
#include "fwbuilder/Firewall.h"
#include "fwbuilder/ObjectGroup.h"
#include "fwbuilder/ServiceGroup.h"

#include <iostream>

using namespace libfwbuilder;
using namespace fwcompiler;
using namespace std;

string Preprocessor::myPlatformName() { return "generic_preprocessor"; }

Preprocessor::~Preprocessor() {}

Preprocessor::Preprocessor(FWObjectDatabase *_db,
                           const string &fwname, bool ipv6_policy) :
    Compiler(_db, fwname, ipv6_policy)
{
    // This is the main difference between Preprocessor and other
    // compilers.  All compilers create a copy of the whole database
    // and work with it, but Preprocessor works with the original
    // database. Therefore it copies only pointer here.
    dbcopy = _db;
}

void Preprocessor::convertObject(FWObject *obj)
{
    MultiAddress *adt=MultiAddress::cast(obj);
    if (adt!=NULL && adt->isCompileTime() && isUsedByThisFirewall(obj))
    {
        try
        {
            adt->loadFromSource(ipv6);
        } catch (FWException &ex)
        {
            abort(ex.toString());
        }
    }
}

/*
 * Check if object obj is used in any rules of the firewall we compile.
 * Tricky part: object might be used by a group and then the group used
 * in a rule
 */
bool Preprocessor::isUsedByThisFirewall(FWObject *obj)
{
    set<FWObject*> resset;
    obj->getRoot()->findWhereUsed(obj,obj->getRoot(),resset);

    set<FWObject*>::iterator i;
    for (i=resset.begin(); i!=resset.end(); ++i)
    {
        FWObject *p = (*i);

        if (ObjectGroup::isA(p) || ServiceGroup::isA(p))
        {
            // object (*i) is used in a group. Check if the group
            // is used in any rule
            if (isUsedByThisFirewall(p)) return true;
            // but if this group is not used in any rule of this firewall
            // we should keep scanning objects in resset
            continue;
        }


        while (p && !Firewall::isA(p)) p = p->getParent();
        if (p && p->getId()==fw->getId()) return true;
    }
    return false;
}

void Preprocessor::convertObjectsRecursively(FWObject *o)
{
    for (FWObject::iterator i=o->begin(); i!=o->end(); ++i)
    {
        FWObject *obj = *i;
        convertObject(obj);
// we only need to convert objects in system groups where objects
// represent themselves (rather than by reference)
// All other objects that have children must be ignored, these are
// hosts, firewalls etc. Objects DNSName and MultiAddress can only
// be in groups.
        if (Group::cast(obj)!=NULL) convertObjectsRecursively(obj);
    }
}

int  Preprocessor::prolog()
{
    return 0;
}

void Preprocessor::compile()
{
/* resolving MultiAddress objects */
    convertObjectsRecursively(dbcopy);
}

void Preprocessor::epilog()
{}

