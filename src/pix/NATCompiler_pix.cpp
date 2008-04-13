/* 

                          Firewall Builder

                 Copyright (C) 2002 NetCitadel, LLC

  Author:  Vadim Kurland     vadim@vk.crocodile.org

  $Id: NATCompiler_pix.cpp,v 1.1 2008/03/06 06:48:59 vkurland Exp $

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

#include "config.h"

#include "NATCompiler_pix.h"

#include "fwbuilder/FWObjectDatabase.h"
#include "fwbuilder/RuleElement.h"
#include "fwbuilder/NAT.h"
#include "fwbuilder/AddressRange.h"
#include "fwbuilder/ICMPService.h"
#include "fwbuilder/TCPService.h"
#include "fwbuilder/UDPService.h"
#include "fwbuilder/Interface.h"
#include "fwbuilder/IPv4.h"
#include "fwbuilder/InetAddr.h"
#include "fwbuilder/Network.h"
#include "fwbuilder/Resources.h"
#include "fwbuilder/AddressTable.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <assert.h>

using namespace libfwbuilder;
using namespace fwcompiler;
using namespace std;

string NATCompiler_pix::myPlatformName() { return "pix"; }

NATCompiler_pix::NATCompiler_pix(FWObjectDatabase *_db,
                                 const std::string &fwname,
                                 OSConfigurator *_oscnf) : 
    NATCompiler(_db,fwname,_oscnf) , helper(this)
{ 
}

string NATCompiler_pix::getNATACLname(Rule *rule,int nat_id)
{
    int    n=-1;
    string res;
    do 
    {
        n++;
        ostringstream os;
        os << rule->getUniqueId() << "." << nat_id << "." << n;
        res=os.str();
    } while (nat_acl_names.count(res)!=0);
    return res;
}

string NATCompiler_pix::getNATACLname(Rule *rule,string suffix)
{
    int    n=-1;
    string res;
    do 
    {
        n++;
        ostringstream os;
        os << rule->getUniqueId();
        if (!suffix.empty()) os << "." << suffix;
        os << "." << n;
        res=os.str();
    } while (nat_acl_names.count(res)!=0);
    return res;
}

int NATCompiler_pix::prolog()
{
    global_pool_no=1;

    final_ruleset=new NAT();
    fw->add( final_ruleset );

    return NATCompiler::prolog();
}

string NATCompiler_pix::debugPrintRule(Rule *r)
{
    NATRule *rule=NATRule::cast(r);

    Interface *iface1 = getCachedFwInterface( rule->getStr("nat_iface_orig") );
    Interface *iface2 = getCachedFwInterface( rule->getStr("nat_iface_trn")  );
    string iface1_name=(iface1!=NULL)?iface1->getName():"";
    string iface2_name=(iface2!=NULL)?iface2->getName():"";

    ostringstream os;

    switch (rule->getRuleType()) 
    {
    case NATRule::NONAT:
        break;

    case NATRule::SNAT:
    {
        if ( ! rule->exists("nat_cmd") ) break;
        NATCmd *natcmd=nat_commands[ rule->getInt("nat_cmd") ];
        if (natcmd!=NULL)
        {
            os <<" NATCmd: ";
            os << " rule=[" << natcmd->rule_label << "]";
            os << " id=" << natcmd->nat_id;
            os << " rule=" << natcmd->rule_label;
            os << " nat_acl_name="   <<  natcmd->nat_acl_name;
            os << " (" << nat_acl_names[natcmd->nat_acl_name] << ")";
            os << " o_src="          <<  natcmd->o_src->getAddress().toString();
            os << " o_dst="          <<  natcmd->o_dst->getAddress().toString();
            os << " o_srv="          <<  natcmd->o_srv->getName();
            os << " o_iface="        <<  natcmd->o_iface->getLabel();
            os << " t_addr="         <<  natcmd->t_addr->getAddress().toString();
            os << " t_iface="        <<  natcmd->t_iface->getLabel();
            os << " ignore_global="  <<  string((natcmd->ignore_global)?"1":"0");
            os << " ignore_nat="     <<  string((natcmd->ignore_nat)?"1":"0");
            os << " ignore_nat_and_print_acl=" << string((natcmd->ignore_nat_and_print_acl)?"1":"0");
            os << " use_nat_0_0="    <<  string((rule->getBool("use_nat_0_0"))?"1":"0");
        }
    }
    break;

    case NATRule::DNAT:
    {
        if ( ! rule->exists("sc_cmd") ) break;
        StaticCmd *scmd=static_commands[ rule->getInt("sc_cmd") ];
        if (scmd!=NULL)
        {
            os << " StaticCmd:";
            os << " acl=" << scmd->acl_name;
            os << " (" << nat_acl_names[scmd->acl_name] << ")";
            os << " iaddr=" << scmd->iaddr->getAddress().toString();
            os << " oaddr=" << scmd->oaddr->getAddress().toString();
            os << " osrc=" << scmd->osrc->getAddress().toString();
            os << " osrv=" << scmd->osrv->getName();
            os << " tsrv=" << scmd->tsrv->getName();
        }
    }
    break;
    default: ;    // TODO: should actually be always_assert
    }

    return NATCompiler::debugPrintRule(rule)+
        " "+iface1_name+" "+iface2_name+
        " (type="+rule->getRuleTypeAsString()+") "+
        "use_nat_0_0=" + string((rule->getBool("use_nat_0_0"))?"1":"0") + " " +
        os.str();
}

bool NATCompiler_pix::storeProcessedRules::processNext()
{
    NATCompiler_pix *pix_comp=dynamic_cast<NATCompiler_pix*>(compiler);
    NATRule *rule=getNext(); if (rule==NULL) return false;
    tmp_queue.push_back(rule);

    pix_comp->final_ruleset->add(rule);

    return true;
}

list<triplet> NATCompiler_pix::findDNATForAddress(Address *src,
                                                  Address *dst,
                                                  Service *srv)
{
    list<triplet> res;
    map<string,triplet> res_dict;

    for (FWObject::iterator i=final_ruleset->begin();
         i!=final_ruleset->end(); ++i)
    {
        NATRule *rule=NATRule::cast(*i);

        switch (rule->getRuleType())
        {
        case NATRule::DNAT:
        {
            Address  *osrc=getFirstOSrc(rule);  assert(osrc);
            Address  *odst=getFirstODst(rule);  assert(odst);
            Service  *osrv=getFirstOSrv(rule);  assert(osrv);
                           
            Address  *tsrc=getFirstTSrc(rule);  assert(tsrc);
            Address  *tdst=getFirstTDst(rule);  assert(tdst);
            Service  *tsrv=getFirstTSrv(rule);  assert(tsrv);

            if (src->getAddress()==osrc->getAddress() &&
                (osrv->isAny() || srv->getId()==tsrv->getId()) &&
                dst->getAddress()==tdst->getAddress())
            {
                if (osrv->isAny())
                {
                    triplet tr(src,odst,srv);
                    res_dict[tr.hash()] = tr;
                }
                else
                {
                    triplet tr(src,odst,osrv);
                    res_dict[tr.hash()] = tr;
                }
            }
        }
        break;
        default: ;    // TODO: should actually be always_assert
        }
    }
    for (map<string,triplet>::iterator i=res_dict.begin(); 
         i!=res_dict.end(); ++i)
    {
        res.push_back(i->second);
    }
    return res;
}
	

bool NATCompiler_pix::VerifyRules::processNext()
{
    NATRule *rule=getNext(); if (rule==NULL) return false;
    string vers=compiler->fw->getStr("version");

    tmp_queue.push_back(rule);

    bool  version_lt_63= ( compiler->fw->getStr("platform")=="pix" &&
                           libfwbuilder::XMLTools::version_compare(compiler->fw->getStr("version"),"6.3")<0);  // fwsm is always above 6.3 - its OS is based on 6.3


    RuleElementOSrc  *osrc=rule->getOSrc();  assert(osrc);
    RuleElementODst  *odst=rule->getODst();  assert(odst);
    RuleElementOSrv  *osrv=rule->getOSrv();  assert(osrv);

    RuleElementTSrc  *tsrc=rule->getTSrc();  assert(tsrc);
    RuleElementTDst  *tdst=rule->getTDst();  assert(tdst);
    RuleElementTSrv  *tsrv=rule->getTSrv();  assert(tsrv);

    if (rule->getRuleType()==NATRule::LB)
        compiler->abort("Load balancing rules are not supported. Rule "+rule->getLabel());

    if (rule->getRuleType()==NATRule::NONAT && (!osrv->isAny() || !tsrv->isAny()))
        compiler->abort("'no nat' rules should have no services");


    if (osrc->getNeg() ||
        odst->getNeg() ||
        osrv->getNeg() ||
        tsrc->getNeg() ||
        tdst->getNeg() ||
        tsrv->getNeg())
        compiler->abort("Negation is not supported in NAT rules. Rule "+rule->getLabel());

    if (rule->getRuleType()==NATRule::SNAT) 
    {
//        if ( tsrc->size()!=1)
//            compiler->abort("There should be no more than one object in translated source in the rule "+rule->getLabel());

        if ( ! odst->isAny() && version_lt_63)  // can do on fwsm
        {
            compiler->warning("Original destination is ignored in 'nat' NAT rules when compiling for PIX v6.2 and earlier. Rule "+rule->getLabel());
            odst->clearChildren();
            odst->setAnyElement();
        }
    }

    if (rule->getRuleType()==NATRule::DNAT) 
    {
        if ( odst->size()!=1 && version_lt_63)
            compiler->abort("There should be no more than one object in original destination in the rule "+rule->getLabel());

        if ( ! osrc->isAny() && version_lt_63)
            compiler->warning("Original source is ignored in 'static' NAT rules when compiling for PIX v6.2 and earlier. Rule "+rule->getLabel());
    }

    if (osrv->size()!=1 && !tsrv->isAny())
        compiler->abort("Can not translate multiple services into one service in one rule. Rule: "+rule->getLabel());

    if (tsrv->size()!=1)
        compiler->abort("Translated service should be 'Original' or should contain single object. Rule: "+rule->getLabel());

    if ( Group::cast( compiler->getFirstTSrv(rule) )!=NULL)
        compiler->abort("Can not use group in translated service. Rule "+rule->getLabel());

    if (rule->getRuleType()==NATRule::SNetnat && !tsrc->isAny() ) 
    {
        Network *a1=Network::cast(compiler->getFirstOSrc(rule));
        Network *a2=Network::cast(compiler->getFirstTSrc(rule));
        if ( a1==NULL || a2==NULL ||
             a1->getNetmask().getLength()!=a2->getNetmask().getLength() )
            compiler->abort("Original and translated source should both be networks of the same size . Rule "+rule->getLabel());
    }

    if (rule->getRuleType()==NATRule::DNetnat && !tsrc->isAny() ) 
    {
        Network *a1=Network::cast(compiler->getFirstODst(rule));
        Network *a2=Network::cast(compiler->getFirstTDst(rule));
        if ( a1==NULL || a2==NULL ||
             a1->getNetmask().getLength()!=a2->getNetmask().getLength() )
            compiler->abort("Original and translated destination should both be networks of the same size . Rule "+rule->getLabel());
    }

    if (rule->getRuleType()==NATRule::SNetnat) rule->setRuleType(NATRule::SNAT);
    if (rule->getRuleType()==NATRule::DNetnat) rule->setRuleType(NATRule::DNAT);

    return true;
}


bool NATCompiler_pix::AssignInterface::processNext()
{
    Helper helper(compiler);
    NATRule *rule=getNext(); if (rule==NULL) return false;
    tmp_queue.push_back(rule);

    Address  *a1=NULL;
    Address  *a2=NULL;

    if (rule->getRuleType()==NATRule::SNAT) {
	a1=compiler->getFirstOSrc(rule);
	a2=compiler->getFirstTSrc(rule);
    }
    if (rule->getRuleType()==NATRule::DNAT) {
	a1=compiler->getFirstODst(rule);
	a2=compiler->getFirstTDst(rule);
    }
    if (rule->getRuleType()==NATRule::NONAT) {
	a1=compiler->getFirstOSrc(rule);
	a2=compiler->getFirstODst(rule);
    }

    assert(a1!=NULL && a2!=NULL);

    rule->setStr("nat_iface_orig",helper.findInterfaceByNetzone(a1));
    rule->setStr("nat_iface_trn", helper.findInterfaceByNetzone(a2));

    if ( rule->getStr("nat_iface_orig")=="" ) 
	compiler->abort("Object '"+a1->getName()+"' does not belong to any known network zone. Rule: "+rule->getLabel());
    if ( rule->getStr("nat_iface_trn")=="" ) 
	compiler->abort("Object '"+a2->getName()+"' does not belong to any known network zone. Rule: "+rule->getLabel());

//    if ( rule->getStr("nat_iface_orig")==rule->getStr("nat_iface_trn"))
//	compiler->abort("Objects '"+a1->getName()+"' and '"+a2->getName()+"' belong to the same network zone. Can not build NAT configuration. Rule: "+rule->getLabel());

    return true;
}

bool NATCompiler_pix::verifyInterfaces::processNext()
{
    NATRule *rule=getNext(); if (rule==NULL) return false;
    tmp_queue.push_back(rule);

#ifdef WRONG_CHECK
    if ( rule->getStr("nat_iface_orig")!=rule->getStr("nat_iface_trn") )
    {
	if (rule->getRuleType()==NATRule::SNAT) {
	    Interface *iface1=
		Interface::cast( rule->getRoot()->findInIndex(rule->getStr("nat_iface_orig")) );
	    Interface *iface2=
		Interface::cast( rule->getRoot()->findInIndex(rule->getStr("nat_iface_trn")) );

	    if ( iface1->getSecurityLevel() <= iface2->getSecurityLevel() ) {
		char lvl1[32];
		char lvl2[32];
		sprintf(lvl1,"%d",iface1->getSecurityLevel());
		sprintf(lvl2,"%d",iface2->getSecurityLevel());
		compiler->abort(
"Security level of internal interface "+
 iface1->getName() + " (level "+ lvl1 +") "+
" set lower than that of external interface "+
 iface2->getName() + " (level "+ lvl2 +") "+
" for NAT rule "+rule->getLabel());
	    }
	}
    }
#endif
    return true;
}

bool NATCompiler_pix::verifyRuleElements::processNext()
{
    NATRule *rule=getNext(); if (rule==NULL) return false;
    tmp_queue.push_back(rule);

    Address  *osrc=compiler->getFirstOSrc(rule);  assert(osrc);
    Address  *odst=compiler->getFirstODst(rule);  assert(odst);
    Service  *osrv=compiler->getFirstOSrv(rule);  assert(osrv);

    Address  *tsrc=compiler->getFirstTSrc(rule);  assert(tsrc);
    Address  *tdst=compiler->getFirstTDst(rule);  assert(tdst);
    Service  *tsrv=compiler->getFirstTSrv(rule);  assert(tsrv);

    bool      version_lt_63=libfwbuilder::XMLTools::version_compare(compiler->fw->getStr("version"),"6.3")<0;

    if (rule->getRuleType()==NATRule::SNAT)
    {
	if ((! osrv->isAny() || ! tsrv->isAny()) && version_lt_63)
	    compiler->abort("only PIX v6.3 recognizes services in global NAT. Rule: "+rule->getLabel() );
    }

    if (rule->getRuleType()==NATRule::DNAT)
    {
	if (AddressRange::cast(odst) || AddressRange::cast(tdst)) 
	    compiler->abort(
		"Address ranges are not supported in original destination or translated destination in NAT rule "+rule->getLabel() );

	if (Network::isA(odst) && Network::isA(tdst))
        {
            InetNetmask n1=(Interface::cast(odst))?InetNetmask(InetAddr::getAllOnes()):odst->getNetmask();
            InetNetmask n2=(Interface::cast(tdst))?InetNetmask(InetAddr::getAllOnes()):tdst->getNetmask();

            if ( !(n1==n2) )
                compiler->abort(
                    "Original and translated destination must be of the same size in the NAT rule "+rule->getLabel());
        }


	if (osrv->getTypeName()!=tsrv->getTypeName()) 
	    compiler->abort("Original and translated services must be of the same type. Rule: "+rule->getLabel());

	if (ICMPService::isA(osrv))
	    compiler->abort("ICMP services are not supported in static NAT. Rule: "+rule->getLabel());

	if (TCPService::isA(osrv) || UDPService::isA(osrv))
        {
	    int drs=osrv->getInt("dst_range_start");
	    int dre=osrv->getInt("dst_range_end");

	    if (drs!=dre)
		compiler->abort("TCP or UDP service with a port range is not supported in NAT. Rule "+rule->getLabel());
	}
	if (TCPService::isA(tsrv) || UDPService::isA(tsrv))
        {
	    int drs=tsrv->getInt("dst_range_start");
	    int dre=tsrv->getInt("dst_range_end");

	    if (drs!=dre)
		compiler->abort("TCP or UDP service with a port range is not supported in NAT. Rule "+rule->getLabel());
	}
    }

    return true;
}

bool NATCompiler_pix::fillTranslatedSrv::processNext()
{
    NATRule *rule=getNext(); if (rule==NULL) return false;

    tmp_queue.push_back(rule);

    Service  *osrv_o=compiler->getFirstOSrv(rule);
    Service  *tsrv_o=compiler->getFirstTSrv(rule);

    if ( ! osrv_o->isAny()  && tsrv_o->isAny() ) {
	RuleElementTSrv  *tsrv=rule->getTSrv();
	tsrv->addRef(osrv_o);
    }
    return true;
}


/**
 * unlike standard inspector addressRanges in the base class NATCompiler,
 * this one does not expand address ranges in TSrc and TDst 
 */
bool NATCompiler_pix::ExpandAddressRanges::processNext()
{
    NATRule *rule=getNext(); if (rule==NULL) return false;

    tmp_queue.push_back(rule);

    RuleElement *rel;

    rel=rule->getOSrc();    assert(rel);
    compiler->_expandAddressRanges(rule,rel);
    rel=rule->getODst();    assert(rel);
    compiler->_expandAddressRanges(rule,rel);

#if 0
// if we want to support NAT rules with address ranges. For example,
// could  compile these as a bunch of individual host translations

    switch (rule->getRuleType())
    {
    case NATRule::SNAT:
        rel=rule->getTSrc();    assert(rel);
        compiler->_expandAddressRanges(rule,rel);
        break;

    case NATRule::DNAT:
        rel=rule->getTDst();    assert(rel);
        compiler->_expandAddressRanges(rule,rel);
        break;
    }
#endif
    return true;
}

/*
 *  I assume that there is always only one object in ODst, TSrc and TDst
 *  rule elements. This should have been assured by inspector VerifyRules
 */
bool NATCompiler_pix::ReplaceFirewallObjectsODst::processNext()
{
    NATRule *rule=getNext(); if (rule==NULL) return false;

    tmp_queue.push_back(rule);

    list<FWObject*> cl;
    RuleElementODst *rel;
    Address     *obj=NULL;

    switch (rule->getRuleType()) {

    case NATRule::Masq:
//    case NATRule::Redirect: 
	return true;
    case NATRule::DNAT:
	rel=rule->getODst();      assert(rel);
	obj=compiler->getFirstODst(rule);  assert(obj!=NULL);

	if (obj->getId()==compiler->getFwId() ) 
	{

	    list<FWObject*> l2=compiler->fw->getByType(Interface::TYPENAME);
	    for (list<FWObject*>::iterator i=l2.begin(); i!=l2.end(); ++i) {
		Interface *interface_=Interface::cast(*i);

		if (! interface_->isLoopback() && 
                      interface_->isExt() ) cl.push_back(interface_);
	    }
	    if ( ! cl.empty() ) {
		while (rel->size()) 
		    rel->remove( rel->front()  );
		
		for (FWObject::iterator i1=cl.begin(); i1!=cl.end(); ++i1) 
		{
		    rel->addRef( *i1 );
		}
	    }
	}
        default: ;    // TODO: should actually be always_assert
    }
    return true;
}

bool NATCompiler_pix::ReplaceFirewallObjectsTSrc::processNext()
{
    Helper helper(compiler);
    NATRule *rule=getNext(); if (rule==NULL) return false;

    tmp_queue.push_back(rule);

    list<FWObject*> cl;
    RuleElementTSrc *rel;
    Address     *obj=NULL;

    switch (rule->getRuleType()) {

    case NATRule::Masq:
    case NATRule::Redirect: return true;

    case NATRule::SNAT:
    {
        int        osrc_level=100;
        Address   *osrc=NULL;
        Interface *osrc_iface=NULL;

        if ( ! rule->getOSrc()->isAny())
        {
            osrc=compiler->getFirstOSrc(rule); assert(osrc!=NULL);
            osrc_iface=compiler->getCachedFwInterface( helper.findInterfaceByNetzone(osrc ) );
            osrc_level=osrc_iface->getSecurityLevel();
        }

	rel=rule->getTSrc();      assert(rel);
	obj=compiler->getFirstTSrc(rule);  assert(obj!=NULL);

	if (obj->getId()==compiler->getFwId() ) 
	{

/* if ODst is 'any', pick all interfaces with security level _less_ than
 * level of the interface OSrc is associated with. If ODst is not 'any', 
 * find interface it is associated with and use only it.
 */
            if (rule->getODst()->isAny()) 
            {
                list<FWObject*> l2=compiler->fw->getByType(Interface::TYPENAME);
                for (list<FWObject*>::iterator i=l2.begin(); i!=l2.end(); ++i) 
                {
                    Interface *interface_=Interface::cast(*i);

                    if (interface_->getSecurityLevel()<osrc_level )  
                        cl.push_back(interface_);
                }
            } else
            {
                Address   *odst=compiler->getFirstODst(rule); assert(odst!=NULL);
                Interface *odst_iface=compiler->getCachedFwInterface( helper.findInterfaceByNetzone(odst ) );
                if (odst_iface!=NULL) cl.push_back(odst_iface);
            }
	    if ( ! cl.empty() ) {
		while (rel->size()) 
		    rel->remove( rel->front()  );
		
		for (FWObject::iterator i1=cl.begin(); i1!=cl.end(); ++i1) 
		{
		    rel->addRef( *i1 );
		}
	    }
	}
    }
    break;
    default: ;    // TODO: should actually be always_assert
    }
    return true;
}

void NATCompiler_pix::UseFirewallInterfaces::scanInterfaces(RuleElement *rel)
{
    FWObject *o= rel->front();
    if (FWReference::cast(o)!=NULL) o=FWReference::cast(o)->getPointer();
    Address *obj=Address::cast(o);
    if(obj==NULL)
        compiler->abort("Broken rule element "+
                        rel->getTypeName()+
                        " in rule "+
                        NATRule::cast(rel->getParent())->getLabel()+
                        " ( found object with type "+
                        string((o!=NULL)?o->getTypeName():"<NULL>") +
                        ")");

    list<FWObject*> l2=compiler->fw->getByType(Interface::TYPENAME);
    for (list<FWObject*>::iterator i=l2.begin(); i!=l2.end(); ++i)
    {
        Interface *interface_=Interface::cast(*i);

        if (interface_->getAddress()==obj->getAddress()) 
        {
            rel->removeRef(obj);
            rel->addRef(interface_);
            return;
        }        
    }
}

bool NATCompiler_pix::UseFirewallInterfaces::processNext()
{
    NATRule *rule=getNext(); if (rule==NULL) return false;

    tmp_queue.push_back(rule);

    RuleElement *rel;

    rel=rule->getODst();      assert(rel);
    if (!rel->isAny()) scanInterfaces(rel);

    rel=rule->getTSrc();      assert(rel);
    if (!rel->isAny()) scanInterfaces(rel);

    return true;
}

bool NATCompiler_pix::processNONATRules::processNext()
{
    Helper helper(compiler);
    NATCompiler_pix *pix_comp=dynamic_cast<NATCompiler_pix*>(compiler);
    NATRule *rule=getNext(); if (rule==NULL) return false;
    tmp_queue.push_back(rule);

    if (rule->getRuleType()==NATRule::NONAT) {
	Address  *osrc=compiler->getFirstOSrc(rule);  assert(osrc);
	Address  *odst=compiler->getFirstODst(rule);  assert(odst);

        Interface *osrc_iface=compiler->getCachedFwInterface( helper.findInterfaceByNetzone(osrc ) );
        Interface *odst_iface=compiler->getCachedFwInterface( helper.findInterfaceByNetzone(odst ) );

        int osrc_level=osrc_iface->getSecurityLevel();
        int odst_level=odst_iface->getSecurityLevel();

/*
 * PIX has two types of NONAT rules, one is when connection goes from
 * low security interface to the high security interface and another
 * for the opposite
 */
        if (osrc_level>odst_level)
        {
            rule->setInt("nonat_type",NONAT_NAT0);
            nonat n0;
            n0.i_iface=osrc_iface;
            n0.o_iface=odst_iface;
            n0.src=osrc;
            n0.dst=odst;
            n0.acl_name="nat0."+osrc_iface->getLabel();
            n0.last=true;

            pix_comp->nonat_rules[rule->getId()]= n0;
            pix_comp->registerACL(n0.acl_name);

            if ( pix_comp->first_nonat_rule_id[osrc_iface->getId()].empty() ) 
                pix_comp->first_nonat_rule_id[osrc_iface->getId()]=rule->getId();
            
        } else
        {
            rule->setInt("nonat_type",NONAT_STATIC);
        }
    }

    return true;
}

bool NATCompiler_pix::createNATCmd::processNext()
{
//    Helper helper(compiler);
    NATCompiler_pix *pix_comp=dynamic_cast<NATCompiler_pix*>(compiler);
    NATRule *rule=getNext(); if (rule==NULL) return false;
    tmp_queue.push_back(rule);

    if (rule->getRuleType()==NATRule::SNAT) 
    {
	Address  *osrc=compiler->getFirstOSrc(rule);  assert(osrc);
	Address  *odst=compiler->getFirstODst(rule);  assert(osrc);
	Service  *osrv=compiler->getFirstOSrv(rule);  assert(osrv);
	Address  *tsrc=compiler->getFirstTSrc(rule);  assert(tsrc);

	NATCmd *natcmd=new NATCmd();

        natcmd->nat_id=nat_id_counter;
        natcmd->rule_label=rule->getLabel();

        natcmd->o_src   = osrc;
        natcmd->o_dst   = odst;
        natcmd->o_srv   = osrv;
        natcmd->o_iface = compiler->getCachedFwInterface( rule->getStr("nat_iface_orig") );
        natcmd->t_addr  = tsrc;
        natcmd->t_iface = compiler->getCachedFwInterface( rule->getStr("nat_iface_trn" ) );

        natcmd->nat_acl_name = pix_comp->getNATACLname(rule,"");
        pix_comp->registerACL(natcmd->nat_acl_name);

        if (Interface::cast(tsrc)!=NULL || natcmd->t_iface->isDyn()) 
        {
            natcmd->type=INTERFACE;
        } else {
            if (Network::cast(tsrc)) 
            {
                natcmd->type=NETWORK_ADDRESS;
            } else {
                if (AddressRange::cast(tsrc))       natcmd->type=ADDRESS_RANGE;
                else                                natcmd->type=SINGLE_ADDRESS;
            }
        }

        natcmd->ignore_nat=natcmd->ignore_nat_and_print_acl=natcmd->ignore_global=false;
        natcmd->use_nat_0_0 = rule->getBool("use_nat_0_0");
        
/*
 * "nat ... outside" is only supported in PIX 6.2
 */
        natcmd->outside= ( natcmd->o_iface->getSecurityLevel()<natcmd->t_iface->getSecurityLevel());

        if (natcmd->outside && compiler->fw->getStr("platform")=="pix" &&
            libfwbuilder::XMLTools::version_compare(compiler->fw->getStr("version"),"6.2")<0 )
            compiler->abort("Bi-Directional NAT of source addresses is only supported in  PIX 6.2 and newer. Rule "+rule->getLabel());

/* 
 * map is sorted container, this means that objects are going to be arranged
 * in nat_commands in the order of the key.
 */
        pix_comp->nat_commands[nat_id_counter]= natcmd;
        rule->setInt("nat_cmd",nat_id_counter);
        nat_id_counter++;
    }

    return true;
}

bool NATCompiler_pix::createStaticCmd::processNext()
{
    NATCompiler_pix *pix_comp=dynamic_cast<NATCompiler_pix*>(compiler);
    NATRule *rule=getNext(); if (rule==NULL) return false;
    tmp_queue.push_back(rule);

    if (rule->getRuleType()==NATRule::DNAT) 
    {
	Address  *osrc=compiler->getFirstOSrc(rule);  assert(osrc);
	Address  *odst=compiler->getFirstODst(rule);  assert(odst);
	Service  *osrv=compiler->getFirstOSrv(rule);  assert(osrv);
	Address  *tdst=compiler->getFirstTDst(rule);  assert(tdst);
	Service  *tsrv=compiler->getFirstTSrv(rule);  assert(tsrv);

	StaticCmd *scmd=new StaticCmd();

        scmd->acl_name = pix_comp->getNATACLname(rule,"");
        pix_comp->registerACL(scmd->acl_name);
        scmd->rule=rule->getLabel();
        // source and destination addresses are swapped here because
        // access lists used for NAT should have 'real' addresses in source
        scmd->iaddr=tdst;
        scmd->oaddr=odst;
        scmd->osrc= osrc;
        scmd->osrv= osrv;
        scmd->tsrv= tsrv;
        scmd->ignore_scmd_and_print_acl=false;

        pix_comp->static_commands[sc_id_counter]=scmd;
        rule->setInt("sc_cmd",sc_id_counter);

        sc_id_counter++;
    }

    return true;
}



/*
 * this processor uses slurp to make sure all previous processors ran before
 * it starts scanning rules. 
 */
bool NATCompiler_pix::mergeNATCmd::processNext()
{
    NATCompiler_pix *pix_comp=dynamic_cast<NATCompiler_pix*>(compiler);

    slurp();
    if (tmp_queue.size()==0) return false;

    for (deque<Rule*>::iterator k=tmp_queue.begin(); k!=tmp_queue.end(); ++k) 
    {
        NATRule *rule = NATRule::cast( *k );

        if (rule->getRuleType()==NATRule::DNAT)
        {
            StaticCmd *scmd=pix_comp->static_commands[rule->getInt("sc_cmd")];

            for (map<int,StaticCmd*>::iterator i1=pix_comp->static_commands.begin();
                 i1!=pix_comp->static_commands.end(); ++i1) 
            {
                StaticCmd *sc=(*i1).second;
                if (scmd==sc) break;
                
                if (*(scmd->oaddr) == *(sc->oaddr) &&
                    *(scmd->iaddr) == *(sc->iaddr) &&
                    *(scmd->osrv)  == *(sc->osrv)  &&
                    *(scmd->tsrv)  == *(sc->tsrv))
                {
/* rule 'sc' is above rule 'scmd', we need to print 'static' command
 * only in the last rule using the same access list. That's why we set
 * flag ignore_scmd_and_print acl in sc and not in scmd
 */

                    scmd->acl_name = sc->acl_name;
                    sc->ignore_scmd_and_print_acl=true;
                }
            }
        }

	if (rule->getRuleType()==NATRule::SNAT) 
        { 
            NATCmd *natcmd=pix_comp->nat_commands[ rule->getInt("nat_cmd") ];

            for (map<int,NATCmd*>::iterator i1=pix_comp->nat_commands.begin();
                 i1!=pix_comp->nat_commands.end(); ++i1) 
            {
                NATCmd *nc=(*i1).second;
/* since map nat_commands is sorted by the key, we only have to scan it
 * until we hit natcmd 
 */
                if (natcmd==nc) break;

                InetAddr  a1=natcmd->t_addr->getAddress();
                InetAddr  a2=nc->t_addr->getAddress();

                Interface *int1=natcmd->t_iface;
                Interface *int2=nc->t_iface;

                if ( a1 == a2 && int1->getId()==int2->getId() ) 
                {
                    natcmd->ignore_global=true;
                    natcmd->nat_id=nc->nat_id;
                }
            }

            for (map<int,NATCmd*>::iterator i1=pix_comp->nat_commands.begin();
                 i1!=pix_comp->nat_commands.end(); ++i1) 
            {
                NATCmd *nc=(*i1).second;
/* since map nat_commands is sorted by the key, we only have to scan it
 * until we hit natcmd 
 */
                if (natcmd==nc) break;
                if (nc->ignore_nat) continue;

/* using operator==(const Address &o1,const Address &o2) here */

                if ( *(natcmd->o_src) == *(nc->o_src) &&
                     *(natcmd->o_dst) == *(nc->o_dst) &&
                     *(natcmd->o_srv) == *(nc->o_srv) &&
                     natcmd->o_iface->getId() == nc->o_iface->getId() )
                {
/*
 * there is another nat rule (rule #2) with the same "original"
 * addresses and the same interface. We can drop this nat rule, but need
 * to merge its global pool with pool of the rule #2.
 *
 * This nat rule could have been sharing a global pool with some other
 * nat rule; in this case we need to find this other rule and also
 * reassign it to the global pool of the rule #2.
 */
                    natcmd->ignore_nat=true;

                    for (map<int,NATCmd*>::iterator i2=pix_comp->nat_commands.begin();
                         i2!=pix_comp->nat_commands.end(); ++i2) 
                    {
                        NATCmd *nc2=i2->second;
                        if (natcmd->nat_id == nc2->nat_id)
                            nc2->nat_id=nc->nat_id;
                    }

                    natcmd->nat_id=nc->nat_id;
                }
            }

            if (!natcmd->use_nat_0_0) 
            {
                for (map<int,NATCmd*>::iterator i1=pix_comp->nat_commands.begin();
                     i1!=pix_comp->nat_commands.end(); ++i1) 
                {
                    NATCmd *nc=(*i1).second;
/* since map nat_commands is sorted by the key, we only have to scan it
 * until we hit natcmd 
 */
                    if (natcmd==nc) break;

/* ignore nat natcmd entries for rules where we won't print 'nat'
 * command or use 'nat 0' command since this means we won't print
 * access-list for those rules and hense can not merge lists
 */

                    if (nc->ignore_nat) continue;
                    if (nc->use_nat_0_0) continue;

                    if ( natcmd->nat_id==nc->nat_id &&
                         natcmd->t_addr == nc->t_addr &&
                         natcmd->o_iface->getId() == nc->o_iface->getId() )
                    {
/* two nat commands with the same id, the same interface and the same
 * translated address, but different osrc and odst. OSrc and ODst must
 * be different, otherwise these two commands would have been merged
 * in the previous cycle.  We can merge access lists and drop one of
 * these nat commands. We merge ACLs by assigning them the same name.
 */

                        natcmd->nat_acl_name = nc->nat_acl_name;
                        nc->ignore_nat_and_print_acl=true;
                    }
                }
            }
	}


    }
    return true;
}

/*
 * The goal of this processor is to find SNAT rules that could be
 * translated as "nat (interface) 0.0.0.0 0.0.0.0. These rules should
 * have the same network object in OSrc that is used to define
 * interface's network zone. The logic is simple: if network "A" is a
 * network zone for internal interface, then only packets from this
 * network can hit it and therefore there is no need to check source
 * address once more in the "nat" rule. 
 *
 * We also check for ODst and OSrv, because if the destination or the
 * service are defined, then this optimization can not be done.
 *
 * This optimization can be turned off using checkbutton in the
 * "Firewall" tab.
 *
 * call this processor really early, when groups have not been
 * expanded yet.  At this point both NAT rule type and interfaces it
 * is associated with are unknown yet. We have to partially repeat
 * algorithms used in other rule processors to determine NAT rule type
 * and interface.
 *
 * We do this optimization in two steps:
 *
 * 1. in this rule processor we replace object in OSrc with firewall's
 * interface. This way we can still use other rule processors that
 * determine rule type and assign it to interfaces, but rule won't be
 * split onto multiple rules because of objects in OSrc. We also set
 * boolean flags "clear_osrc" and "use_nat_0_0" on the rule.
 *
 * 2. further down in rule processor clearOSrc we check the flag and
 * clear OSrc if it is set.
 *
 * 3. flag "use_nat_0_0" is used in printRule processor.
 */
bool  NATCompiler_pix::optimizeDefaultNAT::processNext()
{
//    NATCompiler_pix *pix_comp=dynamic_cast<NATCompiler_pix*>(compiler);
    NATRule *rule=getNext(); if (rule==NULL) return false;
    tmp_queue.push_back(rule);

    RuleElementOSrc  *osrc=rule->getOSrc();
    RuleElementOSrv  *osrv=rule->getOSrv();
    RuleElementODst  *odst=rule->getODst();
    RuleElementTSrc  *tsrc=rule->getTSrc();
    RuleElementTDst  *tdst=rule->getTDst();

    if (osrc->size()>1) return true;
    if (osrc->isAny())  return true;
    if (!osrv->isAny()) return true;
    if (!odst->isAny()) return true;

/*
 * can't use RuleElementOSrc::getFirst(bool dereference) because it
 * returns Address::cast(o), but child element of rule element may be
 * a group when this processor is called.
 */    
    FWObject *o=osrc->front();
    string osrc_id;
    if (FWReference::cast(o)!=NULL) osrc_id=FWReference::cast(o)->getPointerId();
    else osrc_id=o->getId();

    if ( ( !tsrc->isAny() && tdst->isAny()) ||
         ( !osrc->isAny() && odst->isAny() && tsrc->isAny() && tdst->isAny() )
    )
    {
//  this rule type is SNAT or NONAT

        list<FWObject*> l2=compiler->fw->getByType(Interface::TYPENAME);
        for (list<FWObject*>::iterator i=l2.begin(); i!=l2.end(); ++i) 
        {
            Interface *iface=Interface::cast(*i);

            if (iface->getStr("orig_netzone_id")==osrc_id )
            {
                rule->setBool("clear_osrc",true);
                rule->setBool("use_nat_0_0",true);
                osrc->clearChildren();
                osrc->addRef(iface);
                break;
            }
        }
    }

    return true;
}

bool  NATCompiler_pix::clearOSrc::processNext()
{
//    NATCompiler_pix *pix_comp=dynamic_cast<NATCompiler_pix*>(compiler);
    NATRule *rule=getNext(); if (rule==NULL) return false;

    if (rule->getBool("clear_osrc"))
    {
        RuleElementOSrc  *osrc=rule->getOSrc();
        osrc->clearChildren();
        osrc->setAnyElement();
    }

    tmp_queue.push_back(rule);
    return true;
}

bool NATCompiler_pix::processMultiAddressObjectsInRE::processNext()
{
    NATRule *rule=getNext(); if (rule==NULL) return false;
    RuleElement *re=RuleElement::cast( rule->getFirstByType(re_type) );

    for (FWObject::iterator i=re->begin(); i!=re->end(); i++)
    {
        FWObject *o= *i;
        if (FWReference::cast(o)!=NULL) o=FWReference::cast(o)->getPointer();
        MultiAddress *atrt = MultiAddress::cast(o);
        if (atrt!=NULL && atrt->isRunTime())
            compiler->abort("Run-time AddressTable and DNSName objects are not supported. Rule " + rule->getLabel());
    }

    tmp_queue.push_back(rule);
    return true;
}


bool  NATCompiler_pix::SuppressDuplicateNONATStatics::processNext()
{
    Helper helper(compiler);
//    NATCompiler_pix *pix_comp=dynamic_cast<NATCompiler_pix*>(compiler);
    NATRule *rule=getNext(); if (rule==NULL) return false;

    if (rule->getRuleType()== NATRule::NONAT && 
        rule->getInt("nonat_type")==NONAT_STATIC)
    {
	Address  *osrc=compiler->getFirstOSrc(rule);  assert(osrc);
	Address  *odst=compiler->getFirstODst(rule);  assert(odst);

        nonat_static_parameters  sp;
        sp.iface1= helper.findInterfaceByNetzone(osrc );
        sp.iface2= helper.findInterfaceByNetzone(odst );
        sp.addr=odst->getAddress();
        sp.mask=odst->getNetmask();

        for (deque<nonat_static_parameters>::iterator i=all_nonat_statics.begin(); 
             i!=all_nonat_statics.end();  ++i )
        {
            if ( i->iface1==sp.iface1 &&
                 i->iface2==sp.iface2 &&
                 i->addr==sp.addr &&
                 i->mask==sp.mask ) return true;
        }
        all_nonat_statics.push_back(sp);
    }

    tmp_queue.push_back(rule);
    return true;
}

NATCompiler_pix::DetectOverlap::~DetectOverlap() {};

bool NATCompiler_pix::DetectOverlap::checkOverlapping(
    const libfwbuilder::Address   &addr1,
    const libfwbuilder::InetAddr &addr2)
{
    if (AddressRange::isA(&addr1))
    {
        const InetAddr a1=AddressRange::constcast(&addr1)->getRangeStart();
        const InetAddr a2=AddressRange::constcast(&addr1)->getRangeEnd();
        return (addr2==a1 || addr2==a2 || (addr2>a1 && addr2<a2));
    } else
    {
        return addr1.getAddress() == addr2 || addr1.belongs(addr2);
    }
}

string NATCompiler_pix::DetectOverlap::printGlobalPoolAddress(const Address &pool)
{
    if (AddressRange::isA(&pool))
    {
        const InetAddr a1=AddressRange::constcast(&pool)->getRangeStart();
        const InetAddr a2=AddressRange::constcast(&pool)->getRangeEnd();
        return a1.toString()+"-"+a2.toString();
    } else
    {
        return pool.getAddress().toString()+"/"+pool.getNetmask().toString();
    }
}

bool  NATCompiler_pix::DetectGlobalPoolProblems::processNext()
{
    NATCompiler_pix *pix_comp=dynamic_cast<NATCompiler_pix*>(compiler);
    NATRule *rule=getNext(); if (rule==NULL) return false;
    tmp_queue.push_back(rule);

    if (rule->getRuleType()== NATRule::SNAT )
    {
        NATCmd *natcmd=pix_comp->nat_commands[ rule->getInt("nat_cmd") ];

        if (natcmd->ignore_global) return true;

        if (natcmd->type!= INTERFACE) 
        {
            if (checkOverlapping(*(natcmd->t_addr),
                                 natcmd->t_iface->getAddress()))
                compiler->abort("Global pool "
                                +printGlobalPoolAddress(*(natcmd->t_addr)) 
                                +" overlaps with interface address. Rule "
                                +rule->getLabel());

            if (checkOverlapping(*(natcmd->t_addr),
                                 natcmd->t_iface->getBroadcastAddress()) ||
                checkOverlapping(*(natcmd->t_addr),
                                 natcmd->t_iface->getAddress()) )
                compiler->warning("Global pool "
                                  +printGlobalPoolAddress(*(natcmd->t_addr)) 
                                  +" overlaps with broadcast address. Rule "
                                  +rule->getLabel());
        }

        for (map<int,NATCmd*>::iterator i1=pix_comp->nat_commands.begin();
             i1!=pix_comp->nat_commands.end(); ++i1) 
        {
            NATCmd *nc=(*i1).second;
/* since map nat_commands is sorted by the key, we only have to scan it
 * until we hit natcmd 
 */
            if (nc->ignore_global) continue;
            if (natcmd==nc) break;

            Interface *int1=natcmd->t_iface;
            Interface *int2=nc->t_iface;

            if ( int1->getId()==int2->getId() )
            {
                if ( ! fwcompiler::_find_obj_intersection(natcmd->t_addr,nc->t_addr).empty() )
                {
                    compiler->abort(string("Global pool overlapping: \n")
                                    +"           "+rule->getLabel()+" : "
                                    +printGlobalPoolAddress(*(natcmd->t_addr)) 
                                    +"\n"
                                    +"           "+nc->rule_label+" : "
                                    +printGlobalPoolAddress(*(nc->t_addr)) );
                }
            }

        }

    }
    return true;
}


bool  NATCompiler_pix::DetectOverlappingGlobalPoolsAndStaticRules::processNext()
{
    NATCompiler_pix *pix_comp=dynamic_cast<NATCompiler_pix*>(compiler);
    NATRule *rule=getNext(); if (rule==NULL) return false;
    tmp_queue.push_back(rule);

    if (rule->getRuleType()== NATRule::DNAT )
    {
        Address  *outa=compiler->getFirstODst(rule);  assert(outa);
        Address  *insa=compiler->getFirstTDst(rule);  assert(insa);

        for (map<int,NATCmd*>::iterator i=pix_comp->nat_commands.begin(); 
             i!=pix_comp->nat_commands.end(); ++i)
        {
            NATCmd *natcmd=(*i).second;

            if (natcmd->ignore_global) return true;

            /* in this case natcmd->t_addr is interface. Interface creates
             * single-address global pool, but since it has netmask,
             * method checkOverlapping would treat it as network. I create
             * temporary substitution Address object to avoid this .
             *
             * If interface is used for a global pool (SNAT rule) and
             * for a static (DNAT rule), then this is ok even though
             * such global pool overlaps with such static (added 10/17/03)
             *
             * But first I need to check if this interface has dynamic
             * address, in which case I can not really do this check
             * at all.
             */
            IPv4 addr;

            Interface *iface=Interface::cast(natcmd->t_addr);
            if (iface!=NULL && iface->isDyn()) return true;

            if (iface!=NULL && iface->getId()==outa->getId()) return true;

            addr.setAddress(natcmd->t_addr->getAddress());
            addr.setNetmask(natcmd->t_addr->getNetmask());

            if (natcmd->type== INTERFACE) 
            {
                addr.setNetmask(InetNetmask(InetAddr::getAllOnes()));
            }

            if ( checkOverlapping(  addr, outa->getAddress()) ||
                 checkOverlapping( *outa, addr.getAddress()) )
                compiler->abort("Global pool "
                                +printGlobalPoolAddress(addr)
                                +" from rule "
                                +natcmd->rule_label
                                +" overlaps with static translation address in rule "
                                +rule->getLabel());
        }
    }
    return true;
}

bool  NATCompiler_pix::DetectDuplicateNAT::processNext()
{
    NATCompiler_pix *pix_comp=dynamic_cast<NATCompiler_pix*>(compiler);
    NATRule *rule=getNext(); if (rule==NULL) return false;
    tmp_queue.push_back(rule);

    if (rule->getRuleType()== NATRule::SNAT)
    {
        NATCmd *natcmd=pix_comp->nat_commands[ rule->getInt("nat_cmd") ];

        if (natcmd->ignore_nat) return true;

        for (map<int,NATCmd*>::iterator i1=pix_comp->nat_commands.begin();
             i1!=pix_comp->nat_commands.end(); ++i1) 
        {
            NATCmd *nc=(*i1).second;
/* since map nat_commands is sorted by the key, we only have to scan it
 * until we hit natcmd 
 */
            if (nc->ignore_nat) continue;
            if (natcmd==nc) break;

            Interface *int1=natcmd->t_iface;
            Interface *int2=nc->t_iface;

//            InetAddr a1=natcmd->o_addr->getAddress();
//            InetAddr a2=nc->o_addr->getAddress();
//
//            InetNetmask   m1=natcmd->o_addr->getInetNetmask();
//            InetNetmask   m2=nc->o_addr->getNetmask();

            if ( int1->getId()==int2->getId() &&
                 natcmd->o_src==nc->o_src &&
                 natcmd->o_dst==nc->o_dst &&
                 *(natcmd->o_srv)==*(nc->o_srv)
            )
            {
                compiler->abort("Duplicate NAT detected: rules "
                                +rule->getLabel()
                                +" and "+nc->rule_label
                                +" : "+natcmd->o_src->getAddress().toString()
                                +"/"+natcmd->o_src->getNetmask().toString()
                                + " "
                                + natcmd->o_srv->getProtocolName()
                                + natcmd->o_srv->getStr("src_range_start")+":"
                                + natcmd->o_srv->getStr("src_range_end")+":"
                                + " "
                                +"->"+natcmd->o_dst->getAddress().toString()
                                +"/"+natcmd->o_dst->getNetmask().toString()
                                + " "
                                + natcmd->o_srv->getStr("dst_range_start")+"/"
                                + natcmd->o_srv->getStr("dst_range_end"));
            }
        }
    }
    return true;
}

bool  NATCompiler_pix::DetectOverlappingStatics::processNext()
{
    NATCompiler_pix *pix_comp=dynamic_cast<NATCompiler_pix*>(compiler);
    NATRule *rule=getNext(); if (rule==NULL) return false;
    tmp_queue.push_back(rule);

    if (rule->getRuleType()== NATRule::DNAT )
    {
        StaticCmd *scmd=pix_comp->static_commands[ rule->getInt("sc_cmd") ];

        for (map<int,StaticCmd*>::iterator i1=pix_comp->static_commands.begin();
             i1!=pix_comp->static_commands.end();  i1++ )
        {
//            int        scid=i1->first;
            StaticCmd *sc=  i1->second;
            if (sc->ignore_scmd_and_print_acl) continue;
            if (sc==scmd) break;

            if (Interface::isA(scmd->oaddr) && Interface::isA(sc->oaddr))
            {
                if ( *(sc->osrv) == *(scmd->osrv) && 
                     *(sc->tsrv) == *(scmd->tsrv) && 
                     *(sc->osrc) == *(scmd->osrc) &&
                     sc->oaddr->getId() == scmd->oaddr->getId())
                    compiler->abort(
                        "Static NAT rules overlap or are redundant : rules "+
                        sc->rule+" and "+scmd->rule+" : "+
                        "outside address: "+
                        "interface "+Interface::cast(scmd->oaddr)->getLabel()+
                        " inside address: "+
                        scmd->iaddr->getAddress().toString()+"/"+
                        scmd->iaddr->getNetmask().toString());
            } else
            {
                if ( *(sc->osrv) == *(scmd->osrv) && 
                     *(sc->tsrv) == *(scmd->tsrv) && 
                     *(sc->osrc) == *(scmd->osrc) &&
                     ( ! getOverlap(*(scmd->iaddr), *(sc->iaddr)).empty() ||
                       ! getOverlap(*(scmd->oaddr), *(sc->oaddr)).empty() ) )
                    compiler->abort(
                        "Static NAT rules overlap or are redundant : rules "+
                        sc->rule+" and "+scmd->rule+" : "+
                        "outside address: "+
                        scmd->oaddr->getAddress().toString()+"/"+
                        scmd->oaddr->getNetmask().toString()+
                        " inside address: "+
                        scmd->iaddr->getAddress().toString()+"/"+
                        scmd->iaddr->getNetmask().toString());
            }
        }
    }

    return true;
}


void NATCompiler_pix::compile()
{
    cout << " Compiling NAT rules for " << fw->getName() << " ..." << endl << flush;

    try {

	Compiler::compile();

        add( new Begin( "Begin processing"));

        add( new printTotalNumberOfRules());

        if (fw->getOptionsObject()->getBool( "pix_optimize_default_nat"))
            add (new optimizeDefaultNAT(
                     "optimize commands 'nat (interface) 0.0.0.0 0.0.0.0'"));

        add( new recursiveGroupsInOSrc("check for recursive groups in OSRC"));
        add( new recursiveGroupsInODst("check for recursive groups in ODST"));
        add( new recursiveGroupsInOSrv("check for recursive groups in OSRV"));

        add( new recursiveGroupsInTSrc("check for recursive groups in TSRC"));
        add( new recursiveGroupsInTDst("check for recursive groups in TDST"));
        add( new recursiveGroupsInTSrv("check for recursive groups in TSRV"));


        add( new emptyGroupsInOSrc("check for empty groups in OSRC"));
        add( new emptyGroupsInODst("check for empty groups in ODST"));
        add( new emptyGroupsInOSrv("check for empty groups in OSRV"));

        add( new emptyGroupsInTSrc("check for empty groups in TSRC"));
        add( new emptyGroupsInTDst("check for empty groups in TDST"));
        add( new emptyGroupsInTSrv("check for empty groups in TSRV"));

        add( new ExpandGroups("expand groups"));
        add( new eliminateDuplicatesInOSRC("eliminate duplicates in OSRC"));
        add( new eliminateDuplicatesInODST("eliminate duplicates in ODST"));
        add( new eliminateDuplicatesInOSRV("eliminate duplicates in OSRV"));

        add( new processMultiAddressObjectsInOSrc(
                 "process MultiAddress objects in OSrc"));
        add( new processMultiAddressObjectsInODst(
                 "process MultiAddress objects in ODst"));

        add( new ExpandMultipleAddresses("expand multiple addresses"));
        add( new MACFiltering( "check for MAC address filtering"));
        add( new ExpandAddressRanges("expand address range objects"));
        add( new checkForUnnumbered("check for unnumbered interfaces"));

//        add( new ConvertToAtomic("convert to atomic rules"));
        add( new classifyNATRule("determine NAT rule types"));
//        add( new fillTranslatedSrv("fill translated service element"));
        add( new VerifyRules("verify rules" ));
        add( new ReplaceFirewallObjectsODst("replace fw object in ODst" ));
        add( new ReplaceFirewallObjectsTSrc("replace fw object in TSrc" ));
        add( new UseFirewallInterfaces(
                 "replace host objects with firewall's interfaces if the have the same address"));
        add( new ConvertToAtomic("convert to atomic rules" ));
        add( new AssignInterface("assign rules to interfaces" ));
        add( new verifyInterfaces("verify interfaces assignment" ));
        add( new fillTranslatedSrv("fill translated service element" ));
        add( new verifyRuleElements(
                 "verify rule elements for static NAT rules"));
        add( new processNONATRules("process NONAT" ));

        if (fw->getOptionsObject()->getBool("pix_optimize_default_nat"))
            add (new clearOSrc ("clear OSrc" ));

        add( new createNATCmd ("create NAT commands" ));
        add( new createStaticCmd ("create static commands" ));
        add( new mergeNATCmd ("merge NAT commands" ));
        add( new SuppressDuplicateNONATStatics(
                 "suppress duplicate NONAT statics" ));

        add( new PrintClearCommands( "Clear ACLs" ));

        add( new PrintRule ("generate PIX code" ));
        add( new storeProcessedRules ("store processed rules" ));
        add( new simplePrintProgress ());

        bool pix_check_duplicate_nat = 
            fw->getOptionsObject()->getBool("pix_check_duplicate_nat");
        bool pix_check_overlapping_global_pools =
            fw->getOptionsObject()->getBool("pix_check_overlapping_global_pools");
        bool pix_check_overlapping_statics =
            fw->getOptionsObject()->getBool("pix_check_overlapping_statics");
        bool pix_check_overlapping_global_statics =
            fw->getOptionsObject()->getBool("pix_check_overlapping_global_statics");

        if ( pix_check_duplicate_nat || 
             pix_check_overlapping_global_pools ||
             pix_check_overlapping_statics ||
             pix_check_overlapping_global_statics )
        {
            add( new createNewCompilerPass(" Detecting nat problems  ..."));

            if ( pix_check_duplicate_nat )
                add( new DetectDuplicateNAT(" Detect duplicate nat entries"));

            if ( pix_check_overlapping_global_pools )
                add( new DetectGlobalPoolProblems(
                         " Detect global pool overlapping" ));

            if ( pix_check_overlapping_statics )
                add( new DetectOverlappingStatics(
                         " Detect overlapping statics" ));

            if ( pix_check_overlapping_global_statics )
                add( new DetectOverlappingGlobalPoolsAndStaticRules(
                         " Detect overlapping global pools and statics" ));

            add( new simplePrintProgress ( ));
        }

        runRuleProcessors();

    } catch (FWException &ex) {
	error(ex.toString());
        exit(1);
    }
}


void NATCompiler_pix::regroup()
{
    list<string>              commands;
    map<string,list<string> > script;

    commands.push_back("THE_REST");
    commands.push_back("access-list ");
    commands.push_back("global ");
    commands.push_back("nat ");
    commands.push_back("static ");

    string acl, agrp, icmp, telnet, ssh;
    string new_output;
    char   buf[1024];

    istringstream in(output.str());

    while (in)
    {
        in.getline(buf, 1023, '\n');

        strcat(buf,"\n");

        if (buf[0]=='!') continue;

        string slot="THE_REST";
        for (list<string>::iterator i=commands.begin(); i!=commands.end(); ++i)
        {
            if (strncmp(buf, (*i).c_str(), (*i).size())==0)
            {
                slot= *i;
                break;
            }
        }
        script[slot].push_back(buf);
    }

    output.str("");

    for (list<string>::iterator i=commands.begin(); i!=commands.end(); ++i)
    {

        for (list<string>::iterator j=script[*i].begin(); j!=script[*i].end(); ++j)
            output << *j;
        output << "! \n";
        output << "! \n";
    }
}


void NATCompiler_pix::epilog()
{
    if ( fw->getOptionsObject()->getBool("pix_regroup_commands")) 
    {
        cout << " Regrouping commands \n" << flush;
        regroup();
    }
}
