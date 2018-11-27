// vid_x.c -- general x video driver

#define _BSD

#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/extensions/XShm.h>

#include <X11/AIX.h>
#include <X11/extensions/Xdfb.h>

#include "quakedef.h"
#include "d_local.h"

typedef struct
{
	int input;
	int output;
} keymap_t;

viddef_t vid; // global video state
unsigned short       d_8to16table[256];

int		num_shades=32;

int	d_con_indirect = 0;

int		vid_buffersize;

static qboolean			doShm;
static Display			*x_disp;
static Colormap			x_cmap;
static Window			x_win;
static GC				x_gc;
static Visual			*x_vis;
static XVisualInfo		*x_visinfo;
//static XImage			*x_image;
static int screen;

static int				x_shmeventtype;
//static XShmSegmentInfo	x_shminfo;

static qboolean			oktodraw = false;

int XShmQueryExtension(Display *);
int XShmGetEventBase(Display *);

int current_framebuffer;
static char			*x_framebuffer[2] = { 0, 0 };
static XShmSegmentInfo	x_shminfo[2];

static int verbose=0;

static byte current_palette[768];

static long X11_highhunkmark;
static long X11_buffersize;

int vid_surfcachesize;
void *vid_surfcache;

static XDFB_Info *dfb_info;

unsigned char *memfb;

static int double_pixel;

static unsigned char *single_pixel_fb;

void (*vid_menudrawfn)(void);
void (*vid_menukeyfn)(int key);
void VID_MenuKey (int key);

// ========================================================================
// Tragic death handler
// ========================================================================

void TragicDeath(int signal_num)
{
	XAutoRepeatOn(x_disp);
	XCloseDisplay(x_disp);
	Sys_Error("This death brought to you by the number %d\n", signal_num);
}

// ========================================================================
// makes a null cursor
// ========================================================================

static Cursor CreateNullCursor(Display *display, Window root)
{
    Pixmap cursormask; 
    XGCValues xgc;
    GC gc;
    XColor dummycolour;
    Cursor cursor;

    cursormask = XCreatePixmap(display, root, 1, 1, 1/*depth*/);
    xgc.function = GXclear;
    gc =  XCreateGC(display, cursormask, GCFunction, &xgc);
    XFillRectangle(display, cursormask, gc, 0, 0, 1, 1);
    dummycolour.pixel = 0;
    dummycolour.red = 0;
    dummycolour.flags = 04;
    cursor = XCreatePixmapCursor(display, cursormask, cursormask,
          &dummycolour,&dummycolour, 0,0);
    XFreePixmap(display,cursormask);
    XFreeGC(display,gc);
    return cursor;
}

void ResetFrameBuffer(void)
{

	d_pzbuffer = NULL;
	X11_highhunkmark = Hunk_HighMark ();

// alloc an extra line in case we want to wrap, and allocate the z-buffer
	X11_buffersize = vid.width * vid.height * sizeof (*d_pzbuffer);

	vid_surfcachesize = D_SurfaceCacheForRes (vid.width, vid.height);

	X11_buffersize += vid_surfcachesize;

	d_pzbuffer = Hunk_HighAllocName (X11_buffersize, "video");
	if (d_pzbuffer == NULL)
		Sys_Error ("Not enough memory for video mode\n");

	vid_surfcache = (byte *) d_pzbuffer
		+ vid.width * vid.height * sizeof (*d_pzbuffer);

	D_InitCaches(vid_surfcache, vid_surfcachesize);

}

// Called at startup to set up translation tables, takes 256 8 bit RGB values
// the palette data will go away after the call, so it must be copied off if
// the video driver will need it again

void	VID_Init (unsigned char *palette)
{

	int pnum, i;
	XVisualInfo template;
	int num_visuals;
	int template_mask;
	int status = Success;
	Window root, child, rootwin, parentwin, currentwin, *children;
	int src_x, src_y, x0, y0, topwidth;
	int width, height, border_width, depth, x, y;
	unsigned nchildren;
	XWindowAttributes attribs;

	double_pixel = COM_CheckParm("-double_pixel")+1;

	vid.width = 640 / double_pixel;
	vid.height = 480 / double_pixel;
	vid.maxwarpwidth = WARP_WIDTH;
	vid.maxwarpheight = WARP_HEIGHT;
	vid.numpages = 2;
	vid.colormap = host_colormap;
//	vid.cbits = VID_CBITS;
//	vid.grades = VID_GRADES;
	vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));

	if (COM_CheckParm("-memfb"))
		memfb = (unsigned char*) malloc(vid.width * vid.height);
	if (double_pixel == 2)
		single_pixel_fb = (unsigned char *) malloc(vid.width * vid.height);

	srandom(getpid());

	verbose=COM_CheckParm("-verbose");

// open the display
	x_disp = XOpenDisplay(0);
	if (!x_disp)
	{
		if (getenv("DISPLAY"))
			Sys_Error("VID: Could not open display [%s]\n",
				getenv("DISPLAY"));
		else
			Sys_Error("VID: Could not open local display\n");
	}

	screen = XDefaultScreen(x_disp);
	template.visualid = XVisualIDFromVisual(XDefaultVisual(x_disp, screen));
	template_mask = VisualIDMask;
	x_visinfo = XGetVisualInfo(x_disp, template_mask, &template, &num_visuals);

	XSynchronize(x_disp, True);

// catch signals so i can turn on auto-repeat

	{
		struct sigaction sa;
		sigaction(SIGINT, 0, &sa);
		sa.sa_handler = TragicDeath;
		sigaction(SIGINT, &sa, 0);
		sigaction(SIGTERM, &sa, 0);
	}

// setup attributes for main window
	{
		int attribmask = CWEventMask  | CWBorderPixel;
		XSetWindowAttributes attribs;

		attribs.event_mask = StructureNotifyMask | KeyPressMask
			| KeyReleaseMask | ExposureMask;
		attribs.border_pixel = 0;

// create the main window
		x_win = XCreateWindow(	x_disp,
			XRootWindow(x_disp, x_visinfo->screen),
			0, 0,	// x, y
			640, 480,
			0, // borderwidth
			x_visinfo->depth,
			InputOutput,
			DefaultVisual(x_disp, screen),
			attribmask,
			&attribs );

	}

//	XDirectWindowAccess(x_disp, x_win);

	dfb_info = XdfbCreateDirectAccess(x_disp, x_win, 0);

	parentwin = x_win;

// create and upload the palette
	x_cmap = XCreateColormap(x_disp, x_win, DefaultVisual(x_disp, screen), AllocAll);
	VID_SetPalette(palette);
	if (!memfb)
	{
		XSetWindowColormap(x_disp, x_win, x_cmap);
		XInstallColormap(x_disp, x_cmap);
	}

// inviso cursor
	XDefineCursor(x_disp, x_win, CreateNullCursor(x_disp, x_win));

// don't need it?
/*
	{
		XGCValues xgcvalues;
		int valuemask = GCGraphicsExposures;
		xgcvalues.graphics_exposures = False;
		x_gc = XCreateGC(x_disp, x_win, valuemask, &xgcvalues );
	}
*/

// map the window
	XMapWindow(x_disp, x_win);

// wait for first exposure event
	{
		XEvent event;
		do
		{
			XNextEvent(x_disp, &event);
			if (event.type == Expose && !event.xexpose.count)
				oktodraw = true;
		} while (!oktodraw);
	}

	ResetFrameBuffer();

//	status = XGrabPointer(x_disp, x_win, False, KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
//		GrabModeAsync, GrabModeAsync, x_win, None, CurrentTime);

	XGetGeometry(x_disp, x_win, &root, &src_x, &src_y, &width, &height, &border_width, &depth);

	XTranslateCoordinates (x_disp, x_win, root, src_x, src_y, &x, &y, &child);

	do
	{
		currentwin = parentwin;
		XQueryTree(x_disp, currentwin, &rootwin, &parentwin, &children, &nchildren);
		XFree(children);
	} while (rootwin != parentwin);

	XGetWindowAttributes(x_disp, currentwin, &attribs);

	printf("x=%d,y=%d,width=%d,height=%d\n", attribs.x, attribs.y, attribs.width, attribs.height);

	XMoveWindow(x_disp, x_win, -((attribs.width - width)/2), -(attribs.height - height - ((attribs.width - width)/2)));

	fprintf(stderr, "1\n");

	fprintf(stderr, "status = %d\n", status);
	fprintf(stderr, "d %x, w %d\n", x_disp, x_win);

	XDAEChangeDisplayResolution(x_disp, x_win, Resolution640X480X60Hz);

	fprintf(stderr, "howdy\n");

	current_framebuffer = 1;
	if (memfb)
	{
		vid.buffer = memfb;
		vid.rowbytes = vid.width;
	}
	else if (double_pixel == 1)
	{
		vid.buffer = (byte *) dfb_info->current_draw_buffer;
		vid.rowbytes = (int) dfb_info->stride;
	}
	else
	{
		vid.buffer = (byte *) single_pixel_fb;
		vid.rowbytes = 320;
	}
	vid.direct = 0;
	vid.conbuffer =  vid.buffer;
	vid.conrowbytes = vid.rowbytes;
	vid.conwidth = vid.width;
	vid.conheight = vid.height;
	vid.aspect = ((float)vid.height / (float)vid.width) * (320.0 / 240.0);

}

void VID_ShiftPalette(unsigned char *p)
{
	VID_SetPalette(p);
}

void VID_SetPalette(unsigned char *palette)
{

	int i;
	XColor colors[256];

	if (palette != current_palette)
		memcpy(current_palette, palette, 768);
	for (i=0 ; i<256 ; i++)
	{
		colors[i].pixel = i;
		colors[i].flags = DoRed|DoGreen|DoBlue;
		colors[i].red = palette[i*3] * 257;
		colors[i].green = palette[i*3+1] * 257;
		colors[i].blue = palette[i*3+2] * 257;
	}
	XStoreColors(x_disp, x_cmap, colors, 256);

}

// Called at shutdown

void	VID_Shutdown (void)
{
	Con_Printf("VID_Shutdown\n");
	XDAEChangeDisplayResolution(x_disp, x_win, DefaultResolution);
	XUngrabPointer(x_disp, CurrentTime);
	XdfbDestroyDirectAccess(dfb_info);
	XCloseDisplay(x_disp);
}

int XLateKey(XKeyEvent *ev)
{

	int key;
	char buf[64];
	KeySym keysym;

	key = 0;

	XLookupString(ev, buf, sizeof buf, &keysym, 0);

	switch(keysym)
	{
		case XK_Page_Up:	 key = K_PGUP; break;
		case XK_Page_Down:	 key = K_PGDN; break;
		case XK_Home:	 key = K_HOME; break;
		case XK_End:	 key = K_END; break;
		case XK_Left:	 key = K_LEFTARROW; break;
		case XK_Right:	key = K_RIGHTARROW;		break;
		case XK_Down:	 key = K_DOWNARROW; break;
		case XK_Up:		 key = K_UPARROW;	 break;
		case XK_Escape: key = K_ESCAPE;		break;
		case XK_Return: key = K_ENTER;		 break;
		case XK_Tab:		key = K_TAB;			 break;
		case XK_F1:		 key = K_F1;				break;
		case XK_F2:		 key = K_F2;				break;
		case XK_F3:		 key = K_F3;				break;
		case XK_F4:		 key = K_F4;				break;
		case XK_F5:		 key = K_F5;				break;
		case XK_F6:		 key = K_F6;				break;
		case XK_F7:		 key = K_F7;				break;
		case XK_F8:		 key = K_F8;				break;
		case XK_F9:		 key = K_F9;				break;
		case XK_F10:		key = K_F10;			 break;
		case XK_F11:		key = K_F11;			 break;
		case XK_F12:		key = K_F12;			 break;
		case XK_BackSpace:
		case XK_Delete: key = K_BACKSPACE; break;
		case XK_Pause:	key = K_PAUSE;		 break;
		case XK_Shift_L:
		case XK_Shift_R:		key = K_SHIFT;		break;
		case XK_Execute: 
		case XK_Control_L: 
		case XK_Control_R:	key = K_CTRL;		 break;
		case XK_Alt_L:	
		case XK_Meta_L: 
		case XK_Alt_R:	
		case XK_Meta_R: key = K_ALT;			break;

		case 0x021: key = '1';break;/* [!] */
		case 0x040: key = '2';break;/* [@] */
		case 0x023: key = '3';break;/* [#] */
		case 0x024: key = '4';break;/* [$] */
		case 0x025: key = '5';break;/* [%] */
		case 0x05e: key = '6';break;/* [^] */
		case 0x026: key = '7';break;/* [&] */
		case 0x02a: key = '8';break;/* [*] */
		case 0x028: key = '9';;break;/* [(] */
		case 0x029: key = '0';break;/* [)] */
		case 0x05f: key = '-';break;/* [_] */
		case 0x02b: key = '=';break;/* [+] */
		case 0x07c: key = '\'';break;/* [|] */
		case 0x07d: key = '[';break;/* [}] */
		case 0x07b: key = ']';break;/* [{] */
		case 0x022: key = '\'';break;/* ["] */
		case 0x03a: key = ';';break;/* [:] */
		case 0x03f: key = '/';break;/* [?] */
		case 0x03e: key = '.';break;/* [>] */
		case 0x03c: key = ',';break;/* [<] */

		default:
			key = *(unsigned char*)buf;
			if (key >= 'A' && key <= 'Z')
				key = key - 'A' + 'a';
//			fprintf(stderr, "case 0x0%x: key = ___;break;/* [%c] */\n", keysym);
			break;
	} 

	return key;

}

struct
{
	int key;
	int down;
} keyq[64];
int keyq_head=0;
int keyq_tail=0;

int config_notify=0;
int config_notify_width;
int config_notify_height;

void GetEvent(void)
{

	XEvent x_event;

	XNextEvent(x_disp, &x_event);
	switch(x_event.type)
	{
		case KeyPress:
			keyq[keyq_head].key = XLateKey(&x_event.xkey);
			keyq[keyq_head].down = true;
			keyq_head = (keyq_head + 1) & 63;
			break;
		case KeyRelease:
			keyq[keyq_head].key = XLateKey(&x_event.xkey);
			keyq[keyq_head].down = false;
			keyq_head = (keyq_head + 1) & 63;
			break;
		case ConfigureNotify:
//			printf("config notify\n");
			config_notify_width = x_event.xconfigure.width;
			config_notify_height = x_event.xconfigure.height;
			config_notify = 1;
			break;
		default:
			if (doShm && x_event.type == x_shmeventtype)
				oktodraw = true;
	}

}

void VID_DoublePixel1(byte *out, vrect_t *rects)
{

	int x, y;
	unsigned short *o, *o2;
	byte *i;
	unsigned short p;
	int rowshorts;

	rowshorts = dfb_info->stride / 2;

	while (rects)
	{
		o = (unsigned short*) out + rects->y * rowshorts + rects->x;
		o2 = o + rowshorts;
		i = (byte *) vid.buffer + rects->y * 320 + rects->x;
		for (y=0 ; y<rects->height ; y++)
		{
			for (x=0 ; x<rects->width ; x++)
			{
				p = i[x];
				p += (p<<8);
				o[x] = p;
				o2[x] = p;
			}
			o += dfb_info->stride;
			o2 += dfb_info->stride;
			i += 320;
		}
		rects = rects->pnext;
	}

}

void VID_DoublePixel2(byte *out, vrect_t *rects)
{

	int x, y;
	unsigned int *o;
	unsigned int *o2;
	byte *i;
	unsigned int p;
	int rowints;

	rects->x = rects->x & ~1;
	rects->width = (rects->width + 1) & ~1;

	rowints = dfb_info->stride / 4;

	while (rects)
	{
		o = (unsigned int*) out + rects->y * rowints + rects->x / 2;
		o2 = o + rowints;
		i = (byte *) vid.buffer + rects->y * 320 + rects->x;
		for (y=0 ; y<rects->height ; y++)
		{
			for (x=0 ; x<rects->width/2 ; x++)
			{
				p = i[x*2];
				p += (p<<8);
				p = (p<<8) + i[x*2+1];
				p = (p<<8) + i[x*2+1];
				o[x] = p;
				o2[x] = p;
			}
			o += rowints * 2;
			o2 += rowints * 2;
			i += 320;
		}
		rects = rects->pnext;
	}

}

// flushes the given rectangles from the view buffer to the screen

void	VID_Update (vrect_t *rects)
{

// flip pages

	if (!memfb)
	{
		if (double_pixel == 1)
		{
			XdfbSwapBuffer(dfb_info, XDFB_SwapVertRetrace);
			vid.buffer = (byte *) dfb_info->current_draw_buffer;
		}
		else
		{
			VID_DoublePixel2((byte *) dfb_info->current_draw_buffer, rects);
			XdfbSwapBuffer(dfb_info, XDFB_SwapVertRetrace);
		}
	}
	vid.conbuffer = vid.buffer;

}

static int dither;

void VID_DitherOn(void)
{
    if (dither == 0)
    {
		vid.recalc_refdef = 1;
        dither = 1;
    }
}

void VID_DitherOff(void)
{
    if (dither)
    {
		vid.recalc_refdef = 1;
        dither = 0;
    }
}

int Sys_OpenWindow(void)
{
	return 0;
}

void Sys_EraseWindow(int window)
{
}

void Sys_DrawCircle(int window, int x, int y, int r)
{
}

void Sys_DisplayWindow(int window)
{
}

void Sys_SendKeyEvents(void)
{
// get events from x server
	if (x_disp)
	{
		while (XPending(x_disp)) GetEvent();
		while (keyq_head != keyq_tail)
		{
			Key_Event(keyq[keyq_tail].key, keyq[keyq_tail].down);
			keyq_tail = (keyq_tail + 1) & 63;
		}
	}
}

char *Sys_ConsoleInput (void)
{

	static char	text[256];
	int		len;
	fd_set  readfds;
	int		ready;
	struct timeval timeout;

	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	FD_ZERO(&readfds);
	FD_SET(0, &readfds);
	ready = select(1, &readfds, 0, 0, &timeout);

	if (ready>0)
	{
		len = read (0, text, sizeof(text));
		if (len >= 1)
		{
			text[len-1] = 0;	// rip off the /n and terminate
			return text;
		}
	}

	return 0;
	
}

void D_BeginDirectRect (int x, int y, byte *pbitmap, int width, int height)
{
// direct drawing of the "accessing disk" icon isn't supported under Linux
}

void D_EndDirectRect (int x, int y, int width, int height)
{
// direct drawing of the "accessing disk" icon isn't supported under Linux
}

