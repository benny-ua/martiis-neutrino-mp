/*
 * adzap.cpp
 * 
 * (C)2012-2013 by martii
 * 
 * License: GPL
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * AS SOME FOLKS DON'T SEEM TO UNDERSTAND THE GPL: YOU ARE FREE TO USE
 * THIS CODE FOR YOUR OWN LITTLE STB IMAGE. BUT IF YOU ARE DISTRIBUTING
 * THAT IMAGE, YOU ARE BOUND TO THE GPL, AND YOU HAVE TO DISTRIBUTE THE
 * SOURCE CODE, TOO. IF YOU DON'T: A) YOU ACKNOWLEDGE THAT YOU'RE AN
 * ABSOLUTE JERK, AND B) YOU'RE NOT PERMITTED TO USE THIS CODE. AT ALL.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <global.h>
#include <neutrino.h>
#include <algorithm>
#include <gui/adzap.h>
#include <gui/widget/hintbox.h>
#include <eitd/sectionsd.h>
#include <driver/screen_max.h>
#include <system/set_threadname.h>

#define ZAPBACK_ALERT_PERIOD 15	// seconds. Keep this in sync with the locales.

static CAdZapMenu *azm = NULL;
static pthread_t thrAdZap;

CAdZapMenu::CAdZapMenu()
{
    if (azm)
	return;

    azm = this;
    frameBuffer = CFrameBuffer::getInstance();
    width = w_max(40, 10);
    hheight = g_Font[SNeutrinoSettings::FONT_TYPE_MENU_TITLE]->getHeight();
    mheight = g_Font[SNeutrinoSettings::FONT_TYPE_MENU]->getHeight();
    height = hheight + 13 * mheight + 10;

    x = (((g_settings.screen_EndX - g_settings.screen_StartX) -
	  width) / 2) + g_settings.screen_StartX;
    y = (((g_settings.screen_EndY - g_settings.screen_StartY) -
	  height) / 2) + g_settings.screen_StartY;

    sem_init(&sem, 0, 0);
    if (pthread_create(&thrAdZap, 0, CAdZapMenu::Run, NULL) != 0) {
	fprintf(stderr, "ERROR: pthread_create(CAdZapMenu::CAdZapMenu)\n");
	running = false;
    } else
	running = true;
    channelId = -1;
    armed = false;
    monitor = false;
    alerted = false;
}

static bool
sortByDateTime(const CChannelEvent & a, const CChannelEvent & b)
{
    return a.startTime < b.startTime;
}

void
 CAdZapMenu::Update()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    memcpy(&zapBackTime, &ts, sizeof(ts));
    zapBackTime.tv_sec +=
	g_settings.adzap_zapBackPeriod - ZAPBACK_ALERT_PERIOD;

    sem_post(&azm->sem);
}

void *CAdZapMenu::Run(void *)
{
    set_threadname("CAdZapMenu::Run");
    while (azm->running) {
	CChannelList *channelList = NULL;
	t_channel_id curChannelId = -1;

	if (azm->monitor) {
	    struct timespec ts;
	    clock_gettime(CLOCK_REALTIME, &ts);
	    ts.tv_sec += 1;

	    sem_timedwait(&azm->sem, &ts);

	    if (azm->monitor && (azm->monitorLifeTime.tv_sec > ts.tv_sec)) {
		channelList = CNeutrinoApp::getInstance()->channelList;
		curChannelId =
		    channelList ? channelList->getActiveChannel_ChannelID()
		    : -1;
		if (!azm->armed && (azm->channelId != curChannelId)) {
		    azm->armed = true;
		    clock_gettime(CLOCK_REALTIME, &azm->zapBackTime);
		    azm->zapBackTime.tv_sec +=
			g_settings.adzap_zapBackPeriod -
			ZAPBACK_ALERT_PERIOD;
		    azm->alerted = false;
		} else if (azm->channelId == curChannelId) {
		    azm->armed = false;
		    azm->alerted = false;
		}
	    } else {
		azm->monitor = false;
		azm->armed = false;
		azm->alerted = false;
	    }
	} else if (azm->armed)
	    sem_timedwait(&azm->sem, &azm->zapBackTime);
	else
	    sem_wait(&azm->sem);

	if (azm->armed) {
	    struct timespec ts;
	    clock_gettime(CLOCK_REALTIME, &ts);
	    if (ts.tv_sec >= azm->zapBackTime.tv_sec) {
		if (!channelList) {
		    channelList = CNeutrinoApp::getInstance()->channelList;
		    curChannelId =
			channelList ?
			channelList->getActiveChannel_ChannelID() : -1;
		}
		if (!azm->alerted) {
		    if (azm->channelId != curChannelId) {
			std::string name =
			    g_Locale->getText(LOCALE_ADZAP_ANNOUNCE);
			name += "\n" + azm->channelName;
			ShowHint(LOCALE_ADZAP, name.c_str());
		    }
		    azm->alerted = true;
		    azm->zapBackTime.tv_sec += ZAPBACK_ALERT_PERIOD;
		} else {
		    azm->alerted = false;
		    if (channelList)
			channelList->zapTo_ChannelID(azm->channelId);
		    azm->armed = false;
		}
	    }
	}
    }
    return NULL;
}

int CAdZapMenu::exec(CMenuTarget *, const std::string & actionKey)
{
    int res = menu_return::RETURN_EXIT_ALL;

    if (actionKey == "enable") {
	if (!monitor)
	    armed = true;
	alerted = false;
	Update();
	return res;
    }
    if (actionKey == "disable") {
	armed = false;
	monitor = false;
	alerted = false;
	Update();
	return res;
    }
    if (actionKey == "monitor") {
	azm->armed = false;
	azm->monitor = true;
	alerted = false;
	Update();
	return res;
    }

    if (actionKey == "adzap") {
	if (armed || monitor) {
	    armed = false;
	    monitor = false;
	    alerted = false;
	    Update();
	    ShowHint(LOCALE_ADZAP, LOCALE_ADZAP_CANCEL, 450, 1);
	    return res;
	}
    }
    if (actionKey.length() == 1) {
	g_settings.adzap_zapBackPeriod = actionKey[0] - '0';
	for (int shortcut = 1; shortcut < 10; shortcut++)
	    forwarders[shortcut - 1]->iconName_Info_right =
		(shortcut ==
		 g_settings.
		 adzap_zapBackPeriod) ? NEUTRINO_ICON_BUTTON_OKAY : "";
	g_settings.adzap_zapBackPeriod *= 60;
	return menu_return::RETURN_REPAINT;
    }

    Settings();

    return res;
}

void CAdZapMenu::Settings()
{
    CChannelList *channelList = CNeutrinoApp::getInstance()->channelList;
    channelId =
	channelList ? channelList->getActiveChannel_ChannelID() : -1;
    channelName = channelList->getActiveChannelName();

    CMenuWidget *menu = new CMenuWidget(LOCALE_ADZAP, "settings", width);
    menu->addItem(new
		  CMenuSeparator(CMenuSeparator::LINE |
				 CMenuSeparator::STRING,
				 LOCALE_ADZAP_SWITCHBACK));
    neutrino_locale_t minute = LOCALE_ADZAP_MINUTE;
    for (int shortcut = 1; shortcut < 10; shortcut++) {
	char actionKey[2];
	actionKey[0] = '0' + shortcut;
	actionKey[1] = 0;
	bool selected = g_settings.adzap_zapBackPeriod == 60 * shortcut;
	forwarders[shortcut - 1] =
	    new CMenuForwarder(minute, true, NULL, this, actionKey,
			       CRCInput::convertDigitToKey(shortcut));
	if (selected)
	    forwarders[shortcut - 1]->iconName_Info_right =
		NEUTRINO_ICON_BUTTON_OKAY;
	menu->addItem(forwarders[shortcut - 1], selected);
	minute = LOCALE_ADZAP_MINUTES;
    }

    menu->addItem(GenericMenuSeparatorLine);

    menu->addItem(new
		  CMenuForwarder(LOCALE_ADZAP_DISABLE, true, NULL, this,
				 "disable", CRCInput::RC_red,
				 NEUTRINO_ICON_BUTTON_RED));
    menu->addItem(new
		  CMenuForwarder(LOCALE_ADZAP_ENABLE, true, NULL, this,
				 "enable", CRCInput::RC_green,
				 NEUTRINO_ICON_BUTTON_GREEN));

    CChannelEventList evtlist;
    CEitManager::getInstance()->getEventsServiceKey(azm->
						    channelId &
						    0xFFFFFFFFFFFFULL,
						    evtlist);
    azm->monitorLifeTime.tv_sec = 0;
    if (!evtlist.empty()) {
	sort(evtlist.begin(), evtlist.end(), sortByDateTime);
	CChannelEventList::iterator eli;
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	for (eli = evtlist.begin(); eli != evtlist.end(); ++eli) {
	    if ((u_int) eli->startTime + (u_int) eli->duration >
		(u_int) ts.tv_sec) {
		azm->monitorLifeTime.tv_sec =
		    (uint) eli->startTime + eli->duration;
		azm->Update();
		break;
	    }
	}
    }

    menu->addItem(new
		  CMenuForwarder(LOCALE_ADZAP_MONITOR,
				 azm->monitorLifeTime.tv_sec, NULL, this,
				 "monitor", CRCInput::RC_blue,
				 NEUTRINO_ICON_BUTTON_BLUE));

    monitor = false;
    menu->exec(NULL, "");
    menu->hide();
    delete menu;
    Update();
}
