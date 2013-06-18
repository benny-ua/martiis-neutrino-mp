/*
	Experimental OPKG-Manager - Neutrino-GUI

	Based upon Neutrino-GUI 
	Copyright (C) 2001 Steffen Hehn 'McClean'
	and some other guys
	Homepage: http://dbox.cyberphoria.org/

	Implementation: 
	Copyright (C) 2012 T. Graf 'dbt'
	Homepage: http://www.dbox2-tuning.net/

	Copyright (C) 2013 martii

        License: GPL

        This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Library General Public
	License as published by the Free Software Foundation; either
	version 2 of the License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	Library General Public License for more details.

	You should have received a copy of the GNU Library General Public
	License along with this library; if not, write to the
	Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
	Boston, MA  02110-1301, USA.

		
	NOTE for ignorant distributors:
	It's not allowed to distribute any compiled parts of this code, if you don't accept the terms of GPL.
	Please read it and understand it right!
	This means for you: Hold it, if not, leave it! You could face legal action! 
	Otherwise ask the copyright owners, anything else would be theft!
*/

#ifndef __OPKG_MANAGER__
#define __OPKG_MANAGER__

#include <gui/widget/menue.h>
#include <driver/framebuffer.h>

#include <string>

class COPKGManager : public CMenuTarget
{
	private:
		int width;
		
		CFrameBuffer *frameBuffer;

		struct pkg {
			std::string name;
			std::string description;
			bool installed;
			bool upgradable;
			int index;
			CMenuForwarder *forwarder;
		};
		std::map<std::string,struct pkg> pkg_map;
		struct pkg **pkg_arr;

		CMenuWidget *menu;
		CMenuForwarder *upgrade_forwarder;
		bool list_installed_done;
		bool list_upgradeable_done;
		bool installed;
		bool expert_mode;
		int menu_offset;

		int execCmd(const char* cmdstr, bool verbose = false, bool acknowledge = false);
		int execCmd(std::string cmdstr, bool verbose = false, bool acknowledge = false) {
			return execCmd(cmdstr.c_str(), verbose, acknowledge);
		};
		void getPkgData(const int pkg_content_id);
		std::string getBlankPkgName(const std::string& line);
		int showMenu();
		void updateMenu();
		void refreshMenu();

	public:	
		COPKGManager();
		~COPKGManager();
		
		int exec(CMenuTarget* parent, const std::string & actionKey);
		static bool hasOpkgSupport();
};
#endif
