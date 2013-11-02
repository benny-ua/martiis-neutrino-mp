/*
	WebTV Setup

	(c) 2012 by martii


	License: GPL

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#define __USE_FILE_OFFSET64 1
#include "filebrowser.h"
#include <stdio.h>
#include <global.h>
#include <libgen.h>
#include <neutrino.h>
#include <driver/screen_max.h>
#include "webtv_setup.h"

CWebTVSetup::CWebTVSetup()
{
	width = w_max (40, 10);
	selected = -1;
}


int CWebTVSetup::exec(CMenuTarget* parent, const std::string & actionKey)
{
	int res = menu_return::RETURN_REPAINT;

    if(parent)
		parent->hide();

	if(actionKey == "select_xml") {
		CFileBrowser fileBrowser;
		CFileFilter fileFilter;
		fileFilter.addFilter("xml");
		fileBrowser.Filter = &fileFilter;
		std::string dn = g_settings.webtv_xml;
		char *d = dirname((char *) dn.c_str());
		if (fileBrowser.exec(d) == true) {
			g_settings.webtv_xml = fileBrowser.getSelectedFile()->Name;
			g_Zapit->reinitChannels();
		}
		return res;
	}

	Show();

	return res;
}

void CWebTVSetup::Show()
{
	int shortcut = 1;

	CMenuWidget m(LOCALE_WEBTV_HEAD, NEUTRINO_ICON_MOVIEPLAYER, width);
	m.setSelected(selected);
	m.addIntroItems(LOCALE_EPGPLUS_OPTIONS);
	m.addItem(new CMenuForwarder(LOCALE_WEBTV_XML, true, g_settings.webtv_xml, this, "select_xml",
								 CRCInput::convertDigitToKey(shortcut++)));
	m.exec(NULL, "");
	m.hide();
}
// vim:ts=4
