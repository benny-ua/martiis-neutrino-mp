/*
  Neutrino-GUI  -   DBoxII-Project

  Movieplayer (c) 2003, 2004 by gagga
  Based on code by Dirch, obi and the Metzler Bros. Thanks.
  (C) 2011 Stefan Seyfried

  Copyright (C) 2011 CoolStream International Ltd

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

#define __STDC_CONSTANT_MACROS
#include <global.h>
#include <neutrino.h>

#include <gui/audiomute.h>
#include <gui/movieplayer.h>
#include <gui/infoviewer.h>
#include <gui/timeosd.h>
#include <gui/widget/helpbox.h>
#include <gui/infoclock.h>
#include <gui/plugins.h>
#include <driver/screenshot.h>
#include <driver/volume.h>
#include <driver/display.h>
#include <driver/abstime.h>
#include <system/helpers.h>

#include <unistd.h>
#include <stdlib.h>
#include <sys/timeb.h>

#include <video.h>
#include <libtuxtxt/teletext.h>
#include <zapit/zapit.h>
#include <fstream>
#include <iostream>
#include <sstream>
#ifdef MARTII
#include <libdvbsub/dvbsub.h>
#include <audio.h>
#include <driver/volume.h>
#include <driver/nglcd.h>
#include <gui/widget/stringinput_ext.h>
#include <gui/screensetup.h>
#include <system/set_threadname.h>
#endif

//extern CPlugins *g_PluginList;
#ifndef HAVE_COOL_HARDWARE
#define LCD_MODE CVFD::MODE_MOVIE
#else
#define LCD_MODE CVFD::MODE_MENU_UTF8
#endif

extern cVideo * videoDecoder;
extern CRemoteControl *g_RemoteControl;	/* neutrino.cpp */
extern CInfoClock *InfoClock;
extern bool has_hdd;
#ifdef MARTII
extern cAudio * audioDecoder;
#endif

#define TIMESHIFT_SECONDS 3

CMoviePlayerGui* CMoviePlayerGui::instance_mp = NULL;

CMoviePlayerGui& CMoviePlayerGui::getInstance()
{
	if ( !instance_mp )
	{
		instance_mp = new CMoviePlayerGui();
		printf("[neutrino CMoviePlayerGui] Instance created...\n");
	}
	return *instance_mp;
}

CMoviePlayerGui::CMoviePlayerGui()
{
	Init();
}

CMoviePlayerGui::~CMoviePlayerGui()
{
#ifndef MARTII
	playback->Close();
#endif
	delete moviebrowser;
	delete filebrowser;
	delete bookmarkmanager;
#ifndef MARTII
	delete playback;
#endif
	instance_mp = NULL;
}
#ifdef MARTII
// for libeplayer3/libass subtitles
static void framebuffer_callback(
	unsigned char** destination,
	unsigned int *screen_width,
	unsigned int *screen_height,
	unsigned int *destStride,
	int *framebufferFD)
{
	CFrameBuffer *frameBuffer = CFrameBuffer::getInstance();
	*destination = (unsigned char *) frameBuffer->getFrameBufferPointer(true);
	*framebufferFD = frameBuffer->getFileHandle();
	fb_var_screeninfo s;
	ioctl(*framebufferFD, FBIOGET_VSCREENINFO, &s);
	*screen_width = s.xres;
	*screen_height = s.yres;
	fb_fix_screeninfo fix;
	ioctl(*framebufferFD, FBIOGET_FSCREENINFO, &fix);
	*destStride = fix.line_length;
}
#endif

void CMoviePlayerGui::Init(void)
{
	playing = false;

	frameBuffer = CFrameBuffer::getInstance();

#ifndef MARTII
	playback = new cPlayback(3);
#endif
	moviebrowser = new CMovieBrowser();
	bookmarkmanager = new CBookmarkManager();

	tsfilefilter.addFilter("ts");
#if HAVE_TRIPLEDRAGON
	tsfilefilter.addFilter("vdr");
#else
	tsfilefilter.addFilter("avi");
	tsfilefilter.addFilter("mkv");
	tsfilefilter.addFilter("wav");
	tsfilefilter.addFilter("asf");
	tsfilefilter.addFilter("aiff");
#endif
	tsfilefilter.addFilter("mpg");
	tsfilefilter.addFilter("mpeg");
	tsfilefilter.addFilter("m2p");
	tsfilefilter.addFilter("mpv");
	tsfilefilter.addFilter("vob");
	tsfilefilter.addFilter("m2ts");
	tsfilefilter.addFilter("mp4");
	tsfilefilter.addFilter("mov");
#ifdef MARTII
	tsfilefilter.addFilter("m3u");
	tsfilefilter.addFilter("pls");
#endif
#ifdef HAVE_SPARK_HARDWARE
	tsfilefilter.addFilter("vdr");
	tsfilefilter.addFilter("flv");
	tsfilefilter.addFilter("wmv");
#endif

	if (strlen(g_settings.network_nfs_moviedir) != 0)
		Path_local = g_settings.network_nfs_moviedir;
	else
		Path_local = "/";

	if (g_settings.filebrowser_denydirectoryleave)
		filebrowser = new CFileBrowser(Path_local.c_str());
	else
		filebrowser = new CFileBrowser();

	filebrowser->Filter = &tsfilefilter;
	filebrowser->Hide_records = true;

	speed = 1;
	timeshift = 0;
	numpida = 0;
#ifdef MARTII
	numpidd = 0;
	numpids = 0;
	numpidt = 0;
#endif
}

void CMoviePlayerGui::cutNeutrino()
{
	if (playing)
		return;

	playing = true;
	g_Zapit->lockPlayBack();
	g_Sectionsd->setPauseScanning(true);

#ifdef HAVE_AZBOX_HARDWARE
	/* we need sectionsd to get idle and zapit to release the demuxes
	 * and decoders so that the external player can do its work
	 * TODO: what about timeshift? */
	g_Sectionsd->setServiceChanged(0, false);
	g_Zapit->setStandby(true);
#endif

	CNeutrinoApp::getInstance()->handleMsg(NeutrinoMessages::CHANGEMODE, NeutrinoMessages::mode_ts);
	m_LastMode = (CNeutrinoApp::getInstance()->getLastMode() | NeutrinoMessages::norezap);
	/* set g_InfoViewer update timer to 1 sec, should be reset to default from restoreNeutrino->set neutrino mode  */
	g_InfoViewer->setUpdateTimer(1000 * 1000);
}

void CMoviePlayerGui::restoreNeutrino()
{
	if (!playing)
		return;

	playing = false;
#ifdef HAVE_AZBOX_HARDWARE
	g_Zapit->setStandby(false);
	CZapit::getInstance()->SetVolume(CZapit::getInstance()->GetVolume());
#endif

	g_Zapit->unlockPlayBack();
	g_Sectionsd->setPauseScanning(false);

	CNeutrinoApp::getInstance()->handleMsg(NeutrinoMessages::CHANGEMODE, m_LastMode);
}

#ifdef MARTII
static bool running = false;
#endif
int CMoviePlayerGui::exec(CMenuTarget * parent, const std::string & actionKey)
{
	printf("[movieplayer] actionKey=%s\n", actionKey.c_str());
#ifdef MARTII
	if (running)
		return menu_return::RETURN_EXIT_ALL;
	running = true;
#endif

	if (parent)
		parent->hide();

	file_name = "";
	full_name = "";

	startposition = 0;

	puts("[movieplayer.cpp] executing " MOVIEPLAYER_START_SCRIPT ".");
	if (my_system(MOVIEPLAYER_START_SCRIPT) != 0)
		perror(MOVIEPLAYER_START_SCRIPT " failed");
	
	isMovieBrowser = false;
#ifdef MARTII
	isWebTV = false;
	isYT = false;
#endif
	isBookmark = false;
	timeshift = 0;
	if (actionKey == "tsmoviebrowser") {
		isMovieBrowser = true;
		moviebrowser->setMode(MB_SHOW_RECORDS);
	}
	else if (actionKey == "ytplayback") {
		frameBuffer->Clear();
		CAudioMute::getInstance()->enableMuteIcon(false);
		isMovieBrowser = true;
		moviebrowser->setMode(MB_SHOW_YT);
		isWebTV = false;
		isYT = true;
	}
	else if (actionKey == "fileplayback") {
	}
	else if (actionKey == "timeshift") {
		timeshift = 1;
	}
	else if (actionKey == "ptimeshift") {
		timeshift = 2;
	}
	else if (actionKey == "rtimeshift") {
		timeshift = 3;
	}
#if 0 // TODO ?
	else if (actionKey == "bookmarkplayback") {
		isBookmark = true;
	}
#endif
#ifdef MARTII
	else if (actionKey == "netstream" || actionKey == "webtv")
	{
		isWebTV = actionKey == "webtv";
		isYT = false;
		full_name = g_settings.streaming_server_url;
		file_name = (isWebTV ? g_settings.streaming_server_name : g_settings.streaming_server_url);
		p_movie_info = NULL;
		is_file_player = 1;
		PlayFile (isWebTV);
	}
#endif
	else {
#ifdef MARTII
		running = false;
#endif
		return menu_return::RETURN_REPAINT;
	}

#ifdef MARTII
	std::string oldservicename = CVFD::getInstance()->getServicename();
#endif
#ifdef MARTII
	if (!isWebTV)
#endif
	while(SelectFile()) {
#ifdef MARTII
		CVFD::getInstance()->setMode(CVFD::MODE_TVRADIO);
		if (isWebTV || isYT)
			CVFD::getInstance()->showServicename(g_settings.streaming_server_name.c_str());
		else
			CVFD::getInstance()->showServicename(full_name.c_str());
#endif
		PlayFile();
		if(timeshift)
			break;
	}
#ifdef MARTII
	CVFD::getInstance()->showServicename(oldservicename.c_str());
#endif

	bookmarkmanager->flush();

	puts("[movieplayer.cpp] executing " MOVIEPLAYER_END_SCRIPT ".");
	if (my_system(MOVIEPLAYER_END_SCRIPT) != 0)
		perror(MOVIEPLAYER_END_SCRIPT " failed");

	CVFD::getInstance()->setMode(CVFD::MODE_TVRADIO);

#ifdef MARTII
	running = false;
#endif

	if (isWebTV || isYT)
		CAudioMute::getInstance()->enableMuteIcon(true);

	if (timeshift){
		timeshift = 0;
		return menu_return::RETURN_EXIT_ALL;
	}
	return menu_ret; //menu_return::RETURN_REPAINT;
}

void CMoviePlayerGui::updateLcd()
{
#ifndef MARTII
	char tmp[20];
	std::string lcd;
	std::string name;

#ifdef MARTII
	if (isMovieBrowser && p_movie_info && strlen(p_movie_info->epgTitle.c_str()) && strncmp(p_movie_info->epgTitle.c_str(), "not", 3))
#else
	if (isMovieBrowser && strlen(p_movie_info->epgTitle.c_str()) && strncmp(p_movie_info->epgTitle.c_str(), "not", 3))
#endif
		name = p_movie_info->epgTitle;
	else
		name = file_name;

	switch (playstate) {
		case CMoviePlayerGui::PAUSE:
			if (speed < 0) {
				sprintf(tmp, "%dx<| ", abs(speed));
				lcd = tmp;
			} else if (speed > 0) {
				sprintf(tmp, "%dx|> ", abs(speed));
				lcd = tmp;
			} else
				lcd = "|| ";
			break;
		case CMoviePlayerGui::REW:
			sprintf(tmp, "%dx<< ", abs(speed));
			lcd = tmp;
			break;
		case CMoviePlayerGui::FF:
			sprintf(tmp, "%dx>> ", abs(speed));
			lcd = tmp;
			break;
		case CMoviePlayerGui::PLAY:
			lcd = "> ";
			break;
		default:
			break;
	}
	lcd += name;
	CVFD::getInstance()->setMode(LCD_MODE);
	CVFD::getInstance()->showMenuText(0, lcd.c_str(), -1, true);
#endif
}

void CMoviePlayerGui::fillPids()
{
	if(p_movie_info == NULL)
		return;

	numpida = 0; currentapid = 0;
#ifdef MARTII
	numpidd = 0; currentdpid = 0;
	numpids = 0; currentspid = 0;
	numpidt = 0; currentttxsub = "";
#endif
	if(!p_movie_info->audioPids.empty()) {
		currentapid = p_movie_info->audioPids[0].epgAudioPid;
		currentac3 = p_movie_info->audioPids[0].atype;
	}
	for (int i = 0; i < (int)p_movie_info->audioPids.size(); i++) {
		apids[i] = p_movie_info->audioPids[i].epgAudioPid;
		ac3flags[i] = p_movie_info->audioPids[i].atype;
		numpida++;
		if (p_movie_info->audioPids[i].selected) {
			currentapid = p_movie_info->audioPids[i].epgAudioPid;
			currentac3 = p_movie_info->audioPids[i].atype;
		}
#ifdef MARTII
		if (numpida == REC_MAX_APIDS)
			break;
#endif
	}
	vpid = p_movie_info->epgVideoPid;
	vtype = p_movie_info->VideoType;
}

bool CMoviePlayerGui::SelectFile()
{
	bool ret = false;
	menu_ret = menu_return::RETURN_REPAINT;

	/*clear audiopids */
	for (int i = 0; i < numpida; i++) {
		apids[i] = 0;
		ac3flags[i] = 0;
		language[i].clear();
	}
	numpida = 0; currentapid = 0;
#ifdef MARTII
	// clear subtitlepids
	for (int i = 0; i < numpids; i++) {
		spids[i] = 0;
		slanguage[i].clear();
	}
	numpids = 0; currentspid = 0xffff;
	// clear dvbsubtitlepids
	for (int i = 0; i < numpidd; i++) {
		dpids[i] = 0;
		dlanguage[i].clear();
	}
	numpidd = 0; currentdpid = 0xffff;
	// clear dvbsubtitlepids
	for (int i = 0; i < numpidt; i++) {
		tpids[i] = 0;
		tlanguage[i].clear();
	}
	numpidt = 0; currentttxsub = "";
#endif
#if 0
	currentspid = -1;
	numsubs = 0;
#endif

	is_file_player = false;
	p_movie_info = NULL;
	file_name = "";

	printf("CMoviePlayerGui::SelectFile: isBookmark %d timeshift %d isMovieBrowser %d\n", isBookmark, timeshift, isMovieBrowser);
	if (has_hdd)
		wakeup_hdd(g_settings.network_nfs_recordingdir);

	if (timeshift) {
		t_channel_id live_channel_id = CZapit::getInstance()->GetCurrentChannelID();
		p_movie_info = CRecordManager::getInstance()->GetMovieInfo(live_channel_id);
		full_name = CRecordManager::getInstance()->GetFileName(live_channel_id) + ".ts";
		fillPids();
		ret = true;
	}
#if 0 // TODO
	else if(isBookmark) {
		const CBookmark * theBookmark = bookmarkmanager->getBookmark(NULL);
		if (theBookmark == NULL) {
			bookmarkmanager->flush();
			return false;
		}
		full_name = theBookmark->getUrl();
		sscanf(theBookmark->getTime(), "%lld", &startposition);
		startposition *= 1000;
		ret = true;
	}
#endif
	else if (isMovieBrowser) {
		if (moviebrowser->exec(Path_local.c_str())) {
			// get the current path and file name
			Path_local = moviebrowser->getCurrentDir();
			CFile *file;
			if ((file = moviebrowser->getSelectedFile()) != NULL) {
				// get the movie info handle (to be used for e.g. bookmark handling)
				p_movie_info = moviebrowser->getCurrentMovieInfo();
				if (moviebrowser->getMode() == MB_SHOW_RECORDS) {
					full_name = file->Name;
				}
				else if (isYT) {
					g_settings.streaming_server_name = std::string(file->Name);
					g_settings.streaming_server_url = std::string(file->Url);
					full_name = file->Url;
					is_file_player = true;
				}
				fillPids();
					
				// get the start position for the movie
				startposition = 1000 * moviebrowser->getCurrentStartPos();
				printf("CMoviePlayerGui::SelectFile: file %s start %d apid %X atype %d vpid %x vtype %d\n", full_name.c_str(), startposition, currentapid, currentac3, vpid, vtype);

				ret = true;
			}
		} else
			menu_ret = moviebrowser->getMenuRet();
	}
	else { // filebrowser
		CAudioMute::getInstance()->enableMuteIcon(false);
		if (filebrowser->exec(Path_local.c_str()) == true) {
			Path_local = filebrowser->getCurrentDir();
			CFile *file;
			if ((file = filebrowser->getSelectedFile()) != NULL) {
				is_file_player = true;
				full_name = file->Name.c_str();
				ret = true;
				if(file->getType() == CFile::FILE_PLAYLIST) {
					std::ifstream infile;
					char cLine[1024];
					char name[1024] = { 0 };
					infile.open(file->Name.c_str(), std::ifstream::in);
					while (infile.good())
					{
						infile.getline(cLine, sizeof(cLine));
						if (cLine[strlen(cLine)-1]=='\r')
							cLine[strlen(cLine)-1]=0;

						int dur;
						sscanf(cLine, "#EXTINF:%d,%[^\n]\n", &dur, name);
						if (strlen(cLine) > 0 && cLine[0]!='#')
						{
							char *url = strstr(cLine, "http://");
							if (url != NULL) {
								printf("name %s [%d] url: %s\n", name, dur, url);
								full_name = url;
								if(strlen(name))
									file_name = name;
							}
						}
					}
				}
			}
		} else
			menu_ret = filebrowser->getMenuRet();
		CAudioMute::getInstance()->enableMuteIcon(true);
	}
	if(ret && file_name.empty()) {
		std::string::size_type pos = full_name.find_last_of('/');
		if(pos != std::string::npos) {
			file_name = full_name.substr(pos+1);
			std::replace(file_name.begin(), file_name.end(), '_', ' ');
		} else
			file_name = full_name;
		printf("CMoviePlayerGui::SelectFile: full_name [%s] file_name [%s]\n", full_name.c_str(), file_name.c_str());
	}
	//store last multiformat play dir
	if( (sizeof(g_settings.network_nfs_moviedir)) > Path_local.size() && (strcmp(g_settings.network_nfs_moviedir,Path_local.c_str()) != 0)){
		strcpy(g_settings.network_nfs_moviedir,Path_local.c_str());
	}

	return ret;
}

#ifdef MARTII
void *CMoviePlayerGui::ShowWebTVHint(void *arg) {
	set_threadname(__func__);
	CMoviePlayerGui *caller = (CMoviePlayerGui *)arg;
	neutrino_locale_t title = caller->isYT ? LOCALE_MOVIEPLAYER_YTPLAYBACK : LOCALE_WEBTV_HEAD;
	CHintBox hintbox(title, g_settings.streaming_server_name.c_str(), 450, NEUTRINO_ICON_MOVIEPLAYER);
	hintbox.paint();
	while (caller->showWebTVHint) {
		neutrino_msg_t msg;
		neutrino_msg_data_t data;
		g_RCInput->getMsg(&msg, &data, 1);
		if (msg == (neutrino_msg_t) CRCInput::RC_home) {
			if(caller->playback)
				caller->playback->RequestAbort();
		}
	}
	hintbox.hide();
	return NULL;
}

void CMoviePlayerGui::PlayFile(bool doCutNeutrino)
#else
void CMoviePlayerGui::PlayFile(void)
#endif
{
	neutrino_msg_t msg;
	neutrino_msg_data_t data;
	menu_ret = menu_return::RETURN_REPAINT;

	bool first_start_timeshift = false;
	bool time_forced = false;
	bool update_lcd = true;
#ifndef MARTII
	int eof = 0;
#endif

	//CTimeOSD FileTime;
	position = 0, duration = 0;

	playstate = CMoviePlayerGui::STOPPED;
	printf("Startplay at %d seconds\n", startposition/1000);
	handleMovieBrowser(CRCInput::RC_nokey, position);

#ifdef MARTII
	if(doCutNeutrino)
#endif
	cutNeutrino();
#ifdef MARTII
	playback = new cPlayback(3, &framebuffer_callback);
	CMPSubtitleChangeExec SubtitleChanger(playback);
	playback->SetTeletextPid(0xffff);
#endif
#if 0
	clearSubtitle();
#endif

	playback->Open(is_file_player ? PLAYMODE_FILE : PLAYMODE_TS);

	printf("IS FILE PLAYER: %s\n", is_file_player ?  "true": "false" );

	if(p_movie_info != NULL) {
		duration = p_movie_info->length * 60 * 1000;
		int percent = CZapit::getInstance()->GetPidVolume(p_movie_info->epgId, currentapid, currentac3 == 1);
		CZapit::getInstance()->SetVolumePercent(percent);
#ifdef MARTII
		CScreenSetup cSS;
		cSS.showBorder(p_movie_info->epgId);
#endif
	}
#ifdef MARTII
	else {
		CScreenSetup cSS;
		cSS.showBorder(0);
	}
#endif

	file_prozent = 0;
#ifdef MARTII
	CFrameBuffer::Mode3D old3dmode = frameBuffer->get3DMode();
#endif
#ifdef ENABLE_GRAPHLCD // MARTII
	nGLCD::MirrorOSD(false);
	if (p_movie_info)
		nGLCD::lockChannel(p_movie_info->epgChannel, p_movie_info->epgTitle);
#endif
#ifdef MARTII
	pthread_t thrWebTVHint = 0;
	if (isWebTV || isYT) {
		showWebTVHint = true;
		pthread_create(&thrWebTVHint, NULL, CMoviePlayerGui::ShowWebTVHint, this);
	}
	bool res = playback->Start((char *) full_name.c_str(), vpid, vtype, currentapid, currentac3, duration, !isMovieBrowser || !moviebrowser || !moviebrowser->doProbe());
	if (thrWebTVHint) {
		showWebTVHint = false;
		pthread_join(thrWebTVHint, NULL);
	}
	if (!res) {
#else
	if(!playback->Start((char *) full_name.c_str(), vpid, vtype, currentapid, currentac3, duration)) {
#endif
		playback->Close();
	} else {
#ifdef MARTII
		numpida = REC_MAX_APIDS;
		playback->FindAllPids(apids, ac3flags, &numpida, language);
		if (p_movie_info)
			for (int i = 0; i < numpida; i++) {
				EPG_AUDIO_PIDS pids;
				pids.epgAudioPid = apids[i];
				pids.selected = 0;
				pids.atype = ac3flags[i];
				pids.epgAudioPidName = language[i];
				p_movie_info->audioPids.push_back(pids);
			}
#endif
		playstate = CMoviePlayerGui::PLAY;
		CVFD::getInstance()->ShowIcon(FP_ICON_PLAY, true);
		if(timeshift) {
			first_start_timeshift = true;
			startposition = -1;
			int i;
			int towait = (timeshift == 1) ? TIMESHIFT_SECONDS+1 : TIMESHIFT_SECONDS;
			for(i = 0; i < 500; i++) {
				playback->GetPosition(position, duration);
				startposition = (duration - position);

				//printf("CMoviePlayerGui::PlayFile: waiting for data, position %d duration %d (%d), start %d\n", position, duration, towait, startposition);
				if(startposition > towait*1000)
					break;

				usleep(20000);
			}
			if(timeshift == 3) {
				startposition = duration;
			} else {
				if(g_settings.timeshift_pause)
					playstate = CMoviePlayerGui::PAUSE;
				if(timeshift == 1)
					startposition = 0;
				else
					startposition = duration - TIMESHIFT_SECONDS*1000;
			}
			printf("******************* Timeshift %d, position %d, seek to %d seconds\n", timeshift, position, startposition/1000);
		}
		if(!is_file_player && startposition >= 0)//FIXME no jump for file at start yet
			playback->SetPosition(startposition, true);

		/* playback->Start() starts paused */
		if(timeshift == 3) {
			playback->SetSpeed(-1);
		} else if(!timeshift || !g_settings.timeshift_pause) {
			playback->SetSpeed(1);
		}
	}

	CAudioMute::getInstance()->enableMuteIcon(true);

	while (playstate >= CMoviePlayerGui::PLAY)
	{
#ifdef ENABLE_GRAPHLCD // MARTII
		if (p_movie_info)
			nGLCD::lockChannel(p_movie_info->epgChannel, p_movie_info->epgTitle, duration ? (100 * position / duration) : 0);
#endif
		if (update_lcd) {
			update_lcd = false;
			updateLcd();
		}
		if (first_start_timeshift) {
			callInfoViewer(/*duration, position*/);
			first_start_timeshift = false;
		}

		g_RCInput->getMsg(&msg, &data, 10);	// 1 secs..

		if ((playstate >= CMoviePlayerGui::PLAY) && (timeshift || (playstate != CMoviePlayerGui::PAUSE))) {
#ifdef MARTII
			if (isWebTV) {
				if (!playback->GetPosition(position, duration))
					g_RCInput->postMsg((neutrino_msg_t) g_settings.mpkey_stop, 0);
			} else {
#endif
			if(playback->GetPosition(position, duration)) {
				if(duration > 100)
					file_prozent = (unsigned char) (position / (duration / 100));
#if HAVE_TRIPLEDRAGON
				CVFD::getInstance()->showPercentOver(file_prozent, true, CVFD::MODE_MOVIE);
#endif

				playback->GetSpeed(speed);
				/* at BOF lib set speed 1, check it */
				if ((playstate != CMoviePlayerGui::PLAY) && (speed == 1)) {
					playstate = CMoviePlayerGui::PLAY;
					update_lcd = true;
				}
#ifndef MARTII
#ifdef DEBUG
				printf("CMoviePlayerGui::PlayFile: speed %d position %d duration %d (%d, %d%%)\n", speed, position, duration, duration-position, file_prozent);
#endif
				/* in case ffmpeg report incorrect values */
				int posdiff = duration - position;
				if ((posdiff > 0) && (posdiff < 1000) && !timeshift)
				{
					/* 10 seconds after end-of-file, stop */
					if (++eof > 10)
						g_RCInput->postMsg((neutrino_msg_t) g_settings.mpkey_stop, 0);
				}
				else
					eof = 0;
#endif
			}
#ifdef MARTII
				else
					g_RCInput->postMsg((neutrino_msg_t) g_settings.mpkey_stop, 0);
			}
#endif
			handleMovieBrowser(0, position);
			FileTime.update(position, duration);
		}
#if 0
		showSubtitle(0);
#endif

		if (msg == (neutrino_msg_t) g_settings.mpkey_plugin) {
			//g_PluginList->start_plugin_by_name (g_settings.movieplayer_plugin.c_str (), pidt);
		} else if (msg == (neutrino_msg_t) g_settings.mpkey_stop) {
			playstate = CMoviePlayerGui::STOPPED;
#ifdef MARTII
			playback->RequestAbort();
		} else if (msg == (neutrino_msg_t) CRCInput::RC_home) {
			playstate = CMoviePlayerGui::STOPPED;
			playback->RequestAbort();
		} else if (msg == (neutrino_msg_t) g_settings.mpkey_next3dmode) {
			frameBuffer->set3DMode((CFrameBuffer::Mode3D)(((frameBuffer->get3DMode()) + 1) % CFrameBuffer::Mode3D_SIZE));
		} else if( msg == (neutrino_msg_t) g_settings.key_next43mode) {
			g_videoSettings->next43Mode();
		} else if( msg == (neutrino_msg_t) g_settings.key_switchformat) {
			g_videoSettings->SwitchFormat();
		} else if (msg == (neutrino_msg_t) CRCInput::RC_setup) {
			StopSubtitles(true);
			CNeutrinoApp::getInstance()->handleMsg(NeutrinoMessages::SHOW_MAINSETTINGS, 0);
			StartSubtitles(true);
#endif
		} else if (msg == (neutrino_msg_t) g_settings.mpkey_play) {
			if (playstate > CMoviePlayerGui::PLAY) {
				playstate = CMoviePlayerGui::PLAY;
				speed = 1;
				playback->SetSpeed(speed);
				//update_lcd = true;
				updateLcd();
				if (!timeshift)
					callInfoViewer(/*duration, position*/);
			}
			if (time_forced) {
				time_forced = false;
				FileTime.hide();
			}
		} else if (msg == (neutrino_msg_t) g_settings.mpkey_pause) {
			if (playstate == CMoviePlayerGui::PAUSE) {
				playstate = CMoviePlayerGui::PLAY;
				//CVFD::getInstance()->ShowIcon(VFD_ICON_PAUSE, false);
				speed = 1;
				playback->SetSpeed(speed);
			} else {
				playstate = CMoviePlayerGui::PAUSE;
				//CVFD::getInstance()->ShowIcon(VFD_ICON_PAUSE, true);
				speed = 0;
				playback->SetSpeed(speed);
			}
			//update_lcd = true;
			updateLcd();
			if (!timeshift)
				callInfoViewer(/*duration, position*/);

		} else if (msg == (neutrino_msg_t) g_settings.mpkey_bookmark) {
#if 0
			if (is_file_player)
				selectChapter();
			else
#endif
				handleMovieBrowser((neutrino_msg_t) g_settings.mpkey_bookmark, position);
			update_lcd = true;
#if 0
			clearSubtitle();
#endif
		} else if (msg == (neutrino_msg_t) g_settings.mpkey_audio) {
#ifdef MARTII
			StopSubtitles(true);
#endif
			selectAudioPid(is_file_player);
#ifdef MARTII
			StartSubtitles(true);
#endif
			update_lcd = true;
#if 0
			clearSubtitle();
#endif
		} else if ( msg == (neutrino_msg_t) g_settings.mpkey_subtitle) {
#if 0
			selectSubtitle();
#endif
#if 0
			clearSubtitle();
#endif
			update_lcd = true;
		} else if (msg == (neutrino_msg_t) g_settings.mpkey_time) {
			FileTime.switchMode(position, duration);
		} else if (/*!is_file_player &&*/ ((msg == (neutrino_msg_t) g_settings.mpkey_rewind) ||
				(msg == (neutrino_msg_t) g_settings.mpkey_forward))) {

			int newspeed;
			if (msg == (neutrino_msg_t) g_settings.mpkey_rewind) {
				newspeed = (speed >= 0) ? -1 : speed - 1;
			} else {
				newspeed = (speed <= 0) ? 2 : speed + 1;
			}
			/* if paused, playback->SetSpeed() start slow motion */
			if (playback->SetSpeed(newspeed)) {
				printf("SetSpeed: update speed\n");
				speed = newspeed;
				if (playstate != CMoviePlayerGui::PAUSE)
					playstate = msg == (neutrino_msg_t) g_settings.mpkey_rewind ? CMoviePlayerGui::REW : CMoviePlayerGui::FF;
				updateLcd();
			}
			//update_lcd = true;

			if (!timeshift)
				callInfoViewer(/*duration, position*/);

			if (!FileTime.IsVisible()) {
				FileTime.show(position);
				time_forced = true;
			}
		} else if (msg == CRCInput::RC_1) {	// Jump Backwards 1 minute
#if 0
			clearSubtitle();
#endif
			playback->SetPosition(-60 * 1000);
		} else if (msg == CRCInput::RC_3) {	// Jump Forward 1 minute
#if 0
			clearSubtitle();
#endif
			playback->SetPosition(60 * 1000);
		} else if (msg == CRCInput::RC_4) {	// Jump Backwards 5 minutes
#if 0
			clearSubtitle();
#endif
			playback->SetPosition(-5 * 60 * 1000);
		} else if (msg == CRCInput::RC_6) {	// Jump Forward 5 minutes
#if 0
			clearSubtitle();
#endif
			playback->SetPosition(5 * 60 * 1000);
		} else if (msg == CRCInput::RC_7) {	// Jump Backwards 10 minutes
#if 0
			clearSubtitle();
#endif
			playback->SetPosition(-10 * 60 * 1000);
		} else if (msg == CRCInput::RC_9) {	// Jump Forward 10 minutes
#if 0
			clearSubtitle();
#endif
			playback->SetPosition(10 * 60 * 1000);
		} else if (msg == CRCInput::RC_2) {	// goto start
#if 0
			clearSubtitle();
#endif
			playback->SetPosition(0, true);
		} else if (msg == CRCInput::RC_5) {	// goto middle
#if 0
			clearSubtitle();
#endif
			playback->SetPosition(duration/2, true);
		} else if (msg == CRCInput::RC_8) {	// goto end
#if 0
			clearSubtitle();
#endif
			playback->SetPosition(duration - 60 * 1000, true);
		} else if (msg == CRCInput::RC_page_up) {
#if 0
			clearSubtitle();
#endif
			playback->SetPosition(10 * 1000);
		} else if (msg == CRCInput::RC_page_down) {
#if 0
			clearSubtitle();
#endif
			playback->SetPosition(-10 * 1000);
		} else if (msg == CRCInput::RC_0) {	// cancel bookmark jump
			handleMovieBrowser(CRCInput::RC_0, position);
#ifdef MARTII
		} else if (msg == (neutrino_msg_t) g_settings.mpkey_goto) {
			char Value[10];
			bool cancel = true;
			playback->GetPosition(position, duration);
			int ss = position/1000;
			int hh = ss/3600;
			ss -= hh * 3600;
			int mm = ss/60;
			ss -= mm * 60;
#if 1 // eplayer lacks precision, omit seconds
			snprintf(Value, sizeof(Value), "%.2d:%.2d", hh, mm);
			ss = 0;
#else
			snprintf(Value, sizeof(Value), "%.2d:%.2d:%.2d", hh, mm, ss);
#endif
			CTimeInput jumpTime (LOCALE_MPKEY_GOTO, Value, NONEXISTANT_LOCALE, NONEXISTANT_LOCALE, NULL, &cancel);
			jumpTime.exec(NULL, "");
			jumpTime.hide();
			if (!cancel && ((3 == sscanf(Value, "%d:%d:%d", &hh, &mm, &ss)) || (2 == sscanf(Value, "%d:%d", &hh, &mm))))
				playback->SetPosition(1000 * (hh * 3600 + mm * 60 + ss), true);
		} else if (msg == (uint32_t)g_settings.key_help || msg == CRCInput::RC_info) {
#else
		} else if (msg == CRCInput::RC_help || msg == CRCInput::RC_info) {
#endif
			callInfoViewer(/*duration, position*/);
			update_lcd = true;
#if 0
			clearSubtitle();
#endif
			//showHelpTS();
#ifdef MARTII
		} else if((msg == CRCInput::RC_text || msg == (neutrino_msg_t) g_settings.mpkey_vtxt)) {
			uint16_t pid = playback->GetTeletextPid();
			if (pid) {
				playback->SetTeletextPid(0xffff);
				StopSubtitles(true);
				if(g_settings.cacheTXT)
					tuxtxt_stop();
				playback->SetTeletextPid(pid);
				tuxtx_stop_subtitle();
				tuxtx_main(g_RCInput->getFileHandle(), pid, 0, 2, true);
				tuxtxt_stop();
				playback->SetTeletextPid(0xffff);
				if (currentttxsub != "")
					SubtitleChanger.exec(NULL, currentttxsub);
				StartSubtitles(true);
				frameBuffer->paintBackground();
				//purge input queue
				do
					g_RCInput->getMsg(&msg, &data, 1);
				while (msg != CRCInput::RC_timeout);
			} else if (g_RemoteControl->current_PIDs.PIDs.vtxtpid) {
				StopSubtitles(true);
				// The playback stream doesn't come with teletext.
				tuxtx_main(g_RCInput->getFileHandle(), g_RemoteControl->current_PIDs.PIDs.vtxtpid, 0, 2);
				frameBuffer->paintBackground();
				StartSubtitles(true);
				//purge input queue
				do
					g_RCInput->getMsg(&msg, &data, 1);
				while (msg != CRCInput::RC_timeout);
			}
		} else if(timeshift && (msg == CRCInput::RC_epg || msg == NeutrinoMessages::SHOW_EPG)) {
#else
		} else if(timeshift && (msg == CRCInput::RC_text || msg == CRCInput::RC_epg || msg == NeutrinoMessages::SHOW_EPG)) {
#endif
			bool restore = FileTime.IsVisible();
			FileTime.hide();

#ifdef MARTII
			StopSubtitles(true);
#endif
			if( msg == CRCInput::RC_epg )
				g_EventList->exec(CNeutrinoApp::getInstance()->channelList->getActiveChannel_ChannelID(), CNeutrinoApp::getInstance()->channelList->getActiveChannelName());
			else if(msg == NeutrinoMessages::SHOW_EPG)
				g_EpgData->show(CNeutrinoApp::getInstance()->channelList->getActiveChannel_ChannelID());
#ifndef MARTII
			else {
				if(g_settings.cacheTXT)
					tuxtxt_stop();
				tuxtx_main(g_RCInput->getFileHandle(), g_RemoteControl->current_PIDs.PIDs.vtxtpid, 0, 2);
				frameBuffer->paintBackground();
			}
#endif
#ifdef MARTII
			StartSubtitles(true);
#endif
			if(restore)
				FileTime.show(position);
#ifdef SCREENSHOT
		} else if (msg == (neutrino_msg_t) g_settings.key_screenshot) {

			char ending[(sizeof(int)*2) + 6] = ".jpg";
			if(!g_settings.screenshot_cover)
				snprintf(ending, sizeof(ending) - 1, "_%x.jpg", position);

			std::string fname = full_name;
			std::string::size_type pos = fname.find_last_of('.');
			if(pos != std::string::npos) {
				fname.replace(pos, fname.length(), ending);
			} else
				fname += ending;

			if(!g_settings.screenshot_cover){
				pos = fname.find_last_of('/');
				if(pos != std::string::npos) {
					fname.replace(0, pos, g_settings.screenshot_dir);
				}
			}

#if 0 // TODO disable overwrite ?
			if(!access(fname.c_str(), F_OK)) {
			}
#endif
			CScreenShot * sc = new CScreenShot(fname);
			if(g_settings.screenshot_cover && !g_settings.screenshot_video)
				sc->EnableVideo(true);
			sc->Start();
#endif

		} else if ( msg == NeutrinoMessages::EVT_SUBT_MESSAGE) {
#if 0
			showSubtitle(data);
#endif
		} else if ( msg == NeutrinoMessages::ANNOUNCE_RECORD ||
				msg == NeutrinoMessages::RECORD_START) {
			CNeutrinoApp::getInstance()->handleMsg(msg, data);
		} else if ( msg == NeutrinoMessages::ZAPTO ||
				msg == NeutrinoMessages::STANDBY_ON ||
				msg == NeutrinoMessages::SHUTDOWN ||
				msg == NeutrinoMessages::SLEEPTIMER) {	// Exit for Record/Zapto Timers
			printf("CMoviePlayerGui::PlayFile: ZAPTO etc..\n");
			if(msg != NeutrinoMessages::ZAPTO)
				menu_ret = menu_return::RETURN_EXIT_ALL;

			playstate = CMoviePlayerGui::STOPPED;
			g_RCInput->postMsg(msg, data);
		} else if (msg == CRCInput::RC_timeout) {
			// nothing
		} else if (msg == CRCInput::RC_sat || msg == CRCInput::RC_favorites) {
			//FIXME do nothing ?
		} else {
			if (CNeutrinoApp::getInstance()->handleMsg(msg, data) & messages_return::cancel_all) {
				printf("CMoviePlayerGui::PlayFile: neutrino handleMsg messages_return::cancel_all\n");
				playstate = CMoviePlayerGui::STOPPED;
				menu_ret = menu_return::RETURN_EXIT_ALL;
			}
			else if ( msg <= CRCInput::RC_MaxRC ) {
				update_lcd = true;
#if 0
				clearSubtitle();
#endif
			}
		}

		if (playstate == CMoviePlayerGui::STOPPED) {
			printf("CMoviePlayerGui::PlayFile: exit, isMovieBrowser %d p_movie_info %p\n", isMovieBrowser, p_movie_info);
			playstate = CMoviePlayerGui::STOPPED;
			handleMovieBrowser((neutrino_msg_t) g_settings.mpkey_stop, position);
		}
	}
#ifdef MARTII
	playback->SetTeletextPid(0xffff);    
	tuxtx_stop_subtitle();
	dvbsub_stop();

	frameBuffer->set3DMode(old3dmode);
#ifdef ENABLE_GRAPHLCD
	if (p_movie_info)
		nGLCD::unlockChannel();
#endif
#endif

	FileTime.hide();
#if 0
	clearSubtitle();
#endif

	playback->SetSpeed(1);
	playback->Close();
#ifdef MARTII
	SubtitleChanger.exec(NULL, "off");
	delete playback;
	playback = NULL;
	CScreenSetup cSS;
	cSS.showBorder(CZapit::getInstance()->GetCurrentChannelID());
#endif

	CVFD::getInstance()->ShowIcon(FP_ICON_PLAY, false);
	CVFD::getInstance()->ShowIcon(FP_ICON_PAUSE, false);

#ifdef MARTII
	if(doCutNeutrino)
#endif
	restoreNeutrino();

	CAudioMute::getInstance()->enableMuteIcon(false);

	if (g_settings.mode_clock)
		InfoClock->StartClock();
}

void CMoviePlayerGui::callInfoViewer(/*const int duration, const int curr_pos*/)
{
	if(timeshift) {
		g_InfoViewer->showTitle(CNeutrinoApp::getInstance()->channelList->getActiveChannelNumber(),
				CNeutrinoApp::getInstance()->channelList->getActiveChannelName(),
				CNeutrinoApp::getInstance()->channelList->getActiveSatellitePosition(),
				CNeutrinoApp::getInstance()->channelList->getActiveChannel_ChannelID());
		return;
	}
	currentaudioname = "Unk";
	getCurrentAudioName( is_file_player, currentaudioname);

	if (isMovieBrowser && p_movie_info) {
		g_InfoViewer->showMovieTitle(playstate, p_movie_info->epgChannel, p_movie_info->epgTitle, p_movie_info->epgInfo1,
					     duration, position);
		return;
	}

	/* not moviebrowser => use the filename as title */
	g_InfoViewer->showMovieTitle(playstate, file_name, "", "", duration, position);
}

bool CMoviePlayerGui::getAudioName(int apid, std::string &apidtitle)
{
	if (p_movie_info == NULL)
		return false;

	for (int i = 0; i < (int)p_movie_info->audioPids.size(); i++) {
		if (p_movie_info->audioPids[i].epgAudioPid == apid && !p_movie_info->audioPids[i].epgAudioPidName.empty()) {
			apidtitle = p_movie_info->audioPids[i].epgAudioPidName;
			return true;
		}
	}
	return false;
}

void CMoviePlayerGui::addAudioFormat(int count, std::string &apidtitle, bool& enabled)
{
	enabled = true;
	switch(ac3flags[count])
	{
		case 1: /*AC3,EAC3*/
			if (apidtitle.find("AC3") == std::string::npos)
				apidtitle.append(" (AC3)");
			break;
		case 2: /*teletext*/
			apidtitle.append(" (Teletext)");
			enabled = false;
			break;
		case 3: /*MP2*/
#ifdef MARTII
			apidtitle.append(" (MP2)");
#else
			apidtitle.append("( MP2)");
#endif
			break;
		case 4: /*MP3*/
			apidtitle.append(" (MP3)");
			break;
		case 5: /*AAC*/
			apidtitle.append(" (AAC)");
			break;
		case 6: /*DTS*/
#if !defined (BOXMODEL_APOLLO)
			if (apidtitle.find("DTS") == std::string::npos)
				apidtitle.append(" (DTS)");
			else
#endif
				enabled = false;
			break;
		case 7: /*MLP*/
			apidtitle.append(" (MLP)");
			break;
		default:
			break;
	}
}

void CMoviePlayerGui::getCurrentAudioName( bool file_player, std::string &audioname)
{
#ifdef MARTII
  	if(true){
		numpida = REC_MAX_APIDS;
#else
  	if(file_player && !numpida){
#endif
		playback->FindAllPids(apids, ac3flags, &numpida, language);
		if(numpida)
			currentapid = apids[0];
	}
	bool dumm = true;
	for (unsigned int count = 0; count < numpida; count++) {
		if(currentapid == apids[count]){
			if(!file_player){
				getAudioName(apids[count], audioname);
				return ;
			} else if (!language[count].empty()){
				audioname = language[count];
				addAudioFormat(count, audioname, dumm);
				if(!dumm && (count < numpida)){
					currentapid = apids[count+1];
					continue;
				}
				return ;
			}
			char apidnumber[20];
			sprintf(apidnumber, "Stream %d %X", count + 1, apids[count]);
			audioname = apidnumber;
			addAudioFormat(count, audioname, dumm);
			if(!dumm && (count < numpida)){
				currentapid = apids[count+1];
				continue;
			}
			return ;
		}
	}
}

void CMoviePlayerGui::selectAudioPid(bool file_player)
{
	CMenuWidget APIDSelector(LOCALE_APIDSELECTOR_HEAD, NEUTRINO_ICON_AUDIO);
	APIDSelector.addIntroItems();

	int select = -1;
	CMenuSelectorTarget * selector = new CMenuSelectorTarget(&select);

#ifdef MARTII
	// these may change in-stream
	numpida = REC_MAX_APIDS;
	playback->FindAllPids(apids, ac3flags, &numpida, language);
	numpids = REC_MAX_SPIDS;
	playback->FindAllSubtitlePids(spids, &numpids, slanguage);
	numpidd = REC_MAX_DPIDS;
	playback->FindAllDvbsubtitlePids(dpids, &numpidd, dlanguage);
	numpidt = REC_MAX_TPIDS;
	playback->FindAllTeletextsubtitlePids(tpids, &numpidt, tlanguage);

	if(numpida)
		currentapid = apids[0];

	std::string apidtitles[numpida];
#else
	if(file_player && !numpida){
		playback->FindAllPids(apids, ac3flags, &numpida, language);
		if(numpida)
			currentapid = apids[0];
	}
#endif
	for (unsigned int count = 0; count < numpida; count++) {
		bool name_ok = false;
		bool enabled = true;
		bool defpid = currentapid ? (currentapid == apids[count]) : (count == 0);
		std::string apidtitle;

		if(!file_player){
			name_ok = getAudioName(apids[count], apidtitle);
		}
		else if (!language[count].empty()){
			apidtitle = language[count];
			name_ok = true;
		}
#ifdef MARTII
		char apidnumber[20];
#endif
		if (!name_ok) {
#ifndef MARTII
			char apidnumber[20];
#endif
			sprintf(apidnumber, "Stream %d %X", count + 1, apids[count]);
			apidtitle = apidnumber;
		}
#ifdef MARTII // ?!?
		if (p_movie_info && p_movie_info->epgId)
			apidtitles[count] = std::string(apidtitle);
#endif
		addAudioFormat(count, apidtitle, enabled);
		if(defpid && !enabled && (count < numpida)){
			currentapid = apids[count+1];
			defpid = false;
		}

		char cnt[5];
		sprintf(cnt, "%d", count);
		CMenuForwarderNonLocalized * item = new CMenuForwarderNonLocalized(apidtitle.c_str(), enabled, NULL, selector, cnt, CRCInput::convertDigitToKey(count + 1));
		APIDSelector.addItem(item, defpid);
	}

#ifdef MARTII
	unsigned int shortcut_num = numpida + 1;
	currentdpid = playback->GetDvbsubtitlePid();
	currentspid = playback->GetSubtitlePid();
	if (numpidd > 0 || numpids > 0 || numpidt > 0)
		APIDSelector.addItem(new CMenuSeparator(CMenuSeparator::LINE | CMenuSeparator::STRING, LOCALE_SUBTITLES_HEAD));

	CMPSubtitleChangeExec SubtitleChanger(playback);

	for (unsigned int count = 0; count < numpidt; count++) {
		char lang[10];
		unsigned int type, magazine, page;
		int pid;
		if (5 != sscanf(tlanguage[count].c_str(), "%d %s %u %u %u", &pid, lang, &type, &magazine, &page))
			continue;
		if (!magazine)
			magazine = 8;
		page |= magazine << 8;
		char spid[40];
		snprintf(spid,sizeof(spid), "TTX:%d:%03X:%s", pid, page, lang);
		char item[64];
		snprintf(item,sizeof(item), "TTX: %s (pid %x page %03X)", lang, pid, page);
		APIDSelector.addItem(new CMenuForwarderNonLocalized(item, strcmp(spid, currentttxsub.c_str()), NULL, &SubtitleChanger, spid, CRCInput::convertDigitToKey(shortcut_num)));
		shortcut_num++;
	}
	for (unsigned int count = 0; count < numpidd; count++) {
		char spid[10];
		snprintf(spid,sizeof(spid), "DVB:%d", dpids[count]);
		char item[64];
		snprintf(item,sizeof(item), "DVB: %s (track %x)", dlanguage[count].c_str(), dpids[count]);
		APIDSelector.addItem(new CMenuForwarderNonLocalized(item, dpids[count] != currentdpid, NULL, &SubtitleChanger, spid, CRCInput::convertDigitToKey(shortcut_num)));
		shortcut_num++;
	}
	for (unsigned int count = 0; count < numpids; count++) {
		char spid[10];
		snprintf(spid,sizeof(spid), "SUB:%d", spids[count]);
		char item[64];
		snprintf(item,sizeof(item), "SUB: %s (track %x)", slanguage[count].c_str(), spids[count]);
		APIDSelector.addItem(new CMenuForwarderNonLocalized(item, spids[count] != currentspid, NULL, &SubtitleChanger, spid, CRCInput::convertDigitToKey(shortcut_num)));
		shortcut_num++;
	}
	if (numpidd > 0)
		APIDSelector.addItem(new CMenuOptionNumberChooser(LOCALE_SUBTITLES_DELAY, (int *)&g_settings.dvb_subtitle_delay, true, -99, 99));
	if (numpidd > 0 || numpids > 0 || numpidt > 0)
		APIDSelector.addItem(new CMenuForwarder(LOCALE_SUBTITLES_STOP, currentdpid || currentspid /* FIXME -- stoppable? */|| currentttxsub.length(), NULL, &SubtitleChanger, "off", CRCInput::RC_stop));
#endif
	if (p_movie_info && numpida <= p_movie_info->audioPids.size()) {
		APIDSelector.addItem(new CMenuSeparator(CMenuSeparator::LINE | CMenuSeparator::STRING, LOCALE_AUDIOMENU_VOLUME_ADJUST));

		CVolume::getInstance()->SetCurrentChannel(p_movie_info->epgId);
		CVolume::getInstance()->SetCurrentPid(currentapid);
		int percent[numpida];
		for (uint i=0; i < numpida; i++) {
			percent[i] = CZapit::getInstance()->GetPidVolume(p_movie_info->epgId, apids[i], ac3flags[i]);
			APIDSelector.addItem(new CMenuOptionNumberChooser(NONEXISTANT_LOCALE, &percent[i],
						currentapid == apids[i],
						0, 999, CVolume::getInstance(), 0, 0, NONEXISTANT_LOCALE,
						p_movie_info->audioPids[i].epgAudioPidName.c_str()));
		}
	}

	APIDSelector.exec(NULL, "");
	delete selector;
#ifdef MARTII
	dvbsub_set_stc_offset(g_settings.dvb_subtitle_delay * 90000);
	printf("CMoviePlayerGui::selectAudioPid: selected %d (%x) current %x\n", select, (select >= 0) ? apids[select] : -1, currentapid);
#else
	printf("CMoviePlayerGui::selectAudioPid: selected %d (%x) current %x\n", select, (select >= 0) ? apids[select] : -1, currentapid);
#endif
#ifdef MARTII
	if(SubtitleChanger.actionKey.substr(0, 3) == "TTX")
		currentttxsub = SubtitleChanger.actionKey;
	else if(SubtitleChanger.actionKey == "off")
		currentttxsub = "";
	if((select >= 0) && (select <= numpida) && (currentapid != apids[select])) {
#else
	if((select >= 0) && (currentapid != apids[select])) {
#endif
		currentapid = apids[select];
		currentac3 = ac3flags[select];
		playback->SetAPid(currentapid, currentac3);
		printf("[movieplayer] apid changed to %d type %d\n", currentapid, currentac3);
	}
}

void CMoviePlayerGui::handleMovieBrowser(neutrino_msg_t msg, int /*position*/)
{
	CMovieInfo cMovieInfo;	// funktions to save and load movie info

	static int jump_not_until = 0;	// any jump shall be avoided until this time (in seconds from moviestart)
	static MI_BOOKMARK new_bookmark;	// used for new movie info bookmarks created from the movieplayer

	static int width = 280;
	static int height = 65;

	static int x = frameBuffer->getScreenX() + (frameBuffer->getScreenWidth() - width) / 2;
	static int y = frameBuffer->getScreenY() + frameBuffer->getScreenHeight() - height - 20;

	static CBox boxposition(x, y, width, height);	// window position for the hint boxes

	static CTextBox endHintBox(g_Locale->getText(LOCALE_MOVIEBROWSER_HINT_MOVIEEND), NULL, CTextBox::CENTER /*CTextBox::AUTO_WIDTH | CTextBox::AUTO_HIGH */ , &boxposition);
	static CTextBox comHintBox(g_Locale->getText(LOCALE_MOVIEBROWSER_HINT_JUMPFORWARD), NULL, CTextBox::CENTER /*CTextBox::AUTO_WIDTH | CTextBox::AUTO_HIGH */ , &boxposition);
	static CTextBox loopHintBox(g_Locale->getText(LOCALE_MOVIEBROWSER_HINT_JUMPBACKWARD), NULL, CTextBox::CENTER /*CTextBox::AUTO_WIDTH | CTextBox::AUTO_HIGH */ , &boxposition);
	static CTextBox newLoopHintBox(g_Locale->getText(LOCALE_MOVIEBROWSER_HINT_NEWBOOK_BACKWARD), NULL, CTextBox::CENTER /*CTextBox::AUTO_WIDTH | CTextBox::AUTO_HIGH */ , &boxposition);
	static CTextBox newComHintBox(g_Locale->getText(LOCALE_MOVIEBROWSER_HINT_NEWBOOK_FORWARD), NULL, CTextBox::CENTER /*CTextBox::AUTO_WIDTH | CTextBox::AUTO_HIGH */ , &boxposition);

	static bool showEndHintBox = false;	// flag to check whether the box shall be painted
	static bool showComHintBox = false;	// flag to check whether the box shall be painted
	static bool showLoopHintBox = false;	// flag to check whether the box shall be painted

	int play_sec = position / 1000;	// get current seconds from moviestart
	if(msg == CRCInput::RC_nokey) {
		printf("CMoviePlayerGui::handleMovieBrowser: reset vars\n");
		// reset statics
		jump_not_until = 0;
		showEndHintBox = showComHintBox = showLoopHintBox = false;
		new_bookmark.pos = 0;
		// move in case osd position changed
		int newx = frameBuffer->getScreenX() + (frameBuffer->getScreenWidth() - width) / 2;
		int newy = frameBuffer->getScreenY() + frameBuffer->getScreenHeight() - height - 20;
		endHintBox.movePosition(newx, newy);
		comHintBox.movePosition(newx, newy);
		loopHintBox.movePosition(newx, newy);
		newLoopHintBox.movePosition(newx, newy);
		newComHintBox.movePosition(newx, newy);
		return;
	}
	else if (msg == (neutrino_msg_t) g_settings.mpkey_stop) {
		// if we have a movie information, try to save the stop position
		printf("CMoviePlayerGui::handleMovieBrowser: stop, isMovieBrowser %d p_movie_info %p\n", isMovieBrowser, p_movie_info);
		if (isMovieBrowser && p_movie_info) {
			timeb current_time;
			ftime(&current_time);
			p_movie_info->dateOfLastPlay = current_time.time;
			current_time.time = time(NULL);
			p_movie_info->bookmarks.lastPlayStop = position / 1000;
			if (!isWebTV)
				cMovieInfo.saveMovieInfo(*p_movie_info);
			//p_movie_info->fileInfoStale(); //TODO: we might to tell the Moviebrowser that the movie info has changed, but this could cause long reload times  when reentering the Moviebrowser
		}
	}
	else if((msg == 0) && isMovieBrowser && (playstate == CMoviePlayerGui::PLAY) && p_movie_info) {
		if (play_sec + 10 < jump_not_until || play_sec > jump_not_until + 10)
			jump_not_until = 0;	// check if !jump is stale (e.g. if user jumped forward or backward)

		// do bookmark activities only, if there is no new bookmark started
		if (new_bookmark.pos != 0)
			return;
#ifdef DEBUG
		//printf("CMoviePlayerGui::handleMovieBrowser: process bookmarks\n");
#endif
		if (p_movie_info->bookmarks.end != 0) {
			// *********** Check for stop position *******************************
			if (play_sec >= p_movie_info->bookmarks.end - MOVIE_HINT_BOX_TIMER && play_sec < p_movie_info->bookmarks.end && play_sec > jump_not_until) {
				if (showEndHintBox == false) {
					endHintBox.paint();	// we are 5 sec before the end postition, show warning
					showEndHintBox = true;
					TRACE("[mp]  user stop in 5 sec...\r\n");
				}
			} else {
				if (showEndHintBox == true) {
					endHintBox.hide();	// if we showed the warning before, hide the box again
					showEndHintBox = false;
				}
			}

			if (play_sec >= p_movie_info->bookmarks.end && play_sec <= p_movie_info->bookmarks.end + 2 && play_sec > jump_not_until)	// stop playing
			{
				// *********** we ARE close behind the stop position, stop playing *******************************
				TRACE("[mp]  user stop: play_sec %d bookmarks.end %d jump_not_until %d\n", play_sec, p_movie_info->bookmarks.end, jump_not_until);
				playstate = CMoviePlayerGui::STOPPED;
				return;
			}
		}
		// *************  Check for bookmark jumps *******************************
		showLoopHintBox = false;
		showComHintBox = false;
		for (int book_nr = 0; book_nr < MI_MOVIE_BOOK_USER_MAX; book_nr++) {
			if (p_movie_info->bookmarks.user[book_nr].pos != 0 && p_movie_info->bookmarks.user[book_nr].length != 0) {
				// valid bookmark found, now check if we are close before or after it
				if (play_sec >= p_movie_info->bookmarks.user[book_nr].pos - MOVIE_HINT_BOX_TIMER && play_sec < p_movie_info->bookmarks.user[book_nr].pos && play_sec > jump_not_until) {
					if (p_movie_info->bookmarks.user[book_nr].length < 0)
						showLoopHintBox = true;	// we are 5 sec before , show warning
					else if (p_movie_info->bookmarks.user[book_nr].length > 0)
						showComHintBox = true;	// we are 5 sec before, show warning
					//else  // TODO should we show a plain bookmark infomation as well?
				}

				if (play_sec >= p_movie_info->bookmarks.user[book_nr].pos && play_sec <= p_movie_info->bookmarks.user[book_nr].pos + 2 && play_sec > jump_not_until)	//
				{
					//for plain bookmark, the following calc shall result in 0 (no jump)
					int jumpseconds = p_movie_info->bookmarks.user[book_nr].length;

					// we are close behind the bookmark, do bookmark activity (if any)
					if (p_movie_info->bookmarks.user[book_nr].length < 0) {
						// if the jump back time is to less, it does sometimes cause problems (it does probably jump only 5 sec which will cause the next jump, and so on)
						if (jumpseconds > -15)
							jumpseconds = -15;

						playback->SetPosition(jumpseconds * 1000);
					} else if (p_movie_info->bookmarks.user[book_nr].length > 0) {
						// jump at least 15 seconds
						if (jumpseconds < 15)
							jumpseconds = 15;

						playback->SetPosition(jumpseconds * 1000);
					}
					TRACE("[mp]  do jump %d sec\r\n", jumpseconds);
					break;	// do no further bookmark checks
				}
			}
		}
		// check if we shall show the commercial warning
		if (showComHintBox == true) {
			comHintBox.paint();
			TRACE("[mp]  com jump in 5 sec...\r\n");
		} else
			comHintBox.hide();

		// check if we shall show the loop warning
		if (showLoopHintBox == true) {
			loopHintBox.paint();
			TRACE("[mp]  loop jump in 5 sec...\r\n");
		} else
			loopHintBox.hide();

		return;
	} else if (msg == CRCInput::RC_0) {	// cancel bookmark jump
		printf("CMoviePlayerGui::handleMovieBrowser: CRCInput::RC_0\n");
		if (isMovieBrowser == true) {
			if (new_bookmark.pos != 0) {
				new_bookmark.pos = 0;	// stop current bookmark activity, TODO:  might bemoved to another key
				newLoopHintBox.hide();	// hide hint box if any
				newComHintBox.hide();
			}
			comHintBox.hide();
			loopHintBox.hide();
			jump_not_until = (position / 1000) + 10; // avoid bookmark jumping for the next 10 seconds, , TODO:  might be moved to another key
		}
		return;
	}
	else if (msg == (neutrino_msg_t) g_settings.mpkey_bookmark && !isWebTV) {
		if (newComHintBox.isPainted() == true) {
			// yes, let's get the end pos of the jump forward
			new_bookmark.length = play_sec - new_bookmark.pos;
			TRACE("[mp] commercial length: %d\r\n", new_bookmark.length);
			if (cMovieInfo.addNewBookmark(p_movie_info, new_bookmark) == true) {
				cMovieInfo.saveMovieInfo(*p_movie_info);	/* save immediately in xml file */
			}
			new_bookmark.pos = 0;	// clear again, since this is used as flag for bookmark activity
			newComHintBox.hide();
		} else if (newLoopHintBox.isPainted() == true) {
			// yes, let's get the end pos of the jump backward
			new_bookmark.length = new_bookmark.pos - play_sec;
			new_bookmark.pos = play_sec;
			TRACE("[mp] loop length: %d\r\n", new_bookmark.length);
			if (cMovieInfo.addNewBookmark(p_movie_info, new_bookmark) == true) {
				cMovieInfo.saveMovieInfo(*p_movie_info);	/* save immediately in xml file */
				jump_not_until = play_sec + 5;	// avoid jumping for this time
			}
			new_bookmark.pos = 0;	// clear again, since this is used as flag for bookmark activity
			newLoopHintBox.hide();
		} else {
			// very dirty usage of the menue, but it works and I already spent to much time with it, feel free to make it better ;-)
#define BOOKMARK_START_MENU_MAX_ITEMS 6
			CSelectedMenu cSelectedMenuBookStart[BOOKMARK_START_MENU_MAX_ITEMS];

			CMenuWidget bookStartMenu(LOCALE_MOVIEBROWSER_BOOK_ADD, NEUTRINO_ICON_STREAMING);
			bookStartMenu.addIntroItems();
#if 0 // not supported, TODO
			bookStartMenu.addItem(new CMenuForwarder(LOCALE_MOVIEPLAYER_HEAD, !isMovieBrowser, NULL, &cSelectedMenuBookStart[0]));
			bookStartMenu.addItem(GenericMenuSeparatorLine);
#endif
			bookStartMenu.addItem(new CMenuForwarder(LOCALE_MOVIEBROWSER_BOOK_NEW, isMovieBrowser, NULL, &cSelectedMenuBookStart[1]));
			bookStartMenu.addItem(new CMenuForwarder(LOCALE_MOVIEBROWSER_BOOK_TYPE_FORWARD, isMovieBrowser, NULL, &cSelectedMenuBookStart[2]));
			bookStartMenu.addItem(new CMenuForwarder(LOCALE_MOVIEBROWSER_BOOK_TYPE_BACKWARD, isMovieBrowser, NULL, &cSelectedMenuBookStart[3]));
			bookStartMenu.addItem(new CMenuForwarder(LOCALE_MOVIEBROWSER_BOOK_MOVIESTART, isMovieBrowser, NULL, &cSelectedMenuBookStart[4]));
			bookStartMenu.addItem(new CMenuForwarder(LOCALE_MOVIEBROWSER_BOOK_MOVIEEND, isMovieBrowser, NULL, &cSelectedMenuBookStart[5]));

			// no, nothing else to do, we open a new bookmark menu
			new_bookmark.name = "";	// use default name
			new_bookmark.pos = 0;
			new_bookmark.length = 0;

			// next seems return menu_return::RETURN_EXIT, if something selected
			bookStartMenu.exec(NULL, "none");
#if 0 // not supported, TODO
			if (cSelectedMenuBookStart[0].selected == true) {
				/* Movieplayer bookmark */
				if (bookmarkmanager->getBookmarkCount() < bookmarkmanager->getMaxBookmarkCount()) {
					char timerstring[200];
					sprintf(timerstring, "%lld", play_sec);
					std::string bookmarktime = timerstring;
					fprintf(stderr, "fileposition: %lld timerstring: %s bookmarktime: %s\n", play_sec, timerstring, bookmarktime.c_str());
					bookmarkmanager->createBookmark(full_name, bookmarktime);
				} else {
					fprintf(stderr, "too many bookmarks\n");
					DisplayErrorMessage(g_Locale->getText(LOCALE_MOVIEPLAYER_TOOMANYBOOKMARKS));	// UTF-8
				}
				cSelectedMenuBookStart[0].selected = false;	// clear for next bookmark menu
			} else
#endif
			if (cSelectedMenuBookStart[1].selected == true) {
				/* Moviebrowser plain bookmark */
				new_bookmark.pos = play_sec;
				new_bookmark.length = 0;
				if (!isWebTV && cMovieInfo.addNewBookmark(p_movie_info, new_bookmark) == true)
					cMovieInfo.saveMovieInfo(*p_movie_info);	/* save immediately in xml file */
				new_bookmark.pos = 0;	// clear again, since this is used as flag for bookmark activity
				cSelectedMenuBookStart[1].selected = false;	// clear for next bookmark menu
			} else if (cSelectedMenuBookStart[2].selected == true) {
				/* Moviebrowser jump forward bookmark */
				new_bookmark.pos = play_sec;
				TRACE("[mp] new bookmark 1. pos: %d\r\n", new_bookmark.pos);
				newComHintBox.paint();
				cSelectedMenuBookStart[2].selected = false;	// clear for next bookmark menu
			} else if (cSelectedMenuBookStart[3].selected == true) {
				/* Moviebrowser jump backward bookmark */
				new_bookmark.pos = play_sec;
				TRACE("[mp] new bookmark 1. pos: %d\r\n", new_bookmark.pos);
				newLoopHintBox.paint();
				cSelectedMenuBookStart[3].selected = false;	// clear for next bookmark menu
			} else if (!isWebTV && cSelectedMenuBookStart[4].selected == true) {
				/* Moviebrowser movie start bookmark */
				p_movie_info->bookmarks.start = play_sec;
				TRACE("[mp] New movie start pos: %d\r\n", p_movie_info->bookmarks.start);
				cMovieInfo.saveMovieInfo(*p_movie_info);	/* save immediately in xml file */
				cSelectedMenuBookStart[4].selected = false;	// clear for next bookmark menu
			} else if (!isWebTV && cSelectedMenuBookStart[5].selected == true) {
				/* Moviebrowser movie end bookmark */
				p_movie_info->bookmarks.end = play_sec;
				TRACE("[mp]  New movie end pos: %d\r\n", p_movie_info->bookmarks.start);
				cMovieInfo.saveMovieInfo(*p_movie_info);	/* save immediately in xml file */
				cSelectedMenuBookStart[5].selected = false;	// clear for next bookmark menu
			}
		}
	}
	return;
}

void CMoviePlayerGui::UpdatePosition()
{
	if(playback->GetPosition(position, duration)) {
		if(duration > 100)
			file_prozent = (unsigned char) (position / (duration / 100));
		FileTime.update(position, duration);
#ifdef DEBUG
		printf("CMoviePlayerGui::PlayFile: speed %d position %d duration %d (%d, %d%%)\n", speed, position, duration, duration-position, file_prozent);
#endif
	}
}

void CMoviePlayerGui::showHelpTS()
{
	Helpbox helpbox;
	helpbox.addLine(NEUTRINO_ICON_BUTTON_RED, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP1));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_GREEN, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP2));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_YELLOW, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP3));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_BLUE, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP4));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_MENU, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP5));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_1, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP6));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_3, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP7));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_4, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP8));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_6, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP9));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_7, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP10));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_9, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP11));
	helpbox.addLine(g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP12));
	helpbox.show(LOCALE_MESSAGEBOX_INFO);
}

#ifdef MARTII
#ifdef ENABLE_GRAPHLCD
void CMoviePlayerGui::StopSubtitles(bool b)
#else
void CMoviePlayerGui::StopSubtitles(bool)
#endif
{
	printf("[neutrino] %s\n", __FUNCTION__);
	int ttx, ttxpid, ttxpage;
	int dvbpid;

	dvbpid = dvbsub_getpid();
	tuxtx_subtitle_running(&ttxpid, &ttxpage, &ttx);
	if(dvbpid)
		dvbsub_pause();
	playback->SuspendSubtitle(true);
	if(ttx) {
		tuxtx_pause_subtitle(true);
		frameBuffer->paintBackground();
	}
#ifdef ENABLE_GRAPHLCD // MARTII
	if (b)
		nGLCD::MirrorOSD();
#endif
}

void CMoviePlayerGui::StartSubtitles(bool show)
{
	printf("%s: %s\n", __FUNCTION__, show ? "Show" : "Not show");
#ifdef ENABLE_GRAPHLCD // MARTII
	nGLCD::MirrorOSD(false);
#endif
	if(!show)
		return;
	playback->SuspendSubtitle(false);
	dvbsub_start(0, true);
	tuxtx_pause_subtitle(false);
}
#endif

#if 0
void CMoviePlayerGui::selectChapter()
{
	if (!is_file_player)
		return;

	std::vector<int> positions; std::vector<std::string> titles;
	playback->GetChapters(positions, titles);
	if (positions.empty())
		return;

	CMenuWidget ChSelector(LOCALE_MOVIEBROWSER_MENU_MAIN_BOOKMARKS, NEUTRINO_ICON_AUDIO);
	ChSelector.addIntroItems();

	int select = -1;
	CMenuSelectorTarget * selector = new CMenuSelectorTarget(&select);
	char cnt[5];
	for (unsigned i = 0; i < positions.size(); i++) {
		sprintf(cnt, "%d", i);
		CMenuForwarderNonLocalized * item = new CMenuForwarderNonLocalized(titles[i].c_str(), true, NULL, selector, cnt, CRCInput::convertDigitToKey(i + 1));
		ChSelector.addItem(item, position > positions[i]);
	}
	ChSelector.exec(NULL, "");
	delete selector;
	printf("CMoviePlayerGui::selectChapter: selected %d (%d)\n", select, (select >= 0) ? positions[select] : -1);
	if(select >= 0)
		playback->SetPosition(positions[select], true);
}
#endif

#if 0
void CMoviePlayerGui::selectSubtitle()
{
	if (!is_file_player)
		return;

	CMenuWidget APIDSelector(LOCALE_SUBTITLES_HEAD, NEUTRINO_ICON_AUDIO);
	APIDSelector.addIntroItems();

	int select = -1;
	CMenuSelectorTarget * selector = new CMenuSelectorTarget(&select);
	if(!numsubs) {
		playback->FindAllSubs(spids, sub_supported, &numsubs, slanguage);
	}
	char cnt[5];
	unsigned int count;
	for (count = 0; count < numsubs; count++) {
		bool enabled = sub_supported[count];
		bool defpid = currentspid >= 0 ? (currentspid == spids[count]) : (count == 0);
		std::string title = slanguage[count];
		if (title.empty()) {
			char pidnumber[20];
			sprintf(pidnumber, "Stream %d %X", count + 1, spids[count]);
			title = pidnumber;
		}
		sprintf(cnt, "%d", count);
		CMenuForwarderNonLocalized * item = new CMenuForwarderNonLocalized(title.c_str(), enabled, NULL, selector, cnt, CRCInput::convertDigitToKey(count + 1));
		item->setItemButton(NEUTRINO_ICON_BUTTON_STOP, false);
		APIDSelector.addItem(item, defpid);
	}
	sprintf(cnt, "%d", count);
	APIDSelector.addItem(new CMenuForwarder(LOCALE_SUBTITLES_STOP, true, NULL, selector, cnt, CRCInput::RC_stop), currentspid < 0);

	APIDSelector.exec(NULL, "");
	delete selector;
	printf("CMoviePlayerGui::selectSubtitle: selected %d (%x) current %x\n", select, (select >= 0) ? spids[select] : -1, currentspid);
	if((select >= 0) && (select < numsubs) && (currentspid != spids[select])) {
		currentspid = spids[select];
		playback->SelectSubtitles(currentspid);
		printf("[movieplayer] spid changed to %d\n", currentspid);
	} else if ( select > 0) {
		currentspid = -1;
		playback->SelectSubtitles(currentspid);
		printf("[movieplayer] spid changed to %d\n", currentspid);
	}
}

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

void CMoviePlayerGui::clearSubtitle()
{
	if ((max_x-min_x > 0) && (max_y-min_y > 0))
		frameBuffer->paintBackgroundBoxRel(min_x, min_y, max_x-min_x, max_y-min_y);

	min_x = CFrameBuffer::getInstance()->getScreenWidth();
	min_y = CFrameBuffer::getInstance()->getScreenHeight();
	max_x = CFrameBuffer::getInstance()->getScreenX();
	max_y = CFrameBuffer::getInstance()->getScreenY();
	end_time = 0;
}

fb_pixel_t * simple_resize32(uint8_t * orgin, uint32_t * colors, int nb_colors, int ox, int oy, int dx, int dy);

void CMoviePlayerGui::showSubtitle(neutrino_msg_data_t data)
{
	if (!data) {
		if (end_time && time_monotonic_ms() > end_time) {
			printf("************************* hide subs *************************\n");
			clearSubtitle();
		}
		return;
	}
	AVSubtitle * sub = (AVSubtitle *) data;

	printf("************************* EVT_SUBT_MESSAGE: num_rects %d fmt %d *************************\n",  sub->num_rects, sub->format);
	if (!sub->num_rects)
		return;

	if (sub->format == 0) {
		int xres = 0, yres = 0, framerate;
		videoDecoder->getPictureInfo(xres, yres, framerate);
		
		double xc = (double) CFrameBuffer::getInstance()->getScreenWidth(/*true*/)/(double) xres;
		double yc = (double) CFrameBuffer::getInstance()->getScreenHeight(/*true*/)/(double) yres;

		clearSubtitle();

		for (unsigned i = 0; i < sub->num_rects; i++) {
			uint32_t * colors = (uint32_t *) sub->rects[i]->pict.data[1];

			int nw = (double) sub->rects[i]->w * xc;
			int nh = (double) sub->rects[i]->h * yc;
			int xoff = (double) sub->rects[i]->x * xc;
			int yoff = (double) sub->rects[i]->y * yc;

			printf("Draw: #%d at %d,%d size %dx%d colors %d (x=%d y=%d w=%d h=%d) \n", i+1,
					sub->rects[i]->x, sub->rects[i]->y, sub->rects[i]->w, sub->rects[i]->h,
					sub->rects[i]->nb_colors, xoff, yoff, nw, nh);

			fb_pixel_t * newdata = simple_resize32 (sub->rects[i]->pict.data[0], colors,
					sub->rects[i]->nb_colors, sub->rects[i]->w, sub->rects[i]->h, nw, nh);
			frameBuffer->blit2FB(newdata, nw, nh, xoff, yoff);
			free(newdata);

			min_x = std::min(min_x, xoff);
			max_x = std::max(max_x, xoff + nw);
			min_y = std::min(min_y, yoff);
			max_y = std::max(max_y, yoff + nh);
		}
		end_time = sub->end_display_time + time_monotonic_ms();
		avsubtitle_free(sub);
		delete sub;
		return;
	}
	std::vector<std::string> subtext;
	for (unsigned i = 0; i < sub->num_rects; i++) {
		char * txt = NULL;
		if (sub->rects[i]->type == SUBTITLE_TEXT)
			txt = sub->rects[i]->text;
		else if (sub->rects[i]->type == SUBTITLE_ASS)
			txt = sub->rects[i]->ass;
		printf("subt[%d] type %d [%s]\n", i, sub->rects[i]->type, txt ? txt : "");
		if (txt) {
			int len = strlen(txt);
			if (len > 10 && memcmp(txt, "Dialogue: ", 10) == 0) {
				char* p = txt;
				int skip_commas = 4;
				/* skip ass times */
				for (int j = 0; j < skip_commas && *p != '\0'; p++)
					if (*p == ',')
						j++;
				/* skip ass tags */
				if (*p == '{') {
					char * d = strchr(p, '}');
					if (d)
						p += d - p + 1;
				}
				char * d = strchr(p, '{');
				if (d && strchr(d, '}'))
					*d = 0;

				len = strlen(p);
				/* remove newline */
				for (int j = len-1; j > 0; j--) {
					if (p[j] == '\n' || p[j] == '\r')
						p[j] = 0;
					else
						break;
				}
				if (*p == '\0')
					continue;
				txt = p;
			}
			//printf("title: [%s]\n", txt);
			std::string str(txt);
			unsigned int start = 0, end = 0;
			/* split string with \N as newline */
			std::string delim("\\N");
			while ((end = str.find(delim, start)) != string::npos) {
				subtext.push_back(str.substr(start, end - start));
				start = end + 2;
			}
			subtext.push_back(str.substr(start));

		}
	}
	for (unsigned i = 0; i < subtext.size(); i++)
		printf("subtext %d: [%s]\n", i, subtext[i].c_str());
	printf("********************************************************************\n");

	if (!subtext.empty()) {
		int sh = frameBuffer->getScreenHeight();
		int sw = frameBuffer->getScreenWidth();
		int h = g_Font[SNeutrinoSettings::FONT_TYPE_MENU]->getHeight();
		int height = h*subtext.size();

		clearSubtitle();

		int x[subtext.size()];
		int y[subtext.size()];
		for (unsigned i = 0; i < subtext.size(); i++) {
			int w = g_Font[SNeutrinoSettings::FONT_TYPE_MENU]->getRenderWidth (subtext[i].c_str(), true);
			x[i] = (sw - w) / 2;
			y[i] = sh - height + h*(i + 1);
			min_x = std::min(min_x, x[i]);
			max_x = std::max(max_x, x[i]+w);
			min_y = std::min(min_y, y[i]-h);
			max_y = std::max(max_y, y[i]);
		}

		frameBuffer->paintBoxRel(min_x, min_y, max_x - min_x, max_y-min_y, COL_MENUCONTENT_PLUS_0);

		for (unsigned i = 0; i < subtext.size(); i++)
			g_Font[SNeutrinoSettings::FONT_TYPE_MENU]->RenderString(x[i], y[i], sw, subtext[i].c_str(), COL_MENUCONTENT, 0, true);

		end_time = sub->end_display_time + time_monotonic_ms();
	}
	avsubtitle_free(sub);
	delete sub;
}
#endif
