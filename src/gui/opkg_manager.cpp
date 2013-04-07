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
	frameBuffer = CFrameBuffer::getInstance();
	pkg_map.clear();
	list_installed_done = false;
	list_upgradeable_done = false;
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
	{OM_REMOVE,		"opkg-cl remove"}
};

int COPKGManager::exec(CMenuTarget* parent, const std::string &actionKey)
{
	int res = menu_return::RETURN_REPAINT;

	if (actionKey == "") {
		if (parent)
			parent->hide();
		return showMenu(); 
	}
	if (actionKey == "rc_spkr") {
		int selected = menu->getSelected() - 5;
		if (selected < 0)
			return menu_return::RETURN_NONE;
		std::map<string, struct pkg>::iterator it;
		for (it = pkg_map.begin(); it != pkg_map.end(); it++) {
			if (it->second.index == selected) {
				char loc[200];
				snprintf(loc, sizeof(loc), g_Locale->getText(LOCALE_OPKG_MESSAGEBOX_REMOVE), it->second.name.c_str());
				if (ShowMsgUTF (LOCALE_OPKG_TITLE, loc, CMessageBox::mbrYes, CMessageBox::mbYes | CMessageBox::mbCancel) != CMessageBox::mbrCancel) {
					if (parent)
						parent->hide();
					std::string action_name = "opkg-cl remove " + it->second.name;
					execCmd(action_name.c_str(), true, true);
					refreshMenu();
					return res;
				}
				return res;
			}
		}
		return menu_return::RETURN_NONE;
	}

	if(actionKey == pkg_types[OM_UPGRADE].cmdstr) {
		if (parent)
			parent->hide();
		int r = execCmd(actionKey.c_str(), true, true);
		if (r) {
			std::string loc = g_Locale->getText(LOCALE_OPKG_FAILURE_UPGRADE);
			char rs[strlen(loc.c_str()) + 20];
			snprintf(rs, sizeof(rs), loc.c_str(), r);
			DisplayInfoMessage(rs);
		} else
			installed = true;
		refreshMenu();
		g_RCInput->postMsg((neutrino_msg_t) CRCInput::RC_up, 0);
		return res;
	}

	std::map<string, struct pkg>::iterator it = pkg_map.find(actionKey);
	if (it != pkg_map.end()) {
		if (it->second.installed)
			return menu_return::RETURN_NONE;
		if (parent)
			parent->hide();
		int r = execCmd(pkg_types[OM_UPDATE].cmdstr);
		if(r) {
			std::string loc = g_Locale->getText(LOCALE_OPKG_FAILURE_UPDATE);
			char rs[strlen(loc.c_str()) + 20];
			snprintf(rs, sizeof(rs), loc.c_str(), r);
			DisplayInfoMessage(rs);
		} else {
			std::string action_name = "opkg-cl install " + it->second.name;
			r = execCmd(action_name.c_str(), true, true);
			if(r) {
				std::string loc = g_Locale->getText(LOCALE_OPKG_FAILURE_INSTALL);
				char rs[strlen(loc.c_str()) + 20];
				snprintf(rs, sizeof(rs), loc.c_str(), r);
				DisplayInfoMessage(rs);
			} else
				installed = true;
		}
		refreshMenu();
	}
	return res;
}

void COPKGManager::updateMenu()
{
	bool upgradesAvailable = false;
	getPkgData(OM_LIST_INSTALLED);
	getPkgData(OM_LIST_UPGRADEABLE);
	for (std::map<string, struct pkg>::iterator it = pkg_map.begin(); it != pkg_map.end(); it++) {
		it->second.forwarder->iconName_Info_right = "";
		it->second.forwarder->setActive(true);
		if (it->second.upgradable) {
			it->second.forwarder->iconName_Info_right = NEUTRINO_ICON_WARNING;
			upgradesAvailable = true;
		} else if (it->second.installed) {
			it->second.forwarder->iconName_Info_right = NEUTRINO_ICON_CHECKMARK;
		}
	}

	upgrade_forwarder->setActive(upgradesAvailable);
}

void COPKGManager::refreshMenu() {
	list_installed_done = false,
	list_upgradeable_done = false;
	updateMenu();
}

//show items
int COPKGManager::showMenu()
{
	installed = false;

	int r = execCmd(pkg_types[OM_UPDATE].cmdstr);
	if (r) {
		std::string loc = g_Locale->getText(LOCALE_OPKG_FAILURE_UPDATE);
		char rs[strlen(loc.c_str()) + 20];
		snprintf(rs, sizeof(rs), loc.c_str(), r);
		DisplayInfoMessage(rs);
	}

	getPkgData(OM_LIST);
	getPkgData(OM_LIST_UPGRADEABLE);

	menu = new CMenuWidget(g_Locale->getText(LOCALE_OPKG_TITLE), NEUTRINO_ICON_UPDATE, width, MN_WIDGET_ID_SOFTWAREUPDATE);
	menu->addIntroItems();
	upgrade_forwarder = new CMenuForwarder(LOCALE_OPKG_UPGRADE, true, NULL , this, pkg_types[OM_UPGRADE].cmdstr, CRCInput::RC_red, NEUTRINO_ICON_BUTTON_RED);
	menu->addItem(upgrade_forwarder);
	menu->addItem(GenericMenuSeparatorLine);
	menu->addKey(CRCInput::RC_spkr, this, "rc_spkr");
	int i = 0;
	for (std::map<string, struct pkg>::iterator it = pkg_map.begin(); it != pkg_map.end(); it++, i++) {
		it->second.index = i;
		menu->addItem(it->second.forwarder);
	}

	updateMenu();

	int res = menu->exec (NULL, "");
	if (installed)
		DisplayInfoMessage(g_Locale->getText(LOCALE_OPKG_SUCCESS_INSTALL));
	menu->hide ();
	delete menu;
	return res;
}

//returns true if opkg support is available
bool COPKGManager::hasOpkgSupport()
{
	const char *deps[] = {"/bin/opkg-cl","/bin/opkg-key", "/etc/opkg/opkg.conf", "/var/lib/opkg", NULL};
	for (const char **d = deps; *d; d++)
		if(access(*d, R_OK) !=0) {
			printf("[neutrino opkg] %s not found\n", *d);
			return false;
		}
	
	return true;
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

	switch (pkg_content_id) {
		case OM_LIST:
			pkg_map.clear();
			list_installed_done = false;
			list_upgradeable_done = false;
			break;
		case OM_LIST_INSTALLED:
			if (list_installed_done)
				return;
			list_installed_done = true;
			for (std::map<string, struct pkg>::iterator it = pkg_map.begin(); it != pkg_map.end(); it++)
				it->second.installed = false;
			break;
		case OM_LIST_UPGRADEABLE:
			if (list_upgradeable_done)
				return;
			list_upgradeable_done = true;
			for (std::map<string, struct pkg>::iterator it = pkg_map.begin(); it != pkg_map.end(); it++)
				it->second.upgradable = false;
			break;
	}

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
						struct pkg p;
						p.description = line;
						p.name = getBlankPkgName(line);
						p.installed = false;
						p.upgradable = false;
						pkg_map[p.name] = p;
						std::map<string, struct pkg>::iterator it = pkg_map.find(p.name); // don't use variables defined in local scope only
						it->second.forwarder = new CMenuForwarderNonLocalized(it->second.description.c_str(), true, NULL , this, it->second.name.c_str());
						break;
					}
					case OM_LIST_INSTALLED: //installed pkgs
					{
						std::string name = getBlankPkgName(line);
						std::map<string, struct pkg>::iterator it = pkg_map.find(name);
						if (it != pkg_map.end())
							it->second.installed = true;
						break;
					}
					case OM_LIST_UPGRADEABLE: //upgradable pkgs
					{
						std::string name = getBlankPkgName(line);
						std::map<string, struct pkg>::iterator it = pkg_map.find(name);
						if (it != pkg_map.end())
							it->second.upgradable = true;
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

int COPKGManager::execCmd(const char *cmdstr, bool verbose, bool acknowledge)
{
	std::string cmd = std::string(cmdstr);
	if (verbose) {
		cmd += " 2>&1";
		int res;
		CShellWindow(cmd, (verbose ? CShellWindow::VERBOSE : 0) | (acknowledge ? CShellWindow::ACKNOWLEDGE : 0), &res);
		return res;
	} else {
		cmd += " 2>/dev/null >&2";
		int r = system(cmd.c_str());
		if (r == -1)
			return r;
		return WEXITSTATUS(r);
	}
}
