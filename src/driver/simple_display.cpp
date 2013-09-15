/*
	LCD-Daemon  -   DBoxII-Project

	Copyright (C) 2001 Steffen Hehn 'McClean'
	Homepage: http://dbox.cyberphoria.org/


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

// FIXME -- there is too much unused code left, but we may need it in the future ...

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <driver/lcdd.h>
#include <global.h>
#include <neutrino.h>
#include <system/settings.h>

#include <fcntl.h>
#include <sys/timeb.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <math.h>

#include <daemonc/remotecontrol.h>
#include <system/set_threadname.h>

#if HAVE_SPARK_HARDWARE
#include <aotom_main.h>
#endif

extern CRemoteControl * g_RemoteControl; /* neutrino.cpp */

CLCD::CLCD()
{
	thrTime = 0;

	has_lcd = 1;

	fd = open("/dev/vfd", O_RDWR | O_CLOEXEC);

	if(fd < 0) {
		perror("/dev/vfd");
		has_lcd = 0;
	}

	mode = MODE_MENU_UTF8;
	servicename = "";

	sem_init(&sem, 0, 0);

	showclock = true;

	if (pthread_create (&thrTime, NULL, TimeThread, NULL))
		perror("[lcdd]: pthread_create(TimeThread)");

}

CLCD::~CLCD()
{
	timeThreadRunning = false;
	if (thrTime) {
		sem_post(&sem);
		pthread_join(thrTime, NULL);
	}
	
	if(fd > -1)
		close(fd);
}

CLCD* CLCD::getInstance()
{
	static CLCD* lcdd = NULL;
	if(lcdd == NULL) {
		lcdd = new CLCD();
	}
	return lcdd;
}

void CLCD::wake_up() {
}

void* CLCD::TimeThread(void *arg __attribute__((unused)))
{
        set_threadname("CLCD::TimeThread");
	CLCD *cvfd = CLCD::getInstance();
	cvfd->timeThreadRunning = true;
	cvfd->waitSec = 0;
	while (cvfd->timeThreadRunning) {
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += cvfd->waitSec;
		int res = sem_timedwait(&cvfd->sem, &ts);
		if (!cvfd->showclock) {
			cvfd->waitSec = -1; // forever
			continue;
		}
		switch (res) {
		case 0:
			// sem_post triggered re-scheduling of time display
			continue;
		case -1:
			if (errno == ETIMEDOUT) {
				 // timed out, so update displayed time or service name
				cvfd->showTime();
				continue;
			}
			// fallthrough, should not happen
		default:
			cvfd->timeThreadRunning = false;
		}
	}
	return NULL;
}

void CLCD::setlcdparameter(int dimm, const int power)
{
	if(!has_lcd) return;

	if(dimm < 0)
		dimm = 0;
	else if(dimm > 15)
		dimm = 15;

	if(!power)
		dimm = 0;

#if HAVE_SPARK_HARDWARE
	struct aotom_ioctl_data vData;
	vData.u.brightness.level = dimm;
	int ret = ioctl(fd, VFDBRIGHTNESS, &vData);
	if(ret < 0)
		perror("VFDBRIGHTNESS");
#endif
}

void CLCD::setlcdparameter(void)
{
	if(!has_lcd) return;
	int last_toggle_state_power = g_settings.lcd_setting[SNeutrinoSettings::LCD_POWER];
	setlcdparameter((mode == MODE_STANDBY)
		? g_settings.lcd_setting[SNeutrinoSettings::LCD_STANDBY_BRIGHTNESS]
		: g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS], last_toggle_state_power);
}

void CLCD::showServicename(const std::string name, bool)
{
	if(!has_lcd) return;

	servicename = name;
	if (mode != MODE_TVRADIO)
		return;

	ShowText((char *) name.c_str());
	wake_up();
}

void CLCD::showTime(bool)
{
	int m = g_settings.lcd_setting[ (mode == MODE_STANDBY)
			? SNeutrinoSettings::LCD_STANDBY_DISPLAYMODE
			: SNeutrinoSettings::LCD_DISPLAYMODE];
	switch (m) {
		case LCD_DISPLAYMODE_TIMEONLY:
			break;
		case LCD_DISPLAYMODE_ON:
			if (g_info.hw_caps->display_type == HW_DISPLAY_LED_NUM)
				break;
		case LCD_DISPLAYMODE_TIMEOFF:
			ShowText(NULL);
			waitSec = -1;
			if (g_info.hw_caps->display_type == HW_DISPLAY_LED_NUM)
				return;
			break;
		case LCD_DISPLAYMODE_OFF:
			Clear();
			if (g_info.hw_caps->display_type == HW_DISPLAY_LED_NUM)
				return;
			break;
	}

	struct tm tm;
	time_t now = time(NULL);
	localtime_r(&now, &tm);
	now += tm.tm_gmtoff;

#if HAVE_SPARK_HARDWARE
	if (ioctl(fd, VFDSETTIME2, &now) < 0 && g_info.hw_caps->display_type == HW_DISPLAY_LED_NUM) {
		char buf[10];
		strftime(buf, sizeof(buf), "%H%M", &tm);
		ShowText(buf, false);
	}
#endif
	waitSec = 60 - tm.tm_sec;
	if (waitSec <= 0)
		waitSec = 60;
}

void CLCD::showRCLock(int duration __attribute__((unused)))
{
}

void CLCD::showVolume(const char vol, const bool perform_update __attribute__((unused)))
{
	char buf[10];
	snprintf(buf, sizeof(buf), "%4d", vol);
	ShowText(buf);
}

void CLCD::showPercentOver(const unsigned char perc __attribute__((unused)), const bool /*perform_update*/, const MODES __attribute__((unused)))
{
}

void CLCD::showMenuText(const int position __attribute__((unused)), const char * txt, const int highlight __attribute__((unused)), const bool utf_encoded __attribute__((unused)))
{
	if(!has_lcd) return;
	if (mode != MODE_MENU_UTF8)
		return;

	ShowText((char *) txt);
	wake_up();
}

void CLCD::showAudioTrack(const std::string & artist __attribute__((unused)), const std::string & title, const std::string & album __attribute__((unused)))
{
	if(!has_lcd) return;
	if (mode != MODE_AUDIO) 
		return;
//printf("CLCD::showAudioTrack: %s\n", title.c_str());
	ShowText((char *) title.c_str());
	wake_up();
}

void CLCD::showAudioPlayMode(AUDIOMODES m __attribute__((unused)))
{
}

void CLCD::showAudioProgress(const char perc __attribute__((unused)), bool isMuted __attribute__((unused)))
{
}

void CLCD::setMode(const MODES m, const char * const title)
{
	if(!has_lcd) return;

	MODES lastmode = mode;

	if(mode == MODE_AUDIO)
		ShowIcon(FP_ICON_MP3, false);
	if(strlen(title))
		ShowText((char *) title);
	mode = m;
	setlcdparameter();

	switch (m) {
	case MODE_TVRADIO:
		resume(m != lastmode);
		break;
	case MODE_AUDIO:
		break;
	case MODE_SCART:
		resume();
		break;
	case MODE_MENU_UTF8:
		pause();
		break;
	case MODE_SHUTDOWN:
		pause();
		break;
	case MODE_STANDBY:
		resume();
		break;
	default:
		;
	}
	wake_up();
}


void CLCD::setBrightness(int bright __attribute__((unused)))
{
	if(!has_lcd) return;

	//g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS] = bright;
	setlcdparameter();
}

int CLCD::getBrightness()
{
	//FIXME for old neutrino.conf
	if(g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS] > 7)
		g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS] = 7;

	return g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS];
}

void CLCD::setBrightnessStandby(int bright __attribute__((unused)))
{
	if(!has_lcd) return;

//	g_settings.lcd_setting[SNeutrinoSettings::LCD_STANDBY_BRIGHTNESS] = bright;
	setlcdparameter();
}

int CLCD::getBrightnessStandby()
{
	//FIXME for old neutrino.conf
	if(g_settings.lcd_setting[SNeutrinoSettings::LCD_STANDBY_BRIGHTNESS] > 7)
		g_settings.lcd_setting[SNeutrinoSettings::LCD_STANDBY_BRIGHTNESS] = 7;
	return g_settings.lcd_setting[SNeutrinoSettings::LCD_STANDBY_BRIGHTNESS];
}

void CLCD::setPower(int power __attribute__((unused)))
{
}

int CLCD::getPower()
{
	return g_settings.lcd_setting[SNeutrinoSettings::LCD_POWER];
}

void CLCD::togglePower(void)
{
}

void CLCD::setMuted(bool mu __attribute__((unused)))
{
}

void CLCD::resume(bool showServiceName)
{
	if(!has_lcd) return;
	showclock = true;
	waitSec = 0;
	if (showServiceName)
		ShowText(NULL);
	sem_post(&sem);
}

void CLCD::pause()
{
	if(!has_lcd) return;
	showclock = false;
}

void CLCD::Lock()
{
}

void CLCD::Unlock()
{
}

void CLCD::Clear()
{
#if HAVE_SPARK_HARDWARE
	if(!has_lcd) return;
        struct vfd_ioctl_data data;
	data.start_address = 0x01;
	data.length = 0x0;
	
	int ret = ioctl(fd, VFDDISPLAYCLR, &data);
	if(ret < 0)
		perror("IOC_VFD_CLEAR_ALL");
#endif
}

void CLCD::ShowIcon(fp_icon icon, bool show)
{
#if HAVE_SPARK_HARDWARE
	int which;
	switch (icon) {
		case FP_ICON_PLAY:
		//case FP_ICON_TIMESHIFT:
			which = LED_GREEN;
			break;
		case FP_ICON_RECORD:
			which = LED_RED;
			break;
		default:
			return;
	}
	struct aotom_ioctl_data vData;
	vData.u.led.led_nr = which;
	vData.u.led.on = show ? LOG_ON : LOG_OFF;
	ioctl(fd, VFDSETLED, &vData);
#endif
}

void CLCD::ShowText(const char * str, bool rescheduleTime)
{
	if (!str && servicename.length() > 0)
		str = (char *) servicename.c_str();

	int m = g_settings.lcd_setting[ (mode == MODE_STANDBY)
			? SNeutrinoSettings::LCD_STANDBY_DISPLAYMODE
			: SNeutrinoSettings::LCD_DISPLAYMODE];
	switch (m) {
		case LCD_DISPLAYMODE_TIMEONLY:
			if (!rescheduleTime)
				break;
			return;
		case LCD_DISPLAYMODE_ON:
			if (g_info.hw_caps->display_type == HW_DISPLAY_LED_NUM)
				break;
		case LCD_DISPLAYMODE_TIMEOFF:
			if (rescheduleTime)
				break;
			return;
		case LCD_DISPLAYMODE_OFF:
			Clear();
			return;
	}

#if HAVE_GENERIC_HARDWARE
        printf("CLCD::ShowText: [%s]\n", str);
#endif

	waitSec = 0;
	if (str) {
		std::string s(str);

		size_t start = s.find_first_not_of (" \t\n");
		if (start != std::string::npos) {
			size_t end = s.find_last_not_of (" \t\n");
			s = s.substr(start, end - start + 1);
		}
		if (s.length() > 0 && (g_info.hw_caps->display_type == HW_DISPLAY_LED_NUM || lastOutput != s)) {
			lastOutput = s;
			if (write(fd , s.c_str(), s.length()) < 0)
				perror("write to vfd failed");
			waitSec = 8;
		}
	}

	sem_post(&sem);
}

void CLCD::init(const char *, const char *, const char *, const char *, const char *, const char *)
{
}

void CLCD::setEPGTitle(const std::string)
{
}
