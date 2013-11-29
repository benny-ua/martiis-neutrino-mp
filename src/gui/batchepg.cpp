/*
   BatchEPG Menu
   (C)2012 by martii

   License: GPL

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gui/batchepg.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include <fstream>

#include <system/debug.h>
#include <system/safe_system.h>

#include <global.h>
#include <neutrino.h>

#include <sectionsdclient/sectionsdclient.h>
#include <zapit/zapit.h>
#include <zapit/channel.h>
#include <gui/psisetup.h>
#include <gui/widget/menue.h>
#include <gui/widget/messagebox.h>
#include <gui/widget/hintbox.h>

#include <driver/rcinput.h>
#include <gui/audiomute.h>

#if !HAVE_SPARK_HARDWARE
#include <video.h>
extern cVideo * videoDecoder;
#endif

CBatchEPG_Menu::CBatchEPG_Menu()
{
	//frameBuffer = CFrameBuffer::getInstance();
	width = 600;
	hheight = g_Font[SNeutrinoSettings::FONT_TYPE_MENU_TITLE]->getHeight();
	mheight = g_Font[SNeutrinoSettings::FONT_TYPE_MENU]->getHeight();
	height = hheight+13*mheight+ 10;

	x = ((g_settings.screen_EndX - g_settings.screen_StartX) - width)/2 + g_settings.screen_StartX;
	y = ((g_settings.screen_EndY - g_settings.screen_StartY) - height)/2 + g_settings.screen_StartY;
}

CBatchEPG_Menu::~CBatchEPG_Menu()
{
}

bool CBatchEPG_Menu::AbortableSystem(const char *command) {
		bool killed = false;
		for(int fd = 3; fd < 256 /* arbitrary, but high enough */; fd++)
			fcntl(fd, F_SETFD, FD_CLOEXEC);
		pid_t child = fork();
		switch(child){
			case -1:
				return -1;
			case 0:
				signal(SIGTERM, SIG_DFL);
				signal(SIGINT, SIG_DFL);
				signal(SIGHUP, SIG_DFL);
				execl("/bin/sh", "sh", "-c", command, (char *) NULL);
				exit(-1);
		}

		neutrino_msg_t msg;
		neutrino_msg_data_t data;
		do
			g_RCInput->getMsg_ms(&msg, &data, 100);
		while (msg != CRCInput::RC_timeout);
		int status;
		while(child != waitpid(child, &status, WNOHANG)) {
      		g_RCInput->getMsg_ms(&msg, &data, 200);
			if ( msg <= CRCInput::RC_MaxRC ) {
				kill(child, SIGKILL);
				killed = true;
			}
		}
	return killed || WIFEXITED(status);
}

void CBatchEPG_Menu::AbortableSleep(time_t seconds) {
	time_t sleep_until = time(NULL) + seconds;
	neutrino_msg_t msg;
	neutrino_msg_data_t data;
	do
		g_RCInput->getMsg_ms(&msg, &data, 100);
	while (msg != CRCInput::RC_timeout);
	while(sleep_until >= time(NULL)) {
		g_RCInput->getMsg_ms(&msg, &data, 200);
		if (msg <= CRCInput::RC_MaxRC)
			break;
	}
}

bool CBatchEPG_Menu::Run(int i)
{
	bool res = false;

	CHintBox * hintBox = new CHintBox(LOCALE_MESSAGEBOX_INFO,
		(string(g_Locale->getText(LOCALE_BATCHEPG_HINT)) + "\n" + epgChannels[i].name).c_str(), width);
	hintBox->paint();

	string mhwVersion = "1";

	switch(epgChannels[i].type) {
#if 0
		case BATCHEPG_MHW2:
			mhwVersion = "2";
		case BATCHEPG_MHW1:
		{
			const char *tmpdirTemplate = "/tmp/mhwepg.XXXXXX";
			size_t tmpdirLen = strlen(tmpdirTemplate) + 1;
			char tmpdir[tmpdirLen];
			strncpy(tmpdir, tmpdirTemplate, tmpdirLen);

			if (!mkdtemp(tmpdir)) {
				fprintf(stderr, "mkdtemp(%s) failed: %s\n", tmpdir, strerror(errno));
				break;
			}

			const char *tmpfileTemplate = "/tmp/mhwepg.XXXXXX";
			size_t tmpfileLen = strlen(tmpfileTemplate) + 1;
			char tmpfile[tmpfileLen];
			strncpy(tmpfile, tmpfileTemplate, tmpfileLen);

			if (!mktemp(tmpfile)) {
				fprintf(stderr, "mktemp(%s) failed: %s\n", tmpfile, strerror(errno));
				break;
			}

			std::string cmd = "exec /bin/mhwepg -" + mhwVersion
				+ " -n " + string(tmpdir) + " >" + string(tmpfile) + " 2>&1";
			fprintf(stderr, "executing %s\n", cmd.c_str());
			if (!AbortableSystem(cmd.c_str())){
				std::ifstream in(tmpfile);
				std::string buf((std::istreambuf_iterator<char>(in)),
					std::istreambuf_iterator<char>());
				res = false;
				hintBox->hide();
				delete hintBox;
				hintBox = new CHintBox(LOCALE_MESSAGEBOX_ERROR, buf.c_str(), width);
				hintBox->paint();
				AbortableSleep(10);
			} else
				res = true;
			g_Sectionsd->readSIfromXML(tmpdir);
			safe_system(string("(sleep 60 ; rm -rf " + string(tmpdir) + " >/dev/null 2>&1)&").c_str());
			unlink(tmpfile);
			break;
		}
#endif
		case BATCHEPG_STANDARD:
		{
			AbortableSleep(g_settings.batchepg_standard_waittime);
			res = true;
			break;
		}
		default:
			break;
	}

	hintBox->hide();
	delete hintBox;
	return res;
}

int CBatchEPG_Menu::exec(CMenuTarget* parent, const std::string & actionKey)
{
	if (actionKey == "save") {
		Save();
		CNeutrinoApp::getInstance()->exec(NULL, "savesettings");
		return menu_return::RETURN_REPAINT;
	}

	t_channel_id live_channel_id = CZapit::getInstance()->GetCurrentChannelID();
	t_channel_id channel_id;

	// read EPG from a single channel
	long long unsigned _channel_id;
	if (1 == sscanf(actionKey.c_str(), "%llx", &_channel_id)) {
		channel_id = _channel_id;
		if (CNeutrinoApp::getInstance()->recordingstatus)
			return menu_return::RETURN_REPAINT;
		
		for (unsigned int i = 0; i < epgChannels.size(); i++)
			if (epgChannels[i].channel_id == channel_id) {
				if (epgChannels[i].type != BATCHEPG_OFF) {
					if (channel_id != live_channel_id)
						g_Zapit->zapTo_serviceID(epgChannels[i].channel_id);
					Run(i);
					break;
				}
			}
		if (channel_id != live_channel_id)
				g_Zapit->zapTo_serviceID(channel_id);
		return menu_return::RETURN_REPAINT;
	}

	if (actionKey == "shutdown" && !g_settings.batchepg_run_at_shutdown)
	 	return menu_return::RETURN_REPAINT;

	if (actionKey == "run" || actionKey == "shutdown" || actionKey == "timer") {
		if (CNeutrinoApp::getInstance()->recordingstatus)
			return menu_return::RETURN_REPAINT;

		bool wakeup = true;
		if (actionKey == "timer")
			wakeup = CNeutrinoApp::getInstance()->timer_wakeup;

		bool muted = false;

		if (!wakeup)
			muted = CNeutrinoApp::getInstance()->isMuted();

		extern CAudioMute *g_audioMute;

		if (actionKey != "run") {
			Load();
#if HAVE_SPARK_HARDWARE
			CNeutrinoApp::getInstance()->chPSISetup->blankScreen();
#else
			videoDecoder->setBlank(true);
#endif
			g_audioMute->AudioMute(true, false);
		}

		channel_id = live_channel_id;
		// read EPG from all channels
		for (unsigned int i = 0; i < epgChannels.size(); i++) {
			channel_id = epgChannels[i].channel_id;
			if (epgChannels[i].type != BATCHEPG_OFF) {
				if (channel_id != live_channel_id)
					g_Zapit->zapTo_serviceID(channel_id);
				Run(i);
			}
		}
		if (channel_id != live_channel_id)
				g_Zapit->zapTo_serviceID(live_channel_id);

		if (!wakeup) {
#if HAVE_SPARK_HARDWARE
			// restore PSI settings
			CNeutrinoApp::getInstance()->chPSISetup->blankScreen(false);
#else
			videoDecoder->setBlank(false);
#endif
			// restore audio
			if (!muted)
					g_audioMute->AudioMute(false, false);
		}

		return menu_return::RETURN_REPAINT;
	}

	if (parent)
		parent->hide();

	Settings();

	return menu_return::RETURN_REPAINT;
}

#if 0
void CBatchEPG_Menu::hide()
{
	frameBuffer->paintBackgroundBoxRel(x,y, width,height);
}
#endif

void CBatchEPG_Menu::Load()
{
	epgChannels.clear();

       FILE *cfg = fopen(BATCHEPGCONFIG, "r");
        if (cfg) {
          char s[1000];
          while (fgets(s, 1000, cfg)) {
				long long unsigned chan;
                int type;
                if (2 == sscanf(s, "%llx %d", &chan, &type)) {
					epgChannel e;
					e.channel_id = chan;
					e.type = type;
					e.type_old = type;
					e.name = g_Zapit->getChannelName(e.channel_id);
					epgChannels.push_back(e);
                }
          }
          fclose(cfg);
	}
}

void CBatchEPG_Menu::AddCurrentChannel()
{
	t_channel_id live_channel_id = CZapit::getInstance()->GetCurrentChannelID();
	t_channel_id channel_id = live_channel_id;
	for (unsigned int i = 0; i < epgChannels.size(); i++)
		if (epgChannels[i].channel_id == channel_id)
			return;
	epgChannel e;
	e.channel_id = channel_id;
	e.type = BATCHEPG_OFF;
	e.type_old = BATCHEPG_OFF;
	e.name = g_Zapit->getChannelName(channel_id);
	epgChannels.push_back(e);
}

bool CBatchEPG_Menu::Changed() {
	for (unsigned int i = 0; i < epgChannels.size(); i++)
		if (epgChannels[i].type != epgChannels[i].type_old)
			return true;
	return false;
}

void CBatchEPG_Menu::Save()
{
	if (Changed()) {
		FILE *cfg = fopen(BATCHEPGCONFIG, "w");
		if (cfg) {
			for (unsigned int i = 0; i < epgChannels.size(); i++) {
				if (epgChannels[i].type != BATCHEPG_OFF)
					fprintf(cfg, "%llx %d\n", (long long unsigned) epgChannels[i].channel_id, epgChannels[i].type);
				epgChannels[i].type_old = epgChannels[i].type;
			}
			fclose(cfg);
		}
	}
}

#define EPG_BATCH_TYPES_COUNT 2
static const CMenuOptionChooser::keyval EPG_BATCH_TYPES[EPG_BATCH_TYPES_COUNT] = {
	{ CBatchEPG_Menu::BATCHEPG_OFF, LOCALE_BATCHEPG_EPG_OFF },
	{ CBatchEPG_Menu::BATCHEPG_STANDARD, LOCALE_BATCHEPG_EPG_STANDARD },
#if 0
	{ CBatchEPG_Menu::BATCHEPG_MHW1, LOCALE_BATCHEPG_EPG_MHW1 },
	{ CBatchEPG_Menu::BATCHEPG_MHW2, LOCALE_BATCHEPG_EPG_MHW2 }
#endif
};

#define ONOFF_OPTION_COUNT 2
const CMenuOptionChooser::keyval ONOFF_OPTIONS[ONOFF_OPTION_COUNT] = {
	{ 0, LOCALE_OPTIONS_OFF },
	{ 1, LOCALE_OPTIONS_ON },
};

void CBatchEPG_Menu::Settings()
{
	CMenuWidget* menu = new CMenuWidget(LOCALE_MISCSETTINGS_EPG_BATCH_SETTINGS, "settings");
	menu->addIntroItems(NONEXISTANT_LOCALE);
	menu->addItem(new CMenuForwarder(LOCALE_BATCHEPG_SAVE, true, NULL, this,
		"save", CRCInput::RC_red, NEUTRINO_ICON_BUTTON_RED));

	Load();
	AddCurrentChannel();

	menu->addItem(new CMenuSeparator(CMenuSeparator::LINE |
					 				 CMenuSeparator::STRING, LOCALE_BATCHEPG_SETTINGS));

	for (unsigned int i = 0; i < epgChannels.size(); i++) {
		menu->addItem(new CMenuOptionChooser(epgChannels[i].name.c_str(),
			     		(int*)&(epgChannels[i].type),
					EPG_BATCH_TYPES, EPG_BATCH_TYPES_COUNT, true));
	}

	menu->addItem(new CMenuSeparator(CMenuSeparator::LINE |
					 				 CMenuSeparator::STRING, LOCALE_BATCHEPG_REFRESH));
	t_channel_id live_channel_id = CZapit::getInstance()->GetCurrentChannelID();
	int shortcut = 0;
	for (unsigned int i = 0; i < epgChannels.size(); i++) {
		char actionKey[80];
		snprintf(actionKey, sizeof(actionKey), "%llx", (long long unsigned) epgChannels[i].channel_id);
		menu->addItem(new CMenuForwarder(epgChannels[i].name,
			true, NULL, this, actionKey, CRCInput::convertDigitToKey (shortcut++)),
			epgChannels[i].channel_id == live_channel_id);
	}

	menu->addItem(GenericMenuSeparatorLine);

	menu->addItem(new CMenuOptionNumberChooser(LOCALE_BATCHEPG_STANDARD_WAIT_TIME,
		&g_settings.batchepg_standard_waittime, true, 10, 1200));

	menu->addItem(new CMenuOptionChooser(LOCALE_BATCHEPG_RUN_AT_SHUTDOWN,
			&g_settings.batchepg_run_at_shutdown, ONOFF_OPTIONS, ONOFF_OPTION_COUNT,true));

	menu->addItem(GenericMenuSeparatorLine);

	menu->addItem(new CMenuForwarder(LOCALE_BATCHEPG_REFRESHALL, true, NULL, this,
		"run", CRCInput::RC_blue, NEUTRINO_ICON_BUTTON_BLUE));
	menu->exec (NULL, "");

	if (Changed() && (ShowMsg (LOCALE_MISCSETTINGS_EPG_BATCH_SETTINGS,
						LOCALE_MESSAGEBOX_ACCEPT, CMessageBox::mbrYes,
						CMessageBox::mbYes | CMessageBox::mbCancel)
					  == CMessageBox::mbrCancel))
		Save();

	menu->hide ();

	delete menu;
}
// vim:ts=4
