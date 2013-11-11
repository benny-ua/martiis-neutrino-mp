/*
  Neutrino-GUI  -   DBoxII-Project

  Copyright (C) 2003,2004 gagga
  Homepage: http://www.giggo.de/dbox

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

#ifndef __movieplayergui__
#define __movieplayergui__

#include <config.h>
#include <configfile.h>
#include <driver/framebuffer.h>
#include <gui/filebrowser.h>
#include <gui/bookmarkmanager.h>
#include <gui/widget/menue.h>
#include <gui/moviebrowser.h>
#include <gui/movieinfo.h>
#include <gui/widget/hintbox.h>
#include <gui/timeosd.h>
#include <driver/record.h>
#include <zapit/channel.h>
#include <playback.h>
#include <audio_td.h>

#include <stdio.h>

#include <string>
#include <vector>

#include <OpenThreads/Thread>
#include <OpenThreads/Condition>

class CMoviePlayerGui : public CMenuTarget
{
 public:
	enum state
		{
		    STOPPED     =  0,
		    PREPARING   =  1,
		    STREAMERROR =  2,
		    PLAY        =  3,
		    PAUSE       =  4,
		    FF          =  5,
		    REW         =  6
		};

	enum repeat_mode_enum { REPEAT_OFF = 0, REPEAT_TRACK = 1, REPEAT_ALL = 2 };

 private:
	CFrameBuffer * frameBuffer;
	int            m_LastMode;	

	std::string	pretty_name;
	std::string	file_name;
	std::string    	currentaudioname;
	bool		playing;
	bool		first_start_timeshift;
	CMoviePlayerGui::state playstate;
	int speed;
	int startposition;
	int position;
	int duration;
	CTimeOSD FileTime;

	unsigned int numpida;
	int vpid;
	int vtype;
	std::string    language[REC_MAX_APIDS];
	int apids[REC_MAX_APIDS];
	unsigned int ac3flags[REC_MAX_APIDS];
	int currentapid, currentac3;
	// subtitle data
	unsigned int numpids;
#ifndef REC_MAX_SPIDS
#define REC_MAX_SPIDS 20 // whatever
#endif
	std::string    slanguage[REC_MAX_SPIDS];
	int spids[REC_MAX_SPIDS];
	// dvb subtitle data
	unsigned int numpidd;
#ifndef REC_MAX_DPIDS
#define REC_MAX_DPIDS 20 // whatever
#endif
	std::string    dlanguage[REC_MAX_DPIDS];
	int dpids[REC_MAX_DPIDS];
	// teletext subtitle data
	unsigned int numpidt;
#ifndef REC_MAX_TPIDS
#define REC_MAX_TPIDS 50 // not pids, actually -- a pid may cover multiple subtitle pages
#endif
	std::string    tlanguage[REC_MAX_TPIDS];
	int tpids[REC_MAX_TPIDS];
	std::string currentttxsub;

	bool probePids;
	AUDIO_FORMAT StreamType;
	
	repeat_mode_enum repeat_mode;

	unsigned long long last_read;

#if 0
	/* subtitles vars */
	int numsubs;
	std::string    slanguage[REC_MAX_APIDS];
	int spids[REC_MAX_APIDS];
	int sub_supported[REC_MAX_APIDS];
	int currentspid;
	int min_x, min_y, max_x, max_y;
	time_t end_time;
#endif

	/* playback from MB */
	bool isMovieBrowser;
	bool isHTTP;
	bool isUPNP;
	bool isWebTV;
	bool isYT;
	bool showStartingHint;
	CMovieBrowser* moviebrowser;
	MI_MOVIE_INFO * p_movie_info;
	MI_MOVIE_INFO mi;
#if HAVE_SPARK_HARDWARE
        CFrameBuffer::Mode3D old3dmode;
#endif

	const static int MOVIE_HINT_BOX_TIMER = 5;	// time to show bookmark hints in seconds

	/* playback from file */
	bool is_file_player;
	CFileBrowser * filebrowser;
	CFileFilter tsfilefilter;
	CFileList filelist;
	CFileList::iterator filelist_it;
	std::string Path_local;
	int menu_ret;

	/* playback from bookmark */
	CBookmarkManager * bookmarkmanager;
	bool isBookmark;

	OpenThreads::Mutex mutex;
	pthread_t bgThread;

	cPlayback *playback;
	static CMoviePlayerGui* instance_mp;

	void Init(void);
	void PlayFile();
	bool PlayFileStart();
	void PlayFileLoop();
	void PlayFileEnd(bool restore = true);
	void cutNeutrino();
	void restoreNeutrino();

	void showHelpTS(void);
	void callInfoViewer(/*const int duration, const int pos*/);
	void fillPids();
	bool getAudioName(int pid, std::string &apidtitle);
	void selectAudioPid(void);
	void getCurrentAudioName( bool file_player, std::string &audioname);
	void addAudioFormat(int count, std::string &apidtitle, bool& enabled );

	void handleMovieBrowser(neutrino_msg_t msg, int position = 0);
	bool SelectFile();
	void updateLcd();

	static void *ShowWebTVHint(void *arg);

#if 0
	void selectSubtitle();
	void showSubtitle(neutrino_msg_data_t data);
	void clearSubtitle();
	void selectChapter();
#endif

	void Cleanup();
	static void *ShowStartHint(void *arg);
	static void* bgPlayThread(void *arg);

	CMoviePlayerGui(const CMoviePlayerGui&) {};
	CMoviePlayerGui();

 public:
	~CMoviePlayerGui();

	static CMoviePlayerGui& getInstance();

	int exec(CMenuTarget* parent, const std::string & actionKey);
	bool Playing() { return playing; };
	std::string CurrentAudioName() { return currentaudioname; };
	int GetSpeed() { return speed; }
	uint64_t GetPts();
	int GetPosition() { return position; }
	int GetDuration() { return duration; }
	size_t GetReadCount();
	void UpdatePosition();
	int timeshift;
	int file_prozent;
	void SetFile(std::string &name, std::string &file) { pretty_name = name; file_name = file; }
	unsigned int getAPID(void);
	unsigned int getAPID(unsigned int i);
	void getAPID(int &apid, unsigned int &is_ac3);
	bool getAPID(unsigned int i, int &apid, unsigned int &is_ac3);
	bool setAPID(unsigned int i);
	AUDIO_FORMAT GetStreamType(void) { return StreamType; }
	void SetStreamType(void);
	cPlayback *getPlayback() { return playback; }
	unsigned int getAPIDCount(void);
	std::string getAPIDDesc(unsigned int i);
	unsigned int getSubtitleCount(void);
	CZapitAbsSub* getChannelSub(unsigned int i, CZapitAbsSub **s);
	int getCurrentSubPid(CZapitAbsSub::ZapitSubtitleType st);
	void setCurrentTTXSub(const char *s) { currentttxsub = s; }
	t_channel_id getChannelId(void);
	void LockPlayback(const char *);
	void UnlockPlayback(void);
	bool PlayBackgroundStart(const std::string &file, const std::string &name, t_channel_id chan);
	void stopPlayBack(void);
	void StopSubtitles(bool b);
	void StartSubtitles(bool show = true);
	void setLastMode(int m) { m_LastMode = m; }
	void Pause(bool b = true);
};

#endif
