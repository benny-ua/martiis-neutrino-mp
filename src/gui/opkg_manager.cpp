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

#include <gui/opkg_manager.h>

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
#include <alloca.h>

enum 
{
	OM_LIST,
	OM_LIST_INSTALLED,
	OM_LIST_UPGRADEABLE,
	OM_UPDATE,
	OM_UPGRADE,
	OM_REMOVE,
	OM_INFO,
	OM_INSTALL,
	OM_MAX
};

static const std::string pkg_types[OM_MAX] =
{
	"opkg-cl list",
	"opkg-cl list-installed",
	"opkg-cl list-upgradable",
	"opkg-cl update",
	"opkg-cl upgrade ",
	"opkg-cl remove ",
	"opkg-cl info ",
	"opkg-cl install "
};

COPKGManager::COPKGManager()
{
	width = w_max (40, 10); //%
	frameBuffer = CFrameBuffer::getInstance();
	pkg_map.clear();
	list_installed_done = false;
	list_upgradeable_done = false;
	expert_mode = false;
}

COPKGManager::~COPKGManager()
{
}

int COPKGManager::exec(CMenuTarget* parent, const std::string &actionKey)
{
	int res = menu_return::RETURN_REPAINT;

	if (actionKey == "") {
		if (parent)
			parent->hide();
		return showMenu(); 
	}
	if (actionKey == "rc_spkr") {
		int selected = menu->getSelected() - menu_offset;
		if (selected < 0 || !pkg_arr[selected]->installed)
			return menu_return::RETURN_NONE;
		char loc[200];
		snprintf(loc, sizeof(loc), g_Locale->getText(LOCALE_OPKG_MESSAGEBOX_REMOVE), pkg_arr[selected]->name.c_str());
		if (ShowMsg(LOCALE_OPKG_TITLE, loc, CMessageBox::mbrCancel, CMessageBox::mbYes | CMessageBox::mbCancel) != CMessageBox::mbrCancel) {
			if (parent)
				parent->hide();
			execCmd(pkg_types[OM_REMOVE] + pkg_arr[selected]->name, true, true);
			refreshMenu();
		}
		return res;
	}
	if (actionKey == "rc_info") {
		if (parent)
			parent->hide();
		int selected = menu->getSelected() - menu_offset;
		if (selected < 0)
			return menu_return::RETURN_NONE;
		execCmd(pkg_types[OM_INFO] + pkg_arr[selected]->name, true, true);
		return res;
	}
	if (actionKey == "rc_setup") {
		expert_mode = !expert_mode;
		updateMenu();
		return res;
	}
	if(actionKey == pkg_types[OM_UPGRADE]) {
		if (parent)
			parent->hide();
		int r = execCmd(actionKey, true, true);
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
		if (parent)
			parent->hide();
		std::string force = "";
		int mode = OM_INSTALL;
		neutrino_locale_t loc = LOCALE_OPKG_FAILURE_INSTALL;
		if (it->second.installed) {
			if (expert_mode)
				force = "--force-reinstall ";
			else
				loc = LOCALE_OPKG_FAILURE_UPGRADE, mode = OM_UPGRADE;
		}
		int r = execCmd(pkg_types[mode] + force + actionKey, true, true);
		if (r) {
			std::string err = g_Locale->getText(loc);
			char rs[strlen(err.c_str()) + 20];
			snprintf(rs, sizeof(rs), err.c_str(), r);
			DisplayInfoMessage(rs);
		} else
				installed = true;
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
			it->second.forwarder->setActive(expert_mode);
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

	int r = execCmd(pkg_types[OM_UPDATE]);
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
	upgrade_forwarder = new CMenuForwarder(LOCALE_OPKG_UPGRADE, true, NULL , this, pkg_types[OM_UPGRADE].c_str(), CRCInput::RC_red, NEUTRINO_ICON_BUTTON_RED);
	menu->addItem(upgrade_forwarder);
	menu->addItem(GenericMenuSeparatorLine);

	menu_offset = menu->getItemsCount();

	menu->addKey(CRCInput::RC_info, this, "rc_info");
	menu->addKey(CRCInput::RC_spkr, this, "rc_spkr");
	menu->addKey(CRCInput::RC_setup, this, "rc_setup");

	pkg_arr = (struct pkg **) alloca(pkg_map.size() * sizeof(struct pkg *));
	int i = 0;
	for (std::map<string, struct pkg>::iterator it = pkg_map.begin(); it != pkg_map.end(); it++, i++) {
		menu->addItem(it->second.forwarder);
		pkg_arr[i] = &it->second;
	}

	updateMenu();

	int res = menu->exec (NULL, "");

	menu->hide ();
	if (installed)
		DisplayInfoMessage(g_Locale->getText(LOCALE_OPKG_SUCCESS_INSTALL));
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
	
	printf("COPKGManager: executing %s\n", pkg_types[pkg_content_id].c_str());
	
	FILE *f = popen(pkg_types[pkg_content_id].c_str(), "r");
	if (!f) {
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
						it->second.forwarder = new CMenuForwarder(it->second.description.c_str(), true, NULL , this, it->second.name.c_str());
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
fprintf(stderr, "execCmd(%s)\n", cmdstr);
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
