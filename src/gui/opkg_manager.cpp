/*
	Experimental OPKG-Manager - Neutrino-GUI

	Based upon Neutrino-GUI 
	Copyright (C) 2001 Steffen Hehn 'McClean'
	and some other guys
	Homepage: http://dbox.cyberphoria.org/

	Implementation: 
	Copyright (C) 2012 T. Graf 'dbt'
	Homepage: http://www.dbox2-tuning.net/

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include "gui/opkg_manager.h"

#include <global.h>
#include <neutrino.h>
#include <neutrino_menue.h>

#include <gui/widget/icons.h>
#include <gui/widget/messagebox.h>
#include <gui/widget/shellwindow.h>
#include <driver/screen_max.h>

#include <system/debug.h>

#include <stdio.h>
#include <poll.h>
#include <fcntl.h>

COPKGManager::COPKGManager()
{
	width = w_max (40, 10); //%
	v_pkg_installed.clear();
	frameBuffer = CFrameBuffer::getInstance();
}


COPKGManager::~COPKGManager()
{
	
}

const opkg_cmd_struct_t pkg_types[OM_MAX] =
{
	{OM_LIST, 		"opkg-cl list"},
	{OM_LIST_INSTALLED, 	"opkg-cl list-installed"},
	{OM_LIST_UPGRADEABLE,	"opkg-cl list-upgradable"},
	{OM_UPDATE,		"opkg-cl update"},
	{OM_UPGRADE,		"opkg-cl upgrade"},
};

int COPKGManager::exec(CMenuTarget* parent, const std::string &actionKey)
{
	int   res = menu_return::RETURN_REPAINT;

	if (parent)
		parent->hide();

	if(actionKey == pkg_types[OM_UPGRADE].cmdstr) {
		int r = execCmd(actionKey.c_str(), true);
		if (r) {
			char rs[80];
			snprintf(rs, sizeof(rs), "Upgrade failed (%d)", r);
			DisplayInfoMessage(rs);
		} else {
			DisplayInfoMessage("Upgrade successful");
		}
		return res;
	}
	
	for(uint i = 0; i < v_pkg_list.size(); i++)
	{
		if(actionKey == v_pkg_list[i]) 
		{
			int r = execCmd(pkg_types[OM_UPDATE].cmdstr);
			if(r)
			{
				std::string action_name = "opkg-cl install " + getBlankPkgName(v_pkg_list[i]);
				r = execCmd(action_name.c_str(), true);
				if(!r)
				{
					DisplayInfoMessage("Install successful, restart of Neutrino required...");
					//CNeutrinoApp::getInstance()->exec(NULL, "restart");
					return res;
				}
				else {
					char rs[80];
					snprintf(rs, sizeof(rs), "Install failed (%d)", r);
					DisplayInfoMessage(rs);
				}
			}
			else {
				char rs[80];
				snprintf(rs, sizeof(rs), "Update failed (%d)", r);
				DisplayInfoMessage(rs);
			}
			
			return res;
		}
	}
	
	res = showMenu(); 
	
	return res;
}



//show items
int COPKGManager::showMenu()
{
	int r = execCmd(pkg_types[OM_UPDATE].cmdstr);
	if (r) {
		char rs[80];
		snprintf(rs, sizeof(rs), "Update failed (%d)", r);
		DisplayInfoMessage(rs);
	}

	CMenuWidget *menu = new CMenuWidget("OPKG-Manager", NEUTRINO_ICON_UPDATE, width, MN_WIDGET_ID_SOFTWAREUPDATE);
	
	menu->addIntroItems();
	menu->addItem(new CMenuForwarderNonLocalized("Upgrade", true, NULL , this, pkg_types[OM_UPGRADE].cmdstr, CRCInput::RC_red, NEUTRINO_ICON_BUTTON_RED));
	menu->addItem(GenericMenuSeparatorLine);
	getPkgData(OM_LIST);
	for(uint i = 0; i < v_pkg_list.size(); i++)
	{
		printf("Update to %s\n", v_pkg_list[i].c_str());
		//std::string action_name = getBlankPkgName(v_pkg_list[i]);
		menu->addItem( new CMenuForwarderNonLocalized(v_pkg_list[i].c_str(), true, NULL , this, v_pkg_list[i].c_str()));
	}


	int res = menu->exec (NULL, "");
	menu->hide ();
	delete menu;
	return res;
}

//returns true if opkg support is available
bool COPKGManager::hasOpkgSupport()
{
	std::string deps[] = {"/bin/opkg-cl","/bin/opkg-key", "/etc/opkg/opkg.conf", "/var/lib/opkg"};
	bool ret = true;
	
	for (uint i = 0; i < (sizeof(deps) / sizeof(deps[0])); i++)
	{
		if(access(deps[i].c_str(), R_OK) !=0)
		{
			printf("[neutrino opkg] %s not found\n", deps[i].c_str());
			ret = false;
		}
	}
	
	return ret;
}


void COPKGManager::getPkgData(const int pkg_content_id)
{
	char cmd[100];
	FILE * f;
	snprintf(cmd, sizeof(cmd), pkg_types[pkg_content_id].cmdstr);
	
	printf("COPKGManager: executing %s\n", cmd);
	
	f = popen(cmd, "r");
	
	if (!f) //failed
	{
		DisplayInfoMessage("Command failed");
		return;
	}
	
	char buf[256];
	setbuf(f, NULL);
	int in, pos;
	pos = 0;
	v_pkg_installed.clear();

	while (true)
	{
		in = fgetc(f);
		if (in == EOF)
			break;

		buf[pos] = (char)in;
		pos++;
		buf[pos] = 0;
		
		if (in == '\b' || in == '\n')
		{
			pos = 0; /* start a new line */
			if (in == '\n')
			{
				//clean up string
				int ipos = -1;
				std::string line = buf;
				while( (ipos = line.find('\n')) != -1 )
					line = line.erase(ipos,1);
								
				//add to lists
				switch (pkg_content_id) 
				{
					case OM_LIST: //list of pkgs
					{
						v_pkg_list.push_back(line);
						//printf("%s\n", buf);
						break;
					}
					case OM_LIST_INSTALLED: //installed pkgs
					{
						v_pkg_installed.push_back(line);
						//printf("%s\n", buf);
						break;
					}
					default:
						printf("unknown output! \n\t");
						printf("%s\n", buf);
						break;
				}
			}
		}
	}

 	pclose(f);
}

std::string COPKGManager::getBlankPkgName(const std::string& line)
{
	int l_pos = line.find(" ");
	std::string name = line.substr(0, l_pos);
	return name;
}

int COPKGManager::execCmd(const char *cmdstr, bool verbose)
{
	std::string cmd = std::string(cmdstr);
	if (!verbose) {
		cmd += " 2>/dev/null >&2";
		int r = system(cmd.c_str());
		if (r == -1)
			return r;
		return WEXITSTATUS(r);
	}

	cmd += " 2>&1";
	int res;
	CShellWindow(cmd, true, &res);
	return res;
}
