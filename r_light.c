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
// r_light.c

#include "quakedef.h"

cvar_t r_lightmodels = {"r_lightmodels", "1"};

void rlight_init()
{
	Cvar_RegisterVariable(&r_lightmodels);
}

int	r_dlightframecount;

/*
==================
R_AnimateLight
==================
*/
void R_AnimateLight (void)
{
	int			i,j,k;
	
//
// light animations
// 'm' is normal light, 'a' is no light, 'z' is double bright
	i = (int)(cl.time*10);
	for (j=0 ; j<MAX_LIGHTSTYLES ; j++)
	{
		if (!cl_lightstyle[j].length)
		{
			d_lightstylevalue[j] = 256;
			continue;
		}
		k = i % cl_lightstyle[j].length;
		k = cl_lightstyle[j].map[k] - 'a';
		k = k*22;
		d_lightstylevalue[j] = k;
	}	
}

/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

/*
=============
R_MarkLights
=============
*/
void R_MarkLights (vec3_t lightorigin, dlight_t *light, int bit, int bitindex, mnode_t *node)
{
	float		dist, l, maxdist;
	msurface_t	*surf;
	int			i, j, s, t;
	vec3_t		impact;
	
loc0:
	if (node->contents < 0)
		return;

	dist = DotProduct (lightorigin, node->plane->normal) - node->plane->dist;
	
	if (dist > light->radius)
	{
		if (node->children[0]->contents >= 0) // LordHavoc: save some time by not pushing another stack frame
		{
			node = node->children[0];
			goto loc0;
		}
		return;
	}
	if (dist < -light->radius)
	{
		if (node->children[1]->contents >= 0) // LordHavoc: save some time by not pushing another stack frame
		{
			node = node->children[1];
			goto loc0;
		}
		return;
	}

	maxdist = light->radius*light->radius;

// mark the polygons
	surf = cl.worldmodel->surfaces + node->firstsurface;
	for (i=0 ; i<node->numsurfaces ; i++, surf++)
	{
		if (surf->flags & SURF_DRAWTURB) // water
		{
			if (surf->dlightframe != r_dlightframecount) // not dynamic until now
			{
				surf->dlightbits[0] = surf->dlightbits[1] = surf->dlightbits[2] = surf->dlightbits[3] = surf->dlightbits[4] = surf->dlightbits[5] = surf->dlightbits[6] = surf->dlightbits[7] = 0;
				surf->dlightframe = r_dlightframecount;
			}
			surf->dlightbits[bitindex] |= bit;
		}
		// LordHavoc: MAJOR dynamic light speedup here, eliminates marking of surfaces that are too far away from light, thus preventing unnecessary uploads
		else /*if (r_dynamicbothsides.value || (((surf->flags & SURF_PLANEBACK) && (dist < -BACKFACE_EPSILON)) || (!(surf->flags & SURF_PLANEBACK) && (dist > BACKFACE_EPSILON))))*/
		{
			// passed the plane side check
			for (j=0 ; j<3 ; j++)
				impact[j] = lightorigin[j] - surf->plane->normal[j]*dist;

			// clamp center of light to corner and check brightness
			l = DotProduct (impact, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3] - surf->texturemins[0];
			s = l+0.5;if (s < 0) s = 0;else if (s > surf->extents[0]) s = surf->extents[0];
			s = l - s;
			l = DotProduct (impact, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3] - surf->texturemins[1];
			t = l+0.5;if (t < 0) t = 0;else if (t > surf->extents[1]) t = surf->extents[1];
			t = l - t;
			// compare to minimum light
			if ((s*s+t*t+dist*dist) < maxdist)
			{
				if (surf->dlightframe != r_dlightframecount) // not dynamic until now
				{
					surf->dlightbits[0] = surf->dlightbits[1] = surf->dlightbits[2] = surf->dlightbits[3] = surf->dlightbits[4] = surf->dlightbits[5] = surf->dlightbits[6] = surf->dlightbits[7] = 0;
					surf->dlightframe = r_dlightframecount;
				}
				surf->dlightbits[bitindex] |= bit;
			}
		}
	}

	if (node->children[0]->contents >= 0)
	{
		if (node->children[1]->contents >= 0)
		{
			R_MarkLights (lightorigin, light, bit, bitindex, node->children[0]);
			node = node->children[1];
			goto loc0;
		}
		else
		{
			node = node->children[0];
			goto loc0;
		}
	}
	else if (node->children[1]->contents >= 0)
	{
		node = node->children[1];
		goto loc0;
	}
}


/*
=============
R_PushDlights
=============
*/
void R_PushDlights (void)
{
	int		i;
	dlight_t	*l;

	r_dlightframecount = r_framecount + 1;	// because the count hasn't advanced yet for this frame

//	if (gl_flashblend.value || !r_dynamic.value)
//		return;

	l = cl_dlights;

	for (i=0 ; i<MAX_DLIGHTS ; i++, l++)
	{
		if (l->die < cl.time || !l->radius)
			continue;
		R_MarkLights (l->origin, l, 1<<(i&31), i >> 5, cl.worldmodel->nodes );
	}
}


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

mplane_t		*lightplane;
vec3_t			lightspot;

int RecursiveLightPoint (vec3_t color, mnode_t *node, vec3_t start, vec3_t end)
{
	float		front, back, frac;
	vec3_t		mid;

loc0:
	if (node->contents < 0)
		return false;		// didn't hit anything
	
// calculate mid point
	front = PlaneDiff (start, node->plane);
	back = PlaneDiff (end, node->plane);

	// LordHavoc: optimized recursion
	if ((back < 0) == (front < 0))
//		return RecursiveLightPoint (color, node->children[front < 0], start, end);
	{
		node = node->children[front < 0];
		goto loc0;
	}
	
	frac = front / (front-back);
	mid[0] = start[0] + (end[0] - start[0])*frac;
	mid[1] = start[1] + (end[1] - start[1])*frac;
	mid[2] = start[2] + (end[2] - start[2])*frac;
	
// go down front side
	if (RecursiveLightPoint (color, node->children[front < 0], start, mid))
		return true;	// hit something
	else
	{
		int i, ds, dt;
		msurface_t *surf;
	// check for impact on this node
		VectorCopy (mid, lightspot);
		lightplane = node->plane;

		surf = cl.worldmodel->surfaces + node->firstsurface;
		for (i = 0;i < node->numsurfaces;i++, surf++)
		{
			if (surf->flags & SURF_DRAWTILED)
				continue;	// no lightmaps

			ds = (int) ((float) DotProduct (mid, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3]);
			dt = (int) ((float) DotProduct (mid, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3]);

			if (ds < surf->texturemins[0] || dt < surf->texturemins[1])
				continue;
			
			ds -= surf->texturemins[0];
			dt -= surf->texturemins[1];
			
			if (ds > surf->extents[0] || dt > surf->extents[1])
				continue;

			if (surf->samples)
			{
				byte *lightmap;
				int maps, line3, dsfrac = ds & 15, dtfrac = dt & 15, r00 = 0, g00 = 0, b00 = 0, r01 = 0, g01 = 0, b01 = 0, r10 = 0, g10 = 0, b10 = 0, r11 = 0, g11 = 0, b11 = 0;
				float scale;
				line3 = ((surf->extents[0]>>4)+1)*3;

				lightmap = surf->samples + ((dt>>4) * ((surf->extents[0]>>4)+1) + (ds>>4))*3; // LordHavoc: *3 for color

				for (maps = 0;maps < MAXLIGHTMAPS && surf->styles[maps] != 255;maps++)
				{
					scale = (float) d_lightstylevalue[surf->styles[maps]] * 1.0 / 256.0;
					r00 += (float) lightmap[      0] * scale;g00 += (float) lightmap[      1] * scale;b00 += (float) lightmap[2] * scale;
					r01 += (float) lightmap[      3] * scale;g01 += (float) lightmap[      4] * scale;b01 += (float) lightmap[5] * scale;
					r10 += (float) lightmap[line3+0] * scale;g10 += (float) lightmap[line3+1] * scale;b10 += (float) lightmap[line3+2] * scale;
					r11 += (float) lightmap[line3+3] * scale;g11 += (float) lightmap[line3+4] * scale;b11 += (float) lightmap[line3+5] * scale;
					lightmap += ((surf->extents[0]>>4)+1) * ((surf->extents[1]>>4)+1)*3; // LordHavoc: *3 for colored lighting
				}

				color[0] += (float) ((int) ((((((((r11-r10) * dsfrac) >> 4) + r10)-((((r01-r00) * dsfrac) >> 4) + r00)) * dtfrac) >> 4) + ((((r01-r00) * dsfrac) >> 4) + r00)));
				color[1] += (float) ((int) ((((((((g11-g10) * dsfrac) >> 4) + g10)-((((g01-g00) * dsfrac) >> 4) + g00)) * dtfrac) >> 4) + ((((g01-g00) * dsfrac) >> 4) + g00)));
				color[2] += (float) ((int) ((((((((b11-b10) * dsfrac) >> 4) + b10)-((((b01-b00) * dsfrac) >> 4) + b00)) * dtfrac) >> 4) + ((((b01-b00) * dsfrac) >> 4) + b00)));
			}
			return true; // success
		}

	// go down back side
		return RecursiveLightPoint (color, node->children[front >= 0], mid, end);
	}
}

void R_LightPoint (vec3_t color, vec3_t p)
{
	vec3_t		end;
	
	if (r_fullbright.value || !cl.worldmodel->lightdata)
	{
		color[0] = color[1] = color[2] = 255;
		return;
	}
	
	end[0] = p[0];
	end[1] = p[1];
	end[2] = p[2] - 2048;

	color[0] = color[1] = color[2] = 0;
	RecursiveLightPoint (color, cl.worldmodel->nodes, p, end);
}

// LordHavoc: R_DynamicLightPoint - acumulates the dynamic lighting
void R_DynamicLightPoint(vec3_t color, vec3_t org, int *dlightbits)
{
	int		i;
	vec3_t	dist;
	float	brightness, r, f;

	if (/*gl_flashblend.value || !r_dynamic.value || */(!dlightbits[0] && !dlightbits[1] && !dlightbits[2] && !dlightbits[3] && !dlightbits[4] && !dlightbits[5] && !dlightbits[6] && !dlightbits[7]))
		return;

	for (i=0 ; i<MAX_DLIGHTS ; i++)
	{
		if (!((1 << (i&31)) & dlightbits[i>>5]))
			continue;
		if (cl_dlights[i].die < cl.time || !cl_dlights[i].radius)
			continue;
		VectorSubtract (org, cl_dlights[i].origin, dist);
		if ((f = DotProduct(dist, dist) + 64.0) < (r = cl_dlights[i].radius*cl_dlights[i].radius))
		{
			brightness = r * 16.0 / f;
			if (cl_dlights[i].dark)
				brightness = -brightness;
			color[0] += brightness * cl_dlights[i].color[0];
			color[1] += brightness * cl_dlights[i].color[1];
			color[2] += brightness * cl_dlights[i].color[2];
		}
	}
}

// same as above but no bitmask to check
void R_DynamicLightPointNoMask(vec3_t color, vec3_t org)
{
	int		i;
	vec3_t	dist;
	float	brightness, r, f;

//	if (gl_flashblend.value || !r_dynamic.value)
//		return;

	for (i=0 ; i<MAX_DLIGHTS ; i++)
	{
		if (cl_dlights[i].die < cl.time || !cl_dlights[i].radius)
			continue;
		VectorSubtract (org, cl_dlights[i].origin, dist);
		if ((f = DotProduct(dist, dist) + 64.0) < (r = cl_dlights[i].radius*cl_dlights[i].radius))
		{
			brightness = r * 16.0 / f;
			if (cl_dlights[i].dark)
				brightness = -brightness;
			color[0] += brightness * cl_dlights[i].color[0];
			color[1] += brightness * cl_dlights[i].color[1];
			color[2] += brightness * cl_dlights[i].color[2];
		}
	}
}

extern float *aliasvert;
extern float *aliasvertnorm;
extern byte *aliasvertcolor;
extern vec_t shadecolor[];
extern float modelalpha;
extern qboolean lighthalf;
void R_LightModel(int numverts, vec3_t center)
{
	int i, j, nearlights = 0;
	vec3_t dist;
	float t, t1, t2, t3, *avn;
	byte r,g,b,a, *avc;
	struct
	{
		vec3_t color;
		vec3_t origin;
	} nearlight[MAX_DLIGHTS];
	if (!lighthalf)
	{
		shadecolor[0] *= 2.0f;
		shadecolor[1] *= 2.0f;
		shadecolor[2] *= 2.0f;
	}
	avc = aliasvertcolor;
	avn = aliasvertnorm;
	a = (byte) bound((int) 0, (int) (modelalpha * 255.0f), (int) 255);
	if (currententity->effects & EF_FULLBRIGHT)
	{
		if (lighthalf)
		{
			r = (byte) ((float) (128.0f * currententity->colormod[0]));
			g = (byte) ((float) (128.0f * currententity->colormod[1]));
			b = (byte) ((float) (128.0f * currententity->colormod[2]));
		}
		else
		{
			r = (byte) ((float) (255.0f * currententity->colormod[0]));
			g = (byte) ((float) (255.0f * currententity->colormod[1]));
			b = (byte) ((float) (255.0f * currententity->colormod[2]));
		}
		for (i = 0;i < numverts;i++)
		{
			*avc++ = r;
			*avc++ = g;
			*avc++ = b;
			*avc++ = a;
		}
		return;
	}
	if (r_lightmodels.value)
	{
		for (i = 0;i < MAX_DLIGHTS;i++)
		{
			if (cl_dlights[i].die < cl.time || !cl_dlights[i].radius)
				continue;
			VectorSubtract (center, cl_dlights[i].origin, dist);
			if ((t2 = DotProduct(dist,dist) + 16.0f) + 64.0f < (t1 = cl_dlights[i].radius*cl_dlights[i].radius))
			{
				VectorCopy(cl_dlights[i].origin, nearlight[nearlights].origin);
				nearlight[nearlights].color[0] = cl_dlights[i].color[0] * cl_dlights[i].radius * cl_dlights[i].radius * 0.5f;
				nearlight[nearlights].color[1] = cl_dlights[i].color[1] * cl_dlights[i].radius * cl_dlights[i].radius * 0.5f;
				nearlight[nearlights].color[2] = cl_dlights[i].color[2] * cl_dlights[i].radius * cl_dlights[i].radius * 0.5f;
				if (cl_dlights[i].dark)
				{
					nearlight[nearlights].color[0] = -nearlight[nearlights].color[0];
					nearlight[nearlights].color[1] = -nearlight[nearlights].color[1];
					nearlight[nearlights].color[2] = -nearlight[nearlights].color[2];
				}
				if (lighthalf)
				{
					nearlight[nearlights].color[0] *= 0.5f;
					nearlight[nearlights].color[1] *= 0.5f;
					nearlight[nearlights].color[2] *= 0.5f;
				}
				t1 = 1.0f / t2;
				shadecolor[0] += nearlight[nearlights].color[0] * t1;
				shadecolor[1] += nearlight[nearlights].color[1] * t1;
				shadecolor[2] += nearlight[nearlights].color[2] * t1;
				nearlight[nearlights].color[0] *= currententity->colormod[0];
				nearlight[nearlights].color[1] *= currententity->colormod[1];
				nearlight[nearlights].color[2] *= currententity->colormod[2];
				nearlights++;
			}
		}
	}
	else
	{
		for (i = 0;i < MAX_DLIGHTS;i++)
		{
			if (cl_dlights[i].die < cl.time || !cl_dlights[i].radius)
				continue;
			VectorSubtract (center, cl_dlights[i].origin, dist);
			if ((t2 = DotProduct(dist,dist)) + 64.0f < (t1 = cl_dlights[i].radius*cl_dlights[i].radius))
			{
				dist[0] = cl_dlights[i].color[0] * cl_dlights[i].radius * cl_dlights[i].radius * 0.5f;
				dist[1] = cl_dlights[i].color[1] * cl_dlights[i].radius * cl_dlights[i].radius * 0.5f;
				dist[2] = cl_dlights[i].color[2] * cl_dlights[i].radius * cl_dlights[i].radius * 0.5f;
				if (cl_dlights[i].dark)
				{
					dist[0] = -dist[0];
					dist[1] = -dist[1];
					dist[2] = -dist[2];
				}
				if (lighthalf)
				{
					dist[0] *= 0.5f;
					dist[1] *= 0.5f;
					dist[2] *= 0.5f;
				}
				t1 = 1.5f / t2;
				shadecolor[0] += dist[0] * t1;
				shadecolor[1] += dist[1] * t1;
				shadecolor[2] += dist[2] * t1;
			}
		}
	}
	shadecolor[0] *= currententity->colormod[0];
	shadecolor[1] *= currententity->colormod[1];
	shadecolor[2] *= currententity->colormod[2];
	t1 = bound(0, shadecolor[0], 255);r = (byte) t1;
	t1 = bound(0, shadecolor[1], 255);g = (byte) t1;
	t1 = bound(0, shadecolor[2], 255);b = (byte) t1;
	if (nearlights)
	{
		int temp;
		vec3_t v;
		float *av;
		av = aliasvert;
		if (nearlights == 1)
		{
			for (i = 0;i < numverts;i++)
			{
				VectorSubtract(nearlight[0].origin, av, v);
				t = DotProduct(avn,v);
				if (t > 0)
				{
					t /= DotProduct(v,v);
					temp = (int) ((float) (shadecolor[0] + nearlight[0].color[0] * t));if (temp < 0) temp = 0;else if (temp > 255) temp = 255;*avc++ = temp;
					temp = (int) ((float) (shadecolor[1] + nearlight[0].color[1] * t));if (temp < 0) temp = 0;else if (temp > 255) temp = 255;*avc++ = temp;
					temp = (int) ((float) (shadecolor[2] + nearlight[0].color[2] * t));if (temp < 0) temp = 0;else if (temp > 255) temp = 255;*avc++ = temp;
				}
				else
				{
					*avc++ = r;
					*avc++ = g;
					*avc++ = b;
				}
				*avc++ = a;
				av+=3;
				avn+=3;
			}
		}
		else
		{
			int i1, i2, i3;
			for (i = 0;i < numverts;i++)
			{
				t1 = shadecolor[0];
				t2 = shadecolor[1];
				t3 = shadecolor[2];
				for (j = 0;j < nearlights;j++)
				{
					VectorSubtract(nearlight[j].origin, av, v);
					t = DotProduct(avn,v);
					if (t > 0)
					{
						t /= DotProduct(v,v);
						t1 += nearlight[j].color[0] * t;
						t2 += nearlight[j].color[1] * t;
						t3 += nearlight[j].color[2] * t;
					}
				}
				i1 = t1;if (i1 < 0) i1 = 0;else if (i1 > 255) i1 = 255;
				i2 = t2;if (i2 < 0) i2 = 0;else if (i2 > 255) i2 = 255;
				i3 = t3;if (i3 < 0) i3 = 0;else if (i3 > 255) i3 = 255;
				*avc++ = i1;
				*avc++ = i2;
				*avc++ = i3;
				*avc++ = a;
			}
		}
	}
	else
	{
		for (i = 0;i < numverts;i++)
		{
			*avc++ = r;
			*avc++ = g;
			*avc++ = b;
			*avc++ = a;
		}
	}
}
