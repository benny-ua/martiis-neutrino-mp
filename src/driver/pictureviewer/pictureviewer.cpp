#include "config.h"
#include <global.h>
#include <neutrino.h>
#include <system/helpers.h>
#include "pictureviewer.h"
#include "pv_config.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <curl/curl.h>

#include <cs_api.h>

#include <algorithm>

#ifdef FBV_SUPPORT_GIF
extern int fh_gif_getsize (const char *, int *, int *, int, int);
extern int fh_gif_load (const char *, unsigned char **, int *, int *);
extern int fh_gif_id (const char *);
#endif
#ifdef FBV_SUPPORT_JPEG
extern int fh_jpeg_getsize (const char *, int *, int *, int, int);
extern int fh_jpeg_load (const char *, unsigned char **, int *, int *);
extern int fh_jpeg_id (const char *);
#endif
#ifdef FBV_SUPPORT_PNG
extern int fh_png_getsize (const char *, int *, int *, int, int);
extern int fh_png_load (const char *, unsigned char **, int *, int *);
extern int png_load_ext (const char * name, unsigned char ** buffer, int * xp, int * yp, int * bpp);
extern int fh_png_id (const char *);
#endif
#ifdef FBV_SUPPORT_BMP
extern int fh_bmp_getsize (const char *, int *, int *, int, int);
extern int fh_bmp_load (const char *, unsigned char **, int *, int *);
extern int fh_bmp_id (const char *);
#endif
#ifdef FBV_SUPPORT_CRW
extern int fh_crw_getsize (const char *, int *, int *, int, int);
extern int fh_crw_load (const char *, unsigned char **, int *, int *);
extern int fh_crw_id (const char *);
#endif

double CPictureViewer::m_aspect_ratio_correction;

void CPictureViewer::add_format (int (*picsize) (const char *, int *, int *, int, int), int (*picread) (const char *, unsigned char **, int *, int *), int (*id) (const char *))
{
  CFormathandler *fhn;
  fhn = (CFormathandler *) malloc (sizeof (CFormathandler));
  fhn->get_size = picsize;
  fhn->get_pic = picread;
  fhn->id_pic = id;
  fhn->next = fh_root;
  fh_root = fhn;
}

void CPictureViewer::getSupportedImageFormats(std::vector<std::string>& exts)
{
#ifdef FBV_SUPPORT_PNG
	exts.push_back(".png");
#endif
#ifdef FBV_SUPPORT_GIF
	exts.push_back(".gif");
#endif
#ifdef FBV_SUPPORT_JPEG
	exts.push_back(".jpg");
	exts.push_back(".jpeg");
#endif
#ifdef FBV_SUPPORT_BMP
	exts.push_back(".bmp");
#endif
#ifdef FBV_SUPPORT_CRW
	exts.push_back(".crw");
#endif
}

void CPictureViewer::init_handlers (void)
{
#ifdef FBV_SUPPORT_PNG
  add_format (fh_png_getsize, fh_png_load, fh_png_id);
#endif
#ifdef FBV_SUPPORT_GIF
  add_format (fh_gif_getsize, fh_gif_load, fh_gif_id);
#endif
#ifdef FBV_SUPPORT_JPEG
  add_format (fh_jpeg_getsize, fh_jpeg_load, fh_jpeg_id);
#endif
#ifdef FBV_SUPPORT_BMP
  add_format (fh_bmp_getsize, fh_bmp_load, fh_bmp_id);
#endif
#ifdef FBV_SUPPORT_CRW
  add_format (fh_crw_getsize, fh_crw_load, fh_crw_id);
#endif
}

CPictureViewer::CFormathandler * CPictureViewer::fh_getsize (const char *name, int *x, int *y, int width_wanted, int height_wanted)
{
	CFormathandler *fh;
	for (fh = fh_root; fh != NULL; fh = fh->next) {
		if (fh->id_pic (name))
			if (fh->get_size (name, x, y, width_wanted, height_wanted) == FH_ERROR_OK)
				return (fh);
	}
	return (NULL);
}
std::string CPictureViewer::DownloadImage(std::string url)
{
	if (strstr(url.c_str(), "://")) {
		std::string tmpname = "/tmp/pictureviewer" + url.substr(url.find_last_of("."));
		FILE *tmpFile = fopen(tmpname.c_str(), "wb");
		if (tmpFile) {
			CURL *ch = curl_easy_init();
			curl_easy_setopt(ch, CURLOPT_VERBOSE, 0L);
			curl_easy_setopt(ch, CURLOPT_NOPROGRESS, 1L);
			curl_easy_setopt(ch, CURLOPT_NOSIGNAL, 1L);
			curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, NULL);
			curl_easy_setopt(ch, CURLOPT_WRITEDATA, tmpFile);
			curl_easy_setopt(ch, CURLOPT_FAILONERROR, 1L);
			curl_easy_setopt(ch, CURLOPT_URL, url.c_str());
			curl_easy_perform(ch);
			curl_easy_cleanup(ch);
			fclose(tmpFile);
			url = true;
		}
		url = tmpname;
	}
	return url;
}

bool CPictureViewer::DecodeImage (const std::string & _name, bool showBusySign, bool unscaled)
{
	// dbout("DecodeImage {\n"); 
#if 0 // quick fix for issue #245. TODO more smart fix for this problem
	if (name == m_NextPic_Name) {
		//      dbout("DecodeImage }\n"); 
		return true;
	}
#endif
	int x, y, imx, imy;

	// 	int xs = CFrameBuffer::getInstance()->getScreenWidth(true);
	// 	int ys = CFrameBuffer::getInstance()->getScreenHeight(true);

	// Show red block for "next ready" in view state
	if (showBusySign)
		showBusy (m_startx + 3, m_starty + 3, 10, 0xff, 00, 00);

	bool url = false;

	std::string name  = DownloadImage(_name);

	CFormathandler *fh;
	if (unscaled)
		fh = fh_getsize (name.c_str (), &x, &y, INT_MAX, INT_MAX);
	else
		fh = fh_getsize (name.c_str (), &x, &y, m_endx - m_startx, m_endy - m_starty);
	if (fh) {
		if (m_NextPic_Buffer != NULL) {
			free (m_NextPic_Buffer);
			m_NextPic_Buffer = NULL;
		}
/*
#if HAVE_SPARK_HARDWARE
		size_t new_bpasize = x * y * 3;

		if (!bpamem || bpasize < new_bpasize) {
			frameBuffer->freeBPAMem(bpafd, bpamem, bpasize);
			bpasize = new_bpasize;
			// try bigphysarea first
			frameBuffer->allocBPAMem(bpafd, bpamem, bpasize, CFbAccel::BPApart_big);
			if (!bpamem) // but fallback to video
				frameBuffer->allocBPAMem(bpafd, bpamem, bpasize, CFbAccel::BPApart_vid);
		}

		if (bpamem) {
			bpa_pan_w = bpa_w = x;
			bpa_pan_h = bpa_h = y;
			bpa_x = bpa_y = 0;

			if (fh->get_pic(name.c_str(), &bpamem, &x, &y) == FH_ERROR_OK) {
				if ((x > (m_endx - m_startx) || y > (m_endy - m_starty)) && m_scaling != NONE && !unscaled) {
					if ((m_aspect_ratio_correction * y * (m_endx - m_startx) / x) <= (m_endy - m_starty)) {
						imx = (m_endx - m_startx);
						imy = (int) (m_aspect_ratio_correction * y * (m_endx - m_startx) / x);
					} else {
						imx = (int) ((1.0 / m_aspect_ratio_correction) * x * (m_endy - m_starty) / y);
						imy = (m_endy - m_starty);
					}
					fb_w = fb_w_initial = imx;
					fb_h = fb_h_initial = imy;
					fb_x = (DEFAULT_XRES - fb_w) >> 1;
					fb_y = (DEFAULT_YRES - fb_h) >> 1;
					frameBuffer->paintBoxRel(0, 0, DEFAULT_XRES, DEFAULT_YRES, 0);
					frameBuffer->blitBPA2FB(bpamem, SURF_BGR888, bpa_w, bpa_h, bpa_x, bpa_y, bpa_pan_w, bpa_pan_h, fb_x, fb_y, fb_w, fb_h, true);
					frameBuffer->blit();
				}
			}
			return false;
		}
#endif
*/
		m_NextPic_Buffer = (unsigned char *) malloc (x * y * 3);
		if (m_NextPic_Buffer == NULL) {
			printf ("DecodeImage: Error: malloc\n");
			return false;
		}
		//      dbout("---Decoding Start(%d/%d)\n",x,y);
		if (fh->get_pic (name.c_str (), &m_NextPic_Buffer, &x, &y) == FH_ERROR_OK) {
			//          dbout("---Decoding Done\n");
			if ((x > (m_endx - m_startx) || y > (m_endy - m_starty)) && m_scaling != NONE && !unscaled) {
				if ((m_aspect_ratio_correction * y * (m_endx - m_startx) / x) <= (m_endy - m_starty)) {
					imx = (m_endx - m_startx);
					imy = (int) (m_aspect_ratio_correction * y * (m_endx - m_startx) / x);
				} else {
					imx = (int) ((1.0 / m_aspect_ratio_correction) * x * (m_endy - m_starty) / y);
					imy = (m_endy - m_starty);
				}
				m_NextPic_Buffer = Resize(m_NextPic_Buffer, x, y, imx, imy, m_scaling);
				x = imx;
				y = imy;
			}
			m_NextPic_X = x;
			m_NextPic_Y = y;
			if (x < (m_endx - m_startx))
				m_NextPic_XPos = (m_endx - m_startx - x) / 2 + m_startx;
			else
				m_NextPic_XPos = m_startx;
			if (y < (m_endy - m_starty))
				m_NextPic_YPos = (m_endy - m_starty - y) / 2 + m_starty;
			else
				m_NextPic_YPos = m_starty;
			if (x > (m_endx - m_startx))
				m_NextPic_XPan = (x - (m_endx - m_startx)) / 2;
			else
				m_NextPic_XPan = 0;
			if (y > (m_endy - m_starty))
				m_NextPic_YPan = (y - (m_endy - m_starty)) / 2;
			else
				m_NextPic_YPan = 0;
		} else {
			printf ("Unable to read file !\n");
			free (m_NextPic_Buffer);
			m_NextPic_Buffer = (unsigned char *) malloc (3);
			if (m_NextPic_Buffer == NULL) {
				printf ("DecodeImage: Error: malloc\n");
				return false;
			}
			memset (m_NextPic_Buffer, 0, 3);
			m_NextPic_X = 1;
			m_NextPic_Y = 1;
			m_NextPic_XPos = 0;
			m_NextPic_YPos = 0;
			m_NextPic_XPan = 0;
			m_NextPic_YPan = 0;
		}
	} else {
		printf ("Unable to read file or format not recognized!\n");
		if (m_NextPic_Buffer != NULL) {
			free (m_NextPic_Buffer);
		}
		m_NextPic_Buffer = (unsigned char *) malloc (3);
		if (m_NextPic_Buffer == NULL) {
			printf ("DecodeImage: Error: malloc\n");
			return false;
		}
		memset (m_NextPic_Buffer, 0, 3);
		m_NextPic_X = 1;
		m_NextPic_Y = 1;
		m_NextPic_XPos = 0;
		m_NextPic_YPos = 0;
		m_NextPic_XPan = 0;
		m_NextPic_YPan = 0;
	}
	m_NextPic_Name = name;
	if (url)
		unlink(name.c_str());
	hideBusy ();
	//   dbout("DecodeImage }\n"); 
	return (m_NextPic_Buffer != NULL);
}

void CPictureViewer::SetVisible (int startx, int endx, int starty, int endy)
{
	m_startx = startx;
	m_endx = endx;
	m_starty = starty;
	m_endy = endy;
}


bool CPictureViewer::ShowImage (const std::string & filename, bool unscaled)
{
	//  dbout("Show Image {\n");
	// Wird eh ueberschrieben ,also schonmal freigeben... (wenig speicher)
	if (m_CurrentPic_Buffer != NULL) {
		free (m_CurrentPic_Buffer);
		m_CurrentPic_Buffer = NULL;
	}
	if (DecodeImage (filename, true, unscaled))
		DisplayNextImage ();
	//  dbout("Show Image }\n");
	return true;
}

bool CPictureViewer::DisplayNextImage ()
{
	//  dbout("DisplayNextImage {\n");
	if (m_CurrentPic_Buffer != NULL) {
		free (m_CurrentPic_Buffer);
		m_CurrentPic_Buffer = NULL;
	}
	if (m_NextPic_Buffer != NULL) {
		//fb_display (m_NextPic_Buffer, m_NextPic_X, m_NextPic_Y, m_NextPic_XPan, m_NextPic_YPan, m_NextPic_XPos, m_NextPic_YPos);
		frameBuffer->displayRGB(m_NextPic_Buffer, m_NextPic_X, m_NextPic_Y, m_NextPic_XPan, m_NextPic_YPan, m_NextPic_XPos, m_NextPic_YPos);
		frameBuffer->blit();
	}
	//  dbout("DisplayNextImage fb_disp done\n");
	m_CurrentPic_Buffer = m_NextPic_Buffer;
	m_NextPic_Buffer = NULL;
	m_CurrentPic_Name = m_NextPic_Name;
	m_CurrentPic_X = m_NextPic_X;
	m_CurrentPic_Y = m_NextPic_Y;
	m_CurrentPic_XPos = m_NextPic_XPos;
	m_CurrentPic_YPos = m_NextPic_YPos;
	m_CurrentPic_XPan = m_NextPic_XPan;
	m_CurrentPic_YPan = m_NextPic_YPan;
	//  dbout("DisplayNextImage }\n");
	return true;
}

void CPictureViewer::Zoom (float factor)
{
	//  dbout("Zoom %f\n",factor);
	showBusy (m_startx + 3, m_starty + 3, 10, 0xff, 0xff, 00);
/*
#if HAVE_SPARK_HARDWARE
	if (bpamem) {
		int center_x = bpa_x + (bpa_pan_w >> 1);
		int center_y = bpa_y + (bpa_pan_h >> 1);

		if (factor > 0.0 && bpa_pan_w/factor > 8.0 && bpa_pan_h/factor > 8.0) {
			bpa_pan_w = (int)(bpa_pan_w / factor);
			bpa_pan_h = (int)(bpa_pan_h / factor);
		}
		if (factor <= 0.0 || bpa_pan_w >= bpa_w || bpa_pan_h >= bpa_h) {
			factor = 1.0;
			bpa_pan_w = bpa_w;
			bpa_pan_h = bpa_h;
			fb_w = fb_w_initial;
			fb_h = fb_h_initial;
			bpa_x = (bpa_w - bpa_pan_w) >> 1;
			bpa_y = (bpa_h - bpa_pan_h) >> 1;
			center_x = bpa_x + (bpa_pan_w >> 1);
			center_y = bpa_y + (bpa_pan_h >> 1);
		}
		if (fb_w < DEFAULT_XRES) {
			float f0 = (float) DEFAULT_XRES             /(float)fb_w;
			float f1 = (float)(bpa_pan_w * DEFAULT_XRES)/(float)fb_w;
			if (f1 * bpa_pan_w > bpa_w)
				f1 = (float)bpa_w/(float)bpa_pan_w;
			f0 = (f0 < f1) ? f0 : f1;
			fb_w *= f0;
			bpa_pan_w *= f0;
		}
		if (fb_h < DEFAULT_YRES) {
			float f0 = (float) DEFAULT_YRES             /(float)fb_h;
			float f1 = (float)(bpa_pan_h * DEFAULT_YRES)/(float)fb_h;
			if (f1 * bpa_pan_h > bpa_h)
				f1 = (float)bpa_h/(float)bpa_pan_h;
			f0 = (f0 < f1) ? f0 : f1;
			fb_h *= f0;
			bpa_pan_h *= f0;
		}
		if (fb_w > DEFAULT_XRES) {
			fb_h *= DEFAULT_XRES;
			fb_h /= fb_w;
			fb_w = DEFAULT_XRES;
		}
		if (fb_h > DEFAULT_YRES) {
			fb_w *= DEFAULT_YRES;
			fb_w /= fb_h;
			fb_h = DEFAULT_YRES;
		}
		if (fb_w < DEFAULT_XRES && fb_h < DEFAULT_YRES) {
			fb_w = fb_w_initial;
			fb_h = fb_h_initial;
		}
		bpa_x = center_x - (bpa_pan_w >> 1);
		bpa_y = center_y - (bpa_pan_h >> 1);
		if (bpa_x < 0)
			bpa_x = 0;
		else if (bpa_x + bpa_pan_w > bpa_w)
			bpa_x = bpa_w - bpa_pan_w;
		if (bpa_y < 0)
			bpa_y = 0;
		else if (bpa_y + bpa_pan_h > bpa_h)
			bpa_y = bpa_h - bpa_pan_h;
		fb_x = (DEFAULT_XRES - fb_w) >> 1;
		fb_y = (DEFAULT_YRES - fb_h) >> 1;
		frameBuffer->paintBoxRel(0, 0, DEFAULT_XRES, DEFAULT_YRES, 0);
		frameBuffer->blitBPA2FB(bpamem, SURF_BGR888, bpa_w, bpa_h, bpa_x, bpa_y, bpa_pan_w, bpa_pan_h, fb_x, fb_y, fb_w, fb_h, true);
		frameBuffer->blit();
		return;
	}
#endif
*/
	int oldx = m_CurrentPic_X;
	int oldy = m_CurrentPic_Y;
	unsigned char *oldBuf = m_CurrentPic_Buffer;
	m_CurrentPic_X = (int) (factor * m_CurrentPic_X);
	m_CurrentPic_Y = (int) (factor * m_CurrentPic_Y);

	m_CurrentPic_Buffer = Resize(m_CurrentPic_Buffer, oldx, oldy, m_CurrentPic_X, m_CurrentPic_Y, m_scaling);

	if (m_CurrentPic_Buffer == oldBuf) {
		// resize failed
		hideBusy ();
		return;
	}

	if (m_CurrentPic_X < (m_endx - m_startx))
		m_CurrentPic_XPos = (m_endx - m_startx - m_CurrentPic_X) / 2 + m_startx;
	else
		m_CurrentPic_XPos = m_startx;
	if (m_CurrentPic_Y < (m_endy - m_starty))
		m_CurrentPic_YPos = (m_endy - m_starty - m_CurrentPic_Y) / 2 + m_starty;
	else
		m_CurrentPic_YPos = m_starty;
	if (m_CurrentPic_X > (m_endx - m_startx))
		m_CurrentPic_XPan = (m_CurrentPic_X - (m_endx - m_startx)) / 2;
	else
		m_CurrentPic_XPan = 0;
	if (m_CurrentPic_Y > (m_endy - m_starty))
		m_CurrentPic_YPan = (m_CurrentPic_Y - (m_endy - m_starty)) / 2;
	else
		m_CurrentPic_YPan = 0;
	//fb_display (m_CurrentPic_Buffer, m_CurrentPic_X, m_CurrentPic_Y, m_CurrentPic_XPan, m_CurrentPic_YPan, m_CurrentPic_XPos, m_CurrentPic_YPos);
	frameBuffer->displayRGB(m_CurrentPic_Buffer, m_CurrentPic_X, m_CurrentPic_Y, m_CurrentPic_XPan, m_CurrentPic_YPan, m_CurrentPic_XPos, m_CurrentPic_YPos);
	frameBuffer->blit();
}

void CPictureViewer::Move (int dx, int dy)
{
	//  dbout("Move %d %d\n",dx,dy);
	showBusy (m_startx + 3, m_starty + 3, 10, 0x00, 0xff, 00);

//#if HAVE_SPARK_HARDWARE
//	if (bpamem) {
//		bpa_x += dx;
//
//		if (bpa_x < 0)
//			bpa_x = 0;
//		else if (bpa_x + bpa_pan_w >= bpa_w)
//			bpa_x = bpa_w - bpa_pan_w;
//
//		bpa_y += dy;
//
//		if (bpa_y < 0)
//			bpa_y = 0;
//		else if (bpa_y + bpa_pan_h >= bpa_h)
//			bpa_y = bpa_h - bpa_pan_h;
//
//		frameBuffer->paintBoxRel(0, 0, DEFAULT_XRES, DEFAULT_YRES, 0);
//		frameBuffer->blitBPA2FB(bpamem, SURF_BGR888, bpa_w, bpa_h, bpa_x, bpa_y, bpa_pan_w, bpa_pan_h, fb_x, fb_y, fb_w, fb_h, true);
//		frameBuffer->blit();
//		return;
//	}
//#endif
	int xs, ys;

	xs = frameBuffer->getScreenWidth(true);
	ys = frameBuffer->getScreenHeight(true);

	m_CurrentPic_XPan += dx;
	if (m_CurrentPic_XPan + xs >= m_CurrentPic_X)
		m_CurrentPic_XPan = m_CurrentPic_X - xs - 1;
	if (m_CurrentPic_XPan < 0)
		m_CurrentPic_XPan = 0;

	m_CurrentPic_YPan += dy;
	if (m_CurrentPic_YPan + ys >= m_CurrentPic_Y)
		m_CurrentPic_YPan = m_CurrentPic_Y - ys - 1;
	if (m_CurrentPic_YPan < 0)
		m_CurrentPic_YPan = 0;

	if (m_CurrentPic_X < (m_endx - m_startx))
		m_CurrentPic_XPos = (m_endx - m_startx - m_CurrentPic_X) / 2 + m_startx;
	else
		m_CurrentPic_XPos = m_startx;
	if (m_CurrentPic_Y < (m_endy - m_starty))
		m_CurrentPic_YPos = (m_endy - m_starty - m_CurrentPic_Y) / 2 + m_starty;
	else
		m_CurrentPic_YPos = m_starty;
	//  dbout("Display x(%d) y(%d) xpan(%d) ypan(%d) xpos(%d) ypos(%d)\n",m_CurrentPic_X, m_CurrentPic_Y, 
	//          m_CurrentPic_XPan, m_CurrentPic_YPan, m_CurrentPic_XPos, m_CurrentPic_YPos);

	//fb_display (m_CurrentPic_Buffer, m_CurrentPic_X, m_CurrentPic_Y, m_CurrentPic_XPan, m_CurrentPic_YPan, m_CurrentPic_XPos, m_CurrentPic_YPos);
	frameBuffer->displayRGB(m_CurrentPic_Buffer, m_CurrentPic_X, m_CurrentPic_Y, m_CurrentPic_XPan, m_CurrentPic_YPan, m_CurrentPic_XPos, m_CurrentPic_YPos);
	frameBuffer->blit();
}

CPictureViewer::CPictureViewer ()
{
	int xs, ys;

	fh_root = NULL;
	m_scaling = COLOR;
	//m_aspect = 4.0 / 3;
	m_aspect = 16.0 / 9;
	m_CurrentPic_Name = "";
	m_CurrentPic_Buffer = NULL;
	m_CurrentPic_X = 0;
	m_CurrentPic_Y = 0;
	m_CurrentPic_XPos = 0;
	m_CurrentPic_YPos = 0;
	m_CurrentPic_XPan = 0;
	m_CurrentPic_YPan = 0;
	m_NextPic_Name = "";
	m_NextPic_Buffer = NULL;
	m_NextPic_X = 0;
	m_NextPic_Y = 0;
	m_NextPic_XPos = 0;
	m_NextPic_YPos = 0;
	m_NextPic_XPan = 0;
	m_NextPic_YPan = 0;

	frameBuffer = CFrameBuffer::getInstance();
	xs = frameBuffer->getScreenWidth(true);
	ys = frameBuffer->getScreenHeight(true);

	m_startx = 0;
	m_endx = xs - 1;
	m_starty = 0;
	m_endy = ys - 1;
	m_aspect_ratio_correction = m_aspect / ((double) xs / ys);

	m_busy_buffer = NULL;
//#if HAVE_SPARK_HARDWARE
//	bpafd = -1;
//	bpamem = NULL;
//	bpasize = 0;
//	bpa_w = 0;
//	bpa_h = 0;
//#endif
	pic_cache_size = 0;
	pic_cache_maxsize = 0x100000; // 1 MB default

	logo_hdd_dir = std::string(g_settings.logo_hdd_dir);
	logo_rename_to_channelname = g_settings.logo_rename_to_channelname;

	init_handlers ();
}

CPictureViewer::~CPictureViewer ()
{
	Cleanup();
	CFormathandler *fh = fh_root;
	while (fh) {
		CFormathandler *tmp = fh->next;
		free(fh);
		fh = tmp;
	}
}

void CPictureViewer::showBusy (int sx, int sy, int width, char r, char g, char b)
{
	//  dbout("Show Busy{\n");
	unsigned char rgb_buffer[3];
	unsigned char *fb_buffer;
	unsigned char *busy_buffer_wrk;
	int cpp = 4;

	rgb_buffer[0] = r;
	rgb_buffer[1] = g;
	rgb_buffer[2] = b;

	fb_buffer = (unsigned char *) frameBuffer->convertRGB2FB (rgb_buffer, 1, 1);
	if (fb_buffer == NULL) {
		printf ("showBusy: Error: malloc 1\n");
		return;
	}
	if (m_busy_buffer != NULL) {
		free (m_busy_buffer);
		m_busy_buffer = NULL;
	}
	m_busy_buffer = (unsigned char *) malloc (width * width * cpp);
	if (m_busy_buffer == NULL) {
		printf ("showBusy: Error: malloc 2: \n");
		return;
	}
	busy_buffer_wrk = m_busy_buffer;
	unsigned char *fb = (unsigned char *) frameBuffer->getFrameBufferPointer();
	unsigned int stride = frameBuffer->getStride ();

	for (int y = sy; y < sy + width; y++) {
		for (int x = sx; x < sx + width; x++) {
			memmove (busy_buffer_wrk, fb + y * stride + x * cpp, cpp);
			busy_buffer_wrk += cpp;
			memmove (fb + y * stride + x * cpp, fb_buffer, cpp);
		}
	}
	m_busy_x = sx;
	m_busy_y = sy;
	m_busy_width = width;
	m_busy_cpp = cpp;
	cs_free_uncached (fb_buffer);
	frameBuffer->blit();
	//  dbout("Show Busy}\n");
}

void CPictureViewer::hideBusy ()
{
	//  dbout("Hide Busy{\n");
	if (m_busy_buffer != NULL) {
		unsigned char *fb = (unsigned char *) frameBuffer->getFrameBufferPointer ();
		unsigned int stride = frameBuffer->getStride ();
		unsigned char *busy_buffer_wrk = m_busy_buffer;

		for (int y = m_busy_y; y < m_busy_y + m_busy_width; y++) {
			for (int x = m_busy_x; x < m_busy_x + m_busy_width; x++) {
				memmove (fb + y * stride + x * m_busy_cpp, busy_buffer_wrk, m_busy_cpp);
				busy_buffer_wrk += m_busy_cpp;
			}
		}
		free (m_busy_buffer);
		m_busy_buffer = NULL;
	}
	frameBuffer->blit();
	//  dbout("Hide Busy}\n");
}
void CPictureViewer::Cleanup ()
{
	if (m_busy_buffer != NULL) {
		free (m_busy_buffer);
		m_busy_buffer = NULL;
	}
	if (m_NextPic_Buffer != NULL) {
		free (m_NextPic_Buffer);
		m_NextPic_Buffer = NULL;
	}
	if (m_CurrentPic_Buffer != NULL) {
		free (m_CurrentPic_Buffer);
		m_CurrentPic_Buffer = NULL;
	}
//#if HAVE_SPARK_HARDWARE
//	if (bpamem)
//		frameBuffer->freeBPAMem(bpafd, bpamem, bpasize);
//#endif
	cacheClear();
}

void CPictureViewer::getSize(const char* name, int* width, int *height)
{
	CFormathandler *fh;

	fh = fh_getsize(name, width, height, INT_MAX, INT_MAX);
	if (fh == NULL) {
		*width = 0;
		*height = 0;
	}
}

#define LOGO_FLASH_DIR DATADIR "/neutrino/icons/logo"

bool CPictureViewer::GetLogoName(const uint64_t& channel_id, const std::string& _ChannelName, std::string & name, int *width, int *height)
{
	if (g_settings.logo_hdd_dir.empty())
		return false;

	std::string ChannelName = _ChannelName;
	std::replace(ChannelName.begin(), ChannelName.end(), '/', '-');

	if(width)
		*width = 0;
	if(height)
		*height = 0;

 	OpenThreads::ScopedLock<OpenThreads::Mutex> m_lock(logo_map_mutex);

	if (logo_hdd_dir != g_settings.logo_hdd_dir || (g_settings.logo_rename_to_channelname && !logo_rename_to_channelname)) {
		logo_map.clear();
		logo_hdd_dir = g_settings.logo_hdd_dir;
		logo_rename_to_channelname = g_settings.logo_rename_to_channelname;
	}

	std::map<uint64_t, logo_data>::iterator it;
	it = logo_map.find(channel_id);
	if (it != logo_map.end()) {
		name = it->second.name;
		if (width)
			*width = it->second.width;
		if (height)
			*height = it->second.height;
		return !name.empty();
	}

	char strChanId[17];
	snprintf(strChanId, sizeof(strChanId), "%llx", channel_id & 0xFFFFFFFFFFFFULL);

	/* first the channelname, then the channel-id */
	std::string strLogoName[2] = { ChannelName, (std::string)strChanId };
	/* first png, then jpg, then gif */
	std::string strLogoExt[3] = { ".png", ".jpg" , ".gif" };

	bool do_rename = false;
	CZapitChannel * cc = NULL;

	for (int i = 0; i < 2; i++)
		for (int j = 0; j < 3; j++) {
			name = g_settings.logo_hdd_dir + "/" + strLogoName[i] + strLogoExt[j];
			if (!access(name, R_OK))
				goto found;
			do_rename = true;
		}

	if (CNeutrinoApp::getInstance()->channelList)
		cc = CNeutrinoApp::getInstance()->channelList->getChannel(channel_id);
	if (cc) {
		char fname[255];
		u_int service_type = (u_int) cc->getServiceType(true);

		snprintf(fname, sizeof(fname), "1_0_%X_%X_%X_%X_%X0000_0_0_0.png",
			service_type,
			(u_int) channel_id & 0xFFFF,
			(u_int) (channel_id >> 32) & 0xFFFF,
			(u_int) (channel_id >> 16) & 0xFFFF,
			(u_int) cc->getSatellitePosition());
		name = g_settings.logo_hdd_dir + "/" + std::string(fname);
		if (!access(name, R_OK))
			goto found;

		if (service_type != 1) {
			snprintf(fname, sizeof(fname), "1_0_%X_%X_%X_%X_%X0000_0_0_0.png",
				1,
				(u_int) channel_id & 0xFFFF,
				(u_int) (channel_id >> 32) & 0xFFFF,
				(u_int) (channel_id >> 16) & 0xFFFF,
				(u_int) cc->getSatellitePosition());
			name = g_settings.logo_hdd_dir + "/" + std::string(fname);
			if (!access(name, R_OK))
				goto found;
		}
	}

	name = "";
	logo_map[channel_id].name = "";
	logo_map[channel_id].width = 0;
	logo_map[channel_id].height = 0;
	return false;

found:
	if (do_rename && g_settings.logo_rename_to_channelname && !ChannelName.empty()) {
		int dot = name.find_last_of(".");
		std::string new_name = g_settings.logo_hdd_dir + "/" + ChannelName + name.substr(dot);
		if (!rename(name.c_str(), new_name.c_str()))
			name = new_name;
	}
	int w, h;
	getSize(name.c_str(), &w, &h);
	if(width)
		*width = w;
	if(height)
		*height = h;
	logo_map[channel_id].name = name;
	logo_map[channel_id].width = w;
	logo_map[channel_id].height = h;
	return true;
}
#if 0
bool CPictureViewer::DisplayLogo (uint64_t channel_id, int posx, int posy, int width, int height)
{
	char fname[255];
	bool ret = false;

	sprintf(fname, "%s/%llx.jpg", g_settings.logo_hdd_dir.c_str(), channel_id & 0xFFFFFFFFFFFFULL);
//	printf("logo file: %s\n", fname);
	if(access(fname, F_OK))
		sprintf(fname, "%s/%llx.gif", g_settings.logo_hdd_dir.c_str(), channel_id & 0xFFFFFFFFFFFFULL);

	if(!access(fname, F_OK)) {
		ret = DisplayImage(fname, posx, posy, width, height);
#if 0
		//ret = DisplayImage(fname, posx, posy, width, height);
		fb_pixel_t * data = getImage(fname, width, height);
		//fb_pixel_t * data = getIcon(fname, &width, &height);
		if(data) {
			frameBuffer->blit2FB(data, width, height, posx, posy);
			cs_free_uncached(data);
		}
#endif
	}
	return ret;
}
#endif
void CPictureViewer::rescaleImageDimensions(int *width, int *height, const int max_width, const int max_height, bool upscale)
{
	float aspect;

	if ((!upscale) && (*width <= max_width) && (*height <= max_height))
		return;

	aspect = (float)(*width) / (float)(*height);
	if (((float)(*width) / (float)max_width) > ((float)(*height) / (float)max_height)) {
		*width = max_width;
		*height = (int)(max_width / aspect);
	}else{
		*height = max_height;
		*width = (int)(max_height * aspect);
	}
}

bool CPictureViewer::DisplayImage(const std::string & name, int posx, int posy, int width, int height, int transp)
{
	fb_pixel_t *data = cacheGet(name, width, height, transp);
	if (data) {
		frameBuffer->blit2FB(data, width, height, posx, posy);
		return true;
	}

	if (transp > CFrameBuffer::TM_EMPTY)
		frameBuffer->SetTransparent(transp);

	data = getImage(name, width, height);

	if (transp > CFrameBuffer::TM_EMPTY)
		frameBuffer->SetTransparentDefault();

	if(data) {
		frameBuffer->blit2FB(data, width, height, posx, posy);
		cachePut(name, width, height, transp, data);
		return true;
	}
	return false;
}

fb_pixel_t * CPictureViewer::int_getImage(const std::string & name, int *width, int *height, bool GetImage)
{
	int x, y, load_ret, bpp = 0;
	CFormathandler *fh;
	unsigned char * buffer;
	fb_pixel_t * ret = NULL;
	std::string mode_str;

	if (GetImage)
		mode_str = "getImage";
	else
		mode_str = "getIcon";

	fh = fh_getsize(name.c_str(), &x, &y, INT_MAX, INT_MAX);
	if (fh)
	{
		buffer = (unsigned char *) malloc(x * y * 4);
		if (buffer == NULL)
		{
			printf("%s: Error: malloc\n", mode_str.c_str());
			return 0;
		}
#ifdef FBV_SUPPORT_PNG
		if ((name.find(".png") == (name.length() - 4)) && (fh_png_id(name.c_str())))
			load_ret = png_load_ext(name.c_str(), &buffer, &x, &y, &bpp);
		else
#endif
			load_ret = fh->get_pic(name.c_str (), &buffer, &x, &y);
		if (load_ret == FH_ERROR_OK)
		{
//			printf("%s: decoded %s, %d x %d \n", mode_str.c_str(), name.c_str(), x, y);
			// resize only getImage
			if ((GetImage) && (x != *width || y != *height))
			{
//				printf("%s: resize  %s to %d x %d \n", mode_str.c_str(), name.c_str(), *width, *height);
				if (bpp == 4)
					buffer = ResizeA(buffer, x, y, *width, *height);
				else
					buffer = Resize(buffer, x, y, *width, *height, COLOR);
				x = *width;
				y = *height;
			}
			if (bpp == 4)
				ret = (fb_pixel_t *) frameBuffer->convertRGBA2FB(buffer, x, y);
			else
				ret = (fb_pixel_t *) frameBuffer->convertRGB2FB(buffer, x, y, convertSetupAlpha2Alpha(g_settings.theme.infobar_alpha));
			*width = x;
			*height = y;
		} else
			printf("%s: Error decoding file %s\n", mode_str.c_str(), name.c_str());
		free(buffer);
	} else
		printf("%s: Error open file %s\n", mode_str.c_str(), name.c_str());
	return ret;
}

fb_pixel_t * CPictureViewer::getImage(const std::string & name, int width, int height)
{
	return int_getImage(name, &width, &height, true);
}

fb_pixel_t * CPictureViewer::getIcon(const std::string & name, int *width, int *height)
{
	return int_getImage(name, width, height, false);
}

unsigned char * CPictureViewer::int_Resize(unsigned char *orgin, int ox, int oy, int dx, int dy, ScalingMode type, unsigned char * dst, bool alpha)
{
	unsigned char * cr;
	if(dst == NULL)
	{
		cr = (unsigned char*) malloc(dx * dy * ((alpha) ? 4 : 3));

		if(cr == NULL)
		{
			printf("Resize Error: malloc\n");
			return(orgin);
		}
	} else
		cr = dst;

	if(type == SIMPLE)
	{
		unsigned char *p,*l;
		int i,j,k,ip;
		l=cr;

		for(j=0;j<dy;j++,l+=dx*3)
		{
			p=orgin+(j*oy/dy*ox*3);
			for(i=0,k=0;i<dx;i++,k+=3)
			{
				ip=i*ox/dx*3;
				memmove(l+k, p+ip, 3);
			}
		}
	} else {
		unsigned char *p,*q;
		int i,j,k,l,ya,yb;
		int sq,r,g,b,a;

		p=cr;

		int xa_v[dx];
		for(i=0;i<dx;i++)
			xa_v[i] = i*ox/dx;
		int xb_v[dx+1];
		for(i=0;i<dx;i++)
		{
			xb_v[i]= (i+1)*ox/dx;
			if(xb_v[i]>=ox)
				xb_v[i]=ox-1;
		}

		if (alpha)
		{
			for(j=0;j<dy;j++)
			{
				ya= j*oy/dy;
				yb= (j+1)*oy/dy; if(yb>=oy) yb=oy-1;
				for(i=0;i<dx;i++,p+=4)
				{
					for(l=ya,r=0,g=0,b=0,a=0,sq=0;l<=yb;l++)
					{
						q=orgin+((l*ox+xa_v[i])*4);
						for(k=xa_v[i];k<=xb_v[i];k++,q+=4,sq++)
						{
							r+=q[0]; g+=q[1]; b+=q[2]; a+=q[3];
						}
					}
					p[0]=r/sq; p[1]=g/sq; p[2]=b/sq; p[3]=a/sq;
				}
			}
		} else
		{
			for(j=0;j<dy;j++)
			{
				ya= j*oy/dy;
				yb= (j+1)*oy/dy; if(yb>=oy) yb=oy-1;
				for(i=0;i<dx;i++,p+=3)
				{
					for(l=ya,r=0,g=0,b=0,sq=0;l<=yb;l++)
					{
						q=orgin+((l*ox+xa_v[i])*3);
						for(k=xa_v[i];k<=xb_v[i];k++,q+=3,sq++)
						{
							r+=q[0]; g+=q[1]; b+=q[2];
						}
					}
					p[0]=r/sq; p[1]=g/sq; p[2]=b/sq;
				}
			}
		}
	}
	free(orgin);
	return(cr);
}

unsigned char * CPictureViewer::Resize(unsigned char *orgin, int ox, int oy, int dx, int dy, ScalingMode type, unsigned char * dst)
{
	return int_Resize(orgin, ox, oy, dx, dy, type, dst, false);
}

unsigned char * CPictureViewer::ResizeA(unsigned char *orgin, int ox, int oy, int dx, int dy)
{
	return int_Resize(orgin, ox, oy, dx, dy, COLOR, NULL, true);
}

fb_pixel_t *CPictureViewer::cacheGet(const std::string &name, int width, int height, int transp)
{
	cached_pic_key k;
	k.name = name;
	k.width = width;
	k.height = height;
	k.transp = transp;

	std::map<cached_pic_key,cached_pic_data>::iterator it = pic_cache.find(k);
	if (it != pic_cache.end()) {
		(*it).second.last_used = time(NULL);
		return (*it).second.data;
	}

	return NULL;
}

void CPictureViewer::cachePut(const std::string &name, int width, int height, int transp, fb_pixel_t *data)
{
	while (pic_cache_size > 0 && pic_cache_size + width * height * 4 > pic_cache_maxsize)
		cacheClearLRU();

	cached_pic_key k;
	cached_pic_data d;
	k.name = name;
	k.width = width;
	k.height = height;
	k.transp = transp;
	d.last_used = time(NULL);
	d.data = data;
	pic_cache[k] = d;
	pic_cache_size += width * height * 4;
}

void CPictureViewer::cacheClear(void)
{
	std::map<cached_pic_key,cached_pic_data>::iterator it;
	for (it = pic_cache.begin(); it != pic_cache.end(); ++it)
		free((*it).second.data);
	pic_cache.clear();
	pic_cache_size = 0;
}

void CPictureViewer::cacheClearLRU(void)
{
	std::map<cached_pic_key,cached_pic_data>::iterator it = pic_cache.begin();
	std::map<cached_pic_key,cached_pic_data>::iterator it_lru = it;
	for (it = it_lru = pic_cache.begin(); it != pic_cache.end(); ++it) {
		if ((*it).second.last_used < (*it_lru).second.last_used)
		it_lru = it;
	}
	if (it_lru != pic_cache.end()) {
		pic_cache_size -= (*it_lru).first.width * (*it_lru).first.height * 4;
		cs_free_uncached ((*it_lru).second.data);
		pic_cache.erase(it_lru);
	}
}

void CPictureViewer::cacheSetSize(size_t maxsize)
{
	pic_cache_maxsize = maxsize;
	while (pic_cache_size > pic_cache_maxsize)
		cacheClearLRU();
}
