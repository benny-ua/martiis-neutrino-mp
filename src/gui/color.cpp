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

#include <gui/color.h>
#include <stdio.h>
#include <math.h>

#ifndef FLT_EPSILON
#define FLT_EPSILON 1E-5
#endif

int convertSetupColor2RGB(const unsigned char r, const unsigned char g, const unsigned char b)
{
	unsigned char red =	(int)r * 255 / 100;
	unsigned char green =	(int)g * 255 / 100;
	unsigned char blue =	(int)b * 255 / 100;

	return (red << 16) | (green << 8) | blue;
}

int convertSetupAlpha2Alpha(unsigned char alpha)
{
	if(alpha == 0) return 0xFF;
	else if(alpha >= 100) return 0;
	int a = 100 - alpha;
	int ret = a * 0xFF / 100;
	return ret;
}

void recalcColor(unsigned char &orginal, int fade)
{
	if(fade==100)
	{
		return;
	}
	int color =  orginal * fade / 100;
	if(color>255)
		color=255;
	if(color<0)
		color=0;
	orginal = color;
}

void protectColor( unsigned char &r, unsigned char &g, unsigned char &b, bool protect )
{
	if (!protect)
		return;
	if ((r==0) && (g==0) && (b==0))
	{
		r=1;
		g=1;
		b=1;
	}
}

void fadeColor(unsigned char &r, unsigned char &g, unsigned char &b, int fade, bool protect)
{
	recalcColor(r, fade);
	recalcColor(g, fade);
	recalcColor(b, fade);
	protectColor(r,g,b, protect);
}

unsigned char getBrightnessRGB(fb_pixel_t color)
{
	RgbColor rgb;
	rgb.r  = (color & 0x00FF0000) >> 16;
	rgb.g  = (color & 0x0000FF00) >>  8;
	rgb.b  =  color & 0x000000FF;

	return rgb.r > rgb.g ? (rgb.r > rgb.b ? rgb.r : rgb.b) : (rgb.g > rgb.b ? rgb.g : rgb.b);
}

fb_pixel_t changeBrightnessRGBRel(fb_pixel_t color, int br)
{
	int br_ = (int)getBrightnessRGB(color);
	br_ += br;
	if (br_ < 0) br_ = 0;
	if (br_ > 255) br_ = 255;
	return changeBrightnessRGB(color, (unsigned char)br_);
}

void changeBrightnessRGBRel2(RgbColor *rgb, int br)
{
	fb_pixel_t color = (((rgb->r << 16) & 0x00FF0000) |
			    ((rgb->g <<  8) & 0x0000FF00) |
			    ((rgb->b      ) & 0x000000FF));
	int br_ = (int)getBrightnessRGB(color);
	br_ += br;
	if (br_ < 0) br_ = 0;
	if (br_ > 255) br_ = 255;

	HsvColor hsv;
	Rgb2Hsv(rgb, &hsv);
	hsv.v = br_;
	Hsv2Rgb(&hsv, rgb);
}

fb_pixel_t changeBrightnessRGB(fb_pixel_t color, unsigned char br)
{
	HsvColor hsv;
	RgbColor rgb;

	unsigned char tr;
	tr     = (color & 0xFF000000) >> 24;
	rgb.r  = (color & 0x00FF0000) >> 16;
	rgb.g  = (color & 0x0000FF00) >>  8;
	rgb.b  =  color & 0x000000FF;

	Rgb2Hsv(&rgb, &hsv);
	hsv.v = br;
	Hsv2Rgb(&hsv, &rgb);

	return (((tr    << 24) & 0xFF000000) |
		((rgb.r << 16) & 0x00FF0000) |
		((rgb.g <<  8) & 0x0000FF00) |
		((rgb.b      ) & 0x000000FF));
}

void Hsv2Rgb(HsvColor *hsv, RgbColor *rgb)
{
<<<<<<< HEAD
	unsigned char region, remainder, p, q, t;

	if (hsv->s == 0) {
		rgb->r = hsv->v;
		rgb->g = hsv->v;
		rgb->b = hsv->v;
		return;
	}

	region = hsv->h / 43;
	remainder = (hsv->h - (region * 43)) * 6;

	p = (hsv->v * (255 - hsv->s)) >> 8;
	q = (hsv->v * (255 - ((hsv->s * remainder) >> 8))) >> 8;
	t = (hsv->v * (255 - ((hsv->s * (255 - remainder)) >> 8))) >> 8;

	switch (region) {
		case 0:
			rgb->r = hsv->v; rgb->g = t; rgb->b = p;
			break;
		case 1:
			rgb->r = q; rgb->g = hsv->v; rgb->b = p;
			break;
		case 2:
			rgb->r = p; rgb->g = hsv->v; rgb->b = t;
			break;
		case 3:
			rgb->r = p; rgb->g = q; rgb->b = hsv->v;
			break;
		case 4:
			rgb->r = t; rgb->g = p; rgb->b = hsv->v;
			break;
		default:
			rgb->r = hsv->v; rgb->g = p; rgb->b = q;
			break;
=======
	float f_H = hsv->h;
	float f_S = hsv->s;
	float f_V = hsv->v;
	if (fabsf(f_S) < FLT_EPSILON) {
		rgb->r = (uint8_t)(f_V * 255);
		rgb->g = (uint8_t)(f_V * 255);
		rgb->b = (uint8_t)(f_V * 255);

	} else {
		float f_R;
		float f_G;
		float f_B;
		float hh = f_H;
		if (hh >= 360) hh = 0;
		hh /= 60;
		int i = (int)hh;
		float ff = hh - (float)i;
		float p = f_V * (1 - f_S);
		float q = f_V * (1 - (f_S * ff));
		float t = f_V * (1 - (f_S * (1 - ff)));

		switch (i) {
			case 0:
				f_R = f_V; f_G = t; f_B = p;
				break;
			case 1:
				f_R = q; f_G = f_V; f_B = p;
				break;
			case 2:
				f_R = p; f_G = f_V; f_B = t;
				break;
			case 3:
				f_R = p; f_G = q; f_B = f_V;
				break;
			case 4:
				f_R = t; f_G = p; f_B = f_V;
				break;
			case 5:
			default:
				f_R = f_V; f_G = p; f_B = q;
				break;
		}
		rgb->r = (uint8_t)(f_R * 255);
		rgb->g = (uint8_t)(f_G * 255);
		rgb->b = (uint8_t)(f_B * 255);
>>>>>>> origin/next-cc
	}

	return;
}

void Rgb2Hsv(RgbColor *rgb, HsvColor *hsv)
{
<<<<<<< HEAD
	unsigned char rgbMin, rgbMax;

	rgbMin = rgb->r < rgb->g ? (rgb->r < rgb->b ? rgb->r : rgb->b) : (rgb->g < rgb->b ? rgb->g : rgb->b);
	rgbMax = rgb->r > rgb->g ? (rgb->r > rgb->b ? rgb->r : rgb->b) : (rgb->g > rgb->b ? rgb->g : rgb->b);

	hsv->v = rgbMax;
	if (hsv->v == 0) {
		hsv->h = 0;
		hsv->s = 0;
		return;
	}

	hsv->s = 255 * long(rgbMax - rgbMin) / hsv->v;
	if (hsv->s == 0) {
		hsv->h = 0;
		return;
=======
	float f_R = (float)rgb->r / (float)255;
	float f_G = (float)rgb->g / (float)255;
	float f_B = (float)rgb->b / (float)255;

	float min = f_R < f_G ? (f_R < f_B ? f_R : f_B) : (f_G < f_B ? f_G : f_B);
	float max = f_R > f_G ? (f_R > f_B ? f_R : f_B) : (f_G > f_B ? f_G : f_B);
	float delta = max - min;

	float f_V = max;
	float f_H = 0;
	float f_S = 0;

	if (fabsf(delta) < FLT_EPSILON) { //gray
		f_S = 0;
		f_H = 0;
	} else {
		f_S = (delta / max);
		if (f_R >= max)
			f_H = (f_G - f_B) / delta;
		else if (f_G >= max)
			f_H = 2 + (f_B - f_R) / delta;
		else
			f_H = 4 + (f_R - f_G) / delta;

		f_H *= 60;
		if (f_H < 0)
			f_H += 360;
>>>>>>> origin/next-cc
	}

	if (rgbMax == rgb->r)
		hsv->h = 0 + 43 * (rgb->g - rgb->b) / (rgbMax - rgbMin);
	else if (rgbMax == rgb->g)
		hsv->h = 85 + 43 * (rgb->b - rgb->r) / (rgbMax - rgbMin);
	else
		hsv->h = 171 + 43 * (rgb->r - rgb->g) / (rgbMax - rgbMin);

	return;
}
