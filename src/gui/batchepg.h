/*
   BatchEPG Menu
   (C)2012 by martii

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

#ifndef __batchepg_h__
#define __batchepg_h__

#include <string>
#include <vector>

#include <driver/framebuffer.h>
#include <zapit/channel.h>
#include <system/setting_helpers.h>
#include <configfile.h>

#define BATCHEPGCONFIG CONFIGDIR "/batchepg.conf"

class CBatchEPG_Menu : public CMenuTarget
{
    private:
//	CFrameBuffer *frameBuffer;
	int x;
	int y;
	int width;
	int height;
	int hheight, mheight;
	struct epgChannel {
		t_channel_id channel_id;
		int type;
		int type_old;
		std::string name;
	};
	std::vector<epgChannel> epgChannels;
	bool Run(int);
	void Load();
	void Save();
	bool Changed();
	void Settings();
	void AddCurrentChannel();
	bool AbortableSystem(const char *command);
	void AbortableSleep(time_t);
    public:
	enum BatchEPG_type { BATCHEPG_OFF = 0, BATCHEPG_MHW1, BATCHEPG_MHW2, BATCHEPG_STANDARD };
	CBatchEPG_Menu();
	~CBatchEPG_Menu();
	int exec(CMenuTarget* parent, const std::string & actionKey);
};

#endif
