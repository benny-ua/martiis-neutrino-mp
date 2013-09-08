/*
	Neutrino ShairPlay class

	Copyright (C) 2013 martii

	Portions based on shairplay.c by Juho Vähä-Herttua. See below for the
	original copyright notice.

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

/**
 *  Copyright (C) 2012-2013  Juho Vähä-Herttua
 *
 *  Permission is hereby granted, free of charge, to any person obtaining
 *  a copy of this software and associated documentation files (the
 *  "Software"), to deal in the Software without restriction, including
 *  without limitation the rights to use, copy, modify, merge, publish,
 *  distribute, sublicense, and/or sell copies of the Software, and to
 *  permit persons to whom the Software is furnished to do so, subject to
 *  the following conditions:
 *  
 *  The above copyright notice and this permission notice shall be included
 *  in all copies or substantial portions of the Software.
 *  
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 *  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 *  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 *  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <gui/shairplay.h>
#include <math.h>
#include <linux/dvb/audio.h>
#include <global.h>
#include <neutrino.h>
#include <audio.h>
#include <video.h>
#include <libnet.h>
#include <zapit/zapit.h>
#include <system/set_threadname.h>
#include <system/locals.h>
#include <system/settings.h>
#include <libmd5sum/libmd5sum.h>
#include <driver/fontrenderer.h>
#include <shairplay/dnssd.h>
#include <shairplay/raop.h>

extern cAudio * audioDecoder;
extern cVideo * videoDecoder;

#define COVERART "/tmp/coverart.jpg"
#define COVERART_M2V "/tmp/coverart.m2v"

CShairPlay::CShairPlay(bool *_enabled, bool *_active)
{
	volume = 1.0f;
	threadId = 0;
	audioThreadId = 0;
	enabled = _enabled;
	active = _active;
	gotCoverArt = false;
	playing = false;
	firstAudioPacket = true;
	secTimer = 0;
	coverArtTimer = 0;
	memset(last_md5sum, 0, 16);
	queuedFramesCount = 0;

	std::string interface = "eth0";
	netGetMacAddr(interface, (unsigned char *) hwaddr);
	if (!memcmp(hwaddr, "\0\0\0\0\0", 6)) {
		interface = "wlan0";
		netGetMacAddr(interface, (unsigned char *) hwaddr);
	}
	if (!memcmp(hwaddr, "\0\0\0\0\0", 6)) {
		interface = "ra0";
		netGetMacAddr(interface, (unsigned char *) hwaddr);
	}
	if (!memcmp(hwaddr, "\0\0\0\0\0", 6)) {
		hwaddr[0] = 0x48;
		hwaddr[1] = 0x5d;
		hwaddr[2] = 0x60;
		hwaddr[3] = 0x7c;
		hwaddr[4] = 0xee;
		hwaddr[5] = 0x22;
	}

	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK_NP);
	pthread_mutex_init(&videoMutex, &attr);
	pthread_mutex_init(&audioMutex, &attr);

	sem_init(&sem, 0, 0);
	sem_init(&audioSem, 0, 0);

	restart();
}

void CShairPlay::restart(void)
{
	if (!threadId) {
		if (pthread_create(&threadId, NULL, run, this))
			threadId = 0;
	}
}

CShairPlay::~CShairPlay(void)
{
	audio_flush(this, NULL);
	initialized = false;
	*active = false;
	sem_post(&sem);
	sem_post(&audioSem);
	while (threadId && audioThreadId)
		usleep(100000);
	g_RCInput->killTimer(secTimer);
	g_RCInput->killTimer(coverArtTimer);
	sem_destroy(&sem);
	sem_destroy(&audioSem);
	unlink(COVERART);
}

void *
CShairPlay::audio_init(void *_this, int _bits, int _channels, int _samplerate)
{
	CShairPlay *T = (CShairPlay *) _this;
	T->bits = _bits;
	T->channels = _channels;
	T->samplerate = _samplerate;
	T->volume = 1.0f;
	T->initialized = false;
	T->lock(&T->audioMutex);
	while (!T->audioQueue.empty()) {
		std::list<audioQueueStruct *>::iterator it = T->audioQueue.begin();
		free(*it);
		T->audioQueue.pop_front();
	}
	T->queuedFramesCount = 0;
	while(!sem_trywait(&T->audioSem));
	T->unlock(&T->audioMutex);
	return NULL;
}

void
CShairPlay::audio_flush(void *_this, void *)
{
	CShairPlay *T = (CShairPlay *) _this;
	T->playing = false;
	T->lock(&T->audioMutex);
	while (!T->audioQueue.empty()) {
		std::list<audioQueueStruct *>::iterator it = T->audioQueue.begin();
		free(*it);
		T->audioQueue.pop_front();
	}
	T->queuedFramesCount = 0;
	while(!sem_trywait(&T->audioSem));
	T->unlock(&T->audioMutex);
	sem_post(&T->audioSem);
	while (T->audioThreadId)
		usleep(100000);
}

void *
CShairPlay::audioThread(void *_this)
{
	set_threadname("CShairPlay::audioThread");

	CShairPlay *T = (CShairPlay *) _this;

#if __BYTE_ORDER == __LITTLE_ENDIAN
	audioDecoder->PrepareClipPlay(T->channels, T->samplerate, T->bits, 1);
#else
	audioDecoder->PrepareClipPlay(T->channels, T->samplerate, T->bits, 0);
#endif
	int running = true;

	while (running) {
		sem_wait(&T->audioSem);
		T->lock(&T->audioMutex);
		if (T->audioQueue.empty()) {
			T->unlock(&T->audioMutex);
			break;
		}
		audioQueueStruct *q = T->audioQueue.front();
		T->audioQueue.pop_front();
		T->unlock(&T->audioMutex);

		for (int i=0; i<q->len/2; i++)
			q->buf[i] = q->buf[i] * T->volume;

		if (T->playing && audioDecoder->WriteClip((unsigned char *)q->buf, q->len) != q->len) // probably shutting down
			running = false;
		free(q);
	}
	audioDecoder->StopClip();
	T->audioThreadId = 0;
	pthread_exit(NULL);
}

void
CShairPlay::audio_process(void *_this, void *, const void *_buffer, int _buflen)
{
	CShairPlay *T = (CShairPlay *) _this;
	*T->active = true;

	if (!T->initialized) {
		if (T->firstAudioPacket) {
			g_RCInput->postMsg(NeutrinoMessages::SHOW_INFOBAR, 0); // trigger exec in main loop
			T->firstAudioPacket = false;
		}
		return;
	}

	if (!T->playing)
		return;

	audioQueueStruct *q = (audioQueueStruct *) malloc(sizeof(audioQueueStruct) + _buflen - sizeof(short));
	if (q) {
		q->len = _buflen;
		memcpy(q->buf, _buffer, _buflen);
		T->lock(&T->audioMutex);
		if (*T->active) {
			T->audioQueue.push_back(q);
			sem_post(&T->audioSem);
			T->queuedFramesCount++;
			if (T->queuedFramesCount >= g_settings.shairplay_minqueue && !T->audioThreadId) {
				if (!pthread_create(&T->audioThreadId, NULL, audioThread, T))
					pthread_detach(T->audioThreadId);
				else
					T->audioThreadId = 0;
			}
		} else
			free(q);
		T->unlock(&T->audioMutex);
	}
}

void
CShairPlay::audio_destroy(void *_this, void *)
{
	CShairPlay *T = (CShairPlay *) _this;
	T->initialized = false;
}

void
CShairPlay::audio_set_volume(void *_this, void *, float volume)
{
	CShairPlay *T = (CShairPlay *) _this;
	T->volume = pow(10.0, 0.05*volume);
}

void
CShairPlay::parseDAAP(const void *_buffer, int _buflen)
{
	char *b = (char *) _buffer;
	char *e = b + _buflen;
	while (b + 8 < e) {
		char *id = b;
		b += 4;
		size_t len = *b++;
		len <<= 8;
		len |= *b++;
		len <<= 8;
		len |= *b++;
		len <<= 8;
		len |= *b++;
		if (!len)
			continue;
		if (b + len > e)
			return;
		if (!strncmp(id, "mlit", 4))
			parseDAAP(b, len);
		else if (!strncmp(id, "minm", 4))
			title = std::string(b, len);
		else if (!strncmp(id, "asal", 4))
			album = std::string(b, len);
		else if (!strncmp(id, "asar", 4))	
			artist = std::string(b, len);
		else if (!strncmp(id, "ascm", 4))
			comment = std::string(b, len);
		else if (!strncmp(id, "ascp", 4))
			composer = std::string(b, len);
		else if (!strncmp(id, "asgn", 4))
			genre = std::string(b, len);
		else if (!strncmp(id, "asdt", 4))
			description = std::string(b, len);
		else if (!strncmp(id, "asyr", 4)) {
			int i = 0;
			while (len > 0) {
				i <<= 8;
				i |= *b++;
				len--;
			}
		}
		b += len;
	}
}

void
CShairPlay::audio_set_metadata(void *_this, void *, const void *_buffer, int _buflen)
{
	CShairPlay *T = (CShairPlay *) _this;
	T->title = "";
	T->album = "";
	T->artist = "";
	T->comment = "";
	T->composer = "";
	T->genre = "";
	T->description = "";
	T->year = "";
	T->parseDAAP(_buffer, _buflen);
	if (T->initialized)
		T->showInfoViewer(true);
	T->gotCoverArt = false;
	if (!T->coverArtTimer)
		T->coverArtTimer = g_RCInput->addTimer(5000000, false);
}

void
CShairPlay::audio_set_coverart(void *_this, void *, const void *_buffer, int _buflen)
{
	CShairPlay *T = (CShairPlay *) _this;

	int orig = _buflen;
	unlink(COVERART);
	unlink(COVERART_M2V);
	FILE *file = fopen(COVERART, "wb");
	if (file) {
		g_RCInput->killTimer(T->coverArtTimer);

		while (_buflen > 0)
			_buflen -= fwrite((unsigned char *)_buffer+orig-_buflen, 1, _buflen, file);
		fclose(file);
		T->gotCoverArt = true;

		unsigned char md5sum[16];
		md5_file(COVERART, 1, md5sum);
		if (T->initialized && memcmp(md5sum, T->last_md5sum, sizeof(md5sum)))
			T->showCoverArt();
		memcpy(T->last_md5sum, md5sum, sizeof(md5sum));
	}
}

void *CShairPlay::showPicThread (void *_this)
{
	set_threadname("CShairPlay::showPic");

	CShairPlay *T = (CShairPlay *) _this;
	unlink(COVERART_M2V);
	T->lock(&T->videoMutex);
	if (*T->active)
		videoDecoder->ShowPicture(COVERART, COVERART_M2V);
	T->unlock(&T->videoMutex);
	pthread_exit(NULL);
}

void
CShairPlay::showCoverArt(void)
{
	showingCoverArt = true;
	pthread_t p;
	if (!pthread_create(&p, NULL, showPicThread, this))
		pthread_detach(p);
}

void
CShairPlay::hideCoverArt(void)
{
	memset(last_md5sum, 0, sizeof(last_md5sum));
	if (!gotCoverArt && showingCoverArt) {
		showingCoverArt = false;
		lock(&videoMutex);
		if (*active)
			videoDecoder->ShowPicture(DATADIR "/neutrino/icons/mp3.jpg");
		unlock(&videoMutex);
	}
}

void
CShairPlay::showInfoViewer(bool startCountDown)
{
	if (startCountDown) {
		infoViewerPeriod = g_settings.timing[SNeutrinoSettings::TIMING_INFOBAR];
		if (!infoViewerPeriod)
			infoViewerPeriod = 0xffff;
	}
	if (!secTimer)
		secTimer = g_RCInput->addTimer(1000000, false);
	g_InfoViewer->showShairPlay(album, artist, title, comment, composer, genre, description, year, dot);
	dot = !dot;
	CFrameBuffer::getInstance()->blit();
}

void
CShairPlay::hideInfoViewer(void)
{
	g_RCInput->killTimer(secTimer);
	g_InfoViewer->killTitle();
	CFrameBuffer::getInstance()->blit();
}

void *
CShairPlay::run(void* _this)
{
	set_threadname("CShairPlay::run");

	CShairPlay *T = (CShairPlay *) _this;

	dnssd_t *dnssd;
	raop_t *raop;
	raop_callbacks_t raop_cbs;

	memset(&raop_cbs, 0, sizeof(raop_cbs));
	raop_cbs.cls = T;
	raop_cbs.audio_init = audio_init;
	raop_cbs.audio_flush = audio_flush;
	raop_cbs.audio_process = audio_process;
	raop_cbs.audio_destroy = audio_destroy;
	raop_cbs.audio_set_volume = audio_set_volume;
	raop_cbs.audio_set_metadata = audio_set_metadata;
	raop_cbs.audio_set_coverart = audio_set_coverart;

	raop = raop_init_from_keyfile(10, &raop_cbs, "/share/shairplay/airport.key", NULL);
	if (raop == NULL) {
		fprintf(stderr, "Could not initialize the RAOP service\n");
		T->threadId = 0;
		pthread_exit(NULL);
	}

	raop_set_log_level(raop, RAOP_LOG_WARNING);

	short unsigned int port = g_settings.shairplay_port;
	raop_start(raop, &port, T->hwaddr, sizeof(T->hwaddr), g_settings.shairplay_password.empty() ? NULL : g_settings.shairplay_password.c_str());

	int error = 0;
	dnssd = dnssd_init(&error);
	if (error) {
		fprintf(stderr, "ERROR: Could not initialize dnssd library!\n");
		raop_destroy(raop);
		T->threadId = 0;
		pthread_exit(NULL);
	}

	dnssd_register_raop(dnssd, g_settings.shairplay_apname.c_str(), port, T->hwaddr, sizeof(T->hwaddr), 0);

	sem_wait(&T->sem);

	dnssd_unregister_raop(dnssd);
	dnssd_destroy(dnssd);

	raop_stop(raop);
	raop_destroy(raop);

	T->threadId = 0;
	pthread_exit(NULL);
}

void CShairPlay::lock(pthread_mutex_t *_m)
{
	pthread_mutex_lock(_m);
}

void CShairPlay::unlock(pthread_mutex_t *_m)
{
	pthread_mutex_unlock(_m);
}

void CShairPlay::exec(void)
{
	g_Zapit->lockPlayBack();

	showingCoverArt = false;
        videoDecoder->ShowPicture(DATADIR "/neutrino/icons/mp3.jpg");
        g_Sectionsd->setPauseScanning(true);

	if (!initialized) {
		lock(&audioMutex);
		while (!audioQueue.empty()) {
			std::list<audioQueueStruct *>::iterator it = audioQueue.begin();
			free(*it);
			audioQueue.pop_front();
		}
		queuedFramesCount = 0;
		while(!sem_trywait(&audioSem));
		unlock(&audioMutex);
		initialized = true;
		playing = true;
	}

	showInfoViewer(true);
	if (!access(COVERART, R_OK)) {
		g_RCInput->killTimer(coverArtTimer);
		md5_file(COVERART, 1, last_md5sum);
		showCoverArt();
	}

	while (*active && *enabled) {
		neutrino_msg_t msg;
		neutrino_msg_data_t data;
		g_RCInput->getMsg(&msg, &data, 50, false);
		switch(msg) {
			case CRCInput::RC_home:
				*enabled = false;
				*active = false;
				if (threadId) {
					sem_post(&sem);
					pthread_join(threadId, NULL);
					threadId = 0;
				}
				break;
			case CRCInput::RC_plus:
			case CRCInput::RC_minus:
			case CRCInput::RC_spkr:
				CNeutrinoApp::getInstance()->handleMsg(msg, data);
				break;
			case CRCInput::RC_ok:
			case CRCInput::RC_info:
				if (secTimer)
					hideInfoViewer();
				else
					showInfoViewer(true);
				break;
			case CRCInput::RC_timeout:
			{
				lock(&audioMutex);
				bool qe = audioQueue.empty();
				unlock(&audioMutex);
				if (qe)
					*active = false;
				break;
			}
			case CRCInput::RC_standby:
				*enabled = false;
				*active = false;
        			g_RCInput->postMsg(msg, 0 );
				break;
			case NeutrinoMessages::EVT_TIMER:
				if (data == secTimer) {
					infoViewerPeriod--;
					if (infoViewerPeriod < 1)
						hideInfoViewer();
					else
						showInfoViewer();
				} else if (data == coverArtTimer) {
					hideCoverArt();
					g_RCInput->killTimer(coverArtTimer);
				}
				break;
			default:
				;
		}
	}
	audio_flush(this, NULL);
	if (initialized) {
		initialized = false;
	}
	if (secTimer) {
		hideInfoViewer();
		secTimer = 0;
	}
	g_RCInput->killTimer(coverArtTimer);
	lock(&videoMutex);
        CNeutrinoApp::getInstance()->handleMsg(NeutrinoMessages::EVT_PROGRAMLOCKSTATUS, (neutrino_msg_data_t) 0x200);
        g_Sectionsd->setPauseScanning(false);
        videoDecoder->StopPicture();
	firstAudioPacket = true;
	unlock(&videoMutex);
	g_Zapit->Rezap();
}
