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
// snd_mem.c: sound caching

#include "quakedef.h"

#include "ogg.h"


/*
================
ResampleSfx
================
*/
void ResampleSfx (sfxcache_t *sc, qbyte *data, char *name)
{
	int i, outcount, srcsample, srclength, samplefrac, fracstep;

	// this is usually 0.5 (128), 1 (256), or 2 (512)
	fracstep = ((double) sc->speed / (double) shm->speed) * 256.0;

	srclength = sc->length << sc->stereo;

	outcount = (double) sc->length * (double) shm->speed / (double) sc->speed;
	Con_DPrintf("ResampleSfx: resampling sound %s from %dhz to %dhz (%d samples to %d samples)\n", name, sc->speed, shm->speed, sc->length, outcount);
	sc->length = outcount;
	if (sc->loopstart != -1)
		sc->loopstart = (double) sc->loopstart * (double) shm->speed / (double) sc->speed;

	sc->speed = shm->speed;

// resample / decimate to the current source rate

	if (fracstep == 256)
	{
		// fast case for direct transfer
		if (sc->width == 1) // 8bit
			for (i = 0;i < srclength;i++)
				((signed char *)sc->data)[i] = ((unsigned char *)data)[i] - 128;
		else //if (sc->width == 2) // 16bit
			for (i = 0;i < srclength;i++)
				((short *)sc->data)[i] = LittleShort (((short *)data)[i]);
	}
	else
	{
		// general case
		samplefrac = 0;
		if ((fracstep & 255) == 0) // skipping points on perfect multiple
		{
			srcsample = 0;
			fracstep >>= 8;
			if (sc->width == 2)
			{
				short *out = (void *)sc->data, *in = (void *)data;
				if (sc->stereo) // LordHavoc: stereo sound support
				{
					fracstep <<= 1;
					for (i=0 ; i<outcount ; i++)
					{
						*out++ = LittleShort (in[srcsample  ]);
						*out++ = LittleShort (in[srcsample+1]);
						srcsample += fracstep;
					}
				}
				else
				{
					for (i=0 ; i<outcount ; i++)
					{
						*out++ = LittleShort (in[srcsample  ]);
						srcsample += fracstep;
					}
				}
			}
			else
			{
				signed char *out = (void *)sc->data;
				unsigned char *in = (void *)data;
				if (sc->stereo) // LordHavoc: stereo sound support
				{
					fracstep <<= 1;
					for (i=0 ; i<outcount ; i++)
					{
						*out++ = in[srcsample  ] - 128;
						*out++ = in[srcsample+1] - 128;
						srcsample += fracstep;
					}
				}
				else
				{
					for (i=0 ; i<outcount ; i++)
					{
						*out++ = in[srcsample  ] - 128;
						srcsample += fracstep;
					}
				}
			}
		}
		else
		{
			int sample;
			int a, b;
			if (sc->width == 2)
			{
				short *out = (void *)sc->data, *in = (void *)data;
				if (sc->stereo) // LordHavoc: stereo sound support
				{
					for (i=0 ; i<outcount ; i++)
					{
						srcsample = (samplefrac >> 8) << 1;
						a = in[srcsample  ];
						if (srcsample+2 >= srclength)
							b = 0;
						else
							b = in[srcsample+2];
						sample = (((b - a) * (samplefrac & 255)) >> 8) + a;
						*out++ = (short) sample;
						a = in[srcsample+1];
						if (srcsample+2 >= srclength)
							b = 0;
						else
							b = in[srcsample+3];
						sample = (((b - a) * (samplefrac & 255)) >> 8) + a;
						*out++ = (short) sample;
						samplefrac += fracstep;
					}
				}
				else
				{
					for (i=0 ; i<outcount ; i++)
					{
						srcsample = samplefrac >> 8;
						a = in[srcsample  ];
						if (srcsample+1 >= srclength)
							b = 0;
						else
							b = in[srcsample+1];
						sample = (((b - a) * (samplefrac & 255)) >> 8) + a;
						*out++ = (short) sample;
						samplefrac += fracstep;
					}
				}
			}
			else
			{
				signed char *out = (void *)sc->data;
				unsigned char *in = (void *)data;
				if (sc->stereo) // LordHavoc: stereo sound support
				{
					for (i=0 ; i<outcount ; i++)
					{
						srcsample = (samplefrac >> 8) << 1;
						a = (int) in[srcsample  ] - 128;
						if (srcsample+2 >= srclength)
							b = 0;
						else
							b = (int) in[srcsample+2] - 128;
						sample = (((b - a) * (samplefrac & 255)) >> 8) + a;
						*out++ = (signed char) sample;
						a = (int) in[srcsample+1] - 128;
						if (srcsample+2 >= srclength)
							b = 0;
						else
							b = (int) in[srcsample+3] - 128;
						sample = (((b - a) * (samplefrac & 255)) >> 8) + a;
						*out++ = (signed char) sample;
						samplefrac += fracstep;
					}
				}
				else
				{
					for (i=0 ; i<outcount ; i++)
					{
						srcsample = samplefrac >> 8;
						a = (int) in[srcsample  ] - 128;
						if (srcsample+1 >= srclength)
							b = 0;
						else
							b = (int) in[srcsample+1] - 128;
						sample = (((b - a) * (samplefrac & 255)) >> 8) + a;
						*out++ = (signed char) sample;
						samplefrac += fracstep;
					}
				}
			}
		}
	}

	// LordHavoc: use this for testing if it ever becomes useful again
	//COM_WriteFile (va("sound/%s.pcm", name), sc->data, (sc->length << sc->stereo) * sc->width);
}

//=============================================================================

/*
==============
S_LoadWavFile
==============
*/
sfxcache_t *S_LoadWavFile (const char *filename, sfx_t *s)
{
	qbyte *data;
	wavinfo_t info;
	int len;
	sfxcache_t *sc;

	// Load the file
	data = FS_LoadFile(filename, false);
	if (!data)
		return NULL;

	// Don't try to load it if it's not a WAV file
	if (memcmp (data, "RIFF", 4) || memcmp (data + 8, "WAVE", 4))
		return NULL;

	info = GetWavinfo (s->name, data, fs_filesize);
	// Stereo sounds are allowed (intended for music)
	if (info.channels < 1 || info.channels > 2)
	{
		Con_Printf("%s has an unsupported number of channels (%i)\n",s->name, info.channels);
		Mem_Free(data);
		return NULL;
	}

	// calculate resampled length
	len = (int) ((double) info.samples * (double) shm->speed / (double) info.rate);
	len = len * info.width * info.channels;

	Mem_FreePool(&s->mempool);
	s->mempool = Mem_AllocPool(s->name);
	sc = s->sfxcache = Mem_Alloc(s->mempool, len + sizeof(sfxcache_t));
	if (!sc)
	{
		Con_Printf("failed to allocate memory for sound \"%s\"\n", s->name);
		Mem_FreePool(&s->mempool);
		Mem_Free(data);
		return NULL;
	}

	sc->length = info.samples;
	sc->loopstart = info.loopstart;
	sc->speed = info.rate;
	sc->width = info.width;
	sc->stereo = info.channels == 2;

	ResampleSfx(sc, data + info.dataofs, s->name);

	Mem_Free(data);
	return sc;
}


/*
==============
S_LoadSound
==============
*/
sfxcache_t *S_LoadSound (sfx_t *s, int complain)
{
	char namebuffer[MAX_QPATH];
	size_t len;
	sfxcache_t *sc;
	qboolean modified_name = false;

	// see if still in memory
	if (!shm || !shm->speed)
		return NULL;
	if (s->sfxcache && (s->sfxcache->speed == shm->speed))
		return s->sfxcache;

	len = snprintf (namebuffer, sizeof (namebuffer), "sound/%s", s->name);
	if (len >= sizeof (namebuffer))
		return NULL;

	// Try to load it as a WAV file
	sc = S_LoadWavFile (namebuffer, s);
	if (sc != NULL)
		return sc;

	// Else, try to load it as an Ogg Vorbis file
	if (!strcasecmp (namebuffer + len - 4, ".wav"))
	{
		strcpy (namebuffer + len - 3, "ogg");
		modified_name = true;
	}
	sc = OGG_LoadVorbisFile (namebuffer, s);
	if (sc != NULL)
		return sc;

	// Can't load the sound!
	if (!complain)
		s->flags |= SFXFLAG_SILENTLYMISSING;
	else
		s->flags &= ~SFXFLAG_SILENTLYMISSING;
	if (complain)
	{
		if (modified_name)
			strcpy (namebuffer + len - 3, "wav");
		Con_Printf("Couldn't load %s\n", namebuffer);
	}
	return NULL;
}

void S_UnloadSound(sfx_t *s)
{
	if (s->sfxcache)
	{
		s->sfxcache = NULL;
		Mem_FreePool(&s->mempool);
	}
}


/*
===============================================================================

WAV loading

===============================================================================
*/


static qbyte *data_p;
static qbyte *iff_end;
static qbyte *last_chunk;
static qbyte *iff_data;
static int iff_chunk_len;


short GetLittleShort(void)
{
	short val;

	val = BuffLittleShort (data_p);
	data_p += 2;

	return val;
}

int GetLittleLong(void)
{
	int val = 0;

	val = BuffLittleLong (data_p);
	data_p += 4;

	return val;
}

void FindNextChunk(char *name)
{
	while (1)
	{
		data_p=last_chunk;

		if (data_p >= iff_end)
		{	// didn't find the chunk
			data_p = NULL;
			return;
		}

		data_p += 4;
		iff_chunk_len = GetLittleLong();
		if (iff_chunk_len < 0)
		{
			data_p = NULL;
			return;
		}
		data_p -= 8;
		last_chunk = data_p + 8 + ( (iff_chunk_len + 1) & ~1 );
		if (!strncmp(data_p, name, 4))
			return;
	}
}

void FindChunk(char *name)
{
	last_chunk = iff_data;
	FindNextChunk (name);
}


void DumpChunks(void)
{
	char str[5];

	str[4] = 0;
	data_p=iff_data;
	do
	{
		memcpy (str, data_p, 4);
		data_p += 4;
		iff_chunk_len = GetLittleLong();
		Con_Printf("0x%x : %s (%d)\n", (int)(data_p - 4), str, iff_chunk_len);
		data_p += (iff_chunk_len + 1) & ~1;
	} while (data_p < iff_end);
}

/*
============
GetWavinfo
============
*/
wavinfo_t GetWavinfo (char *name, qbyte *wav, int wavlength)
{
	wavinfo_t info;
	int i;
	int format;
	int samples;

	memset (&info, 0, sizeof(info));

	if (!wav)
		return info;

	iff_data = wav;
	iff_end = wav + wavlength;

	// find "RIFF" chunk
	FindChunk("RIFF");
	if (!(data_p && !strncmp(data_p+8, "WAVE", 4)))
	{
		Con_Print("Missing RIFF/WAVE chunks\n");
		return info;
	}

	// get "fmt " chunk
	iff_data = data_p + 12;
	//DumpChunks ();

	FindChunk("fmt ");
	if (!data_p)
	{
		Con_Print("Missing fmt chunk\n");
		return info;
	}
	data_p += 8;
	format = GetLittleShort();
	if (format != 1)
	{
		Con_Print("Microsoft PCM format only\n");
		return info;
	}

	info.channels = GetLittleShort();
	info.rate = GetLittleLong();
	data_p += 4+2;
	info.width = GetLittleShort() / 8;

	// get cue chunk
	FindChunk("cue ");
	if (data_p)
	{
		data_p += 32;
		info.loopstart = GetLittleLong();

		// if the next chunk is a LIST chunk, look for a cue length marker
		FindNextChunk ("LIST");
		if (data_p)
		{
			if (!strncmp (data_p + 28, "mark", 4))
			{	// this is not a proper parse, but it works with cooledit...
				data_p += 24;
				i = GetLittleLong ();	// samples in loop
				info.samples = info.loopstart + i;
			}
		}
	}
	else
		info.loopstart = -1;

	// find data chunk
	FindChunk("data");
	if (!data_p)
	{
		Con_Print("Missing data chunk\n");
		return info;
	}

	data_p += 4;
	samples = GetLittleLong () / info.width / info.channels;

	if (info.samples)
	{
		if (samples < info.samples)
			Host_Error ("Sound %s has a bad loop length", name);
	}
	else
		info.samples = samples;

	info.dataofs = data_p - wav;

	return info;
}

