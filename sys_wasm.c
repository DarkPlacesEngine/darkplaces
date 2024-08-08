/*
 * Include this BEFORE darkplaces.h because it breaks wrapping
 * _Static_assert. Cloudwalk has no idea how or why so don't ask.
 */
#include <SDL.h>

#include "darkplaces.h"
#include "fs.h"
#include "vid.h"

#include <emscripten.h>
#include <emscripten/html5.h>
#include <string.h>


EM_JS(float, em_GetViewportWidth, (void), {
	return document.documentElement.clientWidth
});
EM_JS(float, em_GetViewportHeight, (void), {
	return document.documentElement.clientHeight.toString
});
EM_BOOL on_resize(int etype, const EmscriptenUiEvent *event, void *UData)
{
	if(vid_resizable.integer)
	{
		Cvar_SetValueQuick(&vid_width, em_GetViewportWidth());
		Cvar_SetValueQuick(&vid_height, em_GetViewportHeight());
		Cvar_SetQuick(&vid_fullscreen, "0");
	}
	return EM_FALSE;
}


// =======================================================================
// General routines
// =======================================================================

EM_JS(char *, listfiles, (const char *directory), {
	if(UTF8ToString(directory) == "")
	{
		console.log("listing cwd");
		return stringToNewUTF8(FS.readdir(FS.cwd()).toString());
	}

	try
	{
		return stringToNewUTF8(FS.readdir(UTF8ToString(directory)).toString());
	}
	catch (error)
	{
		return stringToNewUTF8("directory not found");
	}
});
void listfiles_f(cmd_state_t *cmd)
{
	char *output = listfiles(Cmd_Argc(cmd) == 2 ? Cmd_Argv(cmd, 1) : "");

	Con_Printf("%s\n", output);
	free(output);
}

EM_JS(bool, syncFS, (bool populate), {
	FS.syncfs(populate, function(err) {
		if(err)
		{
			alert("FileSystem Save Error: " + err);
			return false;
		}

		alert("Filesystem Saved!");
		return true;
	});
});
void savefs_f(cmd_state_t *cmd)
{
	Con_Printf("Saving Files\n");
	syncFS(false);
}

EM_JS(char *, upload, (const char *todirectory), {
	if (UTF8ToString(todirectory).slice(-1) != "/")
	{
		currentname = UTF8ToString(todirectory) + "/";
	}
	else
	{
		currentname = UTF8ToString(todirectory);
	}

	file_selector.click();
	return stringToNewUTF8("Upload started");
});
void upload_f(cmd_state_t *cmd)
{
	char *output = upload(Cmd_Argc(cmd) == 2 ? Cmd_Argv(cmd, 1) : fs_basedir);

	Con_Printf("%s\n", output);
	free(output);
}

EM_JS(char *, rm, (const char *path), {
	const mode = FS.lookupPath(UTF8ToString(path)).node.mode;

	if (FS.isFile(mode))
	{
		FS.unlink(UTF8ToString(path));
		return stringToNewUTF8("File removed");
	}

	return stringToNewUTF8(UTF8ToString(path)+" is not a File.");
});
void rm_f(cmd_state_t *cmd)
{
	if (Cmd_Argc(cmd) != 2)
		Con_Printf("No file to remove\n");
	else
	{
		char *output = rm(Cmd_Argv(cmd, 1));
		Con_Printf("%s\n", output);
		free(output);
	}
}

EM_JS(char *, rmdir, (const char *path), {
	const mode = FS.lookupPath(UTF8ToString(path)).node.mode;
	if (FS.isDir(mode))
	{
		try
		{
			FS.rmdir(UTF8ToString(path));
		}
		catch (error)
		{
			return stringToNewUTF8("Unable to remove directory. Is it not empty?");
		}
		return stringToNewUTF8("Directory removed");
	}

	return stringToNewUTF8(UTF8ToString(path)+" is not a directory.");
});
void rmdir_f(cmd_state_t *cmd)
{
	if (Cmd_Argc(cmd) != 2)
		Con_Printf("No directory to remove\n");
	else
	{
		char *output = rmdir(Cmd_Argv(cmd, 1));
		Con_Printf("%s\n", output);
		free(output);
	}
}

EM_JS(char *, mkd, (const char *path), {
	try
	{
		FS.mkdir(UTF8ToString(path));
	}
	catch (error)
	{
		return stringToNewUTF8("Unable to create directory. Does it already exist?");
	}
	return stringToNewUTF8(UTF8ToString(path)+" directory was created.");
});
void mkdir_f(cmd_state_t *cmd)
{
	if (Cmd_Argc(cmd) != 2)
		Con_Printf("No directory to create\n");
	else
	{
		char *output = mkd(Cmd_Argv(cmd, 1));
		Con_Printf("%s\n", output);
		free(output);
	}
}

EM_JS(char *, move, (const char *oldpath, const char *newpath), {
	try
	{
		FS.rename(UTF8ToString(oldpath),UTF8ToString(newpath))
	}
	catch (error)
	{
		return stringToNewUTF8("unable to move.");
	}
	return stringToNewUTF8("File Moved");
});
void mv_f(cmd_state_t *cmd)
{
	if (Cmd_Argc(cmd) != 3)
		Con_Printf("Nothing to move\n");
	else
	{
		char *output = move(Cmd_Argv(cmd,1), Cmd_Argv(cmd,2));
		Con_Printf("%s\n", output);
		free(output);
	}
}

void wss_f(cmd_state_t *cmd)
{
	if (Cmd_Argc(cmd) != 3)
		Con_Printf("Not Enough Arguments (Expected URL and subprotocol)\n");
	else
	{
		if(strcmp(Cmd_Argv(cmd,2),"binary") == 0 || strcmp(Cmd_Argv(cmd,2),"text") == 0)
			Con_Printf("Set Websocket URL to %s and subprotocol to %s.\n", Cmd_Argv(cmd,1), Cmd_Argv(cmd,2));
		else
			Con_Printf("subprotocol must be either binary or text\n");
	}
}

void Sys_SDL_Shutdown(void)
{
	syncFS(false);
	SDL_Quit();
}

// Sys_Abort early in startup might screw with automated
// workflows or something if we show the dialog by default.
static qbool nocrashdialog = true;
void Sys_SDL_Dialog(const char *title, const char *string)
{
	if(!nocrashdialog)
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, string, NULL);
}

EM_JS(char *, getclipboard, (void), {
	//Thank you again, stack overflow
	return stringToNewUTF8(navigator.clipboard.readText());
});
char *Sys_SDL_GetClipboardData (void)
{
	char *data = NULL;
	char *cliptext;

	cliptext = getclipboard();
	if (cliptext != NULL) {
		size_t allocsize;
		allocsize = min(MAX_INPUTLINE, strlen(cliptext) + 1);
		data = (char *)Z_Malloc (allocsize);
		dp_strlcpy (data, cliptext, allocsize);
		free(cliptext);
	}

	return data;
}

void Sys_SDL_Init(void)
{
	if (SDL_Init(0) < 0)
		Sys_Error("SDL_Init failed: %s\n", SDL_GetError());

	// we don't know which systems we'll want to init, yet...
	// COMMANDLINEOPTION: sdl: -nocrashdialog disables "Engine Error" crash dialog boxes
	if(!Sys_CheckParm("-nocrashdialog"))
		nocrashdialog = false;
	
	emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW,0,EM_FALSE,on_resize);
}

void Sys_Register_Commands(void)
{
#ifdef WASM_USER_ADJUSTABLE
	Cmd_AddCommand(CF_SHARED, "em_ls", listfiles_f, "Lists Files in specified directory defaulting to the current working directory (Emscripten Only)");
	Cmd_AddCommand(CF_SHARED, "em_upload", upload_f, "Upload file to specified directory defaulting to basedir (Emscripten Only)");
	Cmd_AddCommand(CF_SHARED, "em_rm", rm_f, "Remove a file from game Filesystem (Emscripten Only)");
	Cmd_AddCommand(CF_SHARED, "em_rmdir", rmdir_f, "Remove a directory from game Filesystem (Emscripten Only)");
	Cmd_AddCommand(CF_SHARED, "em_mkdir", mkdir_f, "Make a directory in game Filesystem (Emscripten Only)");
	Cmd_AddCommand(CF_SHARED, "em_mv", mv_f, "Rename or Move an item in game Filesystem (Emscripten only)");
	Cmd_AddCommand(CF_SHARED, "em_wss", wss_f, "Set Websocket URL and Protocol (Emscripten Only)");
#endif
	Cmd_AddCommand(CF_SHARED, "em_save", savefs_f, "Save file changes to browser (Emscripten Only)");
}

qbool sys_supportsdlgetticks = true;
unsigned int Sys_SDL_GetTicks(void)
{
	return SDL_GetTicks();
}

void Sys_SDL_Delay(unsigned int milliseconds)
{
	SDL_Delay(milliseconds);
}

int main(int argc, char *argv[])
{
	return Sys_Main(argc, argv);
}
