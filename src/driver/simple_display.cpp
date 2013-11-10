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

#include <fcntl.h>
#include <sys/timeb.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <driver/lcdd.h>
#include <global.h>
#include <neutrino.h>
#include <daemonc/remotecontrol.h>
#include <system/settings.h>
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
#if HAVE_SPARK_HARDWARE
	memset(led_mode, 0, sizeof(led_mode));
#endif

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
	if (lcdd == NULL)
		lcdd = new CLCD();
	return lcdd;
}

void CLCD::wake_up()
{
}

void* CLCD::TimeThread(void *)
{
        set_threadname("CLCD::TimeThread");
	CLCD *cvfd = CLCD::getInstance();
	cvfd->timeThreadRunning = true;
	cvfd->waitSec = 0;
	while (cvfd->timeThreadRunning) {
		int res;

		if (cvfd->waitSec == -1)
			res = sem_wait(&cvfd->sem);
		else {
			struct timespec ts;
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += cvfd->waitSec;
			res = sem_timedwait(&cvfd->sem, &ts);
		}
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

void CLCD::init(const char *, const char *, const char *, const char *, const char *, const char *)
{
}

void CLCD::setlcdparameter(int dimm, const int power)
{
	if(fd < 0)
		return;

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
}

void CLCD::showTime(bool)
{
#if HAVE_SPARK_HARDWARE
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
#endif

	struct tm tm;
	time_t now = time(NULL);
	localtime_r(&now, &tm);
	now += tm.tm_gmtoff;

#if HAVE_SPARK_HARDWARE
	if (fd < 0)
		return;

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
}

void CLCD::showAudioTrack(const std::string & artist __attribute__((unused)), const std::string & title, const std::string & album __attribute__((unused)))
{
	if(!has_lcd) return;
	if (mode != MODE_AUDIO) 
		return;
//printf("CLCD::showAudioTrack: %s\n", title.c_str());
	ShowText((char *) title.c_str());
}

void CLCD::showAudioPlayMode(AUDIOMODES m __attribute__((unused)))
{
}

void CLCD::showAudioProgress(const char perc __attribute__((unused)), bool isMuted __attribute__((unused)))
{
}

void CLCD::setMode(const MODES m, const char * const title)
{
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
}


void CLCD::setBrightness(int bright)
{
	g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS] = bright;

	if(g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS] > 7)
		g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS] = 7;
	else if(g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS] < 0)
		g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS] = 0;

	setlcdparameter();
}

int CLCD::getBrightness()
{
	if(g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS] > 7)
		g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS] = 7;
	else if(g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS] < 0)
		g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS] = 0;

	return g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS];
}

void CLCD::setBrightnessStandby(int bright)
{
	g_settings.lcd_setting[SNeutrinoSettings::LCD_STANDBY_BRIGHTNESS] = bright;

	if(g_settings.lcd_setting[SNeutrinoSettings::LCD_STANDBY_BRIGHTNESS] > 7)
		g_settings.lcd_setting[SNeutrinoSettings::LCD_STANDBY_BRIGHTNESS] = 7;
	else if(g_settings.lcd_setting[SNeutrinoSettings::LCD_STANDBY_BRIGHTNESS] < 0)
		g_settings.lcd_setting[SNeutrinoSettings::LCD_STANDBY_BRIGHTNESS] = 0;

	setlcdparameter();
}

int CLCD::getBrightnessStandby()
{
	if(g_settings.lcd_setting[SNeutrinoSettings::LCD_STANDBY_BRIGHTNESS] > 7)
		g_settings.lcd_setting[SNeutrinoSettings::LCD_STANDBY_BRIGHTNESS] = 7;
	else if(g_settings.lcd_setting[SNeutrinoSettings::LCD_STANDBY_BRIGHTNESS] < 0)
		g_settings.lcd_setting[SNeutrinoSettings::LCD_STANDBY_BRIGHTNESS] = 0;

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

void CLCD::setMuted(bool mu)
{
	ShowIcon(FP_ICON_MUTE, mu);
}

void CLCD::resume(bool showServiceName)
{
	showclock = true;
	waitSec = 0;
	if (showServiceName)
		ShowText(NULL);
	sem_post(&sem);
}

void CLCD::pause()
{
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
	if (fd < 0)
		return;

        struct vfd_ioctl_data data;
	data.start_address = 0x01;
	data.length = 0x0;
	
	int ret = ioctl(fd, VFDDISPLAYCLR, &data);
	if(ret < 0)
		perror("VFDDISPLAYCLR");
#endif
}

void CLCD::ShowIcon(fp_icon icon, bool show)
{
#if HAVE_SPARK_HARDWARE
	if (fd < 0)
		return;

	int leds = 0;

	switch (g_info.hw_caps->display_type) {
		case HW_DISPLAY_LINE_TEXT: {
			int aotom_icon = 0;
			switch (icon) {
				case FP_ICON_HDD:
					aotom_icon = AOTOM_HDD_FULL;
					break;
				case FP_ICON_MP3:
					aotom_icon = AOTOM_MP3;
					break;
				case FP_ICON_MUTE:
					aotom_icon = AOTOM_MUTE;
					break;
				case FP_ICON_PLAY:
					aotom_icon = AOTOM_PLAY_LOG;
					break;
				case FP_ICON_RECORD:
					aotom_icon = AOTOM_REC1;
					break;
				case FP_ICON_PAUSE:
					aotom_icon = AOTOM_PLAY_PAUSE;
					break;
				case FP_ICON_RADIO:
					aotom_icon = AOTOM_AUDIO;
					break;
				case FP_ICON_TIMESHIFT:
					aotom_icon = AOTOM_TIMESHIFT;
					break;
				case FP_ICON_TV:
					aotom_icon = AOTOM_TVMODE_LOG;
					break;
				case FP_ICON_USB:
					aotom_icon = AOTOM_USB;
					break;
				// incomplete.
				default:
					break;
			}
			if (aotom_icon) {
				struct aotom_ioctl_data vData;
				vData.u.icon.icon_nr = aotom_icon;
				vData.u.icon.on = show;
				ioctl(fd, VFDICONDISPLAYONOFF, &vData);
			}
			break;
		}
		default: {
			switch (icon) {
				case FP_ICON_PLAY:
					leds = SNeutrinoSettings::LED_MODE_PLAYBACK;
					break;
				case FP_ICON_RECORD:
					leds = SNeutrinoSettings::LED_MODE_RECORD;
					break;
				default:
					break;
			}
			break;
		}
	}

	if (leds)
		led_mode[leds] = show ? g_settings.led_mode[leds] : 0;
	setled();
#endif
}

void CLCD::ShowText(const char * str, bool rescheduleTime)
{
	if (!str && servicename.length() > 0)
		str = (char *) servicename.c_str();

#if HAVE_SPARK_HARDWARE
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
#endif

#if HAVE_GENERIC_HARDWARE
        printf("CLCD::ShowText: [%s]\n", str);
#endif

	if (fd < 0)
		return;

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

void CLCD::setEPGTitle(const std::string)
{
}

#if HAVE_SPARK_HARDWARE
void CVFD::setledmode(SNeutrinoSettings::LED_MODE m, bool onoff)
{
	if (m >= 0 && m < SNeutrinoSettings::LED_MODE_COUNT) {
		led_mode[m] = onoff ? g_settings.led_mode[m] : 0;
		setled();
	}
}
#endif

void CVFD::setled(void)
{
#if HAVE_SPARK_HARDWARE
	if(mode == MODE_MENU_UTF8 || mode == MODE_TVRADIO) {
		led_mode[SNeutrinoSettings::LED_MODE_STANDBY] = 0;
		led_mode[SNeutrinoSettings::LED_MODE_TV] = g_settings.led_mode[SNeutrinoSettings::LED_MODE_TV];
	} else if(mode == MODE_STANDBY) {
		led_mode[SNeutrinoSettings::LED_MODE_STANDBY] = g_settings.led_mode[SNeutrinoSettings::LED_MODE_STANDBY];
		led_mode[SNeutrinoSettings::LED_MODE_TV] = 0;
	}
	int on = 0;
	for (unsigned int i = 0; i < SNeutrinoSettings::LED_MODE_COUNT; i++)
		on |= led_mode[i];
	struct aotom_ioctl_data vData;
	vData.u.led.led_nr = 0;
	vData.u.led.on = (on & 1) ? LOG_ON : LOG_OFF;
	ioctl(fd, VFDSETLED, &vData);
	vData.u.led.led_nr = 1;
	vData.u.led.on = (on & 2) ? LOG_ON : LOG_OFF;
	ioctl(fd, VFDSETLED, &vData);
#endif
}
