/*
	shellwindow.cpp
	(C)2013 by martii

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


#include "shellwindow.h"

#include <global.h>
#include <neutrino.h>
#include <driver/framebuffer.h>
#include <gui/widget/textbox.h>
#include <stdio.h>
#include <poll.h>
#include <fcntl.h>

CShellWindow::CShellWindow(const std::string command, bool verbose, int *res) {
	textBox = NULL;
	std::string cmd = command;
	if (!verbose) {
		cmd += " 2>/dev/null >&2";
		int r = system(cmd.c_str());
		if (res) {
			if (r == -1)
				*res = r;
			else
				*res = WEXITSTATUS(r);
		}
		return;
	}

	cmd += " 2>&1";
	FILE *f = popen(cmd.c_str(), "r");
	if (!f) {
		if (res)
			*res = -1;
		return;
	}
	Font *font = g_Font[SNeutrinoSettings::FONT_TYPE_GAMELIST_ITEMSMALL];
	CFrameBuffer *frameBuffer = CFrameBuffer::getInstance();
	unsigned int lines_max = frameBuffer->getScreenHeight() / font->getHeight();
	int h = lines_max * font->getHeight();
	list<std::string> lines;
	CBox textBoxPosition(frameBuffer->getScreenX(), frameBuffer->getScreenY(), frameBuffer->getScreenWidth(), h);
	textBox = new CTextBox(cmd.c_str(), font, 0, &textBoxPosition);
	struct pollfd fds;
	fds.fd = fileno(f);
	fds.events = POLLIN | POLLHUP | POLLERR;
	fcntl(fds.fd, F_SETFL, fcntl(fds.fd, F_GETFL, 0) | O_NONBLOCK);

	struct timeval tv;
	gettimeofday(&tv,NULL);
	uint64_t lastPaint = (uint64_t) tv.tv_usec + (uint64_t)((uint64_t) tv.tv_sec * (uint64_t) 1000000);
	bool ok = true, nlseen = false, dirty = false, pushed = false;
	char output[1024];
	int off = 0;
	std::string txt = "";

	do {
		uint64_t now;
		fds.revents = 0;
		int r = poll(&fds, 1, 300);

		if (r > 0) {
			if (!feof(f)) {
				gettimeofday(&tv,NULL);
				now = (uint64_t) tv.tv_usec + (uint64_t)((uint64_t) tv.tv_sec * (uint64_t) 1000000);

				unsigned int lines_read = 0;
				while (fgets(output + off, sizeof(output) - off, f)) {
					char *outputp = output + off;
					dirty = true;

					for (int i = off; output[i]; i++)
						switch (output[i]) {
							case '\b':
								if (outputp > output)
									outputp--;
								*outputp = 0;
								break;
							case '\r':
								outputp = output;
								break;
							case '\n':
								nlseen = true;
								lines_read++;
							default:
								*outputp++ = output[i];
								break;
						}

					if (outputp < output + sizeof(output))
						*outputp = 0;
					if (nlseen) {
						pushed = false;
						nlseen = false;
						off = 0;
					} else {
						off = strlen(output);
						if (pushed)
							lines.pop_back();
					}
					lines.push_back(std::string((output)));
					pushed = true;
					if (lines.size() > lines_max)
						lines.pop_front();
					txt = "";
					for (std::list<std::string>::const_iterator it = lines.begin(), end = lines.end(); it != end; ++it)
						txt += *it;
					if (((lines_read == lines_max) && (lastPaint + 100000 < now)) || (lastPaint + 250000 < now)) {
						textBox->setText(&txt);
						textBox->paint();
						lines_read = 0;
						lastPaint = now;
						dirty = false;
					}
				}
			} else
				ok = false;
		} else if (r < 0)
			ok = false;

		gettimeofday(&tv,NULL);
		now = (uint64_t) tv.tv_usec + (uint64_t)((uint64_t) tv.tv_sec * (uint64_t) 1000000);
		if (r < 1 || dirty || lastPaint + 250000 < now) {
			textBox->setText(&txt);
			textBox->paint();
			lastPaint = now;
			dirty = false;
		}
	} while(ok);

	int r = pclose(f);

	if (res) {
		if (r == -1)
			*res = r;
		else
			*res = WEXITSTATUS(r);
	}
	return;
}

CShellWindow::~CShellWindow()
{
	if (textBox) {
		textBox->hide();
		delete textBox;
	}
}
