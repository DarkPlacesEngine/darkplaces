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
// gl_vidnt.c -- NT GL vid component

#include "quakedef.h"
#include "winquake.h"
#include "resource.h"
#include <commctrl.h>

int cl_available = true;

int (WINAPI *qwglChoosePixelFormat)(HDC, CONST PIXELFORMATDESCRIPTOR *);
int (WINAPI *qwglDescribePixelFormat)(HDC, int, UINT, LPPIXELFORMATDESCRIPTOR);
//int (WINAPI *qwglGetPixelFormat)(HDC);
BOOL (WINAPI *qwglSetPixelFormat)(HDC, int, CONST PIXELFORMATDESCRIPTOR *);
BOOL (WINAPI *qwglSwapBuffers)(HDC);
HGLRC (WINAPI *qwglCreateContext)(HDC);
BOOL (WINAPI *qwglDeleteContext)(HGLRC);
HGLRC (WINAPI *qwglGetCurrentContext)(VOID);
HDC (WINAPI *qwglGetCurrentDC)(VOID);
PROC (WINAPI *qwglGetProcAddress)(LPCSTR);
BOOL (WINAPI *qwglMakeCurrent)(HDC, HGLRC);
BOOL (WINAPI *qwglSwapIntervalEXT)(int interval);
const char *(WINAPI *qwglGetExtensionsStringARB)(HDC hdc);

static gl_extensionfunctionlist_t getextensionsstringfuncs[] =
{
	{"wglGetExtensionsString", (void **) &qwglGetExtensionsStringARB},
	{NULL, NULL}
};

static gl_extensionfunctionlist_t wglfuncs[] =
{
	{"wglChoosePixelFormat", (void **) &qwglChoosePixelFormat},
	{"wglDescribePixelFormat", (void **) &qwglDescribePixelFormat},
//	{"wglGetPixelFormat", (void **) &qwglGetPixelFormat},
	{"wglSetPixelFormat", (void **) &qwglSetPixelFormat},
	{"wglSwapBuffers", (void **) &qwglSwapBuffers},
	{"wglCreateContext", (void **) &qwglCreateContext},
	{"wglDeleteContext", (void **) &qwglDeleteContext},
	{"wglGetProcAddress", (void **) &qwglGetProcAddress},
	{"wglMakeCurrent", (void **) &qwglMakeCurrent},
	{NULL, NULL}
};

static gl_extensionfunctionlist_t wglswapintervalfuncs[] =
{
	{"wglSwapIntervalEXT", (void **) &qwglSwapIntervalEXT},
	{NULL, NULL}
};

#define MAX_MODE_LIST	30
#define VID_ROW_SIZE	3
#define MAXWIDTH		10000
#define MAXHEIGHT		10000

#define MODE_WINDOWED			0
#define NO_MODE					(MODE_WINDOWED - 1)
#define MODE_FULLSCREEN_DEFAULT	(MODE_WINDOWED + 1)

typedef struct {
	modestate_t	type;
	int			width;
	int			height;
	int			modenum;
	int			dib;
	int			fullscreen;
	int			bpp;
	char		modedesc[17];
} vmode_t;

typedef struct {
	int			width;
	int			height;
} lmode_t;

lmode_t	lowresmodes[] = {
	{320, 200},
	{320, 240},
	{400, 300},
	{512, 384},
};

qboolean scr_skipupdate;

static vmode_t modelist[MAX_MODE_LIST];
static int nummodes;
static vmode_t badmode;

static DEVMODE gdevmode;
static qboolean vid_initialized = false;
static qboolean windowed, leavecurrentmode;
static qboolean vid_canalttab = false;
static qboolean vid_wassuspended = false;
static int vid_usingmouse;
extern qboolean mouseactive;  // from in_win.c
static HICON hIcon;

int DIBWidth, DIBHeight;
RECT WindowRect;
DWORD WindowStyle, ExWindowStyle;

HWND mainwindow;

int vid_modenum = NO_MODE;
int vid_realmode;
int vid_default = MODE_WINDOWED;
static int windowed_default;
unsigned char vid_curpal[256*3];

HGLRC baseRC;
HDC maindc;

HWND WINAPI InitializeWindow (HINSTANCE hInstance, int nCmdShow);

// global video state
viddef_t vid;

modestate_t modestate = MS_UNINIT;

void VID_MenuDraw (void);
void VID_MenuKey (int key);

LONG WINAPI MainWndProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void AppActivate(BOOL fActive, BOOL minimize);
char *VID_GetModeDescription (int mode);
void ClearAllStates (void);
void VID_UpdateWindowStatus (void);

//====================================

int window_center_x, window_center_y, window_x, window_y, window_width, window_height;
RECT window_rect;

// direct draw software compatability stuff

void CenterWindow(HWND hWndCenter, int width, int height, BOOL lefttopjustify)
{
	int CenterX, CenterY;

	CenterX = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
	CenterY = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
	if (CenterX > CenterY*2)
		CenterX >>= 1;	// dual screens
	CenterX = (CenterX < 0) ? 0: CenterX;
	CenterY = (CenterY < 0) ? 0: CenterY;
	SetWindowPos (hWndCenter, NULL, CenterX, CenterY, 0, 0,
			SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW | SWP_DRAWFRAME);
}

qboolean VID_SetWindowedMode (int modenum)
{
	int lastmodestate, width, height;
	RECT rect;

	lastmodestate = modestate;

	WindowRect.top = WindowRect.left = 0;

	WindowRect.right = modelist[modenum].width;
	WindowRect.bottom = modelist[modenum].height;

	DIBWidth = modelist[modenum].width;
	DIBHeight = modelist[modenum].height;

	WindowStyle = WS_OVERLAPPED | WS_BORDER | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	ExWindowStyle = 0;

	rect = WindowRect;
	AdjustWindowRectEx(&rect, WindowStyle, false, 0);

	width = rect.right - rect.left;
	height = rect.bottom - rect.top;

	// Create the DIB window
	mainwindow = CreateWindowEx (ExWindowStyle, gamename, gamename, WindowStyle, rect.left, rect.top, width, height, NULL, NULL, global_hInstance, NULL);

	if (!mainwindow)
		Sys_Error ("Couldn't create DIB window");

	// Center and show the DIB window
	CenterWindow(mainwindow, WindowRect.right - WindowRect.left, WindowRect.bottom - WindowRect.top, false);

	ShowWindow (mainwindow, SW_SHOWDEFAULT);
	UpdateWindow (mainwindow);

	modestate = MS_WINDOWED;

	SendMessage (mainwindow, WM_SETICON, (WPARAM)true, (LPARAM)hIcon);
	SendMessage (mainwindow, WM_SETICON, (WPARAM)false, (LPARAM)hIcon);

	return true;
}


qboolean VID_SetFullDIBMode (int modenum)
{
	int lastmodestate, width, height;
	RECT rect;

	if (!leavecurrentmode)
	{
		gdevmode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
		gdevmode.dmBitsPerPel = modelist[modenum].bpp;
		gdevmode.dmPelsWidth = modelist[modenum].width;
		gdevmode.dmPelsHeight = modelist[modenum].height;
		gdevmode.dmSize = sizeof (gdevmode);

		if (ChangeDisplaySettings (&gdevmode, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL)
			Sys_Error ("Couldn't set fullscreen DIB mode");
	}

	lastmodestate = modestate;
	modestate = MS_FULLDIB;

	WindowRect.top = WindowRect.left = 0;

	WindowRect.right = modelist[modenum].width;
	WindowRect.bottom = modelist[modenum].height;

	DIBWidth = modelist[modenum].width;
	DIBHeight = modelist[modenum].height;

	WindowStyle = WS_POPUP;
	ExWindowStyle = 0;

	rect = WindowRect;
	AdjustWindowRectEx(&rect, WindowStyle, false, 0);

	width = rect.right - rect.left;
	height = rect.bottom - rect.top;

	// Create the DIB window
	mainwindow = CreateWindowEx (ExWindowStyle, gamename, gamename, WindowStyle, rect.left, rect.top, width, height, NULL, NULL, global_hInstance, NULL);

	if (!mainwindow)
		Sys_Error ("Couldn't create DIB window");

	ShowWindow (mainwindow, SW_SHOWDEFAULT);
	UpdateWindow (mainwindow);

// needed because we're not getting WM_MOVE messages fullscreen on NT
	window_x = 0;
	window_y = 0;

	SendMessage (mainwindow, WM_SETICON, (WPARAM)true, (LPARAM)hIcon);
	SendMessage (mainwindow, WM_SETICON, (WPARAM)false, (LPARAM)hIcon);

	return true;
}


int VID_SetMode (int modenum)
{
	int original_mode;
	qboolean stat = 0;
	MSG msg;

	if ((windowed && (modenum != 0)) || (!windowed && (modenum < 1)) || (!windowed && (modenum >= nummodes)))
		Sys_Error ("Bad video mode\n");

	CDAudio_Pause ();

	if (vid_modenum == NO_MODE)
		original_mode = windowed_default;
	else
		original_mode = vid_modenum;

	// Set either the fullscreen or windowed mode
	if (modelist[modenum].type == MS_WINDOWED)
		stat = VID_SetWindowedMode(modenum);
	else if (modelist[modenum].type == MS_FULLDIB)
		stat = VID_SetFullDIBMode(modenum);
	else
		Sys_Error ("VID_SetMode: Bad mode type in modelist");

	window_width = DIBWidth;
	window_height = DIBHeight;
	VID_UpdateWindowStatus ();

	CDAudio_Resume ();

	if (!stat)
		Sys_Error ("Couldn't set video mode");

// now we try to make sure we get the focus on the mode switch, because
// sometimes in some systems we don't.  We grab the foreground, then
// finish setting up, pump all our messages, and sleep for a little while
// to let messages finish bouncing around the system, then we put
// ourselves at the top of the z order, then grab the foreground again,
// Who knows if it helps, but it probably doesn't hurt
	SetForegroundWindow (mainwindow);
	vid_modenum = modenum;
	Cvar_SetValue ("vid_mode", (float)vid_modenum);

	while (PeekMessage (&msg, NULL, 0, 0, PM_REMOVE))
	{
		TranslateMessage (&msg);
		DispatchMessage (&msg);
	}

	Sleep (100);

	SetWindowPos (mainwindow, HWND_TOP, 0, 0, 0, 0, SWP_DRAWFRAME | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOCOPYBITS);

	SetForegroundWindow (mainwindow);

// fix the leftover Alt from any Alt-Tab or the like that switched us away
	ClearAllStates ();

	if (!msg_suppress_1)
		Con_SafePrintf ("Video mode %s initialized.\n", VID_GetModeDescription (vid_modenum));

	return true;
}


/*
================
VID_UpdateWindowStatus
================
*/
void VID_UpdateWindowStatus (void)
{
	window_rect.left = window_x;
	window_rect.top = window_y;
	window_rect.right = window_x + window_width;
	window_rect.bottom = window_y + window_height;
	window_center_x = (window_rect.left + window_rect.right) / 2;
	window_center_y = (window_rect.top + window_rect.bottom) / 2;

	IN_UpdateClipCursor ();
}


//====================================

/*
=================
VID_GetWindowSize
=================
*/
void VID_GetWindowSize (int *x, int *y, int *width, int *height)
{
	*x = *y = 0;
	*width = WindowRect.right - WindowRect.left;
	*height = WindowRect.bottom - WindowRect.top;
}


void VID_Finish (void)
{
	int vid_usemouse;
	if (r_render.integer && !scr_skipupdate)
	{
		qglFinish();
		SwapBuffers(maindc);
	}

// handle the mouse state when windowed if that's changed
	vid_usemouse = false;
	if (vid_mouse.integer && !key_consoleactive)
		vid_usemouse = true;
	if (modestate == MS_FULLDIB)
		vid_usemouse = true;
	if (!vid_activewindow)
		vid_usemouse = false;
	if (vid_usemouse)
	{
		if (!vid_usingmouse)
		{
			vid_usingmouse = true;
			IN_ActivateMouse ();
			IN_HideMouse();
		}
	}
	else
	{
		if (vid_usingmouse)
		{
			vid_usingmouse = false;
			IN_DeactivateMouse ();
			IN_ShowMouse();
		}
	}
}

void VID_SetDefaultMode (void)
{
	IN_DeactivateMouse ();
}

void VID_RestoreSystemGamma(void);

void VID_Shutdown (void)
{
	HGLRC hRC;
	HDC hDC;
	int i;
	GLuint temp[8192];

	if (vid_initialized)
	{
		vid_canalttab = false;
		hRC = qwglGetCurrentContext();
		hDC = qwglGetCurrentDC();

		qwglMakeCurrent(NULL, NULL);

		// LordHavoc: free textures before closing (may help NVIDIA)
		for (i = 0;i < 8192;i++)
			temp[i] = i+1;
		qglDeleteTextures(8192, temp);

		if (hRC)
			qwglDeleteContext(hRC);

		// close the library before we get rid of the window
		GL_CloseLibrary();

		if (hDC && mainwindow)
			ReleaseDC(mainwindow, hDC);

		if (modestate == MS_FULLDIB)
			ChangeDisplaySettings (NULL, 0);

		if (maindc && mainwindow)
			ReleaseDC (mainwindow, maindc);

		AppActivate(false, false);

		VID_RestoreSystemGamma();
	}
}


//==========================================================================


BOOL bSetupPixelFormat(HDC hDC)
{
	static PIXELFORMATDESCRIPTOR pfd = {
	sizeof(PIXELFORMATDESCRIPTOR),	// size of this pfd
	1,				// version number
	PFD_DRAW_TO_WINDOW 		// support window
	|  PFD_SUPPORT_OPENGL 	// support OpenGL
	|  PFD_DOUBLEBUFFER ,	// double buffered
	PFD_TYPE_RGBA,			// RGBA type
	24,				// 24-bit color depth
	0, 0, 0, 0, 0, 0,		// color bits ignored
	0,				// no alpha buffer
	0,				// shift bit ignored
	0,				// no accumulation buffer
	0, 0, 0, 0, 			// accum bits ignored
	32,				// 32-bit z-buffer
	0,				// no stencil buffer
	0,				// no auxiliary buffer
	PFD_MAIN_PLANE,			// main layer
	0,				// reserved
	0, 0, 0				// layer masks ignored
	};
	int pixelformat;

	if ( (pixelformat = ChoosePixelFormat(hDC, &pfd)) == 0 )
	{
		MessageBox(NULL, "ChoosePixelFormat failed", "Error", MB_OK);
		return false;
	}

	if (SetPixelFormat(hDC, pixelformat, &pfd) == false)
	{
		MessageBox(NULL, "SetPixelFormat failed", "Error", MB_OK);
		return false;
	}

	return true;
}



qbyte scantokey[128] =
{
//	0           1      2     3     4     5       6       7      8         9      A       B           C     D            E           F
	0          ,27    ,'1'  ,'2'  ,'3'  ,'4'    ,'5'    ,'6'   ,'7'      ,'8'   ,'9'    ,'0'        ,'-'  ,'='         ,K_BACKSPACE,9     , // 0
	'q'        ,'w'   ,'e'  ,'r'  ,'t'  ,'y'    ,'u'    ,'i'   ,'o'      ,'p'   ,'['    ,']'        ,13   ,K_CTRL      ,'a'        ,'s'   , // 1
	'd'        ,'f'   ,'g'  ,'h'  ,'j'  ,'k'    ,'l'    ,';'   ,'\''     ,'`'   ,K_SHIFT,'\\'       ,'z'  ,'x'         ,'c'        ,'v'   , // 2
	'b'        ,'n'   ,'m'  ,','  ,'.'  ,'/'    ,K_SHIFT,'*'   ,K_ALT    ,' '   ,0      ,K_F1       ,K_F2 ,K_F3        ,K_F4       ,K_F5  , // 3
	K_F6       ,K_F7  ,K_F8 ,K_F9 ,K_F10,K_PAUSE,0      ,K_HOME,K_UPARROW,K_PGUP,'-'    ,K_LEFTARROW,'5'  ,K_RIGHTARROW,'+'        ,K_END , // 4
	K_DOWNARROW,K_PGDN,K_INS,K_DEL,0    ,0      ,0      ,K_F11 ,K_F12    ,0     ,0      ,0          ,0    ,0           ,0          ,0     , // 5
	0          ,0     ,0    ,0    ,0    ,0      ,0      ,0     ,0        ,0     ,0      ,0          ,0    ,0           ,0          ,0     , // 6
	0          ,0     ,0    ,0    ,0    ,0      ,0      ,0     ,0        ,0     ,0      ,0          ,0    ,0           ,0          ,0       // 7
};


/*
=======
MapKey

Map from windows to quake keynums
=======
*/
int MapKey (int key, int virtualkey)
{
	key = (key>>16)&255;
	if (key > 127)
		return 0;
	if (scantokey[key] == 0)
		Con_DPrintf("key 0x%02x has no translation\n", key);
	return scantokey[key];
}

/*
===================================================================

MAIN WINDOW

===================================================================
*/

/*
================
ClearAllStates
================
*/
void ClearAllStates (void)
{
	int		i;

// send an up event for each key, to make sure the server clears them all
	for (i=0 ; i<256 ; i++)
	{
		Key_Event (i, false);
	}

	Key_ClearStates ();
	IN_ClearStates ();
}

void VID_RestoreGameGamma(void);
extern qboolean host_loopactive;

void AppActivate(BOOL fActive, BOOL minimize)
/****************************************************************************
*
* Function:     AppActivate
* Parameters:   fActive - True if app is activating
*
* Description:  If the application is activating, then swap the system
*               into SYSPAL_NOSTATIC mode so that our palettes will display
*               correctly.
*
****************************************************************************/
{
	static BOOL	sound_active;

	vid_activewindow = fActive;
	vid_hidden = minimize;

// enable/disable sound on focus gain/loss
	if (!vid_activewindow && sound_active)
	{
		S_BlockSound ();
		sound_active = false;
	}
	else if (vid_activewindow && !sound_active)
	{
		S_UnblockSound ();
		sound_active = true;
	}

	if (fActive)
	{
		if (modestate == MS_FULLDIB)
		{
			if (vid_canalttab && vid_wassuspended)
			{
				vid_wassuspended = false;
				ChangeDisplaySettings (&gdevmode, CDS_FULLSCREEN);
				ShowWindow(mainwindow, SW_SHOWNORMAL);
			}

			// LordHavoc: from dabb, fix for alt-tab bug in NVidia drivers
			MoveWindow(mainwindow,0,0,gdevmode.dmPelsWidth,gdevmode.dmPelsHeight,false);
		}
		if (host_loopactive)
			VID_RestoreGameGamma();
	}

	if (!fActive)
	{
		vid_usingmouse = false;
		IN_DeactivateMouse ();
		IN_ShowMouse ();
		if (modestate == MS_FULLDIB && vid_canalttab)
		{
			ChangeDisplaySettings (NULL, 0);
			vid_wassuspended = true;
		}
		VID_RestoreSystemGamma();
	}
}

LONG CDAudio_MessageHandler(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

/* main window procedure */
LONG WINAPI MainWndProc (HWND hWnd, UINT uMsg, WPARAM  wParam, LPARAM lParam)
{
	LONG    lRet = 1;
	int		fActive, fMinimized, temp;
	extern unsigned int uiWheelMessage;

	if ( uMsg == uiWheelMessage )
		uMsg = WM_MOUSEWHEEL;

	switch (uMsg)
	{
		case WM_KILLFOCUS:
			if (modestate == MS_FULLDIB)
				ShowWindow(mainwindow, SW_SHOWMINNOACTIVE);
			break;

		case WM_CREATE:
			break;

		case WM_MOVE:
			window_x = (int) LOWORD(lParam);
			window_y = (int) HIWORD(lParam);
			VID_UpdateWindowStatus ();
			break;

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
			Key_Event (MapKey(lParam, wParam), true);
			break;

		case WM_KEYUP:
		case WM_SYSKEYUP:
			Key_Event (MapKey(lParam, wParam), false);
			break;

		case WM_SYSCHAR:
		// keep Alt-Space from happening
			break;

	// this is complicated because Win32 seems to pack multiple mouse events into
	// one update sometimes, so we always check all states and look for events
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_MOUSEMOVE:
			temp = 0;

			if (wParam & MK_LBUTTON)
				temp |= 1;

			if (wParam & MK_RBUTTON)
				temp |= 2;

			if (wParam & MK_MBUTTON)
				temp |= 4;

			IN_MouseEvent (temp);

			break;

		// JACK: This is the mouse wheel with the Intellimouse
		// Its delta is either positive or neg, and we generate the proper
		// Event.
		case WM_MOUSEWHEEL:
			if ((short) HIWORD(wParam) > 0) {
				Key_Event(K_MWHEELUP, true);
				Key_Event(K_MWHEELUP, false);
			} else {
				Key_Event(K_MWHEELDOWN, true);
				Key_Event(K_MWHEELDOWN, false);
			}
			break;

		case WM_SIZE:
			break;

		case WM_CLOSE:
			if (MessageBox (mainwindow, "Are you sure you want to quit?", "Confirm Exit", MB_YESNO | MB_SETFOREGROUND | MB_ICONQUESTION) == IDYES)
				Sys_Quit ();

			break;

		case WM_ACTIVATE:
			fActive = LOWORD(wParam);
			fMinimized = (BOOL) HIWORD(wParam);
			AppActivate(!(fActive == WA_INACTIVE), fMinimized);

		// fix the leftover Alt from any Alt-Tab or the like that switched us away
			ClearAllStates ();

			break;

		case WM_DESTROY:
		{
			if (mainwindow)
				DestroyWindow (mainwindow);

			PostQuitMessage (0);
		}
		break;

		case MM_MCINOTIFY:
			lRet = CDAudio_MessageHandler (hWnd, uMsg, wParam, lParam);
			break;

		default:
			/* pass all unhandled messages to DefWindowProc */
			lRet = DefWindowProc (hWnd, uMsg, wParam, lParam);
		break;
	}

	/* return 1 if handled message, 0 if not */
	return lRet;
}


/*
=================
VID_NumModes
=================
*/
int VID_NumModes (void)
{
	return nummodes;
}


/*
=================
VID_GetModePtr
=================
*/
vmode_t *VID_GetModePtr (int modenum)
{

	if ((modenum >= 0) && (modenum < nummodes))
		return &modelist[modenum];
	else
		return &badmode;
}


/*
=================
VID_GetModeDescription
=================
*/
char *VID_GetModeDescription (int mode)
{
	char		*pinfo;
	vmode_t		*pv;
	static char	temp[100];

	if ((mode < 0) || (mode >= nummodes))
		return NULL;

	if (!leavecurrentmode)
	{
		pv = VID_GetModePtr (mode);
		pinfo = pv->modedesc;
	}
	else
	{
		sprintf (temp, "Desktop resolution (%dx%d)", modelist[MODE_FULLSCREEN_DEFAULT].width, modelist[MODE_FULLSCREEN_DEFAULT].height);
		pinfo = temp;
	}

	return pinfo;
}


// KJB: Added this to return the mode driver name in description for console

char *VID_GetExtModeDescription (int mode)
{
	static char	pinfo[40];
	vmode_t		*pv;

	if ((mode < 0) || (mode >= nummodes))
		return NULL;

	pv = VID_GetModePtr (mode);
	if (modelist[mode].type == MS_FULLDIB)
	{
		if (!leavecurrentmode)
			sprintf(pinfo,"%s fullscreen", pv->modedesc);
		else
			sprintf (pinfo, "Desktop resolution (%dx%d)", modelist[MODE_FULLSCREEN_DEFAULT].width, modelist[MODE_FULLSCREEN_DEFAULT].height);
	}
	else
	{
		if (modestate == MS_WINDOWED)
			sprintf(pinfo, "%s windowed", pv->modedesc);
		else
			sprintf(pinfo, "windowed");
	}

	return pinfo;
}


/*
=================
VID_DescribeCurrentMode_f
=================
*/
void VID_DescribeCurrentMode_f (void)
{
	Con_Printf ("%s\n", VID_GetExtModeDescription (vid_modenum));
}


/*
=================
VID_NumModes_f
=================
*/
void VID_NumModes_f (void)
{
	if (nummodes == 1)
		Con_Printf ("%d video mode is available\n", nummodes);
	else
		Con_Printf ("%d video modes are available\n", nummodes);
}


/*
=================
VID_DescribeMode_f
=================
*/
void VID_DescribeMode_f (void)
{
	int		t, modenum;

	modenum = atoi (Cmd_Argv(1));

	t = leavecurrentmode;
	leavecurrentmode = 0;

	Con_Printf ("%s\n", VID_GetExtModeDescription (modenum));

	leavecurrentmode = t;
}


/*
=================
VID_DescribeModes_f
=================
*/
void VID_DescribeModes_f (void)
{
	int			i, lnummodes, t;
	char		*pinfo;
	vmode_t		*pv;

	lnummodes = VID_NumModes ();

	t = leavecurrentmode;
	leavecurrentmode = 0;

	for (i=1 ; i<lnummodes ; i++)
	{
		pv = VID_GetModePtr (i);
		pinfo = VID_GetExtModeDescription (i);
		Con_Printf ("%2d: %s\n", i, pinfo);
	}

	leavecurrentmode = t;
}

void VID_AddMode(int type, int width, int height, int modenum, int dib, int fullscreen, int bpp)
{
	int i;
	if (nummodes >= MAX_MODE_LIST)
		return;
	modelist[nummodes].type = type;
	modelist[nummodes].width = width;
	modelist[nummodes].height = height;
	modelist[nummodes].modenum = modenum;
	modelist[nummodes].dib = dib;
	modelist[nummodes].fullscreen = fullscreen;
	modelist[nummodes].bpp = bpp;
	if (bpp == 0)
		sprintf (modelist[nummodes].modedesc, "%dx%d", width, height);
	else
		sprintf (modelist[nummodes].modedesc, "%dx%dx%d", width, height, bpp);
	for (i = 0;i < nummodes;i++)
	{
		if (!memcmp(&modelist[i], &modelist[nummodes], sizeof(vmode_t)))
			return;
	}
	nummodes++;
}

void VID_InitDIB (HINSTANCE hInstance)
{
	int w, h;
	WNDCLASS		wc;

	// Register the frame class
	wc.style         = 0;
	wc.lpfnWndProc   = (WNDPROC)MainWndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = hInstance;
	wc.hIcon         = 0;
	wc.hCursor       = LoadCursor (NULL,IDC_ARROW);
	wc.hbrBackground = NULL;
	wc.lpszMenuName  = 0;
	wc.lpszClassName = gamename;

	if (!RegisterClass (&wc) )
		Sys_Error ("Couldn't register window class");

	if (COM_CheckParm("-width"))
		w = atoi(com_argv[COM_CheckParm("-width")+1]);
	else
		w = 640;

	if (w < 320)
		w = 320;

	if (COM_CheckParm("-height"))
		h = atoi(com_argv[COM_CheckParm("-height")+1]);
	else
		h = w * 240/320;

	if (h < 240)
		h = 240;

	VID_AddMode(MS_WINDOWED, w, h, 0, 1, 0, 0);
}


/*
=================
VID_InitFullDIB
=================
*/
void VID_InitFullDIB (HINSTANCE hInstance)
{
	DEVMODE	devmode;
	int		modenum;
	int		originalnummodes;
	int		numlowresmodes;
	int		j;
	int		bpp;
	int		done;
	BOOL	stat;

// enumerate >8 bpp modes
	originalnummodes = nummodes;
	modenum = 0;

	do
	{
		stat = EnumDisplaySettings (NULL, modenum, &devmode);

		if ((devmode.dmBitsPerPel >= 15) && (devmode.dmPelsWidth <= MAXWIDTH) && (devmode.dmPelsHeight <= MAXHEIGHT) && (nummodes < MAX_MODE_LIST))
		{
			devmode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;

			if (ChangeDisplaySettings (&devmode, CDS_TEST | CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL)
				VID_AddMode(MS_FULLDIB, devmode.dmPelsWidth, devmode.dmPelsHeight, 0, 1, 1, devmode.dmBitsPerPel);
		}

		modenum++;
	}
	while (stat);

// see if there are any low-res modes that aren't being reported
	numlowresmodes = sizeof(lowresmodes) / sizeof(lowresmodes[0]);
	bpp = 16;
	done = 0;

	do
	{
		for (j=0 ; (j<numlowresmodes) && (nummodes < MAX_MODE_LIST) ; j++)
		{
			devmode.dmBitsPerPel = bpp;
			devmode.dmPelsWidth = lowresmodes[j].width;
			devmode.dmPelsHeight = lowresmodes[j].height;
			devmode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;

			if (ChangeDisplaySettings (&devmode, CDS_TEST | CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL)
				VID_AddMode(MS_FULLDIB, devmode.dmPelsWidth, devmode.dmPelsHeight, 0, 1, 1, devmode.dmBitsPerPel);
		}
		switch (bpp)
		{
			case 16:
				bpp = 32;
				break;

			case 32:
				bpp = 24;
				break;

			case 24:
				done = 1;
				break;
		}
	}
	while (!done);

	if (nummodes == originalnummodes)
		Con_SafePrintf ("No fullscreen DIB modes found\n");
}

//static int grabsysgamma = true;
WORD systemgammaramps[3][256], currentgammaramps[3][256];

int VID_SetGamma(float prescale, float gamma, float scale, float base)
{
	int i;
	HDC hdc;
	hdc = GetDC (NULL);

	BuildGammaTable16(prescale, gamma, scale, base, &currentgammaramps[0][0]);
	for (i = 0;i < 256;i++)
		currentgammaramps[1][i] = currentgammaramps[2][i] = currentgammaramps[0][i];

	i = SetDeviceGammaRamp(hdc, &currentgammaramps[0][0]);

	ReleaseDC (NULL, hdc);
	return i; // return success or failure
}

void VID_RestoreGameGamma(void)
{
	VID_UpdateGamma(true);
}

void VID_GetSystemGamma(void)
{
	HDC hdc;
	hdc = GetDC (NULL);

	GetDeviceGammaRamp(hdc, &systemgammaramps[0][0]);

	ReleaseDC (NULL, hdc);
}

void VID_RestoreSystemGamma(void)
{
	HDC hdc;
	hdc = GetDC (NULL);

	SetDeviceGammaRamp(hdc, &systemgammaramps[0][0]);

	ReleaseDC (NULL, hdc);
}

//========================================================
// Video menu stuff
//========================================================

extern void M_Menu_Options_f (void);
extern void M_Print (float cx, float cy, char *str);
extern void M_PrintWhite (float cx, float cy, char *str);
extern void M_DrawCharacter (float cx, float cy, int num);
extern void M_DrawPic (float cx, float cy, char *picname);

static int vid_wmodes;

typedef struct
{
	int modenum;
	char *desc;
	int iscur;
} modedesc_t;

#define MAX_COLUMN_SIZE		9
#define MODE_AREA_HEIGHT	(MAX_COLUMN_SIZE + 2)
#define MAX_MODEDESCS		(MAX_COLUMN_SIZE*3)

static modedesc_t modedescs[MAX_MODEDESCS];

/*
================
VID_MenuDraw
================
*/
void VID_MenuDraw (void)
{
	cachepic_t *p;
	char *ptr;
	int lnummodes, i, k, column, row;
	vmode_t *pv;

	p = Draw_CachePic ("gfx/vidmodes.lmp");
	M_DrawPic ( (320-p->width)/2, 4, "gfx/vidmodes.lmp");

	vid_wmodes = 0;
	lnummodes = VID_NumModes ();

	for (i=1 ; (i<lnummodes) && (vid_wmodes < MAX_MODEDESCS) ; i++)
	{
		ptr = VID_GetModeDescription (i);
		pv = VID_GetModePtr (i);

		k = vid_wmodes;

		modedescs[k].modenum = i;
		modedescs[k].desc = ptr;
		modedescs[k].iscur = 0;

		if (i == vid_modenum)
			modedescs[k].iscur = 1;

		vid_wmodes++;

	}

	if (vid_wmodes > 0)
	{
		M_Print (2*8, 36+0*8, "Fullscreen Modes (WIDTHxHEIGHTxBPP)");

		column = 8;
		row = 36+2*8;

		for (i=0 ; i<vid_wmodes ; i++)
		{
			if (modedescs[i].iscur)
				M_PrintWhite (column, row, modedescs[i].desc);
			else
				M_Print (column, row, modedescs[i].desc);

			column += 13*8;

			if ((i % VID_ROW_SIZE) == (VID_ROW_SIZE - 1))
			{
				column = 8;
				row += 8;
			}
		}
	}

	M_Print (3*8, 36 + MODE_AREA_HEIGHT * 8 + 8*2, "Video modes must be set from the");
	M_Print (3*8, 36 + MODE_AREA_HEIGHT * 8 + 8*3, "command line with -width <width>");
	M_Print (3*8, 36 + MODE_AREA_HEIGHT * 8 + 8*4, "and -bpp <bits-per-pixel>");
	M_Print (3*8, 36 + MODE_AREA_HEIGHT * 8 + 8*6, "Select windowed mode with -window");
}


/*
================
VID_MenuKey
================
*/
void VID_MenuKey (int key)
{
	switch (key)
	{
	case K_ESCAPE:
		S_LocalSound ("misc/menu1.wav");
		M_Menu_Options_f ();
		break;

	default:
		break;
	}
}

static HINSTANCE gldll;

int GL_OpenLibrary(const char *name)
{
	Con_Printf("Loading GL driver %s\n", name);
	GL_CloseLibrary();
	if (!(gldll = LoadLibrary(name)))
	{
		Con_Printf("Unable to LoadLibrary %s\n", name);
		return false;
	}
	strcpy(gl_driver, name);
	return true;
}

void GL_CloseLibrary(void)
{
	FreeLibrary(gldll);
	gldll = 0;
	gl_driver[0] = 0;
	qwglGetProcAddress = NULL;
	gl_extensions = "";
	gl_platform = "";
	gl_platformextensions = "";
}

void *GL_GetProcAddress(const char *name)
{
	void *p = NULL;
	if (qwglGetProcAddress != NULL)
		p = (void *) qwglGetProcAddress(name);
	if (p == NULL)
		p = (void *) GetProcAddress(gldll, name);
	return p;
}
/*
===================
VID_Init
===================
*/
void VID_Init (int fullscreen, int width, int height, int bpp)
{
	int i, bestmode;
	double rating, bestrating;
	int basenummodes, done;
	HDC hdc;
	DEVMODE devmode;

	if (!GL_OpenLibrary("opengl32.dll"))
		Sys_Error("Unable to load GL driver\n");

	memset(&devmode, 0, sizeof(devmode));

	Cmd_AddCommand ("vid_nummodes", VID_NumModes_f);
	Cmd_AddCommand ("vid_describecurrentmode", VID_DescribeCurrentMode_f);
	Cmd_AddCommand ("vid_describemode", VID_DescribeMode_f);
	Cmd_AddCommand ("vid_describemodes", VID_DescribeModes_f);

	VID_GetSystemGamma();

	hIcon = LoadIcon (global_hInstance, MAKEINTRESOURCE (IDI_ICON2));

	InitCommonControls();

	VID_InitDIB (global_hInstance);
	basenummodes = nummodes = 1;

	VID_InitFullDIB (global_hInstance);

	if (!fullscreen)
	{
		hdc = GetDC (NULL);

		if (GetDeviceCaps(hdc, RASTERCAPS) & RC_PALETTE)
			Sys_Error ("Can't run in non-RGB mode");

		ReleaseDC (NULL, hdc);

		windowed = true;

		vid_default = MODE_WINDOWED;
	}
	else
	{
		if (nummodes == 1)
			Sys_Error ("No RGB fullscreen modes available");

		windowed = false;

		done = 0;

		bestmode = -1;
		bestrating = 1000000000;
		for (i = 0;i < nummodes;i++)
		{
			if (fullscreen == modelist[i].fullscreen)
			{
				rating = VID_CompareMode(width, height, bpp, modelist[i].width, modelist[i].height, modelist[i].bpp);
				if (bestrating > rating)
				{
					bestrating = rating;
					bestmode = i;
				}
			}
		}

		if (bestmode < 0)
			Sys_Error ("Specified video mode not available");
	}

	vid_initialized = true;

	VID_SetMode (vid_default);

	maindc = GetDC(mainwindow);
	bSetupPixelFormat(maindc);

	if (!GL_CheckExtension("wgl", wglfuncs, NULL, false))
		Sys_Error("wgl functions not found\n");

	baseRC = qwglCreateContext( maindc );
	if (!baseRC)
		Sys_Error ("Could not initialize GL (wglCreateContext failed).\n\nMake sure you are in 65536 color mode, and try running -window.");
	if (!qwglMakeCurrent( maindc, baseRC ))
		Sys_Error ("wglMakeCurrent failed");

	gl_renderer = qglGetString(GL_RENDERER);
	gl_vendor = qglGetString(GL_VENDOR);
	gl_version = qglGetString(GL_VERSION);
	gl_extensions = qglGetString(GL_EXTENSIONS);
	gl_platform = "WGL";
	gl_platformextensions = "";

	if (GL_CheckExtension("WGL_ARB_extensions_string", getextensionsstringfuncs, NULL, false))
		gl_platformextensions = qwglGetExtensionsStringARB(maindc);

	gl_videosyncavailable = GL_CheckExtension("WGL_EXT_swap_control", wglswapintervalfuncs, NULL, false);

	GL_Init ();

	// LordHavoc: special differences for ATI (broken 8bit color when also using 32bit? weird!)
	if (strncasecmp(gl_vendor,"ATI",3)==0)
	{
		if (strncasecmp(gl_renderer,"Rage Pro",8)==0)
			isRagePro = true;
	}
	if (strncasecmp(gl_renderer,"Matrox G200 Direct3D",20)==0) // a D3D driver for GL? sigh...
		isG200 = true;

	vid_realmode = vid_modenum;

	vid_menudrawfn = VID_MenuDraw;
	vid_menukeyfn = VID_MenuKey;

	strcpy (badmode.modedesc, "Bad mode");
	vid_canalttab = true;

	vid_hidden = false;
}

