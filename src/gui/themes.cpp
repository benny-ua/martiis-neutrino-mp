/*
	$port: themes.cpp,v 1.16 2010/09/05 21:27:44 tuxbox-cvs Exp $
	
	Neutrino-GUI  -   DBoxII-Project

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

	Copyright (C) 2007, 2008, 2009 (flasher) Frank Liebelt

*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/wait.h>
#include <global.h>
#include <neutrino.h>
#include "widget/menue.h"
#include <system/setting_helpers.h>
#include <gui/widget/stringinput.h>
#include <gui/widget/stringinput_ext.h>
#include <gui/widget/messagebox.h>
#include <driver/screen_max.h>

#include <sys/stat.h>
#include <sys/time.h>

#include "themes.h"

#define THEMEDIR DATADIR "/neutrino/themes/"
#define USERDIR "/var" THEMEDIR
#define FILE_PREFIX ".theme"

CThemes::CThemes()
: themefile('\t')
{
	width 	= w_max (40, 10);
	notifier = NULL;
	hasThemeChanged = false;
}

int CThemes::exec(CMenuTarget* parent, const std::string & actionKey)
{
	int res = menu_return::RETURN_REPAINT;

	if( !actionKey.empty() )
	{
		if (actionKey=="theme_neutrino")
		{
			setupDefaultColors();
			notifier = new CColorSetupNotifier();
			notifier->changeNotify(NONEXISTANT_LOCALE, NULL);
			delete notifier;
		}
		else
		{
			std::string themeFile = actionKey;
			if ( strstr(themeFile.c_str(), "{U}") != 0 ) 
			{
				themeFile.erase(0, 3);
				readFile((char*)((std::string)USERDIR + themeFile + FILE_PREFIX).c_str());
			} 
			else
				readFile((char*)((std::string)THEMEDIR + themeFile + FILE_PREFIX).c_str());
		}
		return res;
	}


	if (parent)
		parent->hide();

	if ( !hasThemeChanged )
		rememberOldTheme( true );

	return Show();
}

void CThemes::readThemes(CMenuWidget &themes)
{
	struct dirent **themelist;
	int n;
	const char *pfade[] = {THEMEDIR, USERDIR};
	bool hasCVSThemes, hasUserThemes;
	hasCVSThemes = hasUserThemes = false;
	std::string userThemeFile = "";
	CMenuForwarder* oj;

	for(int p = 0;p < 2;p++)
	{
		n = scandir(pfade[p], &themelist, 0, alphasort);
		if(n < 0)
			perror("loading themes: scandir");
		else
		{
			for(int count=0;count<n;count++)
			{
				char *file = themelist[count]->d_name;
				char *pos = strstr(file, ".theme");
				if(pos != NULL)
				{
					if ( p == 0 && hasCVSThemes == false ) {
						themes.addItem(new CMenuSeparator(CMenuSeparator::LINE | CMenuSeparator::STRING, LOCALE_COLORTHEMEMENU_SELECT2));
						hasCVSThemes = true;
					} else if ( p == 1 && hasUserThemes == false ) {
						themes.addItem(new CMenuSeparator(CMenuSeparator::LINE | CMenuSeparator::STRING, LOCALE_COLORTHEMEMENU_SELECT1));
						hasUserThemes = true;
					}
					*pos = '\0';
					if ( p == 1 ) {
						userThemeFile = "{U}" + (std::string)file;
						oj = new CMenuForwarder(file, true, NULL, this, userThemeFile.c_str());
					} else
						oj = new CMenuForwarder(file, true, NULL, this, file);
					themes.addItem( oj );
				}
				free(themelist[count]);
			}
			free(themelist);
		}
	}
}

int CThemes::Show()
{
	std::string file_name = "";

	CMenuWidget themes (LOCALE_COLORMENU_MENUCOLORS, NEUTRINO_ICON_SETTINGS, width);
	
	themes.addIntroItems(LOCALE_COLORTHEMEMENU_HEAD2);
	
	//set default theme
	themes.addItem(new CMenuForwarder(LOCALE_COLORTHEMEMENU_NEUTRINO_THEME, true, NULL, this, "theme_neutrino", CRCInput::RC_red, NEUTRINO_ICON_BUTTON_RED));
	
	readThemes(themes);

	CStringInputSMS nameInput(LOCALE_COLORTHEMEMENU_NAME, &file_name, 30, NONEXISTANT_LOCALE, NONEXISTANT_LOCALE, "abcdefghijklmnopqrstuvwxyz0123456789- ");
	CMenuForwarder *m1 = new CMenuForwarder(LOCALE_COLORTHEMEMENU_SAVE, true , NULL, &nameInput, NULL, CRCInput::RC_green, NEUTRINO_ICON_BUTTON_GREEN);

	// Don't show SAVE if UserDir does'nt exist
	if ( access(USERDIR, F_OK) != 0 ) { // check for existance
	// mkdir must be called for each subdir which does not exist 
	//	mkdir (USERDIR, S_IRUSR | S_IREAD | S_IWUSR | S_IWRITE | S_IXUSR | S_IEXEC) == 0) {
		if (system (((std::string)"mkdir -p " + USERDIR).c_str()) != 0) {
			printf("[neutrino theme] error creating %s\n", USERDIR);
		}
	}
	if (access(USERDIR, F_OK) == 0 ) {
		themes.addItem(GenericMenuSeparatorLine);
		themes.addItem(m1);
	} else {
		delete m1;
		printf("[neutrino theme] error accessing %s\n", USERDIR);
	}

	int res = themes.exec(NULL, "");

	if (file_name.length() > 1) {
		saveFile((char*)((std::string)USERDIR + file_name + FILE_PREFIX).c_str());
	}

	if (hasThemeChanged) {
		if (ShowMsg(LOCALE_MESSAGEBOX_INFO, LOCALE_COLORTHEMEMENU_QUESTION, CMessageBox::mbrYes, CMessageBox::mbYes | CMessageBox::mbNo, NEUTRINO_ICON_SETTINGS) != CMessageBox::mbrYes)
			rememberOldTheme( false );
		else
			hasThemeChanged = false;
	}
	return res;
}

void CThemes::rememberOldTheme(bool remember)
{
	if ( remember ) {
		memcpy(&oldTheme, &g_settings.theme, sizeof(SNeutrinoTheme));
	} else {
		memcpy(&g_settings.theme, &oldTheme, sizeof(SNeutrinoTheme));

		notifier = new CColorSetupNotifier;
		notifier->changeNotify(NONEXISTANT_LOCALE, NULL);
		hasThemeChanged = false;
		delete notifier;
	}
}

void CThemes::readFile(char* themename)
{
	if(themefile.loadConfig(themename))
	{
		CNeutrinoApp::getInstance()->getTheme(themefile);

		notifier = new CColorSetupNotifier;
		notifier->changeNotify(NONEXISTANT_LOCALE, NULL);
		hasThemeChanged = true;
		delete notifier;
	}
	else
		printf("[neutrino theme] %s not found\n", themename);
}

void CThemes::saveFile(char * themename)
{
	CNeutrinoApp::getInstance()->setTheme(themefile);

	if (!themefile.saveConfig(themename))
		printf("[neutrino theme] %s write error\n", themename);
}



// setup default Colors
void CThemes::setupDefaultColors()
{
	SNeutrinoTheme &t = g_settings.theme;

	t.menu_Head_alpha = 0x00;
	t.menu_Head_red   = 0x00;
	t.menu_Head_green = 0x0A;
	t.menu_Head_blue  = 0x19;

	t.menu_Head_Text_alpha = 0x00;
	t.menu_Head_Text_red   = 0x5f;
	t.menu_Head_Text_green = 0x46;
	t.menu_Head_Text_blue  = 0x00;

	t.menu_Content_alpha = 0x14;
	t.menu_Content_red   = 0x00;
	t.menu_Content_green = 0x0f;
	t.menu_Content_blue  = 0x23;

	t.menu_Content_Text_alpha = 0x00;
	t.menu_Content_Text_red   = 0x64;
	t.menu_Content_Text_green = 0x64;
	t.menu_Content_Text_blue  = 0x64;

	t.menu_Content_Selected_alpha = 0x14;
	t.menu_Content_Selected_red   = 0x19;
	t.menu_Content_Selected_green = 0x37;
	t.menu_Content_Selected_blue  = 0x64;

	t.menu_Content_Selected_Text_alpha  = 0x00;
	t.menu_Content_Selected_Text_red    = 0x00;
	t.menu_Content_Selected_Text_green  = 0x00;
	t.menu_Content_Selected_Text_blue   = 0x00;

	t.menu_Content_inactive_alpha = 0x14;
	t.menu_Content_inactive_red   = 0x00;
	t.menu_Content_inactive_green = 0x0f;
	t.menu_Content_inactive_blue  = 0x23;

	t.menu_Content_inactive_Text_alpha  = 0x00;
	t.menu_Content_inactive_Text_red    = 55;
	t.menu_Content_inactive_Text_green  = 70;
	t.menu_Content_inactive_Text_blue   = 85;

	t.infobar_alpha = 0x14;
	t.infobar_red   = 0x00;
	t.infobar_green = 0x0e;
	t.infobar_blue  = 0x23;

	t.infobar_Text_alpha = 0x00;
	t.infobar_Text_red   = 0x64;
	t.infobar_Text_green = 0x64;
	t.infobar_Text_blue  = 0x64;

	t.colored_events_alpha = 0x00;
	t.colored_events_red = 95;
	t.colored_events_green = 70;
	t.colored_events_blue = 0;

	t.clock_Digit_alpha = 0x00;
	t.clock_Digit_red   = 0x64;
	t.clock_Digit_green = 0x64;
	t.clock_Digit_blue  = 0x64;
}
