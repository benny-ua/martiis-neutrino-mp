/*
	Mediaplayer selection menu - Neutrino-GUI

	Copyright (C) 2001 Steffen Hehn 'McClean'
	and some other guys
	Homepage: http://dbox.cyberphoria.org/

	Copyright (C) 2011 T. Graf 'dbt'
	Homepage: http://www.dbox2-tuning.net/

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include "mediaplayer.h"

#include <global.h>
#include <neutrino.h>
#include <neutrino_menue.h>
#include <neutrinoMessages.h>

#include <gui/audiomute.h>
#include <gui/infoclock.h>
#include <gui/movieplayer.h>
#include <gui/pictureviewer.h>
#if ENABLE_UPNP
#include <gui/upnpbrowser.h>
#endif

#include <gui/widget/icons.h>

#include <driver/screen_max.h>
#include <driver/shairplay.h>

#include <system/debug.h>
#include <mymenu.h>
#include <video.h>
extern cVideo * videoDecoder;
extern CInfoClock *InfoClock;

CMediaPlayerMenu::CMediaPlayerMenu()
{
	setMenuTitel();

	width = w_max (40, 10); //%
	
	audioPlayer 	= NULL;
	inetPlayer 	= NULL;
}

CMediaPlayerMenu* CMediaPlayerMenu::getInstance()
{
	static CMediaPlayerMenu* mpm = NULL;

	if(!mpm) {
		mpm = new CMediaPlayerMenu();
		printf("[neutrino] mediaplayer menu instance created\n");
	}
	return mpm;
}

CMediaPlayerMenu::~CMediaPlayerMenu()
{
	delete audioPlayer ;
	delete inetPlayer ;
}

int CMediaPlayerMenu::exec(CMenuTarget* parent, const std::string &actionKey)
{
	if (parent)
		parent->hide();
	
	CAudioMute *audiomute = CAudioMute::getInstance();
	if (actionKey == "audioplayer")
	{
		if (audioPlayer == NULL)
			audioPlayer = new CAudioPlayerGui();
		if (!g_settings.show_background_picture)
			videoDecoder->setBlank(true);
		int res = audioPlayer->exec(NULL, "init");
		return res /*menu_return::RETURN_REPAINT*/;
	}
#if ENABLE_SHAIRPLAY
	else if (actionKey == "shairplay")
	{
		CNeutrinoApp::getInstance()->shairplay_enabled_cur = true;
		CNeutrinoApp::getInstance()->shairPlay->restart();
		return menu_return::RETURN_EXIT_ALL;
	}
#endif
	else if	(actionKey == "inetplayer")
	{
		if (inetPlayer == NULL)
			inetPlayer = new CAudioPlayerGui(true);
		if (!g_settings.show_background_picture)
			videoDecoder->setBlank(true);
		int res = inetPlayer->exec(NULL, "init");
		return res; //menu_return::RETURN_REPAINT;
	}
	else if (actionKey == "movieplayer")
	{
		audiomute->enableMuteIcon(false);
		InfoClock->enableInfoClock(false);
		int mode = CNeutrinoApp::getInstance()->getMode();
		if( mode == NeutrinoMessages::mode_radio )
			videoDecoder->StopPicture();
		int res = CMoviePlayerGui::getInstance().exec(NULL, "tsmoviebrowser");
		if( mode == NeutrinoMessages::mode_radio )
			videoDecoder->ShowPicture(DATADIR "/neutrino/icons/radiomode.jpg");
		audiomute->enableMuteIcon(true);
		InfoClock->enableInfoClock(true);
		return res;
	}
	
	int res = initMenuMedia();
	
	return res;
}


//show selectable mediaplayer items
int CMediaPlayerMenu::initMenuMedia(CMenuWidget *m, CPersonalizeGui *p)
{	
	CPersonalizeGui *personalize = p;
	CMenuWidget 	*media = m;
	
	bool show = (personalize == NULL || media == NULL);

	if (personalize == NULL)
		 personalize = new CPersonalizeGui();
	
	if (media == NULL)
		 media = new CMenuWidget(menu_title, NEUTRINO_ICON_MULTIMEDIA, width, MN_WIDGET_ID_MEDIA);

	personalize->addWidget(media);
	personalize->addIntroItems(media);
	
	//audio player
	CMenuForwarder *fw_audio = new CMenuForwarder(LOCALE_MAINMENU_AUDIOPLAYER, true, NULL, this, "audioplayer", CRCInput::RC_red, NEUTRINO_ICON_BUTTON_RED);
	fw_audio->setHint(NEUTRINO_ICON_HINT_APLAY, LOCALE_MENU_HINT_APLAY);
	personalize->addItem(media, fw_audio, &g_settings.personalize[SNeutrinoSettings::P_MEDIA_AUDIO]);

	//internet player
	CMenuForwarder *fw_inet = new CMenuForwarder(LOCALE_INETRADIO_NAME, true, NULL, this, "inetplayer", CRCInput::RC_green, NEUTRINO_ICON_BUTTON_GREEN);
	fw_inet->setHint(NEUTRINO_ICON_HINT_INET_RADIO, LOCALE_MENU_HINT_INET_RADIO);
	personalize->addItem(media, fw_inet, &g_settings.personalize[SNeutrinoSettings::P_MEDIA_INETPLAY]);
		
#if ENABLE_UPNP
	//upnp browser
	CUpnpBrowserGui *upnpbrowsergui = new CUpnpBrowserGui();
	CMenuForwarder *fw_upnp = new CMenuForwarder(LOCALE_UPNPBROWSER_HEAD, true, NULL, upnpbrowsergui, NULL, CRCInput::RC_yellow, NEUTRINO_ICON_BUTTON_YELLOW);
	fw_upnp->setHint(NEUTRINO_ICON_HINT_MEDIA, LOCALE_MENU_HINT_UPNP);
	personalize->addItem(media, fw_upnp, &g_settings.personalize[SNeutrinoSettings::P_MEDIA_UPNP]);
#endif

	//picture viewer
	CPictureViewerGui *pictureviewergui = new CPictureViewerGui();
	CMenuForwarder *fw_pviewer = new CMenuForwarder(LOCALE_MAINMENU_PICTUREVIEWER, true, NULL, pictureviewergui, NULL, CRCInput::RC_blue, NEUTRINO_ICON_BUTTON_BLUE);
	fw_pviewer->setHint(NEUTRINO_ICON_HINT_PICVIEW, LOCALE_MENU_HINT_PICVIEW);

	personalize->addItem(media, fw_pviewer, &g_settings.personalize[SNeutrinoSettings::P_MEDIA_PVIEWER]);

	personalize->addSeparator(*media, LOCALE_MAINMENU_MOVIEPLAYER, true);

	//movie browser
	CMenuForwarder *fw_mbrowser = new CMenuForwarder(LOCALE_MOVIEBROWSER_HEAD, true, NULL, this, "movieplayer");
	fw_mbrowser->setHint(NEUTRINO_ICON_HINT_MB, LOCALE_MENU_HINT_MB);
	personalize->addItem(media, fw_mbrowser, &g_settings.personalize[SNeutrinoSettings::P_MPLAYER_MBROWSER]);
	
	//fileplayback
	CMenuForwarder *fw_file = new CMenuForwarder(LOCALE_MOVIEPLAYER_FILEPLAYBACK, true, NULL, &CMoviePlayerGui::getInstance(), "fileplayback");
	fw_file->setHint(NEUTRINO_ICON_HINT_FILEPLAY, LOCALE_MENU_HINT_FILEPLAY);
	personalize->addItem(media, fw_file, &g_settings.personalize[SNeutrinoSettings::P_MPLAYER_FILEPLAY]);

	//yt
	CMenuForwarder *fw_yt = new CMenuForwarder(LOCALE_MOVIEPLAYER_YTPLAYBACK, true, NULL, &CMoviePlayerGui::getInstance(), "ytplayback");
	fw_yt->setHint(NEUTRINO_ICON_HINT_YTPLAY, LOCALE_MENU_HINT_YTPLAY);
	personalize->addItem(media, fw_yt, &g_settings.personalize[SNeutrinoSettings::P_MPLAYER_YTPLAY]);

	//netzkino
	CMenuForwarder *fw_nk = new CMenuForwarder(LOCALE_MOVIEPLAYER_NKPLAYBACK, true, NULL, &CMoviePlayerGui::getInstance(), "nkplayback");
	fw_nk->setHint(NEUTRINO_ICON_HINT_NKPLAY, LOCALE_MENU_HINT_NKPLAY);
	personalize->addItem(media, fw_nk, &g_settings.personalize[SNeutrinoSettings::P_MPLAYER_NKPLAY]);

#if ENABLE_SHAIRPLAY
	//shairplay
	if (g_settings.shairplay_enabled && !CNeutrinoApp::getInstance()->shairplay_enabled_cur) {
		CMenuForwarder *fw_shairplay = new CMenuForwarder(LOCALE_SHAIRPLAY_REENABLE, true, NULL, this, "shairplay");
		fw_shairplay->setHint(NEUTRINO_ICON_HINT_INET_RADIO, LOCALE_MENU_HINT_SHAIRPLAY_REENABLE);
		personalize->addSeparator(0);
		personalize->addItem(media, fw_shairplay, &g_settings.personalize[SNeutrinoSettings::P_MEDIA_INETPLAY]);
	}
#endif
	
	int res = menu_return::RETURN_NONE;
	
	if (show)
	{
 		//adding personalized items
		personalize->addPersonalizedItems();
		
		res = media->exec(NULL, "");
		delete media;
		delete personalize;
		delete pictureviewergui;
#if ENABLE_UPNP
		delete upnpbrowsergui;
#endif
	}
	return res;
}
