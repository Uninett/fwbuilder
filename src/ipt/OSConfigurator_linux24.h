/* 

                          Firewall Builder

                 Copyright (C) 2002 NetCitadel, LLC

  Author:  Vadim Kurland     vadim@vk.crocodile.org

  $Id: OSConfigurator_linux24.h 1026 2006-05-16 22:39:59Z vkurland $

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

#ifndef _OSNETWORKCONFIGURATOR_LINUX24_HH
#define _OSNETWORKCONFIGURATOR_LINUX24_HH

#include "config.h"

#include "fwcompiler/OSConfigurator.h"

#include "OSData.h"

namespace libfwbuilder {
    class FWObject;
    class MultiAddressRunTime;
};

namespace fwcompiler {

    class OSConfigurator_linux24 : public OSConfigurator {

        OSData   os_data;
        std::map<std::string,std::string>   address_table_objects;
	std::vector<libfwbuilder::InetAddr> virtual_addresses;
        std::list<std::string>              commands_to_add_virtual_addresses;

        std::string getInterfaceVarName(libfwbuilder::FWObject *iface);
        
	public:

	virtual ~OSConfigurator_linux24() {};
	OSConfigurator_linux24(libfwbuilder::FWObjectDatabase *_db,
                               const std::string &fwname);

	virtual std::string myPlatformName();

        virtual int  prolog();
        virtual void epilog();

	virtual void processFirewallOptions();
        virtual void generateCodeForProtocolHandlers(bool have_nat);

	virtual void addVirtualAddressForNAT(const libfwbuilder::Address *addr);
	virtual void addVirtualAddressForNAT(const libfwbuilder::Network *nw);

        void registerMultiAddressObject(libfwbuilder::MultiAddressRunTime *at);
        void printChecksForRunTimeMultiAddress();
        void printShellFunctions();
        std::string printPathForAllTools(const std::string &os);
        void configureInterfaces();
        void printCommandsToAddVirtualAddressesForNAT();
        
        std::string  printRunTimeWrappers(libfwbuilder::FWObject *rule,
                                          const std::string &command);

    };
};

#endif
