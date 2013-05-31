#ifdef MARTII
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

	int fds[2];
	if (pipe2(fds, O_CLOEXEC | O_NONBLOCK))
		return;

	time_notify_reader = fds[0];
	time_notify_writer = fds[1];

#if HAVE_SPARK_HARDWARE
	if (0 < ioctl(fd, VFDGETVERSION, &vfd_version))
		vfd_version = 4; // fallback to 4-digit LED
#endif

	if (pthread_create (&thrTime, NULL, TimeThread, NULL))
		perror("[lcdd]: pthread_create(TimeThread)");

}

CLCD::~CLCD()
{
	timeThreadRunning = false;
	if (thrTime) {
		write(time_notify_writer, "", 1);
		pthread_join(thrTime, NULL);
	}
	
	if(fd > 0)
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
	char buf[10];
	cvfd->waitSec = 0;
	struct pollfd fds;
	memset(&fds, 0, sizeof(fds));
	fds.fd = cvfd->time_notify_reader;
	fds.events = POLLIN;
	while (cvfd->timeThreadRunning) {
		int res = poll(&fds, 1, cvfd->waitSec * 1000);
		if (res == 1)
			while (0 < read(fds.fd, buf, sizeof(buf)));
		if (!cvfd->showclock) {
			cvfd->waitSec = -1; // forever
			continue;
		}
		switch (res) {
		case 0: // timeout, update displayed time or service name
			cvfd->showTime();
			continue;
		case 1: // re-schedule time display
			continue;
		default:
			cvfd->timeThreadRunning = false;
		}
	}
	close (cvfd->time_notify_reader);
	cvfd->time_notify_reader = -1;
	close (cvfd->time_notify_writer);
	cvfd->time_notify_writer = -1;
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
			if (vfd_version == 4)
				break;
		case LCD_DISPLAYMODE_TIMEOFF:
			ShowText(NULL);
			waitSec = -1;
			if (vfd_version == 4) // return for simple 4-digit LED display
				return;
			break;
		case LCD_DISPLAYMODE_OFF:
			Clear();
			if (vfd_version == 4) // return for simple 4-digit LED display
				return;
			break;
	}

	struct tm tm;
	time_t now = time(NULL);
	localtime_r(&now, &tm);
	now += tm.tm_gmtoff;

#if HAVE_SPARK_HARDWARE
	if (ioctl(fd, VFDSETTIME2, &now) < 0 && vfd_version == 4) {
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
printf("CLCD::showAudioTrack: %s\n", title.c_str());
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
	write(time_notify_writer, "", 1);
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
			if (vfd_version == 4)
				break;
		case LCD_DISPLAYMODE_TIMEOFF:
			if (rescheduleTime)
				break;
			return;
		case LCD_DISPLAYMODE_OFF:
			Clear();
			return;
	}

        printf("CLCD::ShowText: [%s]\n", str);

	waitSec = 0;
	if (str) {
		std::string s = std::string(str);

		size_t start = s.find_first_not_of (" \t\n");
		if (start != std::string::npos) {
			size_t end = s.find_last_not_of (" \t\n");
			s = s.substr(start, end - start + 1);
		}
		if (s.length() > 0 && (vfd_version == 4 || lastOutput != s)) {
			lastOutput = s;
			if (write(fd , s.c_str(), s.length()) < 0)
				perror("write to vfd failed");
			waitSec = 8;
		}
	}

	if (rescheduleTime && (time_notify_writer > -1))
		write(time_notify_writer, "", 1);
}

void CLCD::init(const char *, const char *, const char *, const char *, const char *, const char *)
{
}

void CLCD::setEPGTitle(const std::string)
{
}

#else
/*
	Routines to drive simple one-line text or SPARK's 4 digit LED display

	(C) 2012 Stefan Seyfried

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
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <driver/lcdd.h>
#include <driver/framebuffer.h>

#include <global.h>
#include <neutrino.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
//#include <math.h>
#include <sys/stat.h>
#if HAVE_SPARK_HARDWARE
#include <aotom_main.h>
#define DISPLAY_DEV "/dev/vfd"
#endif
#if HAVE_AZBOX_HARDWARE
#define DISPLAY_DEV "/proc/vfd"
#define LED_DEV "/proc/led"
#endif
#if HAVE_GENERIC_HARDWARE
#define DISPLAY_DEV "/dev/null"
#endif

static char volume = 0;
//static char percent = 0;
static bool power = true;
static bool muted = false;
static bool showclock = true;
static time_t last_display = 0;
static char display_text[64] = { 0 };
static bool led_r = false;
static bool led_g = false;
static bool upd_display = false;
static bool vol_active = false;

static inline int dev_open()
{
	int fd = open(DISPLAY_DEV, O_RDWR);
	if (fd < 0)
		fprintf(stderr, "[neutrino] simple_display: open " DISPLAY_DEV ": %m\n");
	return fd;
}

#if HAVE_AZBOX_HARDWARE
static inline int led_open()
{
	int fd = open(LED_DEV, O_RDWR);
	if (fd < 0)
		fprintf(stderr, "[neutrino] simple_display: open " LED_DEV ": %m\n");
	return fd;
}
#endif

static void replace_umlauts(std::string &s)
{
	/* this is crude, it just replaces ÄÖÜ with AOU since the display can't show them anyway */
	/*                       Ä           ä           Ö           ö           Ü           ü   */
	char tofind[][3] = { "\xc3\x84", "\xc3\xa4", "\xc3\x96", "\xc3\xb6", "\xc3\x9c", "\xc3\xbc" };
	char toreplace[] = { "AaOoUu" };
	char repl[2];
	repl[1] = '\0';
	int i = 0;
	size_t pos;
	// print("%s:>> '%s'\n", __func__, s.c_str());
	while (toreplace[i] != 0x0) {
		pos = s.find(tofind[i]);
		if (pos == std::string::npos) {
			i++;
			continue;
		}
		repl[0] = toreplace[i];
		s.replace(pos, 2, std::string(repl));
	}
	// printf("%s:<< '%s'\n", __func__, s.c_str());
}

static void display(const char *s, bool update_timestamp = true)
{
	int fd = dev_open();
	int len = strlen(s);
	if (fd < 0)
		return;
printf("%s '%s'\n", __func__, s);
	write(fd, s, len);
	close(fd);
	if (update_timestamp)
	{
		last_display = time(NULL);
		/* increase timeout to ensure that everything is displayed
		 * the driver displays 5 characters per second */
		if (len > g_info.hw_caps->display_xres)
			last_display += (len - g_info.hw_caps->display_xres) / 5;
	}
}

CLCD::CLCD()
{
	/* do not show menu in neutrino... */
	has_lcd = false;
	servicename = "";
	thread_running = false;
}

CLCD::~CLCD()
{
	if (thread_running)
	{
		thread_running = false;
		pthread_cancel(thrTime);
		pthread_join(thrTime, NULL);
	}
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
	while (CLCD::getInstance()->thread_running) {
		sleep(1);
		CLCD::getInstance()->showTime();
		/* hack, just if we missed the blit() somewhere
		 * this will update the framebuffer once per second */
		if (getenv("SPARK_NOBLIT") == NULL) {
			CFrameBuffer *fb = CFrameBuffer::getInstance();
			/* plugin start locks the framebuffer... */
			if (!fb->Locked())
				fb->blit();
		}
	}
	return NULL;
}

void CLCD::init(const char *, const char *, const char *, const char *, const char *, const char *)
{
	setMode(MODE_TVRADIO);
	thread_running = true;
	if (pthread_create (&thrTime, NULL, TimeThread, NULL) != 0 ) {
		perror("[neutino] CLCD::init pthread_create(TimeThread)");
		thread_running = false;
		return ;
	}
}

void CLCD::setlcdparameter(void)
{
}

void CLCD::showServicename(std::string name, bool)
{
	if (g_info.hw_caps->display_type == HW_DISPLAY_LED_NUM)
		return;
	servicename = name;
	if (mode != MODE_TVRADIO)
		return;
	replace_umlauts(servicename);
	strncpy(display_text, servicename.c_str(), sizeof(display_text) - 1);
	display_text[sizeof(display_text) - 1] = '\0';
	upd_display = true;
}

#if HAVE_SPARK_HARDWARE
void CLCD::setled(int red, int green)
{
	struct aotom_ioctl_data d;
	int leds[2] = { red, green };
	int i;
	int fd = dev_open();
	if (fd < 0)
		return;

printf("%s red:%d green:%d\n", __func__, red, green);

	for (i = 0; i < 2; i++)
	{
		if (leds[i] == -1)
			continue;
		d.u.led.led_nr = i;
		d.u.led.on = leds[i];
		if (ioctl(fd, VFDSETLED, &d) < 0)
			fprintf(stderr, "[neutrino] %s setled VFDSETLED: %m\n", __func__);
	}
	close(fd);
}
#elif HAVE_AZBOX_HARDWARE
void CLCD::setled(int red, int green)
{
	static unsigned char col = '0'; /* need to remember the state. 1 == blue, 2 == red */
	int leds[3] = { -1, green, red };
	int i;
	char s[3];
	int fd = led_open();
	if (fd < 0)
		return;
	for (i = 1; i <= 2; i++)
	{
		if (leds[i] == -1)	/* don't touch */
			continue;
		col &= ~(i);		/* clear the bit... */
		if (leds[i])
			col |= i;	/* ...and set it again */
	}
	sprintf(s, "%c\n", col);
	write(fd, s, 3);
	close(fd);
	//printf("%s(%d, %d): %c\n", __func__, red, green, col);
}
#else
void CLCD::setled(int /*red*/, int /*green*/)
{
}
#endif

void CLCD::showTime(bool force)
{
	static bool blink = false;
	int red = -1, green = -1;

	if (mode == MODE_SHUTDOWN)
	{
		setled(1, 1);
		return;
	}

	time_t now = time(NULL);
	if (upd_display)
	{
		display(display_text);
		upd_display = false;
	}
	else if (power && (force || (showclock && (now - last_display) > 4)))
	{
		char timestr[64]; /* todo: change if we have a simple display with 63+ chars ;) */
		struct tm *t;
		static int hour = 0, minute = 0;

		t = localtime(&now);
		if (force || last_display || (hour != t->tm_hour) || (minute != t->tm_min)) {
			hour = t->tm_hour;
			minute = t->tm_min;
			int ret = -1;
#if HAVE_SPARK_HARDWARE
			now += t->tm_gmtoff;
			int fd = dev_open();
#if 0 /* VFDSETTIME is broken and too complicated anyway -> use VFDSETTIME2 */
			int mjd = 40587 + now  / 86400; /* 1970-01-01 is mjd 40587 */
			struct aotom_ioctl_data d;
			d.u.time.time[0] = mjd >> 8;
			d.u.time.time[1] = mjd & 0xff;
			d.u.time.time[2] = hour;
			d.u.time.time[3] = minute;
			d.u.time.time[4] = t->tm_sec;
			int ret = ioctl(fd, VFDSETTIME, &d);
#else
			ret = ioctl(fd, VFDSETTIME2, &now);
#endif
			close(fd);
#endif
			if (ret < 0 && servicename.empty())
			{
				if (g_info.hw_caps->display_xres < 5)
					sprintf(timestr, "%02d%02d", hour, minute);
				else	/* pad with spaces on the left side to center the time string */
					sprintf(timestr, "%*s%02d:%02d",(g_info.hw_caps->display_xres - 5)/2, "", hour, minute);
				display(timestr, false);
			}
			else
			{
				if (vol_active)
					showServicename(servicename);
				vol_active = false;
			}
			last_display = 0;
		}
	}

	if (led_r)
		red = blink;
	blink = !blink;
	if (led_g)
		green = blink;

	if (led_r || led_g)
		setled(red, green);
}

void CLCD::showRCLock(int)
{
}

/* update is default true, the mute code sets it to false
 * to force an update => inverted logic! */
void CLCD::showVolume(const char vol, const bool update)
{
	char s[32];
	const int type = (g_info.hw_caps->display_xres < 5);
	const char *vol_fmt[] = { "Vol:%3d%%", "%4d" };
	const char *mutestr[] = { "Vol:MUTE", "mute" };
	if (vol == volume && update)
		return;
	volume = vol;
	/* char is unsigned, so vol is never < 0 */
	if (volume > 100)
		volume = 100;

	if (muted)
		strcpy(s, mutestr[type]);
	else
		sprintf(s, vol_fmt[type], volume);

	display(s);
	vol_active = true;
}

void CLCD::showPercentOver(const unsigned char /*perc*/, const bool /*perform_update*/, const MODES)
{
}

void CLCD::showMenuText(const int, const char *text, const int, const bool)
{
	if (mode != MODE_MENU_UTF8)
		return;
	std::string tmp = text;
	replace_umlauts(tmp);
	strncpy(display_text, tmp.c_str(), sizeof(display_text) - 1);
	display_text[sizeof(display_text) - 1] = '\0';
	upd_display = true;
}

void CLCD::showAudioTrack(const std::string &, const std::string & /*title*/, const std::string &)
{
	if (mode != MODE_AUDIO)
		return;
//	ShowText(title.c_str());
}

void CLCD::showAudioPlayMode(AUDIOMODES)
{
}

void CLCD::showAudioProgress(const char, bool)
{
}

void CLCD::setMode(const MODES m, const char * const)
{
	mode = m;

	switch (m) {
	case MODE_TVRADIO:
		setled(0, 0);
		showclock = true;
		power = true;
		if (g_info.hw_caps->display_type != HW_DISPLAY_LED_NUM) {
			strncpy(display_text, servicename.c_str(), sizeof(display_text) - 1);
			display_text[sizeof(display_text) - 1] = '\0';
			upd_display = true;
		}
		showTime();
		break;
	case MODE_SHUTDOWN:
		showclock = false;
		Clear();
		break;
	case MODE_STANDBY:
		setled(0, 1);
		showclock = true;
		showTime(true);
		break;
	default:
		showclock = true;
		showTime();
	}
}

void CLCD::setBrightness(int)
{
}

int CLCD::getBrightness()
{
	return 0;
}

void CLCD::setBrightnessStandby(int)
{
}

int CLCD::getBrightnessStandby()
{
	return 0;
}

void CLCD::setPower(int)
{
}

int CLCD::getPower()
{
	return 0;
}

void CLCD::togglePower(void)
{
	power = !power;
	if (!power)
		Clear();
	else
		showTime(true);
}

void CLCD::setMuted(bool mu)
{
printf("spark_led:%s %d\n", __func__, mu);
	muted = mu;
	showVolume(volume, false);
}

void CLCD::resume()
{
}

void CLCD::pause()
{
}

void CLCD::Lock()
{
}

void CLCD::Unlock()
{
}

#if HAVE_SPARK_HARDWARE
void CLCD::Clear()
{
	int fd = dev_open();
	if (fd < 0)
		return;
	int ret = ioctl(fd, VFDDISPLAYCLR);
	if(ret < 0)
		perror("[neutrino] spark_led Clear() VFDDISPLAYCLR");
	close(fd);
	servicename.clear();
printf("spark_led:%s\n", __func__);
}
#else
void CLCD::Clear()
{
	display(" ", false);
}
#endif

void CLCD::ShowIcon(fp_icon i, bool on)
{
	switch (i)
	{
		case FP_ICON_CAM1:
			led_r = on;
			setled(led_r, -1); /* switch instant on / switch off if disabling */
			break;
		case FP_ICON_PLAY:
			led_g = on;
			setled(-1, led_g);
			break;
		default:
			break;
	}
}

void CLCD::setEPGTitle(const std::string)
{
}
#endif
