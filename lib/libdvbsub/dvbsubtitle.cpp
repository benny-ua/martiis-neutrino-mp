/*
 * dvbsubtitle.c: DVB subtitles
 *
 * See the main source file 'vdr.c' for copyright information and
 * how to reach the author.
 *
 * Original author: Marco Schlüßler <marco@lordzodiac.de>
 * With some input from the "subtitle plugin" by Pekka Virtanen <pekka.virtanen@sci.fi>
 *
 * $Id: dvbsubtitle.cpp,v 1.1 2009/02/23 19:46:44 rhabarber1848 Exp $
 * dvbsubtitle for HD1 ported by Coolstream LTD
 */

#include "dvbsubtitle.h"

extern "C" {
#include <unistd.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#ifdef MARTII
#include <libavcodec/version.h>
#endif
}
#include <driver/framebuffer.h>
#include "Debug.hpp"

// Set these to 'true' for debug output:
#ifdef MARTII
static bool DebugConverter = false;
#else
static bool DebugConverter = true;
#endif

#define dbgconverter(a...) if (DebugConverter) sub_debug.print(Debug::VERBOSE, a)

#ifdef MARTII
// CAVEAT EMPTOR
// THIS IS COPIED FROM ffmpeg/libavcodec/dvbsubdec.c
//
// WE'RE ACCESSING PRIVATE DATA HERE. THIS WILL BREAK RATHER SOONER THAN LATER.
//
//   --martii
//
typedef struct DVBSubDisplayDefinition {
    int version;

    int x;
    int y;
    int width;
    int height;
} DVBSubDisplayDefinition;

typedef struct DVBSubContext {
    int composition_id;
    int ancillary_id;

#if (LIBAVCODEC_VERSION_MAJOR > 54) || (LIBAVCODEC_VERSION_MINOR > 8) // FIXME, needs adjustment
    int version;
#endif
    int time_out;
    void /*DVBSubRegion*/ *region_list;
    void /*DVBSubCLUT*/   *clut_list;
    void /*DVBSubObject*/ *object_list;

    int display_list_size;
    void /*DVBSubRegionDisplay*/ *display_list;
    DVBSubDisplayDefinition *display_definition;
} DVBSubContext;
#endif

// --- cDvbSubtitleBitmaps ---------------------------------------------------

class cDvbSubtitleBitmaps : public cListObject 
{
	private:
		int64_t pts;
		int timeout;
		AVSubtitle sub;
	public:
		cDvbSubtitleBitmaps(int64_t Pts);
		~cDvbSubtitleBitmaps();
		int64_t Pts(void) { return pts; }
		int Timeout(void) { return sub.end_display_time; }
		void Draw(int &min_x, int &min_y, int &max_x, int &max_y);
		int Count(void) { return sub.num_rects; };
		AVSubtitle * GetSub(void) { return &sub; };
};

cDvbSubtitleBitmaps::cDvbSubtitleBitmaps(int64_t pPts)
{
	//dbgconverter("cDvbSubtitleBitmaps::new: PTS: %lld\n", pts);
	pts = pPts;
}

cDvbSubtitleBitmaps::~cDvbSubtitleBitmaps()
{
//    dbgconverter("cDvbSubtitleBitmaps::delete: PTS: %lld rects %d\n", pts, Count());
    int i;

    if(sub.rects) {
	    for (i = 0; i < Count(); i++)
	    {
		    av_freep(&sub.rects[i]->pict.data[0]);
		    av_freep(&sub.rects[i]->pict.data[1]);
		    av_freep(&sub.rects[i]);
	    }

	    av_free(sub.rects);
    }
    memset(&sub, 0, sizeof(AVSubtitle));
}

fb_pixel_t * simple_resize32(uint8_t * orgin, uint32_t * colors, int nb_colors, int ox, int oy, int dx, int dy)
{
	fb_pixel_t  *cr,*l;
	int i,j,k,ip;

#ifndef HAVE_SPARK_HARDWARE
	cr = (fb_pixel_t *) malloc(dx*dy*sizeof(fb_pixel_t));

	if(cr == NULL) {
		printf("Error: malloc\n");
		return NULL;
	}
#else
	cr = CFrameBuffer::getInstance()->getBackBufferPointer();
#endif
	l = cr;

	for(j = 0; j < dy; j++, l += dx)
	{
		uint8_t * p = orgin + (j*oy/dy*ox);
		for(i = 0, k = 0; i < dx; i++, k++) {
			ip = i*ox/dx;
			int idx = p[ip];
			if(idx < nb_colors)
				l[k] = colors[idx];
		}
	}
	return(cr);
}

void cDvbSubtitleBitmaps::Draw(int &min_x, int &min_y, int &max_x, int &max_y)
{
#ifdef MARTII
#define DEFAULT_XRES 1280	// backbuffer width
#define DEFAULT_YRES 720	// backbuffer height

	if (!Count())
		return;

	dbgconverter("cDvbSubtitleBitmaps::%s: start\n", __func__);

	CFrameBuffer* fb = CFrameBuffer::getInstance();
	fb_pixel_t *b = fb->getBackBufferPointer();

	// HACK. When having just switched channels we may not yet have yet
	// received valid authoring data. This check triggers for the most
	// common HD subtitle format and sets our authoring display format
	// accordingly.
	if (max_x == 720 && max_y == 576 && sub.rects[0]->h == 48 && sub.rects[0]->w == 1280)
		min_x = min_y = 0, max_x = 1280, max_y = 720;

	for (int i = 0; i < Count(); i++) {
		uint32_t * colors = (uint32_t *) sub.rects[i]->pict.data[1];
		int width = sub.rects[i]->w;
		int height = sub.rects[i]->h;
		uint8_t *origin = sub.rects[i]->pict.data[0];
		int nb_colors = sub.rects[i]->nb_colors;

		size_t bs = width * height;
		for (unsigned int j = 0; j < bs; j++)
			if (origin[j] < nb_colors)
				b[j] = colors[origin[j]];

		int width_new = (width * DEFAULT_XRES) / max_x;
		int height_new = (height * DEFAULT_YRES) / max_y;
		int x_new = (sub.rects[i]->x * DEFAULT_XRES) / max_x;
		int y_new = (sub.rects[i]->y * DEFAULT_YRES) / max_y;

		dbgconverter("cDvbSubtitleBitmaps::Draw: original bitmap=%d x=%d y=%d, w=%d, h=%d col=%d\n",
			i, sub.rects[i]->x, sub.rects[i]->y, width, height, sub.rects[i]->nb_colors);
		dbgconverter("cDvbSubtitleBitmaps::Draw: scaled bitmap=%d x_new=%d y_new=%d, w_new=%d, h_new=%d\n",
			i, x_new, y_new, width_new, height_new);
		fb->blitArea(width, height, x_new, y_new, width_new, height_new);
		fb->blit();
	}

	dbgconverter("cDvbSubtitleBitmaps::%s: done\n", __func__);
#else // MARTII
	int i;
#ifndef HAVE_SPARK_HARDWARE
	int stride = CFrameBuffer::getInstance()->getScreenWidth(true);
#if 0
	int wd = CFrameBuffer::getInstance()->getScreenWidth();
	int xstart = CFrameBuffer::getInstance()->getScreenX();
	int yend = CFrameBuffer::getInstance()->getScreenY() + CFrameBuffer::getInstance()->getScreenHeight();
	int ystart = CFrameBuffer::getInstance()->getScreenY();
#endif
	uint32_t *sublfb = CFrameBuffer::getInstance()->getFrameBufferPointer();
#endif

//	dbgconverter("cDvbSubtitleBitmaps::Draw: %d bitmaps, x= %d, width= %d yend=%d stride %d\n", Count(), xstart, wd, yend, stride);

	int sw = CFrameBuffer::getInstance()->getScreenWidth(true);
	int sh = CFrameBuffer::getInstance()->getScreenHeight(true);
#if 0
	double xc = (double) CFrameBuffer::getInstance()->getScreenWidth(true)/(double) 720;
	double yc = (double) CFrameBuffer::getInstance()->getScreenHeight(true)/(double) 576;
	xc = yc; //FIXME should we scale also to full width ?
	int xf = xc * (double) 720;
#endif

	for (i = 0; i < Count(); i++) {
		uint32_t * colors = (uint32_t *) sub.rects[i]->pict.data[1];
		int width = sub.rects[i]->w;
		int height = sub.rects[i]->h;
		int xoff, yoff;

#if 0
		int nw = width == 1280 ? ((double) width / xc) : ((double) width * xc);
		int nh = (double) height * yc;

		int xdiff = (wd > xf) ? ((wd - xf) / 2) : 0;
		xoff = sub.rects[i]->x*xc + xstart + xdiff;
		if(sub.rects[i]->y < 576/2) {
			yoff = ystart + sub.rects[i]->y*yc;
		} else {
			yoff = yend - ((width == 1280 ? 704:576) - (double) (sub.rects[i]->y + height))*yc - nh;
			if(yoff < ystart)
				yoff = ystart;
		}
#endif
		int h2 = (width == 1280) ? 720 : 576;
		xoff = sub.rects[i]->x * sw / width;
		yoff = sub.rects[i]->y * sh / h2;
		int nw = width * sw / width;
		int nh = height * sh / h2;

//		dbgconverter("cDvbSubtitleBitmaps::Draw: #%d at %d,%d size %dx%d colors %d (x=%d y=%d w=%d h=%d) \n", i+1, 
//				sub.rects[i]->x, sub.rects[i]->y, sub.rects[i]->w, sub.rects[i]->h, sub.rects[i]->nb_colors, xoff, yoff, nw, nh);

		fb_pixel_t * newdata = simple_resize32 (sub.rects[i]->pict.data[0], colors, sub.rects[i]->nb_colors, width, height, nw, nh);

#ifdef HAVE_SPARK_HARDWARE
		// CFrameBuffer::getInstance()->waitForIdle();
		CFrameBuffer::getInstance()->blit2FB(newdata, nw, nh, xoff, yoff, 0, 0);
#else
		fb_pixel_t * ptr = newdata;
		for (int y2 = 0; y2 < nh; y2++) {
			int y = (yoff + y2) * stride;
			for (int x2 = 0; x2 < nw; x2++)
				*(sublfb + xoff + x2 + y) = *ptr++;
		}
		free(newdata);
#endif

		if(min_x > xoff)
			min_x = xoff;
		if(min_y > yoff)
			min_y = yoff;
		if(max_x < (xoff + nw))
			max_x = xoff + nw;
		if(max_y < (yoff + nh))
			max_y = yoff + nh;
	}
#ifdef HAVE_SPARK_HARDWARE
	if (Count())	/* sync framebuffer */
		CFrameBuffer::getInstance()->blit();
#endif
//	if(Count())
//		dbgconverter("cDvbSubtitleBitmaps::Draw: finish, min/max screen: x=% d y= %d, w= %d, h= %d\n", min_x, min_y, max_x-min_x, max_y-min_y);
//	dbgconverter("\n");
#endif // MARTII
}

static int screen_w, screen_h, screen_x, screen_y;
// --- cDvbSubtitleConverter -------------------------------------------------

cDvbSubtitleConverter::cDvbSubtitleConverter(void)
{
	dbgconverter("cDvbSubtitleConverter: new converter\n");

	bitmaps = new cList<cDvbSubtitleBitmaps>;
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK_NP);
	pthread_mutex_init(&mutex, &attr);
	running = false;

	avctx = NULL;
	avcodec = NULL;

	avcodec_register_all();
	avcodec = avcodec_find_decoder(CODEC_ID_DVB_SUBTITLE);
	if (!avcodec) {
		dbgconverter("cDvbSubtitleConverter: unable to get dvb subtitle codec!\n");
		return;
	}
	avctx = avcodec_alloc_context3(avcodec);
	if (avcodec_open2(avctx, avcodec, NULL) < 0)
		dbgconverter("cDvbSubtitleConverter: unable to open codec !\n");

	av_log_set_level(AV_LOG_PANIC);
	//if(DebugConverter)
	//	av_log_set_level(AV_LOG_INFO);

	screen_w = min_x = CFrameBuffer::getInstance()->getScreenWidth();
	screen_h = min_y = CFrameBuffer::getInstance()->getScreenHeight();
	screen_x = max_x = CFrameBuffer::getInstance()->getScreenX();
	screen_y = max_y = CFrameBuffer::getInstance()->getScreenY();
	Timeout.Set(0xFFFF*1000);
}

cDvbSubtitleConverter::~cDvbSubtitleConverter()
{
	if (avctx) {
		avcodec_close(avctx);
		av_free(avctx);
		avctx = NULL;
	}
	delete bitmaps;
}

void cDvbSubtitleConverter::Lock(void)
{
  pthread_mutex_lock(&mutex);
}

void cDvbSubtitleConverter::Unlock(void)
{
  pthread_mutex_unlock(&mutex);
}

void cDvbSubtitleConverter::Pause(bool pause)
{
	dbgconverter("cDvbSubtitleConverter::Pause: %s\n", pause ? "pause" : "resume");
	if(pause) {
		if(!running)
			return;
		Lock();
		Clear();
		running = false;
		Unlock();
		//Reset();
	} else {
#ifdef MARTII
		// Assume that we've switched channel. Drop the existing display_definition.
		DVBSubContext *ctx = (DVBSubContext *) avctx->priv_data;
		if (ctx) {
			if (ctx->display_definition)
				av_freep(&ctx->display_definition);
		}
#endif
		//Reset();
		running = true;
	}
}

void cDvbSubtitleConverter::Clear(void)
{
#ifdef MARTII
	CFrameBuffer::getInstance()->Clear();
#else
//	dbgconverter("cDvbSubtitleConverter::Clear: x=% d y= %d, w= %d, h= %d\n", min_x, min_y, max_x-min_x, max_y-min_y);
	if(running && (max_x-min_x > 0) && (max_y-min_y > 0)) {
		CFrameBuffer::getInstance()->paintBackgroundBoxRel (min_x, min_y, max_x-min_x, max_y-min_y);
		/* reset area to clear */
		min_x = screen_w;
		min_y = screen_h;
		max_x = screen_x;
		max_y = screen_h;
		//CFrameBuffer::getInstance()->paintBackground();
	}
#endif
}

void cDvbSubtitleConverter::Reset(void)
{
	dbgconverter("Converter reset -----------------------\n");
	Lock();
	bitmaps->Clear();
	Unlock();
	Timeout.Set(0xFFFF*1000);
}

int cDvbSubtitleConverter::Convert(const uchar *Data, int Length, int64_t pts)
{
	AVPacket avpkt;
	int got_subtitle = 0;
	static cDvbSubtitleBitmaps *Bitmaps = NULL;

	if(!avctx) {
		dbgconverter("cDvbSubtitleConverter::Convert: no context\n");
		return -1;
	}

	if(Bitmaps == NULL)
		Bitmaps = new cDvbSubtitleBitmaps(pts);

 	AVSubtitle * sub = Bitmaps->GetSub();

	av_init_packet(&avpkt);
	avpkt.data = (uint8_t*) Data;
	avpkt.size = Length;

//	dbgconverter("cDvbSubtitleConverter::Convert: sub %x pkt %x pts %lld\n", sub, &avpkt, pts);
	//avctx->sub_id = (anc_page << 16) | comp_page; //FIXME not patched ffmpeg needs this !

	avcodec_decode_subtitle2(avctx, sub, &got_subtitle, &avpkt);
//	dbgconverter("cDvbSubtitleConverter::Convert: pts %lld subs ? %s, %d bitmaps\n", pts, got_subtitle? "yes" : "no", sub->num_rects);

	if(got_subtitle) {
		if(DebugConverter) {
			unsigned int i;
			for(i = 0; i < sub->num_rects; i++) {
//				dbgconverter("cDvbSubtitleConverter::Convert: #%d at %d,%d size %d x %d colors %d\n", i+1, 
//						sub->rects[i]->x, sub->rects[i]->y, sub->rects[i]->w, sub->rects[i]->h, sub->rects[i]->nb_colors);
			}
		}
		bitmaps->Add(Bitmaps);
		Bitmaps = NULL;
	}

	return 0;
}

#define LimitTo32Bit(n) (n & 0x00000000FFFFFFFFL)
#define MAXDELTA 40000 // max. reasonable PTS/STC delta in ms
#define MIN_DISPLAY_TIME 1500
#define SHOW_DELTA 20
#define WAITMS 500

void dvbsub_get_stc(int64_t * STC);

int cDvbSubtitleConverter::Action(void)
{
	int WaitMs = WAITMS;
#if 0
 retry:
	bool shown = false;
#endif
	if (!running)
		return 0;

	if(!avctx) {
		dbgconverter("cDvbSubtitleConverter::Action: no context\n");
		return -1;
	}

#ifdef MARTII
	min_x = min_y = 0;
	max_x = 720;
	max_y = 576;

	DVBSubContext *ctx = (DVBSubContext *) avctx->priv_data;
	if (ctx) {
		DVBSubDisplayDefinition *display_def = ctx->display_definition;
		if (display_def && display_def->width && display_def->height) {
			min_x = display_def->x;
			min_y = display_def->y;
			max_x = display_def->width;
			max_y = display_def->height;
			dbgconverter("cDvbSubtitleConverter::Action: Display Definition: min_x=%d min_y=%d max_x=%d max_y=%d\n", min_x, min_y, max_x, max_y);
		}
	}
#endif
	Lock();
	if (cDvbSubtitleBitmaps *sb = bitmaps->First()) {
		int64_t STC;
		dvbsub_get_stc(&STC);
		int64_t Delta = 0;

		Delta = LimitTo32Bit(sb->Pts()) - LimitTo32Bit(STC);
		Delta /= 90; // STC and PTS are in 1/90000s
//		dbgconverter("cDvbSubtitleConverter::Action: PTS: %016llx STC: %016llx (%lld) timeout: %d\n", sb->Pts(), STC, Delta, sb->Timeout());

		if (Delta <= MAXDELTA) {
			if (Delta <= SHOW_DELTA) {
dbgconverter("cDvbSubtitleConverter::Action: PTS: %012llx STC: %012llx (%lld) timeout: %d bmp %d/%d\n", sb->Pts(), STC, Delta, sb->Timeout(), bitmaps->Count(), sb->Index() + 1);
//				dbgconverter("cDvbSubtitleConverter::Action: Got %d bitmaps, showing #%d\n", bitmaps->Count(), sb->Index() + 1);
				if (running) {
					Clear();
					sb->Draw(min_x, min_y, max_x, max_y);
					Timeout.Set(sb->Timeout());
				}
				if(sb->Count())
					WaitMs = MIN_DISPLAY_TIME;
				bitmaps->Del(sb, true);
//				shown = true;
			}
			else if (Delta < WaitMs)
				WaitMs = (Delta > SHOW_DELTA) ? Delta - SHOW_DELTA : Delta;
		}
		else
		{
			dbgconverter("deleted because delta (%lld) > MAXDELTA (%d)\n", Delta, MAXDELTA);
			bitmaps->Del(sb, true);
		}
	} else {
		if (Timeout.TimedOut()) {
			dbgconverter("cDvbSubtitleConverter::Action: timeout, elapsed %lld\n", Timeout.Elapsed());
			Clear();
			Timeout.Set(0xFFFF*1000);
		}
	}
	Unlock();
#if 0
if (shown)
	goto retry;
#endif
	if(WaitMs != WAITMS)
		dbgconverter("cDvbSubtitleConverter::Action: finish, WaitMs %d\n", WaitMs);

	return WaitMs*1000;
}
