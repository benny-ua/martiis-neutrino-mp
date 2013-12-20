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
#include <driver/display.h>
#include <zapit/zapit.h>
#include <audio_td.h>

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
	AudioSelector = NULL;
	width = w_max (40, 10);
	mp = &CMoviePlayerGui::getInstance();
#if HAVE_SPARK_HARDWARE
	dvb_delay_offset = -1;
#endif
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
	int sel = -1;
	if (AudioSelector) {
		sel = AudioSelector->getSelected();
#if HAVE_SPARK_HARDWARE
		if (dvb_delay_offset > -1 && sel == dvb_delay_offset) {
			if (actionkey == "-") {
				if (g_settings.dvb_subtitle_delay > -99)
					g_settings.dvb_subtitle_delay -= 1;
				else
					return menu_return::RETURN_NONE;
			} else if (actionkey == "+") {
				if (g_settings.dvb_subtitle_delay < 99)
					g_settings.dvb_subtitle_delay += 1;
				else
					return menu_return::RETURN_NONE;
			}
			return menu_return::RETURN_REPAINT;
		}
#endif
		sel -= apid_offset;
		if (sel < 0 || sel >= p_count)
			return menu_return::RETURN_NONE;
	}

	if (actionkey == "-" || actionkey == "+") {
		if (actionkey == "-") {
			if (perc_val[sel] == 0)
				return menu_return::RETURN_NONE;
			perc_val[sel]--;
		} else {
			if (perc_val[sel] == 999)
				return menu_return::RETURN_NONE;
			perc_val[sel]++;
		}
		perc_str[sel] = to_string(perc_val[sel]) + "%";

#if !HAVE_SPARK_HARDWARE
		int vol =  CZapit::getInstance()->GetVolume();
		/* keep resulting volume = (vol * percent)/100 not more than 115 */
		if (vol * perc_val[sel] > 11500)
			perc_val[sel] = 11500 / vol;
#endif
                CZapit::getInstance()->SetPidVolume(chan, apid[sel], perc_val[sel]);
		if (sel == sel_apid)
			CZapit::getInstance()->SetVolumePercent(perc_val[sel]);

		return menu_return::RETURN_REPAINT;
	}

	if (actionkey == "s") {
		if (mp->Playing()) {
			mp->setAPID(sel);
			CVFD::getInstance()->setAudioMode(mp->GetStreamType());
		} else if (g_RemoteControl->current_PIDs.PIDs.selected_apid != (unsigned int) sel ) {
			g_RemoteControl->setAPID(sel);
			CVFD::getInstance()->setAudioMode();
		}
		return menu_return::RETURN_EXIT;
	}

	if (mp->Playing())
		playback = mp->getPlayback();
	if (parent)
		parent->hide();

	return doMenu ();
}

int CAudioSelectMenuHandler::doMenu ()
{
	AudioSelector = new CMenuWidget(LOCALE_AUDIOSELECTMENUE_HEAD, NEUTRINO_ICON_AUDIO, width);

	CSubtitleChangeExec SubtitleChanger(playback);

	//show cancel button if configured in usermenu settings
	if (g_settings.personalize[SNeutrinoSettings::P_UMENU_SHOW_CANCEL])
		AudioSelector->addIntroItems(NONEXISTANT_LOCALE, LOCALE_AUDIOSELECTMENUE_VOLUME, CMenuWidget::BTN_TYPE_CANCEL);
	else
		AudioSelector->addItem(new CMenuSeparator(CMenuSeparator::LINE | CMenuSeparator::STRING, LOCALE_AUDIOSELECTMENUE_VOLUME));
	apid_offset = AudioSelector->getItemsCount();
	AudioSelector->addKey(CRCInput::RC_right, this, "+");
	AudioSelector->addKey(CRCInput::RC_left, this, "-");

	bool is_mp = mp->Playing();

	p_count = is_mp ? mp->getAPIDCount() : g_RemoteControl->current_PIDs.APIDs.size();
	sel_apid = is_mp ? mp->getAPID() : g_RemoteControl->current_PIDs.PIDs.selected_apid;

	int _apid[p_count];
	int _perc_val[p_count];
	unsigned int _is_ac3[p_count];
	std::string _perc_str[p_count];
	perc_val = _perc_val;
	perc_str = _perc_str;
	is_ac3 = _is_ac3;
	apid = _apid;
	chan = is_mp ? mp->getChannelId() : 0;

	// -- setup menue due to Audio PIDs
	for(int i = 0; i < p_count; i++) {
		if (is_mp) {
			mp->getAPID(i, apid[i], is_ac3[i]);
		} else {
			apid[i] = g_RemoteControl->current_PIDs.APIDs[i].pid;
			is_ac3[i] = g_RemoteControl->current_PIDs.APIDs[i].is_ac3;
		}
		perc_val[i] = CZapit::getInstance()->GetPidVolume(chan, apid[i], is_ac3[i]);
		perc_str[i] = to_string(perc_val[i]) + "%";

		CMenuForwarder *fw = new CMenuForwarder(is_mp ? mp->getAPIDDesc(i).c_str() : g_RemoteControl->current_PIDs.APIDs[i].desc, 
				true, perc_str[i], this, "s", CRCInput::convertDigitToKey(i + 1));
		fw->setItemButton(NEUTRINO_ICON_BUTTON_OKAY, true);
		fw->setMarked(sel_apid == i);

		AudioSelector->addItem(fw, sel_apid == i);
	}
	unsigned int shortcut_num = p_count;
#if !HAVE_SPARK_HARDWARE
	if (p_count)
		AudioSelector->addItem(GenericMenuSeparatorLine);

	// -- setup menue for to Dual Channel Stereo
	CMenuOptionChooser* oj = new CMenuOptionChooser(LOCALE_AUDIOMENU_ANALOG_MODE,
			&g_settings.audio_AnalogMode,
			AUDIOMENU_ANALOGOUT_OPTIONS, AUDIOMENU_ANALOGOUT_OPTION_COUNT,
			true, audioSetupNotifier, CRCInput::RC_red, NEUTRINO_ICON_BUTTON_RED);

	AudioSelector->addItem( oj );

	oj = new CMenuOptionChooser(LOCALE_AUDIOMENU_ANALOG_OUT, &g_settings.analog_out,
			OPTIONS_OFF0_ON1_OPTIONS, OPTIONS_OFF0_ON1_OPTION_COUNT,
			true, audioSetupNotifier, CRCInput::RC_green, NEUTRINO_ICON_BUTTON_GREEN);

	AudioSelector->addItem( oj );
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
				AudioSelector->addItem(new CMenuSeparator(CMenuSeparator::LINE | CMenuSeparator::STRING, LOCALE_SUBTITLES_HEAD));
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
				AudioSelector->addItem(new CMenuForwarder(item,
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
				AudioSelector->addItem(new CMenuForwarder(item,
							!tuxtx_subtitle_running(&pid, &page, NULL),
							NULL, &SubtitleChanger, spid, CRCInput::convertDigitToKey(++shortcut_num)));
			} else if (is_mp && s->thisSubType == CZapitAbsSub::SUB) {
				printf("[neutrino] adding SUB subtitle %s pid %x\n", s->ISO639_language_code.c_str(), s->pId);
				char spid[10];
				snprintf(spid,sizeof(spid), "SUB:%d", s->pId);
				char item[64];
				snprintf(item,sizeof(item), "SUB: %s (pid %x)", s->ISO639_language_code.c_str(), s->pId);
				AudioSelector->addItem(new CMenuForwarder(item,
							s->pId != mp->getCurrentSubPid(CZapitAbsSub::SUB),
							NULL, &SubtitleChanger, spid, CRCInput::convertDigitToKey(++shortcut_num)));
			}
			if (is_mp)
				delete s;
		}
#if HAVE_SPARK_HARDWARE
		if (have_dvb_sub) {
			dvb_delay_offset = AudioSelector->getItemsCount();
			AudioSelector->addItem(new CMenuOptionNumberChooser(LOCALE_SUBTITLES_DELAY, (int *)&g_settings.dvb_subtitle_delay, true, -99, 99));
		}
#endif

		if(sep_added) {
			CMenuForwarder * item = new CMenuForwarder(LOCALE_SUBTITLES_STOP, true, NULL, &SubtitleChanger, "off", CRCInput::RC_stop);
			item->setItemButton(NEUTRINO_ICON_BUTTON_STOP, false);
			AudioSelector->addItem(item);
		}
	}

	int res = AudioSelector->exec(NULL, "");

	delete AudioSelector;
	AudioSelector = NULL;

#if HAVE_SPARK_HARDWARE
	dvbsub_set_stc_offset(g_settings.dvb_subtitle_delay * 90000);
#endif
	return res;
}
