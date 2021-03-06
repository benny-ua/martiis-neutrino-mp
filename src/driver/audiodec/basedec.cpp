/*
	Neutrino-GUI  -   DBoxII-Project

	Copyright (C) 2004 Zwen
	base decoder class
	Homepage: http://www.cyberphoria.org/

	Kommentar:

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
#include <linux/soundcard.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <OpenThreads/ScopedLock>

#include <driver/audioplay.h> // for ShoutcastCallback()

#include <global.h>
#include <neutrino.h>
#include <zapit/client/zapittools.h>

#include "basedec.h"
#include "ffmpegdec.h"

#include <driver/netfile.h>

unsigned int CBaseDec::mSamplerate=0;
OpenThreads::Mutex CBaseDec::metaDataMutex;
std::map<const std::string,CAudiofile> CBaseDec::metaDataCache;

void ShoutcastCallback(void *arg)
{
	CAudioPlayer::getInstance()->sc_callback(arg);
}

CBaseDec::RetCode CBaseDec::DecoderBase(CAudiofile* const in,
										const int OutputFd, State* const state,
										time_t* const t,
										unsigned int* const secondsToSkip)
{
	RetCode Status = OK;

	FILE* fp = fopen( in->Filename.c_str(), "r" );
	if ( fp == NULL )
	{
		fprintf( stderr, "Error opening file %s for decoding.\n",
				 in->Filename.c_str() );
		Status = INTERNAL_ERR;
	}
	/* jump to first audio frame; audio_start_pos is only set for FILE_MP3 */
	else if ( in->MetaData.audio_start_pos &&
			  fseek( fp, in->MetaData.audio_start_pos, SEEK_SET ) == -1 )
	{
		fprintf( stderr, "fseek() failed.\n" );
		Status = INTERNAL_ERR;
	}

	if ( Status == OK )
	{
		CFile::FileType ft = in->FileType;
		if( in->FileType == CFile::STREAM_AUDIO )
		{
			if ( fstatus( fp, ShoutcastCallback ) < 0 )
				fprintf( stderr, "Error adding shoutcast callback: %s", err_txt );

			if (ftype(fp, "ogg"))
				ft = CFile::FILE_OGG;
			else if (ftype(fp, "mpeg"))
				ft = CFile::FILE_MP3;
			else
				ft = CFile::FILE_UNKNOWN;
		}
		else
		{
			struct stat st;
			if (!fstat(fileno(fp), &st))
						in->MetaData.filesize = st.st_size;

		}
		in->MetaData.type = ft;

		Status = CFfmpegDec::getInstance()->Decoder(fp, OutputFd, state, &in->MetaData, t, secondsToSkip );

		if ( fclose( fp ) == EOF )
		{
			fprintf( stderr, "Could not close file %s.\n", in->Filename.c_str() );
		}
	}

	return Status;
}

bool CBaseDec::LookupMetaData(CAudiofile* const in)
{
	bool res = false;
	metaDataMutex.lock();
	std::map<const std::string,CAudiofile>::const_iterator it = metaDataCache.find(in->Filename);
	if (it != metaDataCache.end()) {
		*in = it->second;
		res = true;
	}
	metaDataMutex.unlock();
	return res;
}

void CBaseDec::CacheMetaData(CAudiofile* const in)
{
	metaDataMutex.lock();
	// FIXME: This places a limit on the cache size. A LRU scheme would be more appropriate.
	if (metaDataCache.size() > 128)
		metaDataCache.clear();
	metaDataCache[in->Filename] = *in;
	metaDataMutex.unlock();
}

void CBaseDec::ClearMetaData()
{
	metaDataMutex.lock();
	metaDataCache.clear();
	metaDataMutex.unlock();
}

bool CBaseDec::GetMetaDataBase(CAudiofile* const in, const bool nice)
{
	if (in->FileType == CFile::STREAM_AUDIO)
		return true;

	if (LookupMetaData(in))
		return true;

	bool Status = true;
	FILE* fp = fopen( in->Filename.c_str(), "r" );
	if ( fp == NULL )
	{
		fprintf( stderr, "Error opening file %s for meta data reading.\n",
				 in->Filename.c_str() );
		Status = false;
	}
	else
	{
		struct stat st;
		if (!fstat(fileno(fp), &st))
			in->MetaData.filesize = st.st_size;
		in->MetaData.type = in->FileType;

		CFfmpegDec d;
		Status = d.GetMetaData(fp, nice, &in->MetaData);
		if (Status)
			CacheMetaData(in);
		if ( fclose( fp ) == EOF )
		{
			fprintf( stderr, "Could not close file %s.\n",
					 in->Filename.c_str() );
		}
	}
	return Status;
}

void CBaseDec::Init()
{
	mSamplerate=0;
}

// vim:ts=4
