/*
	Neutrino-GUI  -   DBoxII-Project

	Copyright (C) 2001 Steffen Hehn 'McClean'
	Homepage: http://dbox.cyberphoria.org/

	Kommentar:

	Diese GUI wurde von Grund auf neu programmiert und sollte nun vom
	Aufbau und auch den Ausbaumoeglichkeiten gut aussehen. Neutrino basiert
	auf der Client-Server Idee, diese GUI ist also von der direkten DBox-
	Steuerung getrennt. Diese wird dann von Daemons uebernommen.


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

#include <gui/screensetup.h>

#include <gui/color.h>
#include <gui/widget/messagebox.h>
#include <gui/widget/icons.h>

#include <driver/fontrenderer.h>
#include <driver/rcinput.h>
#include <system/settings.h>

#include <global.h>
#include <neutrino.h>

//int x_box = 15 * 5;

inline unsigned int make16color(__u32 rgb)
{
	return 0xFF000000 | rgb;
}

CScreenSetup::CScreenSetup()
{
	frameBuffer = CFrameBuffer::getInstance();
#ifdef MARTII
	startX = g_settings.screen_StartX_int;
	startY = g_settings.screen_StartY_int;
	endX = g_settings.screen_EndX_int;
	endY = g_settings.screen_EndY_int;
	channel_id = 0;
#endif
}
#ifdef MARTII
struct borderFrame { int sx, sy, ex, ey; };
static map<t_channel_id, borderFrame> borderMap;

bool CScreenSetup::loadBorder(t_channel_id cid)
{
	loadBorders();
	channel_id = cid;
	std::map<t_channel_id, borderFrame>::iterator it = borderMap.find(cid);
	if (it != borderMap.end()) {
		startX = it->second.sx;
		startY = it->second.sy;
		endX = it->second.ex;
		endY = it->second.ey;
	} else {
		startX = g_settings.screen_StartX_int;
		startY = g_settings.screen_StartY_int;
		endX = g_settings.screen_EndX_int;
		endY = g_settings.screen_EndY_int;
	}
	return (it != borderMap.end());
}

void CScreenSetup::showBorder(t_channel_id cid)
{
	if (loadBorder(cid)) {
		frameBuffer->setBorderColor(0xFF000000);
		frameBuffer->setBorder(startX, startY, endX, endY);
	} else
		hideBorder();
}

void CScreenSetup::hideBorder()
{
	frameBuffer->setBorderColor();
	frameBuffer->setBorder(startX, startY, endX, endY);
}

void CScreenSetup::resetBorder(t_channel_id cid)
{
	loadBorder(cid);
	frameBuffer->setBorderColor();
	std::map<t_channel_id, borderFrame>::iterator it = borderMap.find(cid);
	if (it != borderMap.end())
		borderMap.erase(cid);
	startX = g_settings.screen_StartX_int;
	startY = g_settings.screen_StartY_int;
	endX = g_settings.screen_EndX_int;
	endY = g_settings.screen_EndY_int;
	frameBuffer->setBorder(startX, startY, endX, endY);
}

#define BORDER_CONFIG_FILE "/var/tuxbox/config/zapit/borders.conf"

void CScreenSetup::loadBorders()
{
	if (borderMap.empty()) {
		FILE *f = fopen(BORDER_CONFIG_FILE, "r");
		borderMap.clear();
		if (f) {
			char s[1000];
			while (fgets(s, sizeof(s), f)) {
				t_channel_id chan;
				borderFrame b;
				if (5 == sscanf(s, "%llx %d %d %d %d", &chan, &b.sx, &b.sy, &b.ex, &b.ey)) {
					borderMap[chan] = b;
				} 
			}
			fclose(f);
		}
	}
}

void CScreenSetup::saveBorders()
{
	if (borderMap.empty())
		unlink(BORDER_CONFIG_FILE);
	else {
		  FILE *f = fopen(BORDER_CONFIG_FILE, "w");
		  if (f) {
			std::map<t_channel_id, borderFrame>::iterator it;
			for (it = borderMap.begin(); it != borderMap.end(); it++)
				fprintf(f, "%llx %d %d %d %d\n", it->first, it->second.sx, it->second.sy, it->second.ex, it->second.ey);
			fflush(f);
			fdatasync(fileno(f));
			fclose(f);
		  }
	}
}
#endif
int CScreenSetup::exec(CMenuTarget* parent, const std::string &)
{
	neutrino_msg_t      msg;
	neutrino_msg_data_t data;

	int res = menu_return::RETURN_REPAINT;

	if (parent)
	{
		parent->hide();
	}

#ifndef MARTII
	x_box = 15*5;
	y_box = frameBuffer->getScreenHeight(true) / 2;
#endif

        int icol_w, icol_h;
        frameBuffer->getIconSize(NEUTRINO_ICON_BUTTON_RED, &icol_w, &icol_h);

	BoxHeight = std::max(icol_h+4, g_Font[SNeutrinoSettings::FONT_TYPE_MENU]->getHeight());
	BoxWidth = g_Font[SNeutrinoSettings::FONT_TYPE_MENU]->getRenderWidth(g_Locale->getText(LOCALE_SCREENSETUP_UPPERLEFT));

	int tmp = g_Font[SNeutrinoSettings::FONT_TYPE_MENU]->getRenderWidth(g_Locale->getText(LOCALE_SCREENSETUP_LOWERRIGHT));
	if (tmp > BoxWidth)
		BoxWidth = tmp;
#ifdef MARTII
	if (channel_id) {
		tmp = g_Font[SNeutrinoSettings::FONT_TYPE_MENU]->getRenderWidth(g_Locale->getText(LOCALE_SCREENSETUP_REMOVE));
		if (tmp > BoxWidth)
			BoxWidth = tmp;
	}
#endif

	BoxWidth += 10 + icol_w;

#ifdef MARTII
	x_box = (frameBuffer->getScreenWidth(true) - BoxWidth)/2;
	y_box = (frameBuffer->getScreenHeight(true) - BoxHeight)/2;

	x_coord[0] = startX;
	x_coord[1] = endX;
	y_coord[0] = startY;
	y_coord[1] = endY;
	if (!channel_id) {
		color_bak = frameBuffer->getBorderColor();
		frameBuffer->getBorder(x_coord_bak[0], y_coord_bak[0], x_coord_bak[1], y_coord_bak[1]);
	}
	frameBuffer->setBorder(x_coord[0], y_coord[0], x_coord[1], y_coord[1]);
#else
	x_coord[0] = g_settings.screen_StartX;
	x_coord[1] = g_settings.screen_EndX;
	y_coord[0] = g_settings.screen_StartY;
	y_coord[1] = g_settings.screen_EndY;
#endif

	paint();
	frameBuffer->blit();

	selected = 0;

	uint64_t timeoutEnd = CRCInput::calcTimeoutEnd(g_settings.timing[SNeutrinoSettings::TIMING_MENU] == 0 ? 0xFFFF : g_settings.timing[SNeutrinoSettings
::TIMING_MENU]);

	bool loop=true;
	while (loop)
	{
		g_RCInput->getMsgAbsoluteTimeout( &msg, &data, &timeoutEnd, true );

		if ( msg <= CRCInput::RC_MaxRC )
			timeoutEnd = CRCInput::calcTimeoutEnd(g_settings.timing[SNeutrinoSettings::TIMING_MENU] == 0 ? 0xFFFF : g_settings.timing[SNeutrinoSettings
::TIMING_MENU]);

		switch ( msg )
		{
			case CRCInput::RC_ok:
				// abspeichern
#ifdef MARTII
			startX = x_coord[0];
			endX = x_coord[1];
			startY = y_coord[0];
			endY = y_coord[1];
			if (channel_id) {
				borderFrame b;
				b.sx = startX;
				b.sy = startY;
				b.ex = endX;
				b.ey = endY;
				borderMap[channel_id] = b;
				showBorder(channel_id);
				saveBorders();
			} else {
				x_coord_bak[0] = x_coord[0];
				y_coord_bak[0] = y_coord[0];
				x_coord_bak[1] = x_coord[1];
				y_coord_bak[1] = y_coord[1];
				g_settings.screen_StartX_int = x_coord[0];
				g_settings.screen_EndX_int = x_coord[1];
				g_settings.screen_StartY_int = y_coord[0];
				g_settings.screen_EndY_int = y_coord[1];
				showBorder(channel_id);

				if(g_settings.screen_preset) {
					g_settings.screen_StartX_lcd = g_settings.screen_StartX_int;
					g_settings.screen_StartY_lcd = g_settings.screen_StartY_int;
					g_settings.screen_EndX_lcd = g_settings.screen_EndX_int;
					g_settings.screen_EndY_lcd = g_settings.screen_EndY_int;
				} else {
					g_settings.screen_StartX_crt = g_settings.screen_StartX_int;
					g_settings.screen_StartY_crt = g_settings.screen_StartY_int;
					g_settings.screen_EndX_crt = g_settings.screen_EndX_int;
					g_settings.screen_EndY_crt = g_settings.screen_EndY_int;
				}
			}
#else
				g_settings.screen_StartX = x_coord[0];
				g_settings.screen_EndX = x_coord[1];
				g_settings.screen_StartY = y_coord[0];
				g_settings.screen_EndY = y_coord[1];
				if(g_settings.screen_preset) {
					g_settings.screen_StartX_lcd = g_settings.screen_StartX;
					g_settings.screen_StartY_lcd = g_settings.screen_StartY;
					g_settings.screen_EndX_lcd = g_settings.screen_EndX;
					g_settings.screen_EndY_lcd = g_settings.screen_EndY;
				} else {
					g_settings.screen_StartX_crt = g_settings.screen_StartX;
					g_settings.screen_StartY_crt = g_settings.screen_StartY;
					g_settings.screen_EndX_crt = g_settings.screen_EndX;
					g_settings.screen_EndY_crt = g_settings.screen_EndY;
				}
				if (g_InfoViewer) /* recalc infobar position */
					g_InfoViewer->start();
#endif
				loop = false;
				break;

			case CRCInput::RC_home:
#ifdef MARTII
				if ( ( (startX != x_coord[0] ) || ( endX != x_coord[1] ) || ( startY != y_coord[0] ) || ( endY != y_coord[1] ) ) &&
						(ShowLocalizedMessage(LOCALE_VIDEOMENU_SCREENSETUP, LOCALE_MESSAGEBOX_DISCARD, CMessageBox::mbrYes, CMessageBox::mbYes | CMessageBox::mbCancel) == CMessageBox::mbrCancel)) {
					break;
				}
#else
				if ( ( ( g_settings.screen_StartX != x_coord[0] ) ||
							( g_settings.screen_EndX != x_coord[1] ) ||
							( g_settings.screen_StartY != y_coord[0] ) ||
							( g_settings.screen_EndY != y_coord[1] ) ) &&
						(ShowLocalizedMessage(LOCALE_VIDEOMENU_SCREENSETUP, LOCALE_MESSAGEBOX_DISCARD, CMessageBox::mbrYes, CMessageBox::mbYes | CMessageBox::mbCancel) == CMessageBox::mbrCancel))
					break;
#endif

			case CRCInput::RC_timeout:
#ifdef MARTII
				loadBorder(channel_id);
#endif
				loop = false;
				break;

			case CRCInput::RC_red:
			case CRCInput::RC_green:
				{
					selected = ( msg == CRCInput::RC_green ) ? 1 : 0 ;

#ifndef MARTII
					frameBuffer->paintBoxRel(x_box, y_box, BoxWidth, BoxHeight,
						(selected == 0)?COL_MENUCONTENTSELECTED_PLUS_0:COL_MENUCONTENT_PLUS_0);
					frameBuffer->paintBoxRel(x_box, y_box + BoxHeight, BoxWidth, BoxHeight,
						(selected ==1 )?COL_MENUCONTENTSELECTED_PLUS_0:COL_MENUCONTENT_PLUS_0);
#endif

					paintIcons(selected);
					break;
				}
#ifdef MARTII
			case CRCInput::RC_yellow:
				if (channel_id) {
					startX = g_settings.screen_StartX;
					startY = g_settings.screen_StartY;
					endX = g_settings.screen_EndX;
					endY = g_settings.screen_EndY;
					resetBorder(channel_id);
					saveBorders();
					if (g_InfoViewer)
						g_InfoViewer->start();
					loop = false;
				}
				break;
#endif
			case CRCInput::RC_up:
#ifdef MARTII
				if (((selected == 0) && (y_coord[0] > 0)) || ((selected == 1) && (y_coord[1] > y_coord[0] - 100))) {
					y_coord[selected]--;
					frameBuffer->setBorder(x_coord[0], y_coord[0], x_coord[1], y_coord[1]);
				}
				break;
#else
			{

				int min = (selected == 0) ? 0 : 400;
				if (y_coord[selected] <= min)
					y_coord[selected] = min;
				else
				{
					unpaintBorder(selected);
					y_coord[selected]--;
					paintBorder(selected);
				}
				break;
			}
#endif
			case CRCInput::RC_down:
#ifdef MARTII
				if (((selected == 0) && (y_coord[0] < y_coord[1] - 100)) || ((selected == 1) && (y_coord[1] < (int)frameBuffer->getScreenHeight(true)))) {
					y_coord[selected]++;
					frameBuffer->setBorder(x_coord[0], y_coord[0], x_coord[1], y_coord[1]);
				}
				break;
#else
			{
				int max = (selected == 0 )? 200 : frameBuffer->getScreenHeight(true) - 1;
				if (y_coord[selected] >= max)
					y_coord[selected] = max;
				else
				{
					unpaintBorder(selected);
					y_coord[selected]++;
					paintBorder(selected);
				}
				break;
			}
#endif
			case CRCInput::RC_left:
#ifdef MARTII
				if (((selected == 0) && (x_coord[0] > 0)) || ((selected == 1) && (x_coord[1] > x_coord[0] - 100))) {
					x_coord[selected]--;
					frameBuffer->setBorder(x_coord[0], y_coord[0], x_coord[1], y_coord[1]);
				}
				break;
#else
			{
				int min = (selected == 0) ? 0 : 400;
				if (x_coord[selected] <= min)
					x_coord[selected] = min;
				else
				{
					unpaintBorder(selected);
					x_coord[selected]--;
					paintBorder( selected );
				}
				break;
			}
#endif
			case CRCInput::RC_right:
#ifdef MARTII
				if (((selected == 0) && (x_coord[0] < x_coord[1] - 100)) || ((selected == 1) && (x_coord[1] < (int)frameBuffer->getScreenWidth(true)))) {
					x_coord[selected]++;
					frameBuffer->setBorder(x_coord[0], y_coord[0], x_coord[1], y_coord[1]);
				}
				break;
#else
			{
				int max = (selected == 0) ? 200 : frameBuffer->getScreenWidth(true) - 1;
				if (x_coord[selected] >= max)
					x_coord[selected] = max;
				else
				{
					unpaintBorder(selected);
					x_coord[selected]++;
					paintBorder( selected );
				}
				break;
			}
#endif
			case CRCInput::RC_favorites:
			case CRCInput::RC_sat:
				break;

			default:
				if ( CNeutrinoApp::getInstance()->handleMsg( msg, data ) & messages_return::cancel_all )
				{
					loop = false;
					res = menu_return::RETURN_EXIT_ALL;
				}
		}
		frameBuffer->blit();
	}

	hide();
	return res;
}

void CScreenSetup::hide()
{
	int w = (int) frameBuffer->getScreenWidth(true);
	int h = (int) frameBuffer->getScreenHeight(true);
	frameBuffer->paintBackgroundBox(0, 0, w, h);
#ifdef MARTII
	if (channel_id)
		showBorder(channel_id);
	else {
		frameBuffer->setBorderColor(color_bak);
		frameBuffer->setBorder(x_coord_bak[0], y_coord_bak[0], x_coord_bak[1], y_coord_bak[1]);
	}
#endif
	frameBuffer->blit();
}

void CScreenSetup::paintBorder( int pselected )
{
#ifdef MARTII
	selected = pselected;
#else
	if ( pselected == 0 )
		paintBorderUL();
	else
		paintBorderLR();

	paintCoords();
#endif
}

#ifdef MARTII
void CScreenSetup::unpaintBorder(int)
{
}
#else
void CScreenSetup::unpaintBorder(int pselected)
{
	int cx = x_coord[pselected] - 96 * pselected;
	int cy = y_coord[pselected] - 96 * pselected;
	frameBuffer->paintBoxRel(cx, cy, 96, 96, make16color(0xA0A0A0));
}
#endif

void CScreenSetup::paintIcons(int pselected)
{
#ifdef MARTII
	frameBuffer->paintBoxRel(x_box, y_box, BoxWidth, BoxHeight, (pselected == 0) ? COL_MENUCONTENTSELECTED_PLUS_0 : COL_MENUCONTENT_PLUS_0);   //upper selected box
	frameBuffer->paintBoxRel(x_box, y_box + BoxHeight, BoxWidth, BoxHeight, (pselected == 1) ? COL_MENUCONTENTSELECTED_PLUS_0 : COL_MENUCONTENT_PLUS_0); //lower selected box
	if (channel_id)
		frameBuffer->paintBoxRel(x_box, y_box + BoxHeight * 2, BoxWidth, BoxHeight, (pselected == 3) ? COL_MENUCONTENTSELECTED_PLUS_0 : COL_MENUCONTENT_PLUS_0); //lower selected box
#endif
        int icol_w = 0, icol_h = 0;
        frameBuffer->getIconSize(NEUTRINO_ICON_BUTTON_RED, &icol_w, &icol_h);

	frameBuffer->paintIcon(NEUTRINO_ICON_BUTTON_RED, x_box + 5, y_box, BoxHeight);
	frameBuffer->paintIcon(NEUTRINO_ICON_BUTTON_GREEN, x_box + 5, y_box+BoxHeight, BoxHeight);
#ifdef MARTII
	if (channel_id)
		frameBuffer->paintIcon(NEUTRINO_ICON_BUTTON_YELLOW, x_box + 5, y_box+BoxHeight*2, BoxHeight);
#endif

	g_Font[SNeutrinoSettings::FONT_TYPE_MENU]->RenderString(x_box + icol_w + 10, y_box + BoxHeight, BoxWidth,
		g_Locale->getText(LOCALE_SCREENSETUP_UPPERLEFT ), (pselected == 0) ? COL_MENUCONTENTSELECTED:COL_MENUCONTENT , 0, true); // UTF-8
        g_Font[SNeutrinoSettings::FONT_TYPE_MENU]->RenderString(x_box + icol_w + 10, y_box + BoxHeight * 2, BoxWidth,
		g_Locale->getText(LOCALE_SCREENSETUP_LOWERRIGHT), (pselected == 1) ? COL_MENUCONTENTSELECTED:COL_MENUCONTENT, 0, true); // UTF-8
#ifdef MARTII
	if (channel_id)
		g_Font[SNeutrinoSettings::FONT_TYPE_MENU]->RenderString(x_box + icol_w + 10, y_box + BoxHeight * 3, BoxWidth,
			g_Locale->getText(LOCALE_SCREENSETUP_REMOVE), (pselected == 2) ? COL_MENUCONTENTSELECTED:COL_MENUCONTENT, 0, true); // UTF-8
#endif
}

void CScreenSetup::paintBorderUL()
{
#ifdef MARTII
	frameBuffer->paintIcon(NEUTRINO_ICON_BORDER_UL, 0, 0);
#else
	frameBuffer->paintIcon(NEUTRINO_ICON_BORDER_UL, x_coord[0], y_coord[0] );
#endif
}

void CScreenSetup::paintBorderLR()
{
#ifdef MARTII
	frameBuffer->paintIcon(NEUTRINO_ICON_BORDER_LR, frameBuffer->getScreenWidth() - 1 - 96, frameBuffer->getScreenHeight() - 1 - 96 );
#else
	frameBuffer->paintIcon(NEUTRINO_ICON_BORDER_LR, x_coord[1]- 96, y_coord[1]- 96 );
#endif
}

#ifndef MARTII
void CScreenSetup::paintCoords()
{
	Font *f = g_Font[SNeutrinoSettings::FONT_TYPE_MENU];
	int w = f->getRenderWidth("EX: 2222") * 5 / 4;	/* half glyph border left and right */
	int fh = f->getHeight();
	int h = fh * 4;		/* 4 lines, fonts have enough space around them, no extra border */

	int x1 = (frameBuffer->getScreenWidth(true) - w) / 2;	/* centered */
	int y1 = frameBuffer->getScreenHeight(true) / 2 - h;	/* above center, to avoid conflict */
	int x2 = x1 + w / 10;
	int y2 = y1 + fh;

	frameBuffer->paintBoxRel(x1, y1, w, h, COL_MENUCONTENT_PLUS_0);

	char str[4][16];
	snprintf(str[0], 16, "SX: %d", x_coord[0]);
	snprintf(str[1], 16, "SY: %d", y_coord[0]);
	snprintf(str[2], 16, "EX: %d", x_coord[1]);
	snprintf(str[3], 16, "EY: %d", y_coord[1]);
	/* the code is smaller with this loop instead of open-coded 4x RenderString() :-) */
	for (int i = 0; i < 4; i++)
	{
		f->RenderString(x2, y2, w, str[i], COL_MENUCONTENT);
		y2 += fh;
	}
}
#endif

void CScreenSetup::paint()
{
	if (!frameBuffer->getActive())
		return;

#ifndef MARTII
	int w = (int) frameBuffer->getScreenWidth(true);
	int h = (int) frameBuffer->getScreenHeight(true);
#endif

#ifdef MARTII
	if (channel_id)
		frameBuffer->setBorderColor(0x44444444);
	else
		frameBuffer->setBorderColor(0x88888888);
#else
	frameBuffer->paintBox(0,0, w, h, make16color(0xA0A0A0));

	for(int count = 0; count < h; count += 15)
		frameBuffer->paintHLine(0, w-1, count, make16color(0x505050) );

	for(int count = 0; count < w; count += 15)
		frameBuffer->paintVLine(count, 0, h-1, make16color(0x505050) );

	frameBuffer->paintBox(0, 0, w/3, h/3, make16color(0xA0A0A0));
	frameBuffer->paintBox(w-w/3, h-h/3, w-1, h-1, make16color(0xA0A0A0));
#endif

#ifndef MARTII
	frameBuffer->paintBoxRel(x_box, y_box, BoxWidth, BoxHeight, COL_MENUCONTENTSELECTED_PLUS_0);   //upper selected box
	frameBuffer->paintBoxRel(x_box, y_box + BoxHeight, BoxWidth, BoxHeight, COL_MENUCONTENT_PLUS_0); //lower selected box
#endif

	paintIcons(0);
	paintBorderUL();
	paintBorderLR();
#ifndef MARTII
	paintCoords();
#endif
}
