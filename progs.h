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

#include "pr_comp.h"			// defs shared with qcc
#include "progdefs.h"			// generated by program cdefs

typedef union eval_s
{
	string_t		string;
	float			_float;
	float			vector[3];
	func_t			function;
	int				_int;
	int				edict;
} eval_t;

typedef struct link_s
{
	struct link_s	*prev, *next;
} link_t;

// LordHavoc: increased number of leafs per entity limit from 16 to 256
#define	MAX_ENT_LEAFS	256
typedef struct edict_s
{
	qboolean	free;
	link_t		area;

#ifdef QUAKEENTITIES
	entity_state_t	baseline;
	entity_state_t	deltabaseline; // LordHavoc: previous frame
#endif

	float		freetime;			// sv.time when the object was freed
	entvars_t	v;					// C exported fields from progs
// other fields from progs come immediately after
} edict_t;
//#define	EDICT_FROM_AREA(l) ((edict_t *)((byte *)l - (int)&(((edict_t *)0)->area)))
//#define	EDICT_FROM_AREA(l) STRUCT_FROM_LINK(l,edict_t,area)

// LordHavoc: in an effort to eliminate time wasted on GetEdictFieldValue...  see pr_edict.c for the functions which use these.
extern int eval_gravity;
extern int eval_button3;
extern int eval_button4;
extern int eval_button5;
extern int eval_button6;
extern int eval_button7;
extern int eval_button8;
extern int eval_glow_size;
extern int eval_glow_trail;
extern int eval_glow_color;
extern int eval_items2;
extern int eval_scale;
extern int eval_alpha;
extern int eval_renderamt; // HalfLife support
extern int eval_rendermode; // HalfLife support
extern int eval_fullbright;
extern int eval_ammo_shells1;
extern int eval_ammo_nails1;
extern int eval_ammo_lava_nails;
extern int eval_ammo_rockets1;
extern int eval_ammo_multi_rockets;
extern int eval_ammo_cells1;
extern int eval_ammo_plasma;
extern int eval_idealpitch;
extern int eval_pitch_speed;
extern int eval_viewmodelforclient;
extern int eval_nodrawtoclient;
extern int eval_exteriormodeltoclient;
extern int eval_drawonlytoclient;
extern int eval_ping;
extern int eval_movement;
extern int eval_pmodel;
extern int eval_punchvector;
extern int eval_viewzoom;

#define GETEDICTFIELDVALUE(ed, fieldoffset) (fieldoffset ? (eval_t*)((char*)&ed->v + fieldoffset) : NULL)


extern dfunction_t *SV_PlayerPhysicsQC;
extern dfunction_t *EndFrameQC;

//============================================================================

extern	dprograms_t		*progs;
extern	dfunction_t		*pr_functions;
extern	char			*pr_strings;
extern	ddef_t			*pr_globaldefs;
extern	ddef_t			*pr_fielddefs;
extern	dstatement_t	*pr_statements;
extern	globalvars_t	*pr_global_struct;
extern	float			*pr_globals;			// same as pr_global_struct

extern	int				pr_edict_size;	// in bytes
extern	int				pr_edictareasize; // LordHavoc: for bounds checking

//============================================================================

void PR_Init (void);

void PR_ExecuteProgram (func_t fnum, char *errormessage);
void PR_LoadProgs (void);

void PR_Profile_f (void);

edict_t *ED_Alloc (void);
void ED_Free (edict_t *ed);

char	*ED_NewString (char *string);
// returns a copy of the string allocated from the server's string heap

void ED_Print (edict_t *ed);
void ED_Write (QFile *f, edict_t *ed);
char *ED_ParseEdict (char *data, edict_t *ent);

void ED_WriteGlobals (QFile *f);
void ED_ParseGlobals (char *data);

void ED_LoadFromFile (char *data);

edict_t *EDICT_NUM_ERROR(int n);
#define EDICT_NUM(n) (n >= 0 ? (n < sv.max_edicts ? (edict_t *)((byte *)sv.edicts + (n) * pr_edict_size) : EDICT_NUM_ERROR(n)) : EDICT_NUM_ERROR(n))
//define EDICT_NUM(n) ((edict_t *)(sv.edicts+ (n)*pr_edict_size))
//define NUM_FOR_EDICT(e) (((byte *)(e) - sv.edicts)/pr_edict_size)

//edict_t *EDICT_NUM(int n);
int NUM_FOR_EDICT(edict_t *e);

#define	NEXT_EDICT(e) ((edict_t *)( (byte *)e + pr_edict_size))

#define	EDICT_TO_PROG(e) ((byte *)e - (byte *)sv.edicts)
#define PROG_TO_EDICT(e) ((edict_t *)((byte *)sv.edicts + e))

//============================================================================

#define	G_FLOAT(o) (pr_globals[o])
#define	G_INT(o) (*(int *)&pr_globals[o])
#define	G_EDICT(o) ((edict_t *)((byte *)sv.edicts+ *(int *)&pr_globals[o]))
#define G_EDICTNUM(o) NUM_FOR_EDICT(G_EDICT(o))
#define	G_VECTOR(o) (&pr_globals[o])
#define	G_STRING(o) (pr_strings + *(string_t *)&pr_globals[o])
#define	G_FUNCTION(o) (*(func_t *)&pr_globals[o])

#define	E_FLOAT(e,o) (((float*)&e->v)[o])
#define	E_INT(e,o) (*(int *)&((float*)&e->v)[o])
#define	E_VECTOR(e,o) (&((float*)&e->v)[o])
#define	E_STRING(e,o) (pr_strings + *(string_t *)&((float*)&e->v)[o])

extern	int		type_size[8];

typedef void (*builtin_t) (void);
extern	builtin_t *pr_builtins;
extern int pr_numbuiltins;

extern int		pr_argc;

extern	qboolean	pr_trace;
extern	dfunction_t	*pr_xfunction;
extern	int			pr_xstatement;

extern	unsigned short		pr_crc;

void PR_RunError (char *error, ...);

void ED_PrintEdicts (void);
void ED_PrintNum (int ent);

//eval_t *GetEdictFieldValue(edict_t *ed, char *field);

