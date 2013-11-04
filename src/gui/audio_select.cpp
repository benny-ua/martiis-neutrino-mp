/*
	Neutrino-GUI  -   DBoxII-Project

	Copyright (C) 2001 Steffen Hehn 'McClean'
	Homepage: http://dbox.cyberphoria.org/

	Kommentar:

	Diese GUI wurde von Grund auf neu programmiert und sollte nun vom
	Aufbau und auch den Ausbaumoeglichkeiten gut aussehen. Neutrino basiert
	auf der Client-Server Idee, diese GUI ist also von der direkten DBox-
	Steuerung getrennt. Diese wird dann von Daemons uebernommen.


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


#include <global.h>
#include <neutrino.h>
#include <mymenu.h>
#include <gui/widget/icons.h>
#include <gui/widget/menue.h>
#include <driver/screen_max.h>
#include <driver/volume.h>
#include <zapit/zapit.h>

extern CRemoteControl		* g_RemoteControl; /* neutrino.cpp */
extern CAudioSetupNotifier	* audioSetupNotifier;

#include <gui/audio_select.h>
#include <gui/movieplayer.h>
#include <libdvbsub/dvbsub.h>
#include <libtuxtxt/teletext.h>

//
//  -- AUDIO Selector Menue Handler Class
//  -- to be used for calls from Menue
//  -- (2005-08-31 rasc)

CAudioSelectMenuHandler::CAudioSelectMenuHandler()
{
	width = w_max (40, 10);
}

CAudioSelectMenuHandler::~CAudioSelectMenuHandler()
{

}

// -- this is a copy from neutrino.cpp!!
#define AUDIOMENU_ANALOGOUT_OPTION_COUNT 3
const CMenuOptionChooser::keyval AUDIOMENU_ANALOGOUT_OPTIONS[AUDIOMENU_ANALOGOUT_OPTION_COUNT] =
{
	{ 0, LOCALE_AUDIOMENU_STEREO },
	{ 1, LOCALE_AUDIOMENU_MONOLEFT },
	{ 2, LOCALE_AUDIOMENU_MONORIGHT }
};

int CAudioSelectMenuHandler::exec(CMenuTarget* parent, const std::string &actionkey)
{
	mp = &CMoviePlayerGui::getInstance();

	if (mp->Playing())
		playback = mp->getPlayback();

	int sel= atoi(actionkey.c_str());
	if(sel >= 0) {
		if (mp->Playing())
			mp->setAPID(sel);
		else if (g_RemoteControl->current_PIDs.PIDs.selected_apid!= (unsigned int) sel )
			g_RemoteControl->setAPID(sel);
		return menu_return::RETURN_EXIT;
	}

	if (parent)
		parent->hide();

	return doMenu ();
}

int CAudioSelectMenuHandler::doMenu ()
{
	CMenuWidget AudioSelector(LOCALE_AUDIOSELECTMENUE_HEAD, NEUTRINO_ICON_AUDIO, width);

	CSubtitleChangeExec SubtitleChanger(playback);

	//show cancel button if configured in usermenu settings
	if (g_settings.personalize[SNeutrinoSettings::P_UMENU_SHOW_CANCEL])
		AudioSelector.addIntroItems(NONEXISTANT_LOCALE, NONEXISTANT_LOCALE, CMenuWidget::BTN_TYPE_CANCEL);
	else
		AudioSelector.addItem(GenericMenuSeparator);

	bool is_mp = mp->Playing();

	uint p_count = is_mp ? mp->getAPIDCount() : g_RemoteControl->current_PIDs.APIDs.size();
	uint sel_apid = is_mp ? mp->getAPID() : g_RemoteControl->current_PIDs.PIDs.selected_apid;

	// -- setup menue due to Audio PIDs
	for(uint i=0; i < p_count; i++ )
	{
		char apid[5];
		snprintf(apid, sizeof(apid), "%d", i);
		CMenuForwarder *fw = new CMenuForwarder(is_mp ? mp->getAPIDDesc(i).c_str() : g_RemoteControl->current_PIDs.APIDs[i].desc, 
				true, NULL, this, apid, CRCInput::convertDigitToKey(i + 1));
		fw->setItemButton(NEUTRINO_ICON_BUTTON_OKAY, true);
		AudioSelector.addItem(fw, sel_apid == i);
	}
	unsigned int shortcut_num = p_count;
#if !HAVE_SPARK_HARDWARE
	if (p_count)
		AudioSelector.addItem(GenericMenuSeparatorLine);

	// -- setup menue for to Dual Channel Stereo
	CMenuOptionChooser* oj = new CMenuOptionChooser(LOCALE_AUDIOMENU_ANALOG_MODE,
			&g_settings.audio_AnalogMode,
			AUDIOMENU_ANALOGOUT_OPTIONS, AUDIOMENU_ANALOGOUT_OPTION_COUNT,
			true, audioSetupNotifier, CRCInput::RC_red, NEUTRINO_ICON_BUTTON_RED);

	AudioSelector.addItem( oj );

	oj = new CMenuOptionChooser(LOCALE_AUDIOMENU_ANALOG_OUT, &g_settings.analog_out,
			OPTIONS_OFF0_ON1_OPTIONS, OPTIONS_OFF0_ON1_OPTION_COUNT,
			true, audioSetupNotifier, CRCInput::RC_green, NEUTRINO_ICON_BUTTON_GREEN);

	AudioSelector.addItem( oj );
#endif

	CZapitChannel * cc = NULL;
	int subtitleCount = 0;
	if (is_mp) {
		subtitleCount = mp->getSubtitleCount();
	} else {
		CChannelList *channelList = CNeutrinoApp::getInstance ()->channelList;
		int curnum = channelList->getActiveChannelNumber();
		cc = channelList->getChannel(curnum);
		subtitleCount = (int)cc->getSubtitleCount();
	}

	bool sep_added = false;
	if(subtitleCount > 0)
	{
#if HAVE_SPARK_HARDWARE
		bool have_dvb_sub = false;
#endif
		for (int i = 0 ; i < subtitleCount ; ++i)
		{
			CZapitAbsSub* s = is_mp ? mp->getChannelSub(i, &s) : cc->getChannelSub(i);
			if (!s)
				continue;

			if(!sep_added)
			{
				sep_added = true;
				AudioSelector.addItem(new CMenuSeparator(CMenuSeparator::LINE | CMenuSeparator::STRING, LOCALE_SUBTITLES_HEAD));
			}

			if (s->thisSubType == CZapitAbsSub::DVB) {
#if HAVE_SPARK_HARDWARE
				have_dvb_sub = true;
#endif
				CZapitDVBSub* sd = reinterpret_cast<CZapitDVBSub*>(s);
				printf("[neutrino] adding DVB subtitle %s pid %x\n", sd->ISO639_language_code.c_str(), sd->pId);
				char spid[10];
				snprintf(spid,sizeof(spid), "DVB:%d", sd->pId);
				char item[64];
				snprintf(item,sizeof(item), "DVB: %s (pid %x)", sd->ISO639_language_code.c_str(), sd->pId);
				AudioSelector.addItem(new CMenuForwarder(item,
							sd->pId != (is_mp ? mp->getCurrentSubPid(CZapitAbsSub::DVB) : dvbsub_getpid()),
							NULL, &SubtitleChanger, spid, CRCInput::convertDigitToKey(++shortcut_num)));
			} else if (s->thisSubType == CZapitAbsSub::TTX) {
				CZapitTTXSub* sd = reinterpret_cast<CZapitTTXSub*>(s);
				printf("[neutrino] adding TTX subtitle %s pid %x mag %X page %x\n", sd->ISO639_language_code.c_str(), sd->pId, sd->teletext_magazine_number, sd->teletext_page_number);
				char spid[64];
				int page = ((sd->teletext_magazine_number & 0xFF) << 8) | sd->teletext_page_number;
				int pid = sd->pId;
				snprintf(spid,sizeof(spid), "TTX:%d:%03X:%s", sd->pId, page, sd->ISO639_language_code.c_str());
				char item[64];
				snprintf(item,sizeof(item), "TTX: %s (pid %x page %03X)", sd->ISO639_language_code.c_str(), sd->pId, page);
				AudioSelector.addItem(new CMenuForwarder(item,
							!tuxtx_subtitle_running(&pid, &page, NULL),
							NULL, &SubtitleChanger, spid, CRCInput::convertDigitToKey(++shortcut_num)));
			} else if (is_mp && s->thisSubType == CZapitAbsSub::SUB) {
				printf("[neutrino] adding SUB subtitle %s pid %x\n", s->ISO639_language_code.c_str(), s->pId);
				char spid[10];
				snprintf(spid,sizeof(spid), "SUB:%d", s->pId);
				char item[64];
				snprintf(item,sizeof(item), "SUB: %s (pid %x)", s->ISO639_language_code.c_str(), s->pId);
				AudioSelector.addItem(new CMenuForwarder(item,
							s->pId != mp->getCurrentSubPid(CZapitAbsSub::SUB),
							NULL, &SubtitleChanger, spid, CRCInput::convertDigitToKey(++shortcut_num)));
			}
			if (is_mp)
				delete s;
		}
#if HAVE_SPARK_HARDWARE
		if (have_dvb_sub)
			AudioSelector.addItem(new CMenuOptionNumberChooser(LOCALE_SUBTITLES_DELAY, (int *)&g_settings.dvb_subtitle_delay, true, -99, 99));
#endif

		if(sep_added) {
			CMenuForwarder * item = new CMenuForwarder(LOCALE_SUBTITLES_STOP, true, NULL, &SubtitleChanger, "off", CRCInput::RC_stop);
			item->setItemButton(NEUTRINO_ICON_BUTTON_STOP, false);
			AudioSelector.addItem(item);
		}
	}

	AudioSelector.addItem(new CMenuSeparator(CMenuSeparator::LINE | CMenuSeparator::STRING, LOCALE_AUDIOMENU_VOLUME_ADJUST));
	/* setting volume percent to zapit with channel_id/apid = 0 means current channel and pid */
	t_channel_id chan = 0;
	int apid = -1;
	unsigned int is_ac3;
	if (is_mp) {
		chan = mp->getChannelId();
		mp->getAPID(apid, is_ac3);
	}
	CVolume::getInstance()->SetCurrentChannel(chan);
	CVolume::getInstance()->SetCurrentPid(apid);
	int percent[p_count];
	for (uint i=0; i < p_count; i++) {
		const char *desc;
		if (is_mp) {
			mp->getAPID(i, apid, is_ac3);
			desc = mp->getAPIDDesc(i).c_str();
		} else {
			apid = g_RemoteControl->current_PIDs.APIDs[i].pid;
			is_ac3 = g_RemoteControl->current_PIDs.APIDs[i].is_ac3;
			desc = g_RemoteControl->current_PIDs.APIDs[i].desc;
		}

		percent[i] = CZapit::getInstance()->GetPidVolume(chan, apid, is_ac3);
		AudioSelector.addItem(new CMenuOptionNumberChooser(desc, &percent[i], i == sel_apid, 0, 999, CVolume::getInstance()));
	}

	int res = AudioSelector.exec(NULL, "");
#if HAVE_SPARK_HARDWARE
	dvbsub_set_stc_offset(g_settings.dvb_subtitle_delay * 90000);
#endif
	return res;
}
