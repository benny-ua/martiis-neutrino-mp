/*
        Copyright (C) 2013 CoolStream International Ltd
        Copyright (C) 2013 martii

        License: GPLv2

        This program is free software; you can redistribute it and/or modify
        it under the terms of the GNU General Public License as published by
        the Free Software Foundation;

        This program is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
        GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
        along with this program; if not, write to the Free Software
        Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef __NK_PARSER__
#define __NK_PARSER__

#include <curl/curl.h>
#include <curl/easy.h>

#include <vector>
#include <string>
#include <map>
#include <xmltree/xmlinterface.h>

#include <OpenThreads/Thread>
#include <OpenThreads/Condition>

struct sNKVideoInfo
{
	std::string id;
	std::string title;
	std::string description;// content
	std::string streaming;	// streaming path
	std::string thumbnail;	// thumbnail url
	std::string tfile;	// thumbnail local file
	std::string published;	// modified, actually
};

struct sNKCategory
{
	int id;
	std::string title;
	int post_count;
};

typedef std::vector<sNKVideoInfo> nk_video_list_t;
typedef std::vector<sNKCategory> nk_category_list_t;

class cNKFeedParser
{
	private:

		std::vector<sNKCategory> categories;

		std::string *thumbnail_dir;

		int max_results;
		int concurrent_downloads;
		bool parsed;
		bool stopThumbnailDownload;

		nk_video_list_t videos;

		CURL *curl_handle;
		OpenThreads::Mutex mutex;
		OpenThreads::Mutex thumbnailthread_mutex;
		int threadCount;
		int worker_index;
		static void* DownloadThumbnailsThread(void*);

		static size_t CurlWriteToString(void *ptr, size_t size, size_t nmemb, void *data);
		void encodeUrl(std::string &txt);
		void decodeUrl(std::string &url);
		bool getUrl(std::string &url, std::string &answer, CURL *_curl_handle = NULL);
		bool DownloadUrl(std::string &url, std::string &file, CURL *_curl_handle = NULL);
		bool parseFeedJSON(std::string &answer);
		bool parseCategoriesJSON(std::string &answer);
		bool ParseFeed(std::string &url);
		void removeHTMLMarkup(std::string &s);
		void DownloadThumbnailsEnd(void);
	public:
		enum nk_feed_mode_t
		{
			CATEGORY,
			SEARCH,
			MODE_LAST
		};
		cNKFeedParser();
		~cNKFeedParser();

		bool ParseFeed(nk_feed_mode_t mode, std::string search, int category);
		bool ParseCategories(void);
		bool DownloadThumbnail(sNKVideoInfo &vinfo, CURL *_curl_handle = NULL);
		bool DownloadThumbnails();
		void Cleanup(bool delete_thumbnails = true);

		nk_video_list_t &GetVideoList() { return videos; }
		nk_category_list_t &GetCategoryList() { ParseCategories(); return categories; }
		bool Parsed() { return parsed; }

		void SetMaxResults(int count) { max_results = count; }
		void SetConcurrentDownloads(int count) { concurrent_downloads = count; }
		void setThumbnailDir(std::string &_thumbnail_dir);
		int ThreadCount(int what = 0);
		std::string GetUrl(std::string &s, bool rtmp = false);
};

#endif
