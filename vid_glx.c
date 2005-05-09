/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

//#include <termios.h>
//#include <sys/ioctl.h>
//#include <sys/stat.h>
//#include <sys/vt.h>
//#include <stdarg.h>
//#include <stdio.h>
#include <signal.h>

#include <dlfcn.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glx.h>

#include <X11/keysym.h>
#include <X11/cursorfont.h>

#include <X11/extensions/XShm.h>
#if !defined(__APPLE__) && !defined(__MACH__)
#include <X11/extensions/xf86dga.h>
#endif
#include <X11/extensions/xf86vmode.h>

#include "quakedef.h"

// Tell startup code that we have a client
int cl_available = true;

//GLX prototypes
XVisualInfo *(GLAPIENTRY *qglXChooseVisual)(Display *dpy, int screen, int *attribList);
GLXContext (GLAPIENTRY *qglXCreateContext)(Display *dpy, XVisualInfo *vis, GLXContext shareList, Bool direct);
void (GLAPIENTRY *qglXDestroyContext)(Display *dpy, GLXContext ctx);
Bool (GLAPIENTRY *qglXMakeCurrent)(Display *dpy, GLXDrawable drawable, GLXContext ctx);
void (GLAPIENTRY *qglXSwapBuffers)(Display *dpy, GLXDrawable drawable);
const char *(GLAPIENTRY *qglXQueryExtensionsString)(Display *dpy, int screen);

//GLX_ARB_get_proc_address
void *(GLAPIENTRY *qglXGetProcAddressARB)(const GLubyte *procName);

static dllfunction_t getprocaddressfuncs[] =
{
	{"glXGetProcAddressARB", (void **) &qglXGetProcAddressARB},
	{NULL, NULL}
};

//GLX_SGI_swap_control
GLint (GLAPIENTRY *qglXSwapIntervalSGI)(GLint interval);

static dllfunction_t swapcontrolfuncs[] =
{
	{"glXSwapIntervalSGI", (void **) &qglXSwapIntervalSGI},
	{NULL, NULL}
};

static Display *vidx11_display = NULL;
static int vidx11_screen;
static Window win;
static GLXContext ctx = NULL;

Atom wm_delete_window_atom;

#define KEY_MASK (KeyPressMask | KeyReleaseMask)
#define MOUSE_MASK (ButtonPressMask | ButtonReleaseMask | \
		    PointerMotionMask | ButtonMotionMask)
#define X_MASK (KEY_MASK | MOUSE_MASK | VisibilityChangeMask | \
		StructureNotifyMask | FocusChangeMask | EnterWindowMask | \
		LeaveWindowMask)


static qboolean mouse_avail = true;
static qboolean vid_usingmouse = false;
static qboolean vid_usemouse = false;
static qboolean vid_usingvsync = false;
static qboolean vid_usevsync = false;
static qboolean ignoremousemove = false;
static float	mouse_x, mouse_y;
static int p_mouse_x, p_mouse_y;

#ifndef __APPLE__
cvar_t vid_dga = {CVAR_SAVE, "vid_dga", "1"};
cvar_t vid_dga_mouseaccel = {0, "vid_dga_mouseaccel", "1"};
#endif

qboolean vidmode_ext = false;

static int win_x, win_y;

static int scr_width, scr_height;

static XF86VidModeModeInfo **vidmodes;
static int num_vidmodes;
static qboolean vid_isfullscreen = false;

static Visual *vidx11_visual;
static Colormap vidx11_colormap;

/*-----------------------------------------------------------------------*/

static int XLateKey(XKeyEvent *ev, char *ascii)
{
	int key = 0;
	char buf[64];
	KeySym keysym, shifted;

	keysym = XLookupKeysym (ev, 0);
	XLookupString(ev, buf, sizeof buf, &shifted, 0);
	*ascii = buf[0];

	switch(keysym)
	{
		case XK_KP_Page_Up:	 key = K_KP_PGUP; break;
		case XK_Page_Up:	 key = K_PGUP; break;

		case XK_KP_Page_Down: key = K_KP_PGDN; break;
		case XK_Page_Down:	 key = K_PGDN; break;

		case XK_KP_Home: key = K_KP_HOME; break;
		case XK_Home:	 key = K_HOME; break;

		case XK_KP_End:  key = K_KP_END; break;
		case XK_End:	 key = K_END; break;

		case XK_KP_Left: key = K_KP_LEFTARROW; break;
		case XK_Left:	 key = K_LEFTARROW; break;

		case XK_KP_Right: key = K_KP_RIGHTARROW; break;
		case XK_Right:	key = K_RIGHTARROW;		break;

		case XK_KP_Down: key = K_KP_DOWNARROW; break;
		case XK_Down:	 key = K_DOWNARROW; break;

		case XK_KP_Up:   key = K_KP_UPARROW; break;
		case XK_Up:		 key = K_UPARROW;	 break;

		case XK_Escape: key = K_ESCAPE;		break;

		case XK_KP_Enter: key = K_KP_ENTER;	break;
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

		case XK_BackSpace: key = K_BACKSPACE; break;

		case XK_KP_Delete: key = K_KP_DEL; break;
		case XK_Delete: key = K_DEL; break;

		case XK_Pause:	key = K_PAUSE;		 break;

		case XK_Shift_L:
		case XK_Shift_R:	key = K_SHIFT;		break;

		case XK_Execute:
		case XK_Control_L:
		case XK_Control_R:	key = K_CTRL;		 break;

		case XK_Alt_L:
		case XK_Meta_L:
		case XK_Alt_R:
		case XK_Meta_R: key = K_ALT;			break;

		case XK_KP_Begin: key = K_KP_5;	break;

		case XK_Insert:key = K_INS; break;
		case XK_KP_Insert: key = K_KP_INS; break;

		case XK_KP_Multiply: key = '*'; break;
		case XK_KP_Add:  key = K_KP_PLUS; break;
		case XK_KP_Subtract: key = K_KP_MINUS; break;
		case XK_KP_Divide: key = K_KP_SLASH; break;

		default:
			if (keysym < 32 && keysym > 126)
				break;

			if (keysym >= 'A' && keysym <= 'Z')
				key = keysym - 'A' + 'a';
			else
				key = keysym;

			break;
	}

	return key;
}

static Cursor CreateNullCursor(Display *display, Window root)
{
	Pixmap cursormask;
	XGCValues xgc;
	GC gc;
	XColor dummycolour;
	Cursor cursor;

	cursormask = XCreatePixmap(display, root, 1, 1, 1);
	xgc.function = GXclear;
	gc =  XCreateGC(display, cursormask, GCFunction, &xgc);
	XFillRectangle(display, cursormask, gc, 0, 0, 1, 1);
	dummycolour.pixel = 0;
	dummycolour.red = 0;
	dummycolour.flags = 04;
	cursor = XCreatePixmapCursor(display, cursormask, cursormask, &dummycolour,&dummycolour, 0,0);
	XFreePixmap(display,cursormask);
	XFreeGC(display,gc);
	return cursor;
}

static void IN_Activate (qboolean grab)
{
	if (!vidx11_display)
		return;
	if (grab)
	{
		if (!vid_usingmouse && mouse_avail && win)
		{
			XWindowAttributes attribs_1;
			XSetWindowAttributes attribs_2;

			XGetWindowAttributes(vidx11_display, win, &attribs_1);
			attribs_2.event_mask = attribs_1.your_event_mask | KEY_MASK | MOUSE_MASK;
			XChangeWindowAttributes(vidx11_display, win, CWEventMask, &attribs_2);

		// inviso cursor
			XDefineCursor(vidx11_display, win, CreateNullCursor(vidx11_display, win));

			XGrabPointer(vidx11_display, win,  True, 0, GrabModeAsync, GrabModeAsync, win, None, CurrentTime);

#ifndef __APPLE__
			if (vid_dga.integer)
			{
				int MajorVersion, MinorVersion;

				if (!XF86DGAQueryVersion(vidx11_display, &MajorVersion, &MinorVersion))
				{
					// unable to query, probably not supported
					Con_Print( "Failed to detect XF86DGA Mouse\n" );
					Cvar_SetValueQuick(&vid_dga, 0);
					XWarpPointer(vidx11_display, None, win, 0, 0, 0, 0, scr_width / 2, scr_height / 2);
				}
				else
				{
					XF86DGADirectVideo(vidx11_display, DefaultScreen(vidx11_display), XF86DGADirectMouse);
					XWarpPointer(vidx11_display, None, win, 0, 0, 0, 0, 0, 0);
				}
			}
			else
#endif
				XWarpPointer(vidx11_display, None, win, 0, 0, 0, 0, scr_width / 2, scr_height / 2);

			XGrabKeyboard(vidx11_display, win, False, GrabModeAsync, GrabModeAsync, CurrentTime);

			mouse_x = mouse_y = 0;
			ignoremousemove = true;
			vid_usingmouse = true;
		}
	}
	else
	{
		if (vid_usingmouse)
		{
#ifndef __APPLE__
			if (vid_dga.integer)
				XF86DGADirectVideo(vidx11_display, DefaultScreen(vidx11_display), 0);
#endif

			XUngrabPointer(vidx11_display, CurrentTime);
			XUngrabKeyboard(vidx11_display, CurrentTime);

			// inviso cursor
			if (win)
				XUndefineCursor(vidx11_display, win);

			ignoremousemove = true;
			vid_usingmouse = false;
		}
	}
}

static void HandleEvents(void)
{
	XEvent event;
	int key;
	char ascii;
	qboolean dowarp = false;

	if (!vidx11_display)
		return;

	while (XPending(vidx11_display))
	{
		XNextEvent(vidx11_display, &event);

		switch (event.type)
		{
		case KeyPress:
			// key pressed
			key = XLateKey (&event.xkey, &ascii);
			Key_Event(key, ascii, true);
			break;

		case KeyRelease:
			// key released
			key = XLateKey (&event.xkey, &ascii);
			Key_Event(key, ascii, false);
			break;

		case MotionNotify:
			// mouse moved
			if (vid_usingmouse)
			{
#ifndef __APPLE__
				if (vid_dga.integer == 1)
				{
					mouse_x += event.xmotion.x_root * vid_dga_mouseaccel.value;
					mouse_y += event.xmotion.y_root * vid_dga_mouseaccel.value;
				}
				else
#endif
				{

					if (!event.xmotion.send_event)
					{
						mouse_x += event.xmotion.x - p_mouse_x;
						mouse_y += event.xmotion.y - p_mouse_y;
						if (abs(scr_width/2 - event.xmotion.x) > scr_width / 4 || abs(scr_height/2 - event.xmotion.y) > scr_height / 4)
							dowarp = true;
					}
					p_mouse_x = event.xmotion.x;
					p_mouse_y = event.xmotion.y;
				}
			}
			//else
			//	ui_mouseupdate(event.xmotion.x, event.xmotion.y);
			break;

		case ButtonPress:
			// mouse button pressed
			switch(event.xbutton.button)
			{
			case 1:
				Key_Event(K_MOUSE1, 0, true);
				break;
			case 2:
				Key_Event(K_MOUSE3, 0, true);
				break;
			case 3:
				Key_Event(K_MOUSE2, 0, true);
				break;
			case 4:
				Key_Event(K_MWHEELUP, 0, true);
				break;
			case 5:
				Key_Event(K_MWHEELDOWN, 0, true);
				break;
			case 6:
				Key_Event(K_MOUSE4, 0, true);
				break;
			case 7:
				Key_Event(K_MOUSE5, 0, true);
				break;
			case 8:
				Key_Event(K_MOUSE6, 0, true);
				break;
			case 9:
				Key_Event(K_MOUSE7, 0, true);
				break;
			case 10:
				Key_Event(K_MOUSE8, 0, true);
				break;
			case 11:
				Key_Event(K_MOUSE9, 0, true);
				break;
			case 12:
				Key_Event(K_MOUSE10, 0, true);
				break;
			default:
				Con_Printf("HandleEvents: ButtonPress gave value %d, 1-12 expected\n", event.xbutton.button);
				break;
			}
			break;

		case ButtonRelease:
			// mouse button released
			switch(event.xbutton.button)
			{
			case 1:
				Key_Event(K_MOUSE1, 0, false);
				break;
			case 2:
				Key_Event(K_MOUSE3, 0, false);
				break;
			case 3:
				Key_Event(K_MOUSE2, 0, false);
				break;
			case 4:
				Key_Event(K_MWHEELUP, 0, false);
				break;
			case 5:
				Key_Event(K_MWHEELDOWN, 0, false);
				break;
			case 6:
				Key_Event(K_MOUSE4, 0, false);
				break;
			case 7:
				Key_Event(K_MOUSE5, 0, false);
				break;
			case 8:
				Key_Event(K_MOUSE6, 0, false);
				break;
			case 9:
				Key_Event(K_MOUSE7, 0, false);
				break;
			case 10:
				Key_Event(K_MOUSE8, 0, false);
				break;
			case 11:
				Key_Event(K_MOUSE9, 0, false);
				break;
			case 12:
				Key_Event(K_MOUSE10, 0, false);
				break;
			default:
				Con_Printf("HandleEvents: ButtonRelease gave value %d, 1-12 expected\n", event.xbutton.button);
				break;
			}
			break;

		case CreateNotify:
			// window created
			win_x = event.xcreatewindow.x;
			win_y = event.xcreatewindow.y;
			break;

		case ConfigureNotify:
			// window changed size/location
			win_x = event.xconfigure.x;
			win_y = event.xconfigure.y;
			break;
		case DestroyNotify:
			// window has been destroyed
			Sys_Quit();
			break;
		case ClientMessage:
			// window manager messages
			if ((event.xclient.format == 32) && ((unsigned int)event.xclient.data.l[0] == wm_delete_window_atom))
				Sys_Quit();
			break;
		case MapNotify:
			// window restored
			vid_hidden = false;
			vid_activewindow = false;
			VID_RestoreSystemGamma();
			break;
		case UnmapNotify:
			// window iconified/rolledup/whatever
			vid_hidden = true;
			vid_activewindow = false;
			VID_RestoreSystemGamma();
			break;
		case FocusIn:
			// window is now the input focus
			vid_activewindow = true;
			break;
		case FocusOut:
			// window is no longer the input focus
			vid_activewindow = false;
			VID_RestoreSystemGamma();
			break;
		case EnterNotify:
			// mouse entered window
			break;
		case LeaveNotify:
			// mouse left window
			break;
		}
	}

	if (dowarp)
	{
		/* move the mouse to the window center again */
		p_mouse_x = scr_width / 2;
		p_mouse_y = scr_height / 2;
		XWarpPointer(vidx11_display, None, win, 0, 0, 0, 0, p_mouse_x, p_mouse_y);
	}

	// if told to ignore one mouse move, do so
	if (ignoremousemove)
	{
		ignoremousemove = false;
		mouse_x = 0;
		mouse_y = 0;
	}
}

static void *prjobj = NULL;

static void GL_CloseLibrary(void)
{
	if (prjobj)
		dlclose(prjobj);
	prjobj = NULL;
	gl_driver[0] = 0;
	qglXGetProcAddressARB = NULL;
	gl_extensions = "";
	gl_platform = "";
	gl_platformextensions = "";
}

static int GL_OpenLibrary(const char *name)
{
	Con_Printf("Loading OpenGL driver %s\n", name);
	GL_CloseLibrary();
	if (!(prjobj = dlopen(name, RTLD_LAZY)))
	{
		Con_Printf("Unable to open symbol list for %s\n", name);
		return false;
	}
	strcpy(gl_driver, name);
	return true;
}

void *GL_GetProcAddress(const char *name)
{
	void *p = NULL;
	if (qglXGetProcAddressARB != NULL)
		p = (void *) qglXGetProcAddressARB(name);
	if (p == NULL)
		p = (void *) dlsym(prjobj, name);
	return p;
}

void VID_Shutdown(void)
{
	if (!ctx || !vidx11_display)
		return;

	if (vidx11_display)
	{
		IN_Activate(false);
		VID_RestoreSystemGamma();

		// FIXME: glXDestroyContext here?
		if (vid_isfullscreen)
			XF86VidModeSwitchToMode(vidx11_display, vidx11_screen, vidmodes[0]);
		if (win)
			XDestroyWindow(vidx11_display, win);
		XCloseDisplay(vidx11_display);
	}
	vid_hidden = true;
	vid_isfullscreen = false;
	vidx11_display = NULL;
	win = 0;
	ctx = NULL;

	GL_CloseLibrary();
	Key_ClearStates ();
}

void signal_handler(int sig)
{
	Con_Printf("Received signal %d, exiting...\n", sig);
	VID_RestoreSystemGamma();
	Sys_Quit();
	exit(0);
}

void InitSig(void)
{
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGQUIT, signal_handler);
	signal(SIGILL, signal_handler);
	signal(SIGTRAP, signal_handler);
	signal(SIGIOT, signal_handler);
	signal(SIGBUS, signal_handler);
	signal(SIGFPE, signal_handler);
	signal(SIGSEGV, signal_handler);
	signal(SIGTERM, signal_handler);
}

/*
=================
VID_GetWindowSize
=================
*/
void VID_GetWindowSize (int *x, int *y, int *width, int *height)
{
	*x = *y = 0;
	*width = scr_width;
	*height = scr_height;
}

void VID_Finish (void)
{
	vid_usevsync = vid_vsync.integer && !cls.timedemo && gl_videosyncavailable;
	if (vid_usingvsync != vid_usevsync && gl_videosyncavailable)
	{
		vid_usingvsync = vid_usevsync;
		if (qglXSwapIntervalSGI (vid_usevsync))
			Con_Print("glXSwapIntervalSGI didn't accept the vid_vsync change, it will take effect on next vid_restart (GLX_SGI_swap_control does not allow turning off vsync)\n");
	}

// handle the mouse state when windowed if that's changed
	vid_usemouse = false;
	if (vid_mouse.integer && !key_consoleactive && !cls.demoplayback)
		vid_usemouse = true;
	if (!vid_activewindow)
		vid_usemouse = false;
	if (vid_isfullscreen)
		vid_usemouse = true;
	IN_Activate(vid_usemouse);

	if (r_render.integer)
	{
		if (r_speeds.integer || gl_finish.integer)
			qglFinish();
		qglXSwapBuffers(vidx11_display, win);
	}
}

int VID_SetGamma(unsigned short *ramps)
{
	return XF86VidModeSetGammaRamp(vidx11_display, vidx11_screen, 256, ramps, ramps + 256, ramps + 512);
}

int VID_GetGamma(unsigned short *ramps)
{
	return XF86VidModeGetGammaRamp(vidx11_display, vidx11_screen, 256, ramps, ramps + 256, ramps + 512);
}

void VID_Init(void)
{
#ifndef __APPLE__
	Cvar_RegisterVariable (&vid_dga);
	Cvar_RegisterVariable (&vid_dga_mouseaccel);
#endif
	InitSig(); // trap evil signals
// COMMANDLINEOPTION: Input: -nomouse disables mouse support (see also vid_mouse cvar)
	if (COM_CheckParm ("-nomouse") || COM_CheckParm("-safe"))
		mouse_avail = false;
}

void VID_BuildGLXAttrib(int *attrib, int stencil)
{
	*attrib++ = GLX_RGBA;
	*attrib++ = GLX_RED_SIZE;*attrib++ = 1;
	*attrib++ = GLX_GREEN_SIZE;*attrib++ = 1;
	*attrib++ = GLX_BLUE_SIZE;*attrib++ = 1;
	*attrib++ = GLX_DOUBLEBUFFER;
	*attrib++ = GLX_DEPTH_SIZE;*attrib++ = 1;
	// if stencil is enabled, ask for alpha too
	if (stencil)
	{
		*attrib++ = GLX_STENCIL_SIZE;*attrib++ = 8;
		*attrib++ = GLX_ALPHA_SIZE;*attrib++ = 1;
	}
	*attrib++ = None;
}

int VID_InitMode(int fullscreen, int width, int height, int bpp)
{
	int i;
	int attrib[32];
	XSetWindowAttributes attr;
	unsigned long mask;
	Window root;
	XVisualInfo *visinfo;
	int MajorVersion, MinorVersion;
	const char *drivername;

#if defined(__APPLE__) && defined(__MACH__)
	drivername = "/usr/X11R6/lib/libGL.1.dylib";
#else
	drivername = "libGL.so.1";
#endif
// COMMANDLINEOPTION: Linux GLX: -gl_driver <drivername> selects a GL driver library, default is libGL.so.1, useful only for using fxmesa or similar, if you don't know what this is for, you don't need it
// COMMANDLINEOPTION: BSD GLX: -gl_driver <drivername> selects a GL driver library, default is libGL.so.1, useful only for using fxmesa or similar, if you don't know what this is for, you don't need it
// LordHavoc: although this works on MacOSX, it's useless there (as there is only one system libGL)
	i = COM_CheckParm("-gl_driver");
	if (i && i < com_argc - 1)
		drivername = com_argv[i + 1];
	if (!GL_OpenLibrary(drivername))
	{
		Con_Printf("Unable to load GL driver \"%s\"\n", drivername);
		return false;
	}

	if (!(vidx11_display = XOpenDisplay(NULL)))
	{
		Con_Print("Couldn't open the X display\n");
		return false;
	}

	vidx11_screen = DefaultScreen(vidx11_display);
	root = RootWindow(vidx11_display, vidx11_screen);

	// Get video mode list
	MajorVersion = MinorVersion = 0;
	if (!XF86VidModeQueryVersion(vidx11_display, &MajorVersion, &MinorVersion))
		vidmode_ext = false;
	else
	{
		Con_DPrintf("Using XFree86-VidModeExtension Version %d.%d\n", MajorVersion, MinorVersion);
		vidmode_ext = true;
	}

	if ((qglXChooseVisual = GL_GetProcAddress("glXChooseVisual")) == NULL
	 || (qglXCreateContext = GL_GetProcAddress("glXCreateContext")) == NULL
	 || (qglXMakeCurrent = GL_GetProcAddress("glXMakeCurrent")) == NULL
	 || (qglXSwapBuffers = GL_GetProcAddress("glXSwapBuffers")) == NULL
	 || (qglXQueryExtensionsString = GL_GetProcAddress("glXQueryExtensionsString")) == NULL)
	{
		Con_Printf("glX functions not found in %s\n", gl_driver);
		return false;
	}

	VID_BuildGLXAttrib(attrib, bpp == 32);
	visinfo = qglXChooseVisual(vidx11_display, vidx11_screen, attrib);
	if (!visinfo)
	{
		Con_Print("Couldn't get an RGB, Double-buffered, Depth visual\n");
		return false;
	}

	if (vidmode_ext)
	{
		int best_fit, best_dist, dist, x, y;

		// Are we going fullscreen?  If so, let's change video mode
		if (fullscreen)
		{
			XF86VidModeGetAllModeLines(vidx11_display, vidx11_screen, &num_vidmodes, &vidmodes);
			best_dist = 9999999;
			best_fit = -1;

			for (i = 0; i < num_vidmodes; i++)
			{
				if (width > vidmodes[i]->hdisplay || height > vidmodes[i]->vdisplay)
					continue;

				x = width - vidmodes[i]->hdisplay;
				y = height - vidmodes[i]->vdisplay;
				dist = (x * x) + (y * y);
				if (dist < best_dist)
				{
					best_dist = dist;
					best_fit = i;
				}
			}

			if (best_fit != -1)
			{
				// LordHavoc: changed from ActualWidth/ActualHeight =,
				// to width/height =, so the window will take the full area of
				// the mode chosen
				width = vidmodes[best_fit]->hdisplay;
				height = vidmodes[best_fit]->vdisplay;

				// change to the mode
				XF86VidModeSwitchToMode(vidx11_display, vidx11_screen, vidmodes[best_fit]);
				vid_isfullscreen = true;

				// Move the viewport to top left
				XF86VidModeSetViewPort(vidx11_display, vidx11_screen, 0, 0);
			}
			else
				fullscreen = 0;
		}
	}

	// LordHavoc: save the visual for use in gamma ramp settings later
	vidx11_visual = visinfo->visual;

	/* window attributes */
	attr.background_pixel = 0;
	attr.border_pixel = 0;
	// LordHavoc: save the colormap for later, too
	vidx11_colormap = attr.colormap = XCreateColormap(vidx11_display, root, visinfo->visual, AllocNone);
	attr.event_mask = X_MASK;
	if (vid_isfullscreen)
	{
		mask = CWBackPixel | CWColormap | CWSaveUnder | CWBackingStore | CWEventMask | CWOverrideRedirect;
		attr.override_redirect = True;
		attr.backing_store = NotUseful;
		attr.save_under = False;
	}
	else
		mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;

	win = XCreateWindow(vidx11_display, root, 0, 0, width, height, 0, visinfo->depth, InputOutput, visinfo->visual, mask, &attr);
	XStoreName(vidx11_display, win, gamename);
	XMapWindow(vidx11_display, win);

	// LordHavoc: making the close button on a window do the right thing
	// seems to involve this mess, sigh...
	wm_delete_window_atom = XInternAtom(vidx11_display, "WM_DELETE_WINDOW", false);
	XSetWMProtocols(vidx11_display, win, &wm_delete_window_atom, 1);

	if (vid_isfullscreen)
	{
		XMoveWindow(vidx11_display, win, 0, 0);
		XRaiseWindow(vidx11_display, win);
		XWarpPointer(vidx11_display, None, win, 0, 0, 0, 0, 0, 0);
		XFlush(vidx11_display);
		// Move the viewport to top left
		XF86VidModeSetViewPort(vidx11_display, vidx11_screen, 0, 0);
	}

	//XSync(vidx11_display, False);

	ctx = qglXCreateContext(vidx11_display, visinfo, NULL, True);
	if (!ctx)
		Sys_Error ("glXCreateContext failed\n");

	if (!qglXMakeCurrent(vidx11_display, win, ctx))
		Sys_Error ("glXMakeCurrent failed\n");

	XSync(vidx11_display, False);

	scr_width = width;
	scr_height = height;

	if ((qglGetString = GL_GetProcAddress("glGetString")) == NULL)
		Sys_Error("glGetString not found in %s", gl_driver);

	gl_renderer = qglGetString(GL_RENDERER);
	gl_vendor = qglGetString(GL_VENDOR);
	gl_version = qglGetString(GL_VERSION);
	gl_extensions = qglGetString(GL_EXTENSIONS);
	gl_platform = "GLX";
	gl_platformextensions = qglXQueryExtensionsString(vidx11_display, vidx11_screen);

	gl_videosyncavailable = false;

// COMMANDLINEOPTION: Linux GLX: -nogetprocaddress disables GLX_ARB_get_proc_address (not required, more formal method of getting extension functions)
// COMMANDLINEOPTION: BSD GLX: -nogetprocaddress disables GLX_ARB_get_proc_address (not required, more formal method of getting extension functions)
// COMMANDLINEOPTION: MacOSX GLX: -nogetprocaddress disables GLX_ARB_get_proc_address (not required, more formal method of getting extension functions)
	GL_CheckExtension("GLX_ARB_get_proc_address", getprocaddressfuncs, "-nogetprocaddress", false);
// COMMANDLINEOPTION: Linux GLX: -novideosync disables GLX_SGI_swap_control
// COMMANDLINEOPTION: BSD GLX: -novideosync disables GLX_SGI_swap_control
// COMMANDLINEOPTION: MacOSX GLX: -novideosync disables GLX_SGI_swap_control
	gl_videosyncavailable = GL_CheckExtension("GLX_SGI_swap_control", swapcontrolfuncs, "-novideosync", false);

	vid_usingmouse = false;
	vid_usingvsync = false;
	ignoremousemove = true;
	vid_hidden = false;
	vid_activewindow = true;
	GL_Init();
	return true;
}

void Sys_SendKeyEvents(void)
{
	HandleEvents();
}

void IN_Move (void)
{
	if (mouse_avail)
	{
		in_mouse_x = mouse_x;
		in_mouse_y = mouse_y;
	}
	mouse_x = 0;
	mouse_y = 0;
}
