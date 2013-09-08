/*
	Neutrino ShairPlay class

	Copyright (C) 2013 martii

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

#ifndef __GUI_SHAIRPLAY_H__
#define __GUI_SHAIRPLAY_H__
#include "config.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <semaphore.h>
#include <pthread.h>
#include <string>
#include <list>

class CShairPlay
{
	private:
		char hwaddr[6];
		float volume;
		sem_t sem;
		sem_t audioSem;
		pthread_mutex_t videoMutex;
		pthread_mutex_t audioMutex;
		pthread_t threadId;
		pthread_t audioThreadId;
		int lastMode;
		bool *active;
		bool *enabled;
		bool playing;
		bool initialized;
		bool gotCoverArt;
		bool showingCoverArt;
		bool firstAudioPacket;
		bool dot;
		int infoViewerPeriod;
		uint32_t secTimer;
		uint32_t coverArtTimer;
		int bits;
		int channels;
		int samplerate;
		std::string title;
		std::string album;
		std::string artist;
		std::string comment;
		std::string composer;
		std::string genre;
		std::string description;
		std::string year;
		unsigned char last_md5sum[16];
		static void *audioThread(void *_this);
		static void *audio_init(void *_this, int bits, int channels, int samplerate);
		static void audio_flush(void *_this, void *);
		static void audio_process(void *_this, void *, const void *_buffer, int _buflen);
		static void audio_destroy(void *_this, void *);
		static void audio_set_volume(void *_this, void *, float volume);
		static void audio_set_metadata(void *_this, void *, const void *_buffer, int _buflen);
		static void audio_set_coverart(void *_this, void *, const void *_buffer, int _buflen);
		static void *run(void *);
		static void *showPicThread(void *);
		void parseDAAP(const void *_buffer, int _buflen);
		void showInfoViewer(bool startCountDown = false);
		void hideInfoViewer(void);
		void showCoverArt(void);
		void hideCoverArt(void);
		struct audioQueueStruct {
			int len;
			short buf[1];
		};
		std::list<audioQueueStruct *> audioQueue;
	public:
		CShairPlay(bool *_enabled, bool *_active);
		~CShairPlay(void);
		void exec(void);
		void lock(pthread_mutex_t *);
		void unlock(pthread_mutex_t *);
		void restart(void);
};
#endif
