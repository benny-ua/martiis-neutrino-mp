/*
	shellwindow.h
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

#ifndef __WIDGET_SHELLWINDOW_H__
#define __WIDGET_SHELLWINDOW_H__
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string>
#include <gui/widget/textbox.h>

class CShellWindow
{
	public:
		enum shellwindow_modes { VERBOSE = 1, ACKNOWLEDGE = 2 };
		CShellWindow(const std::string &cmd, const int mode = 0, int *res = NULL);
		~CShellWindow();
	private:
		int mode;
		CFrameBuffer *frameBuffer;
		CTextBox *textBox;
};

#endif
