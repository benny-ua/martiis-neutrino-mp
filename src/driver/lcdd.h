/*
	$Id$

	LCD-Daemon  -   DBoxII-Project

	Copyright (C) 2001 Steffen Hehn 'McClean'
	Homepage: http://dbox.cyberphoria.org/

	Copyright (C) 2008-2012 Stefan Seyfried

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

#ifndef __lcdd__
#define __lcdd__

#define LCDDIR_VAR "/var/share/tuxbox/neutrino/lcdd"

typedef enum
{
	VFD_ICON_BAR8       = 0x00000004,
	VFD_ICON_BAR7       = 0x00000008,
	VFD_ICON_BAR6       = 0x00000010,
	VFD_ICON_BAR5       = 0x00000020,
	VFD_ICON_BAR4       = 0x00000040,
	VFD_ICON_BAR3       = 0x00000080,
	VFD_ICON_BAR2       = 0x00000100,
	VFD_ICON_BAR1       = 0x00000200,
	VFD_ICON_FRAME      = 0x00000400,
	VFD_ICON_HDD        = 0x00000800,
	VFD_ICON_MUTE       = 0x00001000,
	VFD_ICON_DOLBY      = 0x00002000,
	VFD_ICON_POWER      = 0x00004000,
	VFD_ICON_TIMESHIFT  = 0x00008000,
	VFD_ICON_SIGNAL     = 0x00010000,
	VFD_ICON_TV         = 0x00020000,
	VFD_ICON_RADIO      = 0x00040000,
	VFD_ICON_HD         = 0x01000001,
	VFD_ICON_1080P      = 0x02000001,
	VFD_ICON_1080I      = 0x03000001,
	VFD_ICON_720P       = 0x04000001,
	VFD_ICON_480P       = 0x05000001,
	VFD_ICON_480I       = 0x06000001,
	VFD_ICON_USB        = 0x07000001,
	VFD_ICON_MP3        = 0x08000001,
	VFD_ICON_PLAY       = 0x09000001,
	VFD_ICON_COL1       = 0x09000002,
	VFD_ICON_PAUSE      = 0x0A000001,
	VFD_ICON_CAM1       = 0x0B000001,
	VFD_ICON_COL2       = 0x0B000002,
	VFD_ICON_CAM2       = 0x0C000001
#ifdef MARTII
	, VFD_ICON_RECORD
#endif
} vfd_icon;

#ifdef LCD_UPDATE
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
// TODO Why is USE_FILE_OFFSET64 not defined, if file.h is included here????
#ifndef __USE_FILE_OFFSET64
#define __USE_FILE_OFFSET64 1
#endif
#include "driver/file.h"
#endif // LCD_UPDATE

#include <configfile.h>
#include <pthread.h>

#ifdef HAVE_TRIPLEDRAGON
#include <lcddisplay/fontrenderer.h>


class CLCDPainter;
class LcdFontRenderClass;
#endif
class CLCD
{
	public:

		enum MODES
		{
			MODE_TVRADIO,
			MODE_SCART,
			MODE_SHUTDOWN,
			MODE_STANDBY,
			MODE_MENU_UTF8,
			MODE_AUDIO,
			MODE_MOVIE
#ifdef LCD_UPDATE
		,	MODE_FILEBROWSER,
			MODE_PROGRESSBAR,
			MODE_PROGRESSBAR2,
			MODE_INFOBOX
#endif // LCD_UPDATE
		};
		enum AUDIOMODES
		{
			AUDIO_MODE_PLAY,
			AUDIO_MODE_STOP,
			AUDIO_MODE_FF,
			AUDIO_MODE_PAUSE,
			AUDIO_MODE_REV
		};


	private:
#ifdef HAVE_TRIPLEDRAGON
		class FontsDef
		{
			public:
				LcdFont *channelname;
				LcdFont *time; 
				LcdFont *menutitle;
				LcdFont *menu;
		};

		CLCDDisplay			display;
		LcdFontRenderClass		*fontRenderer;
		FontsDef			fonts;

#define LCD_NUMBER_OF_BACKGROUNDS 5
		raw_display_t                   background[LCD_NUMBER_OF_BACKGROUNDS];

		MODES				mode;
		AUDIOMODES			movie_playmode;

		std::string			servicename;
		std::string			epg_title;
		std::string			movie_big;
		std::string			movie_small;
		std::string			menutitle;
		char				volume;
		unsigned char			percentOver;
		bool				muted;
		bool				showclock;
		bool				movie_centered;
		bool				movie_is_ac3;
		CConfigFile			configfile;
		pthread_t			thrTime;
		int                             last_toggle_state_power;
		int				clearClock;
		unsigned int                    timeout_cnt;

		void count_down();

		CLCD();

		static void* TimeThread(void*);
		bool lcdInit(const char * fontfile1, const char * fontname1, 
		             const char * fontfile2=NULL, const char * fontname2=NULL,
		             const char * fontfile3=NULL, const char * fontname3=NULL);
		void setlcdparameter(int dimm, int contrast, int power, int inverse, int bias);
		void displayUpdate();
		void showTextScreen(const std::string & big, const std::string & small, int showmode, bool perform_wakeup, bool centered = false);
#else
		CLCD();
		std::string	menutitle;
		std::string	servicename;
		MODES		mode;
		void setled(int red, int green);
		static void	*TimeThread(void *);
		pthread_t	thrTime;
#ifdef MARTII
		int		fd;
		int		time_notify_reader;
		int		time_notify_writer;
		int		waitSec;
		bool		showclock;
		bool		timeThreadRunning;
		unsigned int	timeout_cnt;
		int		vfd_version;
#endif
		bool		thread_running;
#endif
	public:
		bool has_lcd;
		void wake_up();
		void setled(void) { return; };
		void setlcdparameter(void);
#ifdef MARTII
		void setlcdparameter(int, int);
#endif

		static CLCD* getInstance();
		void init(const char * fontfile, const char * fontname,
		          const char * fontfile2=NULL, const char * fontname2=NULL,
		          const char * fontfile3=NULL, const char * fontname3=NULL); 

		void setMode(const MODES m, const char * const title = "");
		MODES getMode() { return mode; };

		void showServicename(const std::string name, const bool clear_epg = false);
#ifdef MARTII
		std::string getServicename(void) { return servicename; }
#endif
		void setEPGTitle(const std::string title);
		void setMovieInfo(const AUDIOMODES playmode, const std::string big, const std::string small, const bool centered = false);
		void setMovieAudio(const bool is_ac3);
		std::string getMenutitle() { return menutitle; };
		void showTime(bool force = false);
		/** blocks for duration seconds */
		void showRCLock(int duration = 2);
		void showVolume(const char vol, const bool perform_update = true);
		void showPercentOver(const unsigned char perc, const bool perform_update = true, const MODES m = MODE_TVRADIO);
		void showMenuText(const int position, const char * text, const int highlight = -1, const bool utf_encoded = false);
		void showAudioTrack(const std::string & artist, const std::string & title, const std::string & album);
		void showAudioPlayMode(AUDIOMODES m=AUDIO_MODE_PLAY);
		void showAudioProgress(const char perc, bool isMuted);
		void setBrightness(int);
		int getBrightness();

		void setBrightnessStandby(int);
		int getBrightnessStandby();

		void setContrast(int);
		int getContrast();

		void setPower(int);
		int getPower();

		void togglePower(void);

		void setInverse(int);
		int getInverse();

		void setAutoDimm(int);
		int getAutoDimm();
		void setBrightnessDeepStandby(int) { return ; };
		int getBrightnessDeepStandby() { return 0; };

		void setMuted(bool);

#ifdef MARTII
		void resume(bool showLastServiceName = false);
#else
		void resume();
#endif
		void pause();

		void Lock();
		void Unlock();
		void Clear();
		void ShowIcon(vfd_icon icon, bool show);
#ifdef MARTII
		void ShowText(const char * str, bool rescheduleTime = true);
#else
		void ShowText(const char *s) { showServicename(std::string(s), true); };
#endif
#ifndef HAVE_TRIPLEDRAGON
		~CLCD();
#endif
#ifdef LCD_UPDATE
	private:
		CFileList* m_fileList;
		int m_fileListPos;
		std::string m_fileListHeader;

		std::string m_infoBoxText;
		std::string m_infoBoxTitle;
		int m_infoBoxTimer;   // for later use
		bool m_infoBoxAutoNewline;
		
		bool m_progressShowEscape;
		std::string  m_progressHeaderGlobal;
		std::string  m_progressHeaderLocal;
		int m_progressGlobal;
		int m_progressLocal;
	public:
		void showFilelist(int flist_pos = -1,CFileList* flist = NULL,const char * const mainDir=NULL);
		void showInfoBox(const char * const title = NULL,const char * const text = NULL,int autoNewline = -1,int timer = -1);
		void showProgressBar(int global = -1,const char * const text = NULL,int show_escape = -1,int timer = -1);
		void showProgressBar2(int local = -1,const char * const text_local = NULL,int global = -1,const char * const text_global = NULL,int show_escape = -1);
#endif // LCD_UPDATE
};


#endif
