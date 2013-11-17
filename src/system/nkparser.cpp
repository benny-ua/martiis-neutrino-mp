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

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#include <set>
#include <map>
#include <vector>
#include <bitset>
#include <string>

#include <OpenThreads/ScopedLock>
#include "settings.h"
#include "helpers.h"
#include <global.h>
#include "set_threadname.h"
#include <json/json.h>
#include <libmd5sum/md5.h>

#include "nkparser.h"

#if LIBCURL_VERSION_NUM < 0x071507
#include <curl/types.h>
#endif

#define URL_TIMEOUT 60

cNKFeedParser::cNKFeedParser()
{
	thumbnail_dir = NULL;
	parsed = false;
	max_results = 25;
	concurrent_downloads = 2;
	curl_handle = curl_easy_init();
	stopThumbnailDownload = false;
	threadCount = 0;
}

cNKFeedParser::~cNKFeedParser()
{
	DownloadThumbnailsEnd();
	curl_easy_cleanup(curl_handle);
}

void cNKFeedParser::SetThumbnailDir(std::string &_thumbnail_dir)
{
	thumbnail_dir = &_thumbnail_dir;
}

size_t cNKFeedParser::CurlWriteToString(void *ptr, size_t size, size_t nmemb, void *data)
{
	if (size * nmemb > 0) {
		std::string* pStr = (std::string*) data;
		pStr->append((char*) ptr, nmemb);
	}
        return size*nmemb;
}

bool cNKFeedParser::getUrl(std::string &url, std::string &answer, CURL *_curl_handle)
{
	const char hex[] = "0123456789abcdef";
	char filename[33];
	unsigned char md5sum[16];
	md5_buffer (url.data(), url.length(), md5sum);
	for (unsigned int i = 0; i < sizeof(md5sum); i++) {
		filename[2 * i] = hex[(md5sum[i] >> 4) & 0x0f];
		filename[2 * i + 1] = hex[md5sum[i] & 0x0f];
	}
	filename[32] = 0;

	std::string cached_file;
	if (thumbnail_dir)
		cached_file = *thumbnail_dir + "/" + filename;
	else {
		mkdir("/tmp/nkcache", 0755);
		cached_file = std::string("/tmp/nkcache/") + filename;
	}
	struct stat st;
	if (!stat(cached_file.c_str(), &st) && st.st_mtime + 6 * 3600 /* FIXME, should be configurable */ > time(NULL)) {
		std::ifstream in(cached_file.c_str());
		std::stringstream buf;
		buf << in.rdbuf();
		answer = buf.str();
		return true;
	}
	unlink(cached_file.c_str());

	if (!_curl_handle)
		_curl_handle = curl_handle;

	curl_easy_setopt(_curl_handle, CURLOPT_URL, url.c_str());
	curl_easy_setopt(_curl_handle, CURLOPT_WRITEFUNCTION, &cNKFeedParser::CurlWriteToString);
	curl_easy_setopt(_curl_handle, CURLOPT_FILE, (void *)&answer);
	curl_easy_setopt(_curl_handle, CURLOPT_FAILONERROR, 1);
	curl_easy_setopt(_curl_handle, CURLOPT_TIMEOUT, URL_TIMEOUT);
	curl_easy_setopt(_curl_handle, CURLOPT_NOSIGNAL, (long)1);

	if(g_settings.softupdate_proxyserver != "") {
		curl_easy_setopt(_curl_handle, CURLOPT_PROXY, g_settings.softupdate_proxyserver.c_str());
		if(g_settings.softupdate_proxyusername != "") {
			std::string tmp = g_settings.softupdate_proxyusername + ":" + g_settings.softupdate_proxypassword;
			curl_easy_setopt(_curl_handle, CURLOPT_PROXYUSERPWD, tmp.c_str());
		}
	}

	char cerror[CURL_ERROR_SIZE];
	curl_easy_setopt(_curl_handle, CURLOPT_ERRORBUFFER, cerror);

	printf("try to get [%s] ...\n", url.c_str());
	CURLcode httpres = curl_easy_perform(_curl_handle);

	printf("http: res %d size %d\n", httpres, (int)answer.size());

	if (httpres != 0 || answer.empty()) {
		printf("error: %s\n", cerror);
		return false;
	}
	std::ofstream o;
	o.open(cached_file.c_str());
	o << answer;
	o.close();
	return true;
}

bool cNKFeedParser::DownloadUrl(std::string &url, std::string &file, CURL *_curl_handle)
{
	if (!_curl_handle)
		_curl_handle = curl_handle;

	FILE * fp = fopen(file.c_str(), "wb");
	if (fp == NULL) {
		perror(file.c_str());
		return false;
	}
	curl_easy_setopt(_curl_handle, CURLOPT_URL, url.c_str());
	curl_easy_setopt(_curl_handle, CURLOPT_WRITEFUNCTION, NULL);
	curl_easy_setopt(_curl_handle, CURLOPT_FILE, fp);
	curl_easy_setopt(_curl_handle, CURLOPT_FAILONERROR, 1);
	curl_easy_setopt(_curl_handle, CURLOPT_TIMEOUT, URL_TIMEOUT);
	curl_easy_setopt(_curl_handle, CURLOPT_NOSIGNAL, (long)1);

	if(g_settings.softupdate_proxyserver != "") {
		curl_easy_setopt(_curl_handle, CURLOPT_PROXY, g_settings.softupdate_proxyserver.c_str());
		if(g_settings.softupdate_proxyusername != "") {
			std::string tmp = g_settings.softupdate_proxyusername + ":" + g_settings.softupdate_proxypassword;
			curl_easy_setopt(_curl_handle, CURLOPT_PROXYUSERPWD, tmp.c_str());
		}
	}

	char cerror[CURL_ERROR_SIZE];
	curl_easy_setopt(_curl_handle, CURLOPT_ERRORBUFFER, cerror);

	printf("try to get [%s] ...\n", url.c_str());
	CURLcode httpres = curl_easy_perform(_curl_handle);

	double dsize;
	curl_easy_getinfo(_curl_handle, CURLINFO_SIZE_DOWNLOAD, &dsize);
	fclose(fp);

	printf("http: res %d size %g.\n", httpres, dsize);

	if (httpres != 0) {
		printf("curl error: %s\n", cerror);
		unlink(file.c_str());
		return false;
	}
	return true;
}

void cNKFeedParser::decodeUrl(std::string &url)
{
	char * str = curl_easy_unescape(curl_handle, url.c_str(), 0, NULL);
	if(str)
		url = str;
	curl_free(str);
}

void cNKFeedParser::encodeUrl(std::string &txt)
{
	char * str = curl_easy_escape(curl_handle, txt.c_str(), txt.length());
	if(str)
		txt = str;
	curl_free(str);
}

void cNKFeedParser::removeHTMLMarkup(std::string &s)
{
	char t[s.length() + 1];
	char *u = t;
	const char *p = s.c_str();
	while (*p) {
		if (*p == '<') {
			while (*p && *p != '>')
				p++;
			if (*p)
				p++;
			continue;
		}
		if (p[0] == '&' && p[1] == '#') {
			p += 2;
			const char *format;
			if (p[0] == 'x' || p[0] == 'X') {
				format = "%x";
				p++;
			} else
				format = "%d";
			unsigned int r = 0;
			if (1 == sscanf(p, format, &r)) {
				if (r < 0x80) {
					*u++ = r & 0x7f;
				} else if (r < 0x800) {
					*u++ = 0x80 & (r & 0x3f);
					r >>= 6;
					*u++ = 0xC0 & r;
				} else if (r < 0x10000) {
					*u++ = 0x80 | (0x3f & r);
					r >>= 6;
					*u++ = 0x80 | (0x3f & r);
					r >>= 6;
					*u++ = 0xE0 | r;
				} else if (r < 0x110000) {
					*u++ = 0x80 | (0x3f & r);
					r >>= 6;
					*u++ = 0x80 | (0x3f & r);
					r >>= 6;
					*u++ = 0x80 | (0x3f & r);
					r >>= 6;
					*u++ = 0xF0 | r;
				}
			}
			while(*p && *p != ';')
				p++;
			if (*p)
				p++;
			continue;
		}
		if (!strncasecmp(p, "&hellip;", 8)) {
			p += 8;
			*u++ = 0xE2;
			*u++ = 0x80;
			*u++ = 0xA6;
			continue;
		}
		if (!strncasecmp(p, "&nbsp;", 6)) {
			p += 6;
			*u++ = ' ';
			continue;
		}
		if (!strncasecmp(p, "&lt;", 4)) {
			p += 4;
			*u++ = '<';
			continue;
		}
		if (!strncasecmp(p, "&gt;", 4)) {
			p += 4;
			*u++ = '>';
			continue;
		}
		if (!strncasecmp(p, "&#amp;", 6)) {
			p += 6;
			*u++ = '&';
			continue;
		}
		*u++ = *p++;
	}
	*u = 0;
	s = std::string(t);
}

bool cNKFeedParser::parseCategoriesJSON(std::string &answer)
{
	Json::Value root, v;
	Json::Reader reader;

	if (!reader.parse(answer, root))
		return false;

	v = root.get("status", "");
	if (v.type() != Json::stringValue || v.asString() != "ok")
		return false;

	Json::Value cats = root.get("categories", "");
	if (cats.type() != Json::arrayValue)
		return false;

	categories.clear();

	for(unsigned int i = 0; i < cats.size(); ++i) {
		const Json::Value cat = cats[i];
		sNKCategory c;
		c.id = 0;
		c.post_count = 0;
		v = cat.get("id", "");
		if (v.type() == Json::intValue || v.type() == Json::uintValue)
			c.id = v.asInt();
		v = cat.get("title", "");
		if (v.type() == Json::stringValue)
			c.title = v.asString();
		v = cat.get("post_count", "");
		if (v.type() == Json::intValue || v.type() == Json::uintValue)
			c.post_count = v.asInt();
		if (c.id > 0)
			categories.push_back(c);
	}
	return !categories.empty();
}

bool cNKFeedParser::parseFeedJSON(std::string &answer)
{
	Json::Value root, v;
	Json::Reader reader;
	if (!reader.parse(answer, root))
		return false;

	v = root.get("status", "" );
	if (v.type() != Json::stringValue || v.asString() != "ok")
		return false;

	Json::Value posts = root.get("posts", "");
	if (posts.type() != Json::arrayValue)
		return false;

	for(unsigned int i = 0; i < posts.size(); ++i) {
		const Json::Value flick = posts[i];
		sNKVideoInfo vinfo;
		v = flick.get("title_plain", "");
		if (v.type() == Json::stringValue) {
			vinfo.title = v.asString();
			removeHTMLMarkup(vinfo.title);
		}
		v = flick.get("id", "");
		if (v.type() == Json::intValue || v.type() == Json::uintValue) {
			vinfo.id = to_string(v.asInt());
			if (thumbnail_dir)
				vinfo.tfile = *thumbnail_dir + "/" + vinfo.id + ".jpg";
		}
		v = flick.get("content", "");
		if (v.type() == Json::stringValue) {
			vinfo.description = v.asString();
			removeHTMLMarkup(vinfo.description);
		}
		v = flick.get("modified", "");
		if (v.type() == Json::stringValue) {
			vinfo.published = v.asString();
		}
		unsigned int _i = 0;
		v = flick.get("custom_fields", "");
		if (v.type() == Json::objectValue) {
			v = v.get("Streaming", "");
			if (v.type() == Json::arrayValue && v.size() > 0) {
				if (v[_i].type() == Json::stringValue)
					vinfo.streaming = v[_i].asString();
			}
		}
		v = flick.get("attachments", "");
		if (v.type() == Json::arrayValue && v.size() > 0 && v[_i].type() == Json::objectValue) {
			v = v[_i]["url"];
			if (v.type() == Json::stringValue)
				vinfo.thumbnail = v.asString();
		}
		if (!vinfo.id.empty())
			videos.push_back(vinfo);
	}

	parsed = !videos.empty();
	return parsed;
}

bool cNKFeedParser::ParseFeed(std::string &url)
{
	DownloadThumbnailsEnd();
	videos.clear();

	std::string answer;
	if (!getUrl(url, answer))
		return false;
	return parseFeedJSON(answer);
}

bool cNKFeedParser::ParseFeed(nk_feed_mode_t mode, std::string search, int category)
{
	std::string url = "http://www.netzkino.de/capi/";
	if (mode == SEARCH) {
		if (search.empty())
			return false;
		encodeUrl(search);
		url += "get_search_results?search=" + search;
	} else if (mode == CATEGORY && category > 0) {
		url += "get_category_posts?id=" + to_string(category);
	} else
		return false;
	url += "&custom_fields=Streaming&count=" + to_string(max_results) + "d&";

	return ParseFeed(url);
}

bool cNKFeedParser::ParseCategories(void)
{
	if (categories.empty()) {
		std::string url = "http://www.netzkino.de/capi/get_category_index";
		std::string answer;
		if (!getUrl(url, answer))
			return false;
		return parseCategoriesJSON(answer);
	}
	return !categories.empty();
}

void *cNKFeedParser::DownloadThumbnailsThread(void *arg)
{
	set_threadname("NK::DownloadThumbnails");
	bool ret = true;
	cNKFeedParser *caller = (cNKFeedParser *)arg;
	caller->ThreadCount(+1);
	CURL *c = curl_easy_init();
	unsigned int i;
	do {
		OpenThreads::ScopedLock<OpenThreads::Mutex> m_lock(caller->mutex);
		i = caller->worker_index++;
	} while (i < caller->videos.size() && ((ret &= caller->DownloadThumbnail(caller->videos[i], c)) || true));
	curl_easy_cleanup(c);
	caller->ThreadCount(-1);
	pthread_exit(&ret);
}

bool cNKFeedParser::DownloadThumbnail(sNKVideoInfo &vinfo, CURL *_curl_handle)
{
	if (!_curl_handle)
		_curl_handle = curl_handle;
	bool found = false;
	if (!vinfo.thumbnail.empty()) {
		found = !access(vinfo.tfile, F_OK);
		if (!found)
			found = DownloadUrl(vinfo.thumbnail, vinfo.tfile, _curl_handle);
	}
	return found;
}

bool cNKFeedParser::DownloadThumbnails()
{
	if (!thumbnail_dir)
		return false;
	if (safe_mkdir(thumbnail_dir->c_str()) && errno != EEXIST) {
		perror(thumbnail_dir->c_str());
		return false;
	}
	unsigned int max_threads = concurrent_downloads;
	if (videos.size() < max_threads)
		max_threads = videos.size();
	pthread_t thr;
	worker_index = 0;
	for (unsigned i = 0; i < max_threads; i++)
		if (!pthread_create(&thr, NULL, cNKFeedParser::DownloadThumbnailsThread, this))
			pthread_detach(thr);
	return true;
}

void cNKFeedParser::DownloadThumbnailsEnd(void)
{
	stopThumbnailDownload = true;
	while (ThreadCount())
		usleep(100000);
}

int cNKFeedParser::ThreadCount(int what)
{
	OpenThreads::ScopedLock<OpenThreads::Mutex> m_lock(thumbnailthread_mutex);
	threadCount += what;
	return threadCount;
}

void cNKFeedParser::Cleanup(bool delete_thumbnails)
{
	printf("cNKFeedParser::Cleanup: %d videos\n", (int)videos.size());
	if (delete_thumbnails) {
		for (unsigned i = 0; i < videos.size(); i++) {
			unlink(videos[i].tfile.c_str());
		}
	}
	DownloadThumbnailsEnd();
	videos.clear();
	parsed = false;
}

std::string cNKFeedParser::GetUrl(std::string &s, bool rtmp)
{
	if (rtmp)
		return "rtmp://mf.netzkino.c.nmdn.net/netzkino/_definst_/mp4:" + s;
	return "http://dl.netzkinotv.c.nmdn.net/netzkino_tv/" + s + ".mp4";
}
