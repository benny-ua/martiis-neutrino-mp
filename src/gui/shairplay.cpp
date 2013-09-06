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
	thread = 0;
	enabled = _enabled;
	active = _active;
	gotCoverArt = false;
	firstAudioPacket = true;
	secTimer = 0;
	coverArtTimer = 0;
	memset(last_md5sum, 0, 16);

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
	pthread_mutex_init(&mutex, &attr);

	sem_init(&sem, 0, 0);

	restart();

	if (!thread)
		sem_destroy(&sem);
}

void CShairPlay::restart(void)
{
	if (!thread) {
		if (pthread_create(&thread, NULL, run, this))
			thread = 0;
	}
}

CShairPlay::~CShairPlay(void)
{
	if (thread) {
		sem_post(&sem);
		pthread_join(thread, NULL);
	}
	g_RCInput->killTimer(secTimer);
	g_RCInput->killTimer(coverArtTimer);
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
	return NULL;
}

int
CShairPlay::audio_output(const void *_buffer, int _buflen)
{
	short *shortbuf = (short *)_buffer;

	for (int i=0; i<_buflen/2; i++)
		shortbuf[i] = shortbuf[i] * volume;

	if(audioDecoder->WriteClip((unsigned char *)_buffer, _buflen) != _buflen)
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, __func__);
	return _buflen;
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

	T->pcount++;

	int processed;

	if (T->buffering) {
		if (T->buflen+_buflen < (int)sizeof(T->buffer)) {
			memcpy(T->buffer+T->buflen, _buffer, _buflen);
			T->buflen += _buflen;
			return;
		}
		T->buffering = 0;

		processed = 0;
		while (processed < T->buflen)
			processed += T->audio_output(T->buffer+processed, T->buflen-processed);
		T->buflen = 0;
	}

	processed = 0;
	while (processed < _buflen)
		processed += T->audio_output((unsigned char *)_buffer+processed, _buflen-processed);
}

void
CShairPlay::audio_destroy(void *_this, void *)
{
	CShairPlay *T = (CShairPlay *) _this;
	if (T->initialized) {
		audioDecoder->StopClip();
		T->initialized = false;
	}
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
	T->lock();
	if (*T->active)
		videoDecoder->ShowPicture(COVERART, COVERART_M2V);
	T->unlock();
	pthread_exit(NULL);
}

void
CShairPlay::showCoverArt(void)
{
	pthread_t p;
	if (!pthread_create(&p, NULL, showPicThread, this))
		pthread_detach(p);
	showingCoverArt = true;
}

void
CShairPlay::hideCoverArt(void)
{
	memset(last_md5sum, 0, sizeof(last_md5sum));
	if (showingCoverArt) {
		showingCoverArt = false;
		lock();
		if (*active)
			videoDecoder->ShowPicture(DATADIR "/neutrino/icons/mp3.jpg");
		unlock();
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
	CShairPlay *T = (CShairPlay *) _this;
	set_threadname("CShairPlay::run");

	dnssd_t *dnssd;
	raop_t *raop;
	raop_callbacks_t raop_cbs;

	int error;

	memset(&raop_cbs, 0, sizeof(raop_cbs));
	raop_cbs.cls = T;
	raop_cbs.audio_init = audio_init;
	raop_cbs.audio_process = audio_process;
	raop_cbs.audio_destroy = audio_destroy;
	raop_cbs.audio_set_volume = audio_set_volume;
	raop_cbs.audio_set_metadata = audio_set_metadata;
	raop_cbs.audio_set_coverart = audio_set_coverart;

	raop = raop_init_from_keyfile(10, &raop_cbs, "/share/shairplay/airport.key", NULL);
	if (raop == NULL) {
		fprintf(stderr, "Could not initialize the RAOP service\n");
		pthread_exit(NULL);
	}

	raop_set_log_level(raop, RAOP_LOG_WARNING);

	short unsigned int port = g_settings.shairplay_port;
	raop_start(raop, &port, T->hwaddr, sizeof(T->hwaddr), g_settings.shairplay_password.empty() ? NULL : g_settings.shairplay_password.c_str());

	error = 0;
	dnssd = dnssd_init(&error);
	if (error) {
		fprintf(stderr, "ERROR: Could not initialize dnssd library!\n");
		raop_destroy(raop);
		pthread_exit(NULL);
	}

	dnssd_register_raop(dnssd, g_settings.shairplay_apname.c_str(), port, T->hwaddr, sizeof(T->hwaddr), 0);

	sem_wait(&T->sem);

	dnssd_unregister_raop(dnssd);
	dnssd_destroy(dnssd);

	raop_stop(raop);
	raop_destroy(raop);

	pthread_exit(NULL);
}

void CShairPlay::lock(void)
{
	pthread_mutex_lock(&mutex);
}

void CShairPlay::unlock(void)
{
	pthread_mutex_unlock(&mutex);
}

void CShairPlay::exec(void)
{
	g_Zapit->lockPlayBack();

	showingCoverArt = false;
        videoDecoder->ShowPicture(DATADIR "/neutrino/icons/mp3.jpg");
        g_Sectionsd->setPauseScanning(true);

	if (!initialized) {
		buffering = 1;
		buflen = 0;
#if __BYTE_ORDER == __LITTLE_ENDIAN
		audioDecoder->PrepareClipPlay(channels, samplerate, bits, 1);
#else
		audioDecoder->PrepareClipPlay(channels, samplerate, bits, 0);
#endif
		initialized = true;
	}

	showInfoViewer(true);
	if (!access(COVERART, R_OK)) {
		g_RCInput->killTimer(coverArtTimer);
		md5_file(COVERART, 1, last_md5sum);
		showCoverArt();
	}

	int pcount_old = pcount;
	while (*active && *enabled) {
		neutrino_msg_t msg;
		neutrino_msg_data_t data;
		g_RCInput->getMsg(&msg, &data, 50, false);
		switch(msg) {
			case CRCInput::RC_home:
				*enabled = false;
				if (thread) {
					sem_post(&sem);
					pthread_join(thread, NULL);
					thread = 0;
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
				if (pcount == pcount_old) // no data received for 5 seconds;
					*active = false;
				hideInfoViewer();
				break;
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
		pcount = pcount_old;
	}
	if (initialized) {
		audioDecoder->StopClip();
		initialized = false;
	}
	if (secTimer) {
		hideInfoViewer();
		secTimer = 0;
	}
	g_RCInput->killTimer(coverArtTimer);
	lock();
        CNeutrinoApp::getInstance()->handleMsg(NeutrinoMessages::EVT_PROGRAMLOCKSTATUS, (neutrino_msg_data_t) 0x200);
        g_Sectionsd->setPauseScanning(false);
        videoDecoder->StopPicture();
	firstAudioPacket = true;
	unlock();
	g_Zapit->Rezap();
}
