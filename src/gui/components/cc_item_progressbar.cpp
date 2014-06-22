/*
	Based up Neutrino-GUI - Tuxbox-Project
	Copyright (C) 2001 by Steffen Hehn 'McClean'

	(C) 2008,2013,2014 by Thilo Graf
	(C) 2009,2010,2013 Stefan Seyfried

	License: GPL

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public
	License along with this program; if not, write to the
	Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
	Boston, MA  02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <math.h>
#include <global.h>
#include <neutrino.h>

#include "cc_item_progressbar.h"
#include "cc_item_shapes.h"
#define ITEMW 4
#define POINT 2

#define RED    0xFF0000
#define GREEN  0x00FF00
#define YELLOW 0xFFFF00

CProgressBar::CProgressBar(	const int x_pos, const int y_pos, const int w, const int h,
				fb_pixel_t color_frame, fb_pixel_t color_body, fb_pixel_t color_shadow,
				const fb_pixel_t active_col, const fb_pixel_t passive_col,
				const bool blinkenlights,
				const int r, const int g, const int b,
				const bool inv,
				CComponentsForm *parent)
{
	//CComponentsItem
	cc_item_type 	= CC_ITEMTYPE_PROGRESSBAR;

	//CComponents
	x 		= x_pos;
	y 		= y_pos;
	width		= w;
	height		= h;

	col_frame 	= color_frame;
	col_body	= color_body;
	col_shadow	= color_shadow;

 	pb_blink 	= blinkenlights;
 	pb_invert 	= inv;
	pb_red 		= r;
	pb_green 	= g;
	pb_yellow 	= b;
	pb_active_col	= active_col;
	pb_passive_col 	= passive_col;

	pb_bl_changed 		= g_settings.progressbar_color;
	pb_last_width 		= -1;
	pb_value		= 0;
	pb_max_value		= 0;

	// init start positions x/y active bar
	pb_x 			= x + fr_thickness;
	pb_y 			= y + fr_thickness;
	pb_active_width 	= 0;
	pb_max_width		= width - 2*fr_thickness;
	pb_height		= 0;
	pb_start_x_passive 	= 0;
	pb_passive_width 	= width;
	initParent(parent);
}

//calculate bar dimensions
void CProgressBar::initDimensions()
{
	//prevent stupid callers, can give invalid values like "-1"...
	if (pb_value < 0)
		pb_value = 0;
	if (pb_value > pb_max_value)
		pb_max_value = pb_value;

	//assign start positions x/y active bar
	//NOTE: real values are only reqiured, if we paint active/passive bar with own render methodes or not embedded cc-items
	pb_x = (cc_parent ? cc_xr : x) + fr_thickness;
	pb_y = (cc_parent ? cc_yr : y) + fr_thickness;

	// width for active bar with current value
	pb_active_width = max(0, pb_last_width);
	if (pb_max_value)
		pb_active_width = (width - 2*fr_thickness) * pb_value / pb_max_value;

	// max width active/passive bar
	pb_max_width = width - 2*fr_thickness;

	// max height of active/passive bar
	pb_height = height - 2*fr_thickness;

	pb_start_x_passive = pb_x + pb_active_width;

	pb_passive_width = pb_max_width - pb_active_width;

	// background = frame
	if ( col_frame == 0 )
		col_frame = pb_active_col;
}

class CProgressBarCache
{
	private:
		// keys to lookup:
		int pb_height, pb_max_width;
		int pb_active_col, pb_passive_col;
		int design;
		bool colored, pb_invert, gradient;
		int pb_red, pb_yellow, pb_green;

		int yoff;

		fb_pixel_t *active;
		fb_pixel_t *passive;
		static inline unsigned int make16color(__u32 rgb){return 0xFF000000 | rgb;};
		CProgressBarCache(int _height, int _width, int _pb_active_col, int _pb_passive_col, int _design, bool _colored, bool _invert, bool _gradient, int _red, int _yellow, int _green)
			: pb_height(_height), pb_max_width(_width), pb_active_col(_pb_active_col), pb_passive_col(_pb_passive_col), design(_design), colored(_colored), pb_invert(_invert), gradient(_gradient),
			  pb_red(_red), pb_yellow(_yellow), pb_green(_green), yoff(0)
			{
				if (_colored) {
					active = paintAdvanced(true);
					passive = paintAdvanced(false);
				} else {
					active = paintSimple(true);
					passive = paintSimple(false);
				}
			}
		void paintBoxRel(fb_pixel_t *b, int x, int y, int dx, int dy, fb_pixel_t col);
		void applyGradient(fb_pixel_t *buf);
		fb_pixel_t *paintSimple(bool full);
		fb_pixel_t *paintAdvanced(bool full);
	public:
		void paint(int x, int y, int pb_active_width, int pb_passive_width);
		static CProgressBarCache *lookup(int _height, int _width, int _pb_active_col, int _pb_passive_col, int _design, bool _colored, bool _invert, bool _gradient, int _red, int _yellow, int _green);
};

CProgressBarCache *CProgressBarCache::lookup(int _height, int _width, int _pb_active_col, int _pb_passive_col, int _design, bool _colored, bool _invert, bool _gradient, int _red, int _yellow, int _green)
{
	static std::vector<CProgressBarCache *> pbCache;

	// sanitize
	if (!_colored)
		_design = -1, _red = _yellow = _green = 0;

	// lookup
	std::vector<CProgressBarCache *>::iterator it = pbCache.begin();
	for (; it != pbCache.end() && ((*it)->pb_height != _height || (*it)->pb_max_width != _width ||
				       (*it)->pb_active_col != _pb_active_col || (*it)->pb_passive_col != _pb_passive_col ||
				       (*it)->design != _design || (*it)->colored != _colored || (*it)->pb_invert != _invert || (*it)->gradient != _gradient ||
				       (*it)->pb_red != _red || (*it)->pb_yellow != _yellow || (*it)->pb_green != _green); ++it);
	if (it != pbCache.end())
		return *it;

	CProgressBarCache *pbc = new CProgressBarCache(_height, _width, _pb_active_col, _pb_passive_col, _design, _colored, _invert, _gradient, _red, _yellow, _green);
	pbCache.push_back(pbc);
	return pbc;
}

void CProgressBarCache::paint(int x, int y, int pb_active_width, int pb_passive_width)
{
	y += yoff;
	static CFrameBuffer *frameBuffer = CFrameBuffer::getInstance();
	unsigned int stride = frameBuffer->getStride() / sizeof(fb_pixel_t);
	fb_pixel_t *p = frameBuffer->getFrameBufferPointer() + y * stride + x;
	int off = stride - pb_max_width;
	if (pb_active_width > pb_max_width)
		pb_active_width = pb_max_width;
	if (pb_active_width + pb_passive_width != pb_max_width)
		pb_passive_width = pb_max_width - pb_active_width;
	fb_pixel_t *ap = active;
	fb_pixel_t *pp = passive;
	for (int h = 0; h < pb_height; h++) {
		int w = 0;
		for (; w < pb_active_width; w++, p++, ap++)
			if (*ap)
				*p = *ap;
		pp += pb_active_width;
		for (; w < pb_max_width; w++, p++, pp++)
			if (*pp)
				*p = *pp;
		ap += pb_passive_width;
		p += off;
	}
}

void CProgressBarCache::paintBoxRel(fb_pixel_t *b, int x, int y, int dx, int dy, fb_pixel_t col)
{
	if (x < 0) {
		dx -= x;
		x = 0;
	}
	if (y < 0) {
		dy -= y;
		y = 0;
	}
	if (x + dx > pb_max_width)
		dx = pb_max_width - x;
	if (y + dy > pb_height)
		dy = pb_height - y;
	if (dx < 1 || dy < 1)
		return;
	b += pb_max_width * y + x;
	fb_pixel_t *e = b + pb_max_width * (dy - 1) + dx;
	int off = pb_max_width - dx;
	while (b < e) {
		fb_pixel_t *ex = b + dx;
		while (b < ex)
			*b++ = col;
		b += off;
	}
}

fb_pixel_t *CProgressBarCache::paintSimple(bool full)
{
	fb_pixel_t *buf = (fb_pixel_t *) calloc(1, pb_max_width * pb_height * sizeof(fb_pixel_t));
	if (buf)
		paintBoxRel(buf, 0, 0, pb_max_width, pb_height, full ? pb_active_col : pb_passive_col);
	if (gradient)
		applyGradient(buf);
	return buf;
}

fb_pixel_t *CProgressBarCache::paintAdvanced(bool full)
{
	fb_pixel_t *buf = (fb_pixel_t *) calloc(1, pb_max_width * pb_height * sizeof(fb_pixel_t));
	if (!buf)
		return NULL;

	int itemw = ITEMW, itemh = ITEMW, pointx = POINT, pointy = POINT;
	switch (design){
		default:
		case CProgressBar::PB_MATRIX: // ::::: matrix
			break;
		case CProgressBar::PB_LINES_V: // ||||| vert. lines
			itemh = pb_height;
			pointy = pb_height;
			break;
		case CProgressBar::PB_LINES_H: // ===== horiz. lines
			itemw = POINT;
			break;
		case CProgressBar::PB_COLOR: // filled color
			itemw = POINT;
			itemh = pb_height;
			pointy = pb_height;
			break;
	}

	const int spc = itemh - pointy;			/* space between horizontal lines / points */
	int hcnt = (pb_height + spc) / itemh;		/* how many POINTs is the bar high */
	yoff = (pb_height + spc - itemh * hcnt) / 2;

	//printf("height: %d itemh: %d hcnt: %d yoff: %d spc: %d\n", height, itemh, hcnt, yoff, spc);
	/* red, yellow, green are given in percent */
	int rd = pb_red    * pb_max_width / (100 * itemw);	/* how many POINTs red */
	int yw = pb_yellow * pb_max_width / (100 * itemw);	/* how many POINTs yellow */
	int gn = pb_green  * pb_max_width / (100 * itemw);	/* how many POINTs green */

	int maxi = full ? (pb_max_width / itemw) : 0; /* how many POINTs is the active bar */
	int total = pb_max_width / itemw + 1;	/* total number of POINTs, plus one in case of a remainder */

	int i, j;

	uint32_t rgb;
	int step, off;
	int b = 0;
	uint32_t diff = 0;
	for (i = 0; (i < rd) && (i < maxi); i++) {
		diff = i * 255 / rd;
		if (pb_invert)
			rgb = GREEN + (diff << 16); // adding red
		else
			rgb = RED + (diff << 8); // adding green
		fb_pixel_t color = make16color(rgb);
		for (j = 0; j < hcnt; j++) {
			int sh_x = i * itemw;
			int sh_y = j * itemh;
			paintBoxRel(buf, sh_x, sh_y, pointx, pointy, color);
		}
	}
	step = yw - rd - 1;
	if (step < 1)
		step = 1;
	for (; (i < yw) && (i < maxi); i++) {
		diff = b++ * 255 / step / 2;
		if (pb_invert)
			rgb = YELLOW - (diff << 8); // removing green
		else
			rgb = YELLOW - (diff << 16); // removing red
		fb_pixel_t color = make16color(rgb);
		for (j = 0; j < hcnt; j++) {
			int sh_x = i * itemw;
			int sh_y = j * itemh;
			paintBoxRel(buf, sh_x, sh_y, pointx, pointy, color);
		}
	}
	off = diff;
	b = 0;
	step = gn - yw - 1;
	if (step < 1)
		step = 1;
	for (; (i < gn) && (i < maxi); i++) {
		diff = b++ * 255 / step / 2 + off;
		if (pb_invert)
			rgb = YELLOW - (diff << 8); // removing green
		else
			rgb = YELLOW - (diff << 16); // removing red
		fb_pixel_t color = make16color(rgb);
		for (j = 0; j < hcnt; j++) {
			int sh_x = i * itemw;
			int sh_y = j * itemh;
			paintBoxRel(buf, sh_x, sh_y, pointx, pointy, color);
		}
	}
	for(; i < total + 1; i++)
		for (j = 0; j < hcnt; j++) {
			int sh_x = i * itemw;
			int sh_y = j * itemh;
			paintBoxRel(buf, sh_x, sh_y, pointx, pointy, pb_passive_col);
		}

	if (gradient)
		applyGradient(buf);

	return buf;
}

void CProgressBarCache::applyGradient(fb_pixel_t *b)
{
	for (int _y = 0; _y < pb_height; _y++) {
		int _o = _y * pb_max_width;
		fb_pixel_t last_old = 0;
		fb_pixel_t last_new = 0;
		for (int _x = pb_max_width - 1; _x > -1; _x--) {
			fb_pixel_t &v = *(b + _o + _x);
			if (v != last_old) {
				last_old = v;
				double s = sin((_y + .5) * M_PI / pb_height) * .8 + .2;
				float fr = ((last_old >> 16) & 0xff) * s + 0.5;
				float fg = ((last_old >>  8) & 0xff) * s + 0.5;
				float fb = ((last_old      ) & 0xff) * s + 0.5;
				last_new = (last_old & 0xFF000000)
					| ((0xff & (int)fr) << 16)
					| ((0xff & (int)fg) <<  8)
					| ((0xff & (int)fb)      );
			}
			v = last_new;
		}
	}
}

void CProgressBar::paintProgress(bool do_save_bg)
{
	if (g_settings.progressbar_gradient)
		setFrameThickness(0);

	if(pb_bl_changed != g_settings.progressbar_color) {
		pb_bl_changed = g_settings.progressbar_color;
		reset();
	}

	initDimensions();

	//body
	if (pb_last_width == -1 && col_body != 0) /* first paint */
		paintInit(do_save_bg); 

	//progress
	if (pb_active_width != pb_last_width) {
		CProgressBarCache *pbc = CProgressBarCache::lookup(pb_height, pb_max_width, pb_active_col, pb_passive_col, g_settings.progressbar_design, pb_blink && g_settings.progressbar_color, pb_invert, g_settings.progressbar_gradient, pb_red, pb_yellow, pb_green);
		if (pbc)
			pbc->paint(pb_x, pb_y, pb_active_width, pb_passive_width);
		is_painted = true;
	}

	if (is_painted)
		pb_last_width = pb_active_width;
}


void CProgressBar::paint(bool do_save_bg)
{
  	paintProgress(do_save_bg);
}
