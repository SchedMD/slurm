/*****************************************************************************\
 *  block_allocator.c - Assorted functions for layout of bgq blocks,
 *	 wiring, mapping for smap, etc.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "block_allocator.h"
#include "src/common/uid.h"
#include "src/common/timers.h"
#include "src/common/slurmdb_defs.h"

#define DEBUG_PA
#define BEST_COUNT_INIT 20

#define BA_MP_USED_FALSE          0x0000
#define BA_MP_USED_TRUE           0x0001
#define BA_MP_USED_TEMP           0x0002
#define BA_MP_USED_ALTERED        0x0100
#define BA_MP_USED_PASS_BIT       0x1000
#define BA_MP_USED_ALTERED_PASS   0x1100 // This should overlap
					 // BA_MP_USED_ALTERED and
					 // BA_MP_USED_PASS_BIT

#define mp_strip_unaltered(__mp) (__mp & ~BA_MP_USED_ALTERED_PASS)

static bool _initialized = false;

/* _ba_system is the "current" system that the structures will work
 *  on */
ba_system_t *ba_system_ptr = NULL;
int best_count;
int color_count = 0;
uint16_t *deny_pass = NULL;
#if (SYSTEM_DIMENSIONS == 1)
int cluster_dims = 1;
int cluster_base = 10;
#else
int cluster_dims = 3;
int cluster_base = 36;
#endif
uint32_t cluster_flags = 0;
char *p = '\0';
uint32_t ba_debug_flags = 0;

/* extern Global */
uint16_t ba_deny_pass = 0;
char letters[62];
char colors[6];
int DIM_SIZE[HIGHEST_DIMENSIONS] = {0,0,0,0};

static int REAL_DIM_SIZE[HIGHEST_DIMENSIONS] = {0,0,0,0};

s_p_options_t bg_conf_file_options[] = {
	{(char *)"DenyPassthrough", S_P_STRING},
	{(char *)"LayoutMode", S_P_STRING},
	{(char *)"MloaderImage", S_P_STRING},
	{(char *)"BridgeAPILogFile", S_P_STRING},
	{(char *)"BridgeAPIVerbose", S_P_UINT16},
	{(char *)"BasePartitionNodeCnt", S_P_UINT16},
	{(char *)"NodeCardNodeCnt", S_P_UINT16},
	{(char *)"Numpsets", S_P_UINT16},
	{(char *)"BPs", S_P_ARRAY, parse_blockreq, destroy_blockreq},
	/* these are just going to be put into a list that will be
	   freed later don't free them after reading them */
	{(char *)"AltMloaderImage", S_P_ARRAY, parse_image, NULL},
	{NULL}
};

typedef enum {
	BLOCK_ALGO_FIRST,
	BLOCK_ALGO_SECOND
} block_algo_t;

/** internal helper functions */

/* */
static int _check_for_options(select_ba_request_t* ba_request);

/* */
static int _append_geo(uint16_t *geo, List geos, int rotate);

/* */
static void _internal_removable_set_mps(int level, int *start,
					int *end, int *coords, bool mark);

/* */
static int _fill_in_coords(List results, int level, ba_mp_t *start_mp,
				    ba_mp_t **check_mp, uint16_t *block_start,
				    uint16_t *block_end, uint16_t *pass_end,
				    int *coords);

/* */
static char *_copy_from_main(List main_mps, List ret_list);

/* */
static char *_reset_altered_mps(List main_mps);

#ifdef HAVE_BGQ
/* */
static int _copy_the_path(List mps, ba_mp_t *start_mp, ba_mp_t *curr_mp,
			  ba_mp_t *mark_mp, int dim);
#endif

/* */
static int _copy_ba_switch(ba_mp_t *ba_mp, ba_mp_t *orig_mp, int dim);

/* */
static int _check_deny_pass(int dim);

/* */
static int _find_path(List mps, ba_mp_t *start_mp, int dim,
		      uint16_t geometry, uint16_t conn_type,
		      uint16_t *block_end, uint16_t *longest);

/* */
static int _setup_next_mps(ba_mp_t ****grid);


/* */
static void _create_ba_system(void);
/* */
static void _delete_ba_system(void);

/* find the first block match in the system */
static int _find_match(select_ba_request_t* ba_request, List results);

/** */
static bool _mp_used(ba_mp_t* ba_mp, int dim);

/** */
static bool _mp_out_used(ba_mp_t* ba_mp, int dim);

/* */
/* static int _find_passthrough(ba_switch_t *curr_switch, int source_port,  */
/* 			     List mps, int dim,  */
/* 			     int count, int highest_phys_x);  */
/* */

/* */
/* static int _set_one_dim(uint16_t *start, uint16_t *end, uint16_t *coord); */

/* */
static void _destroy_geo(void *object);

/* */
static int _coord(char coord);

extern char *ba_passthroughs_string(uint16_t passthrough)
{
	char *pass = NULL;
	if (passthrough & PASS_FOUND_A)
		xstrcat(pass, "A");
	if (passthrough & PASS_FOUND_X) {
		if (pass)
			xstrcat(pass, ",X");
		else
			xstrcat(pass, "X");
	}
	if (passthrough & PASS_FOUND_Y) {
		if (pass)
			xstrcat(pass, ",Y");
		else
			xstrcat(pass, "Y");
	}
	if (passthrough & PASS_FOUND_Z) {
		if (pass)
			xstrcat(pass, ",Z");
		else
			xstrcat(pass, "Z");
	}

	return pass;
}


extern int parse_blockreq(void **dest, slurm_parser_enum_t type,
			  const char *key, const char *value,
			  const char *line, char **leftover)
{
	s_p_options_t block_options[] = {
		{(char *)"Type", S_P_STRING},
		{(char *)"32CNBlocks", S_P_UINT16},
		{(char *)"128CNBlocks", S_P_UINT16},
		{(char *)"16CNBlocks", S_P_UINT16},
		{(char *)"64CNBlocks", S_P_UINT16},
		{(char *)"256CNBlocks", S_P_UINT16},
		{(char *)"MloaderImage", S_P_STRING},
		{NULL}
	};
	s_p_hashtbl_t *tbl;
	char *tmp = NULL;
	blockreq_t *n = NULL;
	hostlist_t hl = NULL;

	tbl = s_p_hashtbl_create(block_options);
	s_p_parse_line(tbl, *leftover, leftover);
	if (!value) {
		return 0;
	}
	n = (blockreq_t *)xmalloc(sizeof(blockreq_t));
	hl = hostlist_create(value);
	n->block = hostlist_ranged_string_xmalloc(hl);
	hostlist_destroy(hl);
	s_p_get_string(&n->mloaderimage, "MloaderImage", tbl);

	s_p_get_string(&tmp, "Type", tbl);
	if (!tmp || !strcasecmp(tmp,"TORUS"))
		n->conn_type[A] = SELECT_TORUS;
	else if (!strcasecmp(tmp,"MESH"))
		n->conn_type[A] = SELECT_MESH;
	else
		n->conn_type[A] = SELECT_SMALL;
	xfree(tmp);

	s_p_get_uint16(&n->small16, "16CNBlocks", tbl);
	s_p_get_uint16(&n->small32, "32CNBlocks", tbl);
	s_p_get_uint16(&n->small64, "64CNBlocks", tbl);
	s_p_get_uint16(&n->small128, "128CNBlocks", tbl);
	s_p_get_uint16(&n->small256, "256CNBlocks", tbl);

	s_p_hashtbl_destroy(tbl);

	*dest = (void *)n;
	return 1;
}

extern void destroy_blockreq(void *ptr)
{
	blockreq_t *n = (blockreq_t *)ptr;
	if (n) {
		xfree(n->block);
		xfree(n->mloaderimage);
		xfree(n);
	}
}

extern int parse_image(void **dest, slurm_parser_enum_t type,
		       const char *key, const char *value,
		       const char *line, char **leftover)
{
	s_p_options_t image_options[] = {
		{(char *)"GROUPS", S_P_STRING},
		{NULL}
	};
	s_p_hashtbl_t *tbl = NULL;
	char *tmp = NULL;
	image_t *n = NULL;
	image_group_t *image_group = NULL;
	int i = 0, j = 0;

	tbl = s_p_hashtbl_create(image_options);
	s_p_parse_line(tbl, *leftover, leftover);

	n = (image_t *)xmalloc(sizeof(image_t));
	n->name = xstrdup(value);
	n->def = false;
	if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
		info("image %s", n->name);
	n->groups = list_create(destroy_image_group_list);
	s_p_get_string(&tmp, "Groups", tbl);
	if (tmp) {
		for(i=0; i<(int)strlen(tmp); i++) {
			if ((tmp[i] == ':') || (tmp[i] == ',')) {
				image_group = (image_group_t *)
					xmalloc(sizeof(image_group_t));
				image_group->name = (char *)xmalloc(i-j+2);
				snprintf(image_group->name,
					 (i-j)+1, "%s", tmp+j);
				gid_from_string (image_group->name,
						 &image_group->gid);
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
					info("adding group %s %d",
					     image_group->name,
					     image_group->gid);
				list_append(n->groups, image_group);
				j=i;
				j++;
			}
		}
		if (j != i) {
			image_group = (image_group_t *)
				xmalloc(sizeof(image_group_t));
			image_group->name = (char *)xmalloc(i-j+2);
			snprintf(image_group->name, (i-j)+1, "%s", tmp+j);
			if (gid_from_string (image_group->name,
			                     &image_group->gid) < 0)
				fatal("Invalid bluegene.conf parameter "
				      "Groups=%s",
				      image_group->name);
			else if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
				info("adding group %s %d",
				     image_group->name,
				     image_group->gid);
			list_append(n->groups, image_group);
		}
		xfree(tmp);
	}
	s_p_hashtbl_destroy(tbl);

	*dest = (void *)n;
	return 1;
}

extern void destroy_image_group_list(void *ptr)
{
	image_group_t *image_group = (image_group_t *)ptr;
	if (image_group) {
		xfree(image_group->name);
		xfree(image_group);
	}
}

extern void destroy_image(void *ptr)
{
	image_t *n = (image_t *)ptr;
	if (n) {
		xfree(n->name);
		if (n->groups) {
			list_destroy(n->groups);
			n->groups = NULL;
		}
		xfree(n);
	}
}

extern void destroy_ba_mp(void *ptr)
{
	ba_mp_t *ba_mp = (ba_mp_t *)ptr;
	if (ba_mp) {
		xfree(ba_mp->loc);
		xfree(ba_mp);
	}
}

/*
 * create a block request.  Note that if the geometry is given,
 * then size is ignored.  If elongate is true, the algorithm will try
 * to fit that a block of cubic shape and then it will try other
 * elongated geometries.  (ie, 2x2x2 -> 4x2x1 -> 8x1x1).
 *
 * IN/OUT - ba_request: structure to allocate and fill in.
 *
 * ALL below IN's need to be set within the ba_request before the call
 * if you want them to be used.
 * ALL below OUT's are set and returned within the ba_request.
 * IN - avail_mp_bitmap: bitmap of usable midplanes.
 * IN - blrtsimage: BlrtsImage for this block if not default
 * IN - conn_type: connection type of request (TORUS or MESH or SMALL)
 * IN - elongate: if true, will try to fit different geometries of
 *      same size requests
 * IN/OUT - geometry: requested/returned geometry of block
 * IN - linuximage: LinuxImage for this block if not default
 * IN - mloaderimage: MLoaderImage for this block if not default
 * IN - nodecards: Number of nodecards in each block in request only
 *      used of small block allocations.
 * OUT - passthroughs: if there were passthroughs used in the
 *       generation of the block.
 * IN - procs: Number of real processors requested
 * IN - quarters: Number of midplane quarters in each block in request only
 *      used of small block allocations.
 * IN - RamDiskimage: RamDiskImage for this block if not default
 * IN - rotate: if true, allows rotation of block during fit
 * OUT - save_name: hostlist of midplanes used in block
 * IN/OUT - size: requested/returned count of midplanes in block
 * IN - start: geo location of where to start the allocation
 * IN - start_req: if set use the start variable to start at
 * return success of allocation/validation of params
 */
extern int new_ba_request(select_ba_request_t* ba_request)
{
	int i=0;
	float sz=1;
	int i2, picked, total_sz=1, size2=0;
	uint16_t *geo_ptr;
	int messed_with = 0;
	int checked[DIM_SIZE[A]];
	uint16_t geo[cluster_dims];

	/*FIXME: THis needs a good looking over for effencency in 4 dims. */

	memset(geo, 0, sizeof(geo));
	ba_request->save_name= NULL;
	ba_request->rotate_count= 0;
	ba_request->elongate_count = 0;
	ba_request->elongate_geos = list_create(_destroy_geo);
	memcpy(geo, ba_request->geometry, sizeof(geo));

	if (ba_request->deny_pass == (uint16_t)NO_VAL)
		ba_request->deny_pass = ba_deny_pass;

	if (!(cluster_flags & CLUSTER_FLAG_BGQ)) {
		if (geo[X] != (uint16_t)NO_VAL) {
			for (i=0; i<cluster_dims; i++) {
				if ((geo[i] < 1) || (geo[i] > DIM_SIZE[i])) {
					error("new_ba_request Error, "
					      "request geometry is invalid %d",
					      geo[i]);
					return 0;
				}
			}
			ba_request->size = ba_request->geometry[A];
		} else if (ba_request->size) {
			ba_request->geometry[A] = ba_request->size;
		} else
			return 0;
		return 1;
	}

	if (geo[X] != (uint16_t)NO_VAL) {
		for (i=0; i<cluster_dims; i++){
			if ((geo[i] < 1) || (geo[i] > DIM_SIZE[i])) {
				error("new_ba_request Error, "
				      "request geometry is invalid dim %d "
				      "can't be %c, largest is %c",
				      i,
				      alpha_num[geo[i]],
				      alpha_num[DIM_SIZE[i]]);
				return 0;
			}
		}
		_append_geo(geo, ba_request->elongate_geos, 0);
		sz=1;
		for (i=0; i<cluster_dims; i++)
			sz *= ba_request->geometry[i];
		ba_request->size = sz;
		sz=0;
	}

	deny_pass = &ba_request->deny_pass;

	if (ba_request->elongate || sz) {
		sz=1;
		/* decompose the size into a cubic geometry */
		ba_request->rotate= 1;
		ba_request->elongate = 1;

		for (i=0; i<cluster_dims; i++) {
			total_sz *= DIM_SIZE[i];
			geo[i] = 1;
		}

		if (ba_request->size == 1) {
			_append_geo(geo,
				    ba_request->elongate_geos,
				    ba_request->rotate);
			goto endit;
		}

		/* See if it can be placed in the Y dim, (Z is usually
		   the same size so rotate will get this if needed, so
		   only do it for 1 of the 2).
		*/
		if (ba_request->size <= DIM_SIZE[Y]) {
			geo[A] = 1;
			geo[X] = 1;
			geo[Y] = ba_request->size;
			geo[Z] = 1;
			sz=ba_request->size;
			_append_geo(geo,
				    ba_request->elongate_geos,
				    ba_request->rotate);
		}

		/* now see if you can set this in 1 plane. */
		i = ba_request->size/DIM_SIZE[Y];
		if (!(ba_request->size%2)
		    && i <= DIM_SIZE[Y]
		    && i <= DIM_SIZE[Z]
		    && i*i == ba_request->size) {
			geo[A] = 1;
			geo[X] = 1;
			geo[Y] = i;
			geo[Z] = i;
			sz=ba_request->size;
			_append_geo(geo,
				    ba_request->elongate_geos,
				    ba_request->rotate);
		}

		if (ba_request->size > total_sz || ba_request->size < 1)
			return 0;

		/* now see if this is a block that is a mutiple of a
		   plane. */
		sz = ba_request->size % (DIM_SIZE[Y] * DIM_SIZE[Z]);
		if (!sz) {
			i = ba_request->size / (DIM_SIZE[Y] * DIM_SIZE[Z]);
			if (i > DIM_SIZE[A]) {
				geo[A] = DIM_SIZE[A];
				geo[X] = i - DIM_SIZE[A];
			} else {
				geo[A] = i;
				geo[X] = 1;
			}
			geo[Y] = DIM_SIZE[Y];
			geo[Z] = DIM_SIZE[Z];
			sz=ba_request->size;
			if ((geo[A]*geo[X]*geo[Y]*geo[Z]) == ba_request->size)
				_append_geo(geo,
					    ba_request->elongate_geos,
					    ba_request->rotate);
			else
				error("%d I was just trying to add a "
				      "geo of %d%d%d%d "
				      "while I am trying to request "
				      "%d midplanes",
				      __LINE__, geo[A], geo[X], geo[Y], geo[Z],
				      ba_request->size);
		}
//	startagain:
		picked=0;
		for(i=0; i<DIM_SIZE[X]; i++)
			checked[i]=0;

		for (i=0; i<cluster_dims; i++) {
			total_sz *= DIM_SIZE[i];
			geo[i] = 1;
		}

		sz = 1;
		picked=0;
	tryagain:
		size2 = ba_request->size;
		//messedup:
		for (i=picked; i<cluster_dims; i++) {
			if (size2 <= 1)
				break;

			sz = size2 % DIM_SIZE[i];
			if (!sz) {
				geo[i] = DIM_SIZE[i];
				size2 /= DIM_SIZE[i];
			} else if (size2 > DIM_SIZE[i]) {
				for(i2=(DIM_SIZE[i]-1); i2 > 1; i2--) {
					/* go through each number to see if
					   the size is divisable by a smaller
					   number that is
					   good in the other dims. */
					if (!(size2%i2) && !checked[i2]) {
						size2 /= i2;

						if (i==0)
							checked[i2]=1;

						if (i2<DIM_SIZE[i]) {
							geo[i] = i2;
						} else {
							goto tryagain;
						}
						if ((i2-1)!=1 &&
						    i!=(cluster_dims-1))
							break;
					}
				}
				/* This size can not be made into a
				   block return.  If you want to try
				   until we find the next largest block
				   uncomment the code below and the goto
				   above. If a user specifies a max
				   mp count the job will never
				   run.
				*/
				if (i2==1) {
					if (!list_count(
						    ba_request->elongate_geos))
						error("Can't make a block of "
						      "%d into a cube.",
						      ba_request->size);
					goto endit;
/* 					ba_request->size +=1; */
/* 					goto startagain; */
				}
			} else {
				geo[i] = sz;
				break;
			}
		}

		if ((geo[X]*geo[Y]) <= DIM_SIZE[Y]) {
			ba_request->geometry[A] = geo[A];
			ba_request->geometry[X] = 1;
			ba_request->geometry[Y] = geo[X] * geo[Y];
			ba_request->geometry[Z] = geo[Z];
			_append_geo(ba_request->geometry,
				    ba_request->elongate_geos,
				    ba_request->rotate);

		}

		if ((geo[X]*geo[Z]) <= DIM_SIZE[Y]) {
			ba_request->geometry[A] = geo[A];
			ba_request->geometry[X] = 1;
			ba_request->geometry[Y] = geo[Y];
			ba_request->geometry[Z] = geo[X] * geo[Z];
			_append_geo(ba_request->geometry,
				    ba_request->elongate_geos,
				    ba_request->rotate);

		}

		/* Make sure geo[A] is even and then see if we can get
		   it into the Y or Z dim. */
		if (!(geo[A]%2) && ((geo[A]/2) <= DIM_SIZE[Y])) {
			ba_request->geometry[X] = geo[X];
			if (geo[Y] == 1) {
				ba_request->geometry[Y] = geo[A]/2;
				messed_with = 1;
			} else
				ba_request->geometry[Y] = geo[Y];
			if (!messed_with && geo[Z] == 1) {
				messed_with = 1;
				ba_request->geometry[Z] = geo[A]/2;
			} else
				ba_request->geometry[Z] = geo[Z];
			if (messed_with) {
				messed_with = 0;
				ba_request->geometry[A] = 2;
				_append_geo(ba_request->geometry,
					    ba_request->elongate_geos,
					    ba_request->rotate);
			}
		}
		if (geo[X] == DIM_SIZE[X] && (geo[Y] < DIM_SIZE[Y]
					      || geo[Z] < DIM_SIZE[Z])) {
			if (DIM_SIZE[Y]<DIM_SIZE[Z]) {
				i = DIM_SIZE[Y];
				DIM_SIZE[Y] = DIM_SIZE[Z];
				DIM_SIZE[Z] = i;
			}
			ba_request->geometry[A] = geo[A];
			ba_request->geometry[X] = geo[X];
			ba_request->geometry[Y] = geo[Y];
			ba_request->geometry[Z] = geo[Z];
			if (ba_request->geometry[Y] < DIM_SIZE[Y]) {
				i = (DIM_SIZE[Y] - ba_request->geometry[Y]);
				ba_request->geometry[Y] +=i;
			}
			if (ba_request->geometry[Z] < DIM_SIZE[Z]) {
				i = (DIM_SIZE[Z] - ba_request->geometry[Z]);
				ba_request->geometry[Z] +=i;
			}
			for(i = DIM_SIZE[X]; i>0; i--) {
				ba_request->geometry[X]--;
				i2 = (ba_request->geometry[X]
				      * ba_request->geometry[Y]
				      * ba_request->geometry[Z]);
				if (i2 < ba_request->size) {
					ba_request->geometry[X]++;
					messed_with = 1;
					break;
				}
			}
			if (messed_with) {
				messed_with = 0;
				_append_geo(ba_request->geometry,
					    ba_request->elongate_geos,
					    ba_request->rotate);
			}
		}

		if ((geo[A]*geo[X]*geo[Y]*geo[Z]) == ba_request->size)
			_append_geo(geo,
				    ba_request->elongate_geos,
				    ba_request->rotate);
		else
			error("%d I was just trying to add a geo of %d%d%d%d "
			      "while I am trying to request %d midplanes",
			      __LINE__, geo[A], geo[X], geo[Y], geo[Z],
			      ba_request->size);

/* Having the functions pow and powf on an aix system doesn't seem to
 * link well, so since this is only for aix and this doesn't really
 * need to be there just don't allow this extra calculation.
 */
#ifndef HAVE_AIX
		/* see if We can find a cube or square root of the
		   size to make an easy cube */
		for(i=0; i<cluster_dims-1; i++) {
			sz = powf((float)ba_request->size,
				  (float)1/(cluster_dims-i));
			if (pow(sz,(cluster_dims-i)) == ba_request->size)
				break;
		}

		if (i < (cluster_dims-1)) {
			/* we found something that looks like a cube! */
			int i3 = i;

			for (i=0; i<i3; i++)
				geo[i] = 1;

			for (i=i3; i<cluster_dims; i++)
				if (sz<=DIM_SIZE[i])
					geo[i] = sz;
				else
					goto endit;

			if ((geo[X]*geo[Y]*geo[Z]) == ba_request->size)
				_append_geo(geo,
					    ba_request->elongate_geos,
					    ba_request->rotate);
			else
				error("%d I was just trying to add "
				      "a geo of %d%d%d "
				      "while I am trying to request "
				      "%d midplanes",
				      __LINE__, geo[X], geo[Y], geo[Z],
				      ba_request->size);
		}
#endif //HAVE_AIX
	}

endit:
	if (!(geo_ptr = (uint16_t *)list_peek(ba_request->elongate_geos)))
		return 0;

	ba_request->elongate_count++;
	ba_request->geometry[X] = geo_ptr[X];
	ba_request->geometry[Y] = geo_ptr[Y];
	ba_request->geometry[Z] = geo_ptr[Z];
	sz=1;
	for (i=0; i<cluster_dims; i++)
		sz *= ba_request->geometry[i];
	ba_request->size = sz;

	return 1;
}

/**
 * delete a block request
 */
extern void delete_ba_request(void *arg)
{
	select_ba_request_t *ba_request = (select_ba_request_t *)arg;
	if (ba_request) {
		xfree(ba_request->save_name);
		if (ba_request->elongate_geos)
			list_destroy(ba_request->elongate_geos);
		xfree(ba_request->mloaderimage);

		xfree(ba_request);
	}
}

/**
 * print a block request
 */
extern void print_ba_request(select_ba_request_t* ba_request)
{
	int i;

	if (ba_request == NULL){
		error("print_ba_request Error, request is NULL");
		return;
	}
	debug("  ba_request:");
	debug("    geometry:\t");
	for (i=0; i<cluster_dims; i++){
		debug("%d", ba_request->geometry[i]);
	}
	debug("        size:\t%d", ba_request->size);
	debug("   conn_type:\t%d", ba_request->conn_type[A]);
	debug("      rotate:\t%d", ba_request->rotate);
	debug("    elongate:\t%d", ba_request->elongate);
}

/**
 * empty a list that we don't want to destroy the memory of the
 * elements always returns 1
 */
extern int empty_null_destroy_list(void *arg, void *key)
{
	return 1;
}

/**
 * Initialize internal structures by either reading previous block
 * configurations from a file or by running the graph solver.
 *
 * IN: node_info_msg_t * can be null,
 *     should be from slurm_load_node().
 *
 * return: void.
 */
extern void ba_init(node_info_msg_t *node_info_ptr, bool sanity_check)
{
	int x,y,z;
	node_info_t *node_ptr = NULL;
	int number, count;
	char *numeric = NULL;
	int i, j, k;
	slurm_conf_node_t *node = NULL, **ptr_array;
	int coords[HIGHEST_DIMENSIONS];

	/* We only need to initialize once, so return if already done so. */
	if (_initialized)
		return;

	cluster_dims = slurmdb_setup_cluster_dims();
	cluster_flags = slurmdb_setup_cluster_flags();
	set_ba_debug_flags(slurm_get_debug_flags());

	bridge_init("");

	/* make the letters array only contain letters upper and lower
	 * (62) */
	y = 'A';
	for (x = 0; x < 62; x++) {
		if (y == '[')
			y = 'a';
		else if (y == '{')
			y = '0';
		else if (y == ':')
			y = 'A';
		letters[x] = y;
		y++;
	}

	z=1;
	for (x = 0; x < 6; x++) {
		if (z == 4)
			z++;
		colors[x] = z;
		z++;
	}

	best_count=BEST_COUNT_INIT;

	if (ba_system_ptr)
		_delete_ba_system();

	ba_system_ptr = (ba_system_t *) xmalloc(sizeof(ba_system_t));

	ba_system_ptr->num_of_proc = 0;

	/* cluster_dims is already set up off of working_cluster_rec */
	if (cluster_dims == 1) {
		if (node_info_ptr) {
			REAL_DIM_SIZE[A] = DIM_SIZE[A] =
				node_info_ptr->record_count;
			ba_system_ptr->num_of_proc =
				node_info_ptr->record_count;
			REAL_DIM_SIZE[X] = DIM_SIZE[X] = 1;
			REAL_DIM_SIZE[Y] = DIM_SIZE[Y] = 1;
			REAL_DIM_SIZE[Z] = DIM_SIZE[Z] = 1;
		}
		goto setup_done;
	} else if (working_cluster_rec && working_cluster_rec->dim_size) {
		for(i=0; i<working_cluster_rec->dimensions; i++) {
			REAL_DIM_SIZE[i] = DIM_SIZE[i] =
				working_cluster_rec->dim_size[i];
		}
		goto setup_done;
	}


	if (node_info_ptr) {
		for (i = 0; i < (int)node_info_ptr->record_count; i++) {
			node_ptr = &node_info_ptr->node_array[i];
			number = 0;

			if (!node_ptr->name) {
				for (j=0; j<HIGHEST_DIMENSIONS; j++)
					DIM_SIZE[j] = 0;
				goto node_info_error;
			}

			numeric = node_ptr->name;
			while (numeric) {
				if (numeric[0] < '0' || numeric[0] > 'D'
				    || (numeric[0] > '9'
					&& numeric[0] < 'A')) {
					numeric++;
					continue;
				}
				number = xstrntol(numeric, &p, cluster_dims,
						  cluster_base);
				break;
			}
			hostlist_parse_int_to_array(
				number, coords, cluster_dims, cluster_base);

			for (j=0; j<cluster_dims; j++) {
				if (DIM_SIZE[j] < coords[j])
					DIM_SIZE[j] = coords[j];
			}
		}
		for (j=0; j<cluster_dims; j++) {
			DIM_SIZE[j]++;
			/* this will probably be reset below */
			REAL_DIM_SIZE[j] = DIM_SIZE[j];
		}
		ba_system_ptr->num_of_proc = node_info_ptr->record_count;
	}
node_info_error:

	if ((DIM_SIZE[A]==0) || (DIM_SIZE[X]==0)
	    || (DIM_SIZE[Y]==0) || (DIM_SIZE[Z]==0)) {
		debug("Setting dimensions from slurm.conf file");
		count = slurm_conf_nodename_array(&ptr_array);
		if (count == 0)
			fatal("No NodeName information available!");

		for (i = 0; i < count; i++) {
			node = ptr_array[i];
			j = 0;
			while (node->nodenames[j] != '\0') {
				if ((node->nodenames[j] == '['
				     || node->nodenames[j] == ',')
				    && (node->nodenames[j+10] == ']'
					|| node->nodenames[j+10] == ',')
				    && (node->nodenames[j+5] == 'x'
					|| node->nodenames[j+5] == '-')) {
					j+=6;
				} else if ((node->nodenames[j] >= '0'
					    && node->nodenames[j] <= '9')
					   || (node->nodenames[j] >= 'A'
					       && node->nodenames[j] <= 'Z')) {
					/* suppose to be blank, just
					   making sure this is the
					   correct alpha num
					*/
				} else {
					j++;
					continue;
				}
				number = xstrntol(node->nodenames + j,
						  &p, cluster_dims,
						  cluster_base);
				hostlist_parse_int_to_array(
					number, coords, cluster_dims,
					cluster_base);
				j += 4;

				for(k=0; k<cluster_dims; k++)
					DIM_SIZE[k] = MAX(DIM_SIZE[k],
							  coords[k]);

				if (node->nodenames[j] != ',')
					break;
			}
		}

		if ((DIM_SIZE[A]==0) && (DIM_SIZE[X]==0)
		    && (DIM_SIZE[Y]==0) && (DIM_SIZE[Z]==0))
			info("are you sure you only have 1 midplane? %s",
			     node->nodenames);
		for(j=0; j<cluster_dims; j++) {
			DIM_SIZE[j]++;
			/* this will probably be reset below */
			REAL_DIM_SIZE[j] = DIM_SIZE[j];
		}
	}

	/* sanity check.  We can only request part of the system, but
	   we don't want to allow more than we have. */
	if (sanity_check) {
		verbose("Attempting to contact MMCS");
		if (bridge_get_size(REAL_DIM_SIZE) == SLURM_SUCCESS) {
			verbose("BlueGene configured with "
				"%d x %d x %d x %d base blocks",
				REAL_DIM_SIZE[A],
				REAL_DIM_SIZE[X],
				REAL_DIM_SIZE[Y],
				REAL_DIM_SIZE[Z]);
			if ((DIM_SIZE[A] > REAL_DIM_SIZE[A])
			    || (DIM_SIZE[X] > REAL_DIM_SIZE[X])
			    || (DIM_SIZE[Y] > REAL_DIM_SIZE[Y])
			    || (DIM_SIZE[Z] > REAL_DIM_SIZE[Z])) {
				fatal("You requested a %c%c%c%c system, "
				      "but we only have a system of %c%c%c%c.  "
				      "Change your slurm.conf.",
				      alpha_num[DIM_SIZE[A]],
				      alpha_num[DIM_SIZE[X]],
				      alpha_num[DIM_SIZE[Y]],
				      alpha_num[DIM_SIZE[Z]],
				      alpha_num[REAL_DIM_SIZE[A]],
				      alpha_num[REAL_DIM_SIZE[X]],
				      alpha_num[REAL_DIM_SIZE[Y]],
				      alpha_num[REAL_DIM_SIZE[Z]]);
			}
		}
	}

setup_done:
	if (cluster_dims == 1) {
		if (DIM_SIZE[X]==0) {
			debug("Setting default system dimensions");
			REAL_DIM_SIZE[A] = DIM_SIZE[A] = 100;
			REAL_DIM_SIZE[X] = DIM_SIZE[X] = 1;
			REAL_DIM_SIZE[Y] = DIM_SIZE[Y] = 1;
			REAL_DIM_SIZE[Z] = DIM_SIZE[Z] = 1;
		}
	} else {
		debug("We are using %c x %c x %c x %c of the system.",
		      alpha_num[DIM_SIZE[A]],
		      alpha_num[DIM_SIZE[X]],
		      alpha_num[DIM_SIZE[Y]],
		      alpha_num[DIM_SIZE[Z]]);
	}

	if (!ba_system_ptr->num_of_proc) {
		ba_system_ptr->num_of_proc = 1;
		for(i=0; i<cluster_dims; i++)
			ba_system_ptr->num_of_proc *= DIM_SIZE[i];
	}

	_create_ba_system();
	bridge_setup_system();

	_initialized = true;
	init_grid(node_info_ptr);
}


/**
 * destroy all the internal (global) data structs.
 */
extern void ba_fini()
{
	int i = 0;

	if (!_initialized){
		return;
	}

	bridge_fini();

	_delete_ba_system();
	_initialized = false;
	for (i=0; i<HIGHEST_DIMENSIONS; i++)
		DIM_SIZE[i] = 0;

//	debug3("pa system destroyed");
}

extern void set_ba_debug_flags(uint32_t debug_flags)
{
	ba_debug_flags = debug_flags;
}

/*
 * set the mp in the internal configuration as in, or not in use,
 * along with the current state of the mp.
 *
 * IN ba_mp: ba_mp_t to update state
 * IN state: new state of ba_mp_t
 */
extern void ba_update_mp_state(ba_mp_t *ba_mp, uint16_t state)
{
	uint16_t mp_base_state = state & NODE_STATE_BASE;
	uint16_t mp_flags = state & NODE_STATE_FLAGS;

	if (!_initialized){
		error("Error, configuration not initialized, "
		      "calling ba_init(NULL, 1)");
		ba_init(NULL, 1);
	}

#ifdef HAVE_BG_Q
	debug2("ba_update_mp_state: new state of [%s] is %s",
	       ba_mp->coord_str, node_state_string(state));
#else
	debug2("ba_update_mp_state: new state of [%d] is %s",
	       ba_mp->coord[A],
	       node_state_string(state));
#endif

	/* basically set the mp as used */
	if ((mp_base_state == NODE_STATE_DOWN)
	    || (mp_flags & (NODE_STATE_DRAIN | NODE_STATE_FAIL)))
		ba_mp->used = BA_MP_USED_TRUE;
	else
		ba_mp->used = BA_MP_USED_FALSE;

	ba_mp->state = state;
}

extern void ba_setup_mp(ba_mp_t *ba_mp, bool track_down_mps)
{
	int i;
	uint16_t node_base_state = ba_mp->state & NODE_STATE_BASE;

	if (((node_base_state != NODE_STATE_DOWN)
	     && !(ba_mp->state & NODE_STATE_DRAIN)) || !track_down_mps)
		ba_mp->used = BA_MP_USED_FALSE;

	for (i=0; i<cluster_dims; i++){
		ba_mp->axis_switch[i].usage = BG_SWITCH_NONE;
	}
}

/*
 * copy info from a ba_mp, a direct memcpy of the ba_mp_t
 *
 * IN ba_mp: mp to be copied
 * Returned ba_mp_t *: copied info must be freed with destroy_ba_mp
 */
extern ba_mp_t *ba_copy_mp(ba_mp_t *ba_mp)
{
	ba_mp_t *new_ba_mp = (ba_mp_t *)xmalloc(sizeof(ba_mp_t));

	memcpy(new_ba_mp, ba_mp, sizeof(ba_mp_t));
	new_ba_mp->loc = xstrdup(ba_mp->loc);
	/* we have to set this or we would be pointing to the original */
	memset(new_ba_mp->next_mp, 0, sizeof(new_ba_mp->next_mp));

	return new_ba_mp;
}

/*
 * copy the path of the mps given
 *
 * IN mps List of ba_mp_t *'s: mps to be copied
 * OUT dest_mps List of ba_mp_t *'s: filled in list of mps
 * wiring.
 * Return on success SLURM_SUCCESS, on error SLURM_ERROR
 */
extern int copy_mp_path(List mps, List *dest_mps)
{
	int rc = SLURM_ERROR;

#ifdef HAVE_BGQ
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	ba_mp_t *ba_mp = NULL, *new_ba_mp = NULL;
	int dim;
	ba_switch_t *curr_switch = NULL, *new_switch = NULL;

	if (!mps)
		return SLURM_ERROR;
	if (!*dest_mps)
		*dest_mps = list_create(destroy_ba_mp);

	itr = list_iterator_create(mps);
	while ((ba_mp = (ba_mp_t *)list_next(itr))) {
		itr2 = list_iterator_create(*dest_mps);
		while ((new_ba_mp = (ba_mp_t *)list_next(itr2))) {
			if (memcmp(ba_mp->coord, new_ba_mp->coord,
				   sizeof(ba_mp->coord)))
				break;	/* we found it */
		}
		list_iterator_destroy(itr2);

		if (!new_ba_mp) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
				info("adding %s as a new mp",
				     ba_mp->coord_str);
			new_ba_mp = ba_copy_mp(ba_mp);
			ba_setup_mp(new_ba_mp, false);
			list_push(*dest_mps, new_ba_mp);

		}
		new_ba_mp->used = BA_MP_USED_TRUE;
		for(dim=0; dim<cluster_dims; dim++) {
			curr_switch = &ba_mp->axis_switch[dim];
			new_switch = &new_ba_mp->axis_switch[dim];
			if (curr_switch->usage & BG_SWITCH_OUT) {
				if (!_copy_the_path(*dest_mps, new_ba_mp,
						    ba_mp, new_ba_mp,
						    dim)) {
					rc = SLURM_ERROR;
					break;
				}
			}
		}

	}
	list_iterator_destroy(itr);
	rc = SLURM_SUCCESS;
#endif
	return rc;
}

/*
 * Try to allocate a block.
 *
 * IN - ba_request: allocation request
 * OUT - results: List of results of the allocation request.  Each
 * list entry will be a coordinate.  allocate_block will create the
 * list, but the caller must destroy it.
 *
 * return: success or error of request
 */
extern int allocate_block(select_ba_request_t* ba_request, List results)
{
	if (!_initialized){
		error("Error, configuration not initialized, "
		      "calling ba_init(NULL, 1)");
		ba_init(NULL, 1);
	}

	if (!ba_request){
		error("allocate_block Error, request not initialized");
		return 0;
	}

	// _backup_ba_system();
	if (_find_match(ba_request, results)){
		return 1;
	} else {
		return 0;
	}
}


/*
 * Admin wants to remove a previous allocation.
 * will allow Admin to delete a previous allocation retrival by letter code.
 */
extern int remove_block(List mps, int new_count, bool is_small)
{
	int dim;
	ba_mp_t* curr_ba_mp = NULL;
	ba_mp_t* ba_mp = NULL;
	ListIterator itr;

	itr = list_iterator_create(mps);
	while ((curr_ba_mp = (ba_mp_t*) list_next(itr))) {
		/* since the list that comes in might not be pointers
		   to the main list we need to point to that main list */
		ba_mp = &ba_system_ptr->grid
			[curr_ba_mp->coord[A]]
			[curr_ba_mp->coord[X]]
			[curr_ba_mp->coord[Y]]
			[curr_ba_mp->coord[Z]];

		ba_mp->used = false;
		ba_mp->color = 7;
		ba_mp->letter = '.';
		/* Small blocks don't use wires, and only have 1 mp,
		   so just break. */
		if (is_small)
			break;
		for(dim=0; dim<cluster_dims; dim++) {
			if (curr_ba_mp == ba_mp) {
				/* Remove the usage that was altered */
				ba_mp->axis_switch[dim].usage &=
					(~ba_mp->alter_switch[dim].usage);
				ba_mp->alter_switch[dim].usage =
					BG_SWITCH_NONE;
			} else {
				/* Just remove the usage set here */
				ba_mp->axis_switch[dim].usage &=
					(~curr_ba_mp->axis_switch[dim].usage);
			}
		}
	}
	list_iterator_destroy(itr);
	if (new_count == (int)NO_VAL) {
	} else if (new_count == -1)
		color_count--;
	else
		color_count=new_count;
	if (color_count < 0)
		color_count = 0;
	return 1;
}

/*
 * Admin wants to change something about a previous allocation.
 * will allow Admin to change previous allocation by giving the
 * letter code for the allocation and the variable to alter
 * (Not currently used in the system, update this if it is)
 */
extern int alter_block(List mps, int conn_type)
{
	/* int dim; */
/* 	ba_mp_t* ba_mp = NULL; */
/* 	ba_switch_t *curr_switch = NULL;  */
/* 	int size=0; */
/* 	char *name = NULL; */
/* 	ListIterator results_i;	 */

	return SLURM_ERROR;
	/* results_i = list_iterator_create(mps); */
/* 	while ((ba_mp = list_next(results_i)) != NULL) { */
/* 		ba_mp->used = false; */

/* 		for(dim=0;dim<cluster_dims;dim++) { */
/* 			curr_switch = &ba_mp->axis_switch[dim]; */
/* 			if (curr_switch->int_wire[0].used) { */
/* 				_reset_the_path(curr_switch, 0, 1, dim); */
/* 			} */
/* 		} */
/* 		size++; */
/* 	} */
/* 	list_iterator_destroy(results_i); */
/* 	if ((name = _set_internal_wires(mps, size, conn_type)) == NULL) */
/* 		return SLURM_ERROR; */
/* 	else { */
/* 		xfree(name); */
/* 		return SLURM_SUCCESS; */
/* 	} */
}

/*
 * After a block is deleted or altered following allocations must
 * be redone to make sure correct path will be used in the real system
 * (Not currently used in the system, update this if it is)
 */
extern int redo_block(List mps, uint16_t *geo, int conn_type, int new_count)
{
       	/* ba_mp_t* ba_mp; */
	/* char *name = NULL; */

	/* ba_mp = (ba_mp_t *)list_peek(mps); */
	/* if (!ba_mp) */
	/* 	return SLURM_ERROR; */

	/* remove_block(mps, new_count, conn_type); */
	/* list_delete_all(mps, &empty_null_destroy_list, (void *)""); */

	/* name = set_bg_block(mps, ba_mp->coord, geo, conn_type); */
	/* if (!name) */
	/* 	return SLURM_ERROR; */
	/* else { */
	/* 	xfree(name); */
	/* 	return SLURM_SUCCESS; */
	/* } */
	return SLURM_SUCCESS;
}

/*
 * Used to set a block into a virtual system.  The system can be
 * cleared first and this function sets all the wires and midplanes
 * used in the mplist given.  The mplist is a list of ba_mp_t's
 * that are already set up.  This is very handly to test if there are
 * any passthroughs used by one block when adding another block that
 * also uses those wires, and neither use any overlapping
 * midplanes. Doing a simple bitmap & will not reveal this.
 *
 * Returns SLURM_SUCCESS if mplist fits into system without
 * conflict, and SLURM_ERROR if mplist conflicts with something
 * already in the system.
 */
extern int check_and_set_mp_list(List mps)
{
	int rc = SLURM_ERROR;

#ifdef HAVE_BG_Q
	int i, j;
	ba_switch_t *ba_switch = NULL, *curr_ba_switch = NULL;
	ba_mp_t *ba_mp = NULL, *curr_ba_mp = NULL;
	ListIterator itr = NULL;

	if (!mps)
		return rc;

	itr = list_iterator_create(mps);
	while ((ba_mp = list_next(itr))) {
		/* info("checking %c%c%c", */
/* 		     ba_mp->coord[X],  */
/* 		     ba_mp->coord[Y], */
/* 		     ba_mp->coord[Z]); */

		curr_ba_mp = &ba_system_ptr->
			grid[ba_mp->coord[A]]
			[ba_mp->coord[X]]
			[ba_mp->coord[Y]]
			[ba_mp->coord[Z]];

		if (ba_mp->used && curr_ba_mp->used) {
			/* Only error if the midplane isn't already
			 * marked down or in a error state outside of
			 * the bluegene block.
			 */
			uint16_t base_state, mp_flags;
			base_state = curr_ba_mp->state & NODE_STATE_BASE;
			mp_flags = curr_ba_mp->state & NODE_STATE_FLAGS;
			if (!(mp_flags & (NODE_STATE_DRAIN | NODE_STATE_FAIL))
			    && (base_state != NODE_STATE_DOWN)) {
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
					info("I have already been to "
					     "this mp %s %s",
					     ba_mp->coord_str,
					     node_state_string(
						     curr_ba_mp->state));
				rc = SLURM_ERROR;
				goto end_it;
			}
		}

		if (ba_mp->used)
			curr_ba_mp->used = BA_MP_USED_TRUE;
		for(i=0; i<cluster_dims; i++) {
			ba_switch = &ba_mp->axis_switch[i];
			curr_ba_switch = &curr_ba_mp->axis_switch[i];
			//info("checking dim %d", i);

			if (ba_switch->usage == BG_SWITCH_NONE)
				continue;

			if (switch_overlap(ba_switch->usage,
					   curr_ba_switch->usage)) {
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
					info("%s dim %d is already in "
					     "use the way we want to use it."
					     "%u already at %u",
					     ba_mp->coord_str, i,
					     ba_switch->usage,
					     curr_ba_switch->usage);
				rc = SLURM_ERROR;
				goto end_it;
			}

			info("setting %s dim %d to from %d to %d",
			     ba_mp->coord_str, i,
			     curr_ba_switch->usage,
			     (curr_ba_switch->usage | ba_switch->usage));
			curr_ba_switch->usage |= ba_switch->usage;
		}
	}
	rc = SLURM_SUCCESS;
end_it:
	list_iterator_destroy(itr);
#endif
	return rc;
}

/*
 * Used to find, and set up midplanes and the wires in the virtual
 * system and return them in List results
 *
 * IN/OUT results - a list with a NULL destroyer filled in with
 *        midplanes and wires set to create the block with the api. If
 *        only interested in the hostlist NULL can be excepted also.
 * IN start - where to start the allocation.
 * IN geometry - the requested geometry of the block.
 * IN conn_type - mesh, torus, or small.
 *
 * RET char * - hostlist of midplanes results represent must be
 *     xfreed.  NULL on failure
 */
extern char *set_bg_block(List results, uint16_t *start,
			  uint16_t *geometry, uint16_t *conn_type)
{
	List main_mps = NULL;
	char *name = NULL;
	ba_mp_t* ba_mp = NULL;
	ba_mp_t *check_mp[cluster_dims];
	int size = 1, dim;
	uint16_t block_end[cluster_dims];
	uint16_t pass_end[cluster_dims];
	int coords[cluster_dims];

	if (cluster_dims == 1) {
		if (start[A] >= DIM_SIZE[A])
			return NULL;
		size = geometry[X];
		ba_mp = &ba_system_ptr->grid[start[A]][0][0][0];
	} else {
		for (dim=0; dim<cluster_dims; dim++) {
			if (start[dim] >= DIM_SIZE[dim])
				return NULL;
			if (geometry[dim] <= 0) {
				error("problem with geometry of %c in dim %d, "
				      "needs to be at least 1",
				      alpha_num[geometry[dim]], dim);
				return NULL;
			}
			size *= geometry[dim];
		}

		ba_mp = &ba_system_ptr->grid
			[start[A]][start[X]][start[Y]][start[Z]];
		/* info("looking at %s", ba_mp->coord_str); */
	}

	if (!ba_mp)
		return NULL;

	if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
		info("trying mp %s %c%c%c%c %d",
		     ba_mp->coord_str,
		     alpha_num[geometry[A]],
		     alpha_num[geometry[X]],
		     alpha_num[geometry[Y]],
		     alpha_num[geometry[Z]],
		     conn_type[A]);

	if (conn_type[A] >= SELECT_SMALL) {
		/* adding the ba_mp and ending */
		if (results)
			list_append(results, ba_mp);

		ba_mp->used = BA_MP_USED_TRUE;
		name = xstrdup(ba_mp->coord_str);
		if (ba_mp->letter == '.') {
			ba_mp->letter = letters[color_count%62];
			ba_mp->color = colors[color_count%6];
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("count %d setting letter = %c "
				     "color = %d",
				     color_count,
				     ba_mp->letter,
				     ba_mp->color);
			color_count++;
		}
		goto end_it;
	}

	main_mps = list_create(NULL);
	/* This midplane should have already been checked if it was in
	   use or not */
	list_append(main_mps, ba_mp);

	/* set the end to the start and the _find_path will increase each dim.*/
	memcpy(block_end, start, sizeof(block_end));
	memcpy(pass_end, start, sizeof(pass_end));
	for (dim=0; dim<cluster_dims; dim++) {
		if (!_find_path(main_mps, ba_mp, dim,
				geometry[dim], conn_type[dim], &block_end[dim],
				&pass_end[dim])) {
			goto end_it;
		}
	}
	info("complete box is  %c%c%c%c x %c%c%c%c pass to %c%c%c%c",
	     alpha_num[start[A]],
	     alpha_num[start[X]],
	     alpha_num[start[Y]],
	     alpha_num[start[Z]],
	     alpha_num[block_end[A]],
	     alpha_num[block_end[X]],
	     alpha_num[block_end[Y]],
	     alpha_num[block_end[Z]],
	     alpha_num[pass_end[A]],
	     alpha_num[pass_end[X]],
	     alpha_num[pass_end[Y]],
	     alpha_num[pass_end[Z]]);

	if (_fill_in_coords(main_mps, A, ba_mp, check_mp,
			    start, block_end, pass_end, coords) == -1)
		goto end_it;

	/* Success */
	if (results)
		name = _copy_from_main(main_mps, results);
	else
		name = _reset_altered_mps(main_mps);

end_it:

	if (main_mps) {
		list_destroy(main_mps);
		main_mps = NULL;
	}
	if (name!=NULL) {
		debug2("name = %s", name);
	} else {
		debug2("can't allocate");
		xfree(name);
	}

	return name;
}

/*
 * Resets the virtual system to a virgin state.  If track_down_mps is set
 * then those midplanes are not set to idle, but kept in a down state.
 */
extern void reset_ba_system(bool track_down_mps)
{
	int a, x, y, z;

	for (a = 0; a < DIM_SIZE[A]; a++)
		for (x = 0; x < DIM_SIZE[X]; x++)
			for (y = 0; y < DIM_SIZE[Y]; y++)
				for (z = 0; z < DIM_SIZE[Z]; z++) {
					ba_mp_t *ba_mp = &ba_system_ptr->
						grid[a][x][y][z];
					ba_setup_mp(ba_mp, track_down_mps);
				}
}

/*
 * Used to set all midplanes in a special used state except the ones
 * we are able to use in a new allocation.
 *
 * IN: hostlist of midplanes we do not want
 * RET: SLURM_SUCCESS on success, or SLURM_ERROR on error
 *
 * Note: Need to call reset_all_removed_mps before starting another
 * allocation attempt after
 */
extern int removable_set_mps(char *mps)
{
#ifdef HAVE_BGQ
	int j=0, number;
	int a,x;
	int y,z;
	int start[cluster_dims];
        int end[cluster_dims];
	int coords[cluster_dims];

	if (!mps)
		return SLURM_ERROR;

	while (mps[j] != '\0') {
		if ((mps[j] == '[' || mps[j] == ',')
		    && (mps[j+10] == ']' || mps[j+8] == ',')
		    && (mps[j+5] == 'x' || mps[j+4] == '-')) {

			j++;
			number = xstrntol(mps + j, &p, cluster_dims,
					  cluster_base);
			hostlist_parse_int_to_array(
				number, start, cluster_dims, cluster_base);
			j += 4;
			number = xstrntol(mps + j, &p, cluster_dims,
					  cluster_base);
			hostlist_parse_int_to_array(
				number, end, cluster_dims, cluster_base);
			j += 3;

			_internal_removable_set_mps(A, start, end, coords, 1);

			if (mps[j] != ',')
				break;
			j--;
		} else if ((mps[j] >= '0' && mps[j] <= '9')
			   || (mps[j] >= 'A' && mps[j] <= 'D')) {
			number = xstrntol(mps + j, &p, cluster_dims,
					  cluster_base);
			hostlist_parse_int_to_array(
				number, start, cluster_dims, cluster_base);
			a = start[A];
			x = start[X];
			y = start[Y];
			z = start[Z];
			j+=3;
			if (ba_system_ptr->grid[a][x][y][z].used
			    == BA_MP_USED_FALSE)
				ba_system_ptr->grid[a][x][y][z].used =
					BA_MP_USED_TEMP;

			if (mps[j] != ',')
				break;
			j--;
		}
		j++;
	}
#endif
 	return SLURM_SUCCESS;
}

/*
 * Resets the virtual system to the pervious state before calling
 * removable_set_mps, or set_all_mps_except.
 */
extern int reset_all_removed_mps()
{
	int start[cluster_dims];
  	int coords[cluster_dims];

	memset(start, 0, sizeof(start));
	_internal_removable_set_mps(A, start, DIM_SIZE, coords, 0);

	return SLURM_SUCCESS;
}

/*
 * IN: hostlist of midplanes we want to be able to use, mark all
 *     others as used.
 * RET: SLURM_SUCCESS on success, or SLURM_ERROR on error
 *
 * Need to call reset_all_removed_mps before starting another
 * allocation attempt if possible use removable_set_mps since it is
 * faster. It does basically the opposite of this function. If you
 * have to come up with this list though it is faster to use this
 * function than if you have to call bitmap2node_name since that is slow.
 */
extern int set_all_mps_except(char *mps)
{
	int a, x, y, z;
	hostlist_t hl = hostlist_create(mps);
	char *host = NULL, *numeric = NULL;
	int number, coords[HIGHEST_DIMENSIONS];

	memset(coords, 0, sizeof(coords));

	while ((host = hostlist_shift(hl))){
		numeric = host;
		number = 0;
		while (numeric) {
			if (numeric[0] < '0' || numeric[0] > 'D'
			    || (numeric[0] > '9'
				&& numeric[0] < 'A')) {
				numeric++;
				continue;
			}
			number = xstrntol(numeric, &p, cluster_dims,
					  cluster_base);
			break;
		}
		hostlist_parse_int_to_array(
			number, coords, cluster_dims, cluster_base);
		ba_system_ptr->grid
			[coords[A]][coords[X]][coords[Y]][coords[Z]].state
			|= NODE_RESUME;
		free(host);
	}
	hostlist_destroy(hl);

	for (a = 0; a < DIM_SIZE[A]; a++)
		for (x = 0; x < DIM_SIZE[X]; x++)
			for (y = 0; y < DIM_SIZE[Y]; y++)
				for (z = 0; z <= DIM_SIZE[Z]; z++) {
					if (ba_system_ptr->grid
					    [a][x][y][z].state
					    & NODE_RESUME) {
						/* clear the bit and mark as unused */
						ba_system_ptr->grid
							[a][x][y][z].state &=
							~NODE_RESUME;
					} else if (!ba_system_ptr->grid
						   [a][x][y][z].used) {
						ba_system_ptr->grid
							[a][x][y][z].used = BA_MP_USED_TEMP;
					}
				}

 	return SLURM_SUCCESS;
}

/*
 * set values of every grid point (used in smap)
 */
extern void init_grid(node_info_msg_t * node_info_ptr)
{
	int i = 0, j, a, x, y, z;
	ba_mp_t *ba_mp = NULL;

	if (!node_info_ptr) {
		for (a = 0; a < DIM_SIZE[A]; a++) {
			for (x = 0; x < DIM_SIZE[X]; x++) {
				for (y = 0; y < DIM_SIZE[Y]; y++) {
					for (z = 0; z < DIM_SIZE[Z]; z++) {
						ba_mp = &ba_system_ptr->grid
							[a][x][y][z];
						ba_mp->color = 7;
						ba_mp->letter = '.';
						ba_mp->state = NODE_STATE_IDLE;
						ba_mp->index = i++;
					}
				}
			}
		}
		return;
	}

	for (j = 0; j < (int)node_info_ptr->record_count; j++) {
		int coord[cluster_dims];
		node_info_t *node_ptr = &node_info_ptr->node_array[j];
		if (!node_ptr->name)
			continue;

		memset(coord, 0, sizeof(coord));
		if (cluster_dims == 1) {
			coord[0] = j;
		} else {
			if ((i = strlen(node_ptr->name)) < 4)
				continue;
			for (x=0; x<cluster_dims; x++)
				coord[x] = _coord(node_ptr->name[i-(4+x)]);
		}

		for (x=0; x<cluster_dims; x++)
			if (coord[x] < 0)
				break;
		if (x < cluster_dims)
			continue;

		ba_mp = &ba_system_ptr->grid
			[coord[A]][coord[X]][coord[Y]][coord[Z]];
		ba_mp->index = j;
		ba_mp->state = node_ptr->node_state;

		if (IS_NODE_DOWN(node_ptr) || IS_NODE_DRAIN(node_ptr)) {
			ba_mp->color = 0;
			ba_mp->letter = '#';
			ba_update_mp_state(ba_mp, node_ptr->node_state);
		} else {
			ba_mp->color = 7;
			ba_mp->letter = '.';
		}
	}
}

/*
 * find a base blocks bg location
 */
extern uint16_t *find_mp_loc(char* mp_id)
{
	char *check = NULL;
	uint32_t a, x, y, z;
	ba_mp_t *ba_mp = NULL;

	bridge_setup_system();

	check = xstrdup(mp_id);
	/* with BGP they changed the names of the rack midplane action from
	 * R000 to R00-M0 so we now support both formats for each of the
	 * systems */
#ifdef HAVE_BGL
	if (check[3] == '-') {
		if (check[5]) {
			check[3] = check[5];
			check[4] = '\0';
		}
	}

	if ((check[1] < '0' || check[1] > '9')
	    || (check[2] < '0' || check[2] > '9')
	    || (check[3] < '0' || check[3] > '9')) {
		error("%s is not a valid Rack-Midplane (i.e. R000)", mp_id);
		goto cleanup;
	}

#else
	if (check[3] != '-') {
		xfree(check);
		check = xstrdup_printf("R%c%c-M%c",
				       mp_id[1], mp_id[2], mp_id[3]);
	}

	if ((check[1] < '0' || check[1] > '9')
	    || (check[2] < '0' || check[2] > '9')
	    || (check[5] < '0' || check[5] > '9')) {
		error("%s is not a valid Rack-Midplane (i.e. R00-M0)", mp_id);
		goto cleanup;
	}
#endif

	for (a = 0; a < DIM_SIZE[A]; a++)
		for (x = 0; x < DIM_SIZE[X]; x++)
			for (y = 0; y < DIM_SIZE[Y]; y++)
				for (z = 0; z <= DIM_SIZE[Z]; z++)
					if (!strcasecmp(ba_system_ptr->
							grid[a][x][y][z].loc,
							check)) {
						ba_mp = &ba_system_ptr->
							grid[a][x][y][z];
						goto cleanup; /* we found it */
					}

cleanup:
	xfree(check);

	if (ba_mp != NULL)
		return ba_mp->coord;
	else
		return NULL;
}

/*
 * find a rack/midplace location
 */
extern char *find_mp_rack_mid(char* axyz)
{
	int number;
	int coord[cluster_dims];
	int len = strlen(axyz);
	int dim;

	len -= 4;
	if (len<0)
		return NULL;

	if ((axyz[len] < '0' || axyz[len] > '9')
	    || (axyz[len+1] < '0' || axyz[len+1] > '9')
	    || (axyz[len+2] < '0' || axyz[len+2] > '9')
	    || (axyz[len+3] < '0' || axyz[len+3] > '9')) {
		error("%s is not a valid Location (i.e. 0000)", axyz);
		return NULL;
	}

	number = xstrntol(axyz + len, &p, cluster_dims, cluster_base);
	hostlist_parse_int_to_array(number, coord, cluster_dims, cluster_base);

	for (dim=0; dim<cluster_dims; dim++) {
		if (coord[dim] > DIM_SIZE[dim]) {
			error("This location %s is not possible "
			      "in our system %c%c%c%c",
			      axyz,
			      alpha_num[DIM_SIZE[A]],
			      alpha_num[DIM_SIZE[X]],
			      alpha_num[DIM_SIZE[Y]],
			      alpha_num[DIM_SIZE[Z]]);
			return NULL;
		}
	}

	bridge_setup_system();

	return ba_system_ptr->grid[coord[A]][coord[X]][coord[Y]][coord[Z]].loc;
}


/*
 * get the used wires for a block out of the database and return the
 * node list.  The block_ptr here must be gotten with bridge_get_block
 * not bridge_get_block_info, if you are looking to recover from
 * before.  If you are looking to start clean it doesn't matter.
 */
extern List get_and_set_block_wiring(char *bg_block_id,
				     char *block_ptr)
{
#if defined HAVE_BG_FILES && defined HAVE_BG_L_P
	int rc, i, j;
	int cnt = 0;
	int switch_cnt = 0;
	rm_switch_t *curr_switch = NULL;
	rm_BP_t *curr_mp = NULL;
	char *switchid = NULL;
	rm_connection_t curr_conn;
	int dim;
	ba_mp_t *ba_mp = NULL;
	ba_switch_t *ba_switch = NULL;
	uint16_t *geo = NULL;
	List results = list_create(destroy_ba_mp);
	ListIterator itr = NULL;

	if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
		info("getting info for block %s", bg_block_id);

	if ((rc = bridge_get_data(block_ptr, RM_PartitionSwitchNum,
				  &switch_cnt)) != STATUS_OK) {
		error("bridge_get_data(RM_PartitionSwitchNum): %s",
		      bridge_err_str(rc));
		goto end_it;
	}
	if (!switch_cnt) {
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("no switch_cnt");
		if ((rc = bridge_get_data(block_ptr,
					  RM_PartitionFirstBP,
					  &curr_mp))
		    != STATUS_OK) {
			error("bridge_get_data: "
			      "RM_PartitionFirstBP: %s",
			      bridge_err_str(rc));
			goto end_it;
		}
		if ((rc = bridge_get_data(curr_mp, RM_BPID, &switchid))
		    != STATUS_OK) {
			error("bridge_get_data: RM_SwitchBPID: %s",
			      bridge_err_str(rc));
			goto end_it;
		}

		geo = find_mp_loc(switchid);
		if (!geo) {
			error("find_mp_loc: mpid %s not known", switchid);
			goto end_it;
		}
		ba_mp = xmalloc(sizeof(ba_mp_t));
		list_push(results, ba_mp);
		ba_mp->coord[X] = geo[X];
		ba_mp->coord[Y] = geo[Y];
		ba_mp->coord[Z] = geo[Z];

		ba_mp->used = BA_MP_USED_TRUE;
		return results;
	}
	for (i=0; i<switch_cnt; i++) {
		if (i) {
			if ((rc = bridge_get_data(block_ptr,
						  RM_PartitionNextSwitch,
						  &curr_switch))
			    != STATUS_OK) {
				error("bridge_get_data: "
				      "RM_PartitionNextSwitch: %s",
				      bridge_err_str(rc));
				goto end_it;
			}
		} else {
			if ((rc = bridge_get_data(block_ptr,
						  RM_PartitionFirstSwitch,
						  &curr_switch))
			    != STATUS_OK) {
				error("bridge_get_data: "
				      "RM_PartitionFirstSwitch: %s",
				      bridge_err_str(rc));
				goto end_it;
			}
		}
		if ((rc = bridge_get_data(curr_switch, RM_SwitchDim, &dim))
		    != STATUS_OK) {
			error("bridge_get_data: RM_SwitchDim: %s",
			      bridge_err_str(rc));
			goto end_it;
		}
		if ((rc = bridge_get_data(curr_switch, RM_SwitchBPID,
					  &switchid))
		    != STATUS_OK) {
			error("bridge_get_data: RM_SwitchBPID: %s",
			      bridge_err_str(rc));
			goto end_it;
		}

		geo = find_mp_loc(switchid);
		if (!geo) {
			error("find_mp_loc: mpid %s not known", switchid);
			goto end_it;
		}

		if ((rc = bridge_get_data(curr_switch, RM_SwitchConnNum, &cnt))
		    != STATUS_OK) {
			error("bridge_get_data: RM_SwitchBPID: %s",
			      bridge_err_str(rc));
			goto end_it;
		}
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
			info("switch id = %s dim %d conns = %d",
			     switchid, dim, cnt);

		itr = list_iterator_create(results);
		while ((ba_mp = list_next(itr))) {
			if (ba_mp->coord[X] == geo[X] &&
			    ba_mp->coord[Y] == geo[Y] &&
			    ba_mp->coord[Z] == geo[Z])
				break;	/* we found it */
		}
		list_iterator_destroy(itr);
		if (!ba_mp) {
			ba_mp = xmalloc(sizeof(ba_mp_t));

			list_push(results, ba_mp);
			ba_mp->coord[X] = geo[X];
			ba_mp->coord[Y] = geo[Y];
			ba_mp->coord[Z] = geo[Z];
		}
		ba_switch = &ba_mp->axis_switch[dim];
		for (j=0; j<cnt; j++) {
			if (j) {
				if ((rc = bridge_get_data(
					     curr_switch,
					     RM_SwitchNextConnection,
					     &curr_conn))
				    != STATUS_OK) {
					error("bridge_get_data: "
					      "RM_SwitchNextConnection: %s",
					      bridge_err_str(rc));
					goto end_it;
				}
			} else {
				if ((rc = bridge_get_data(
					     curr_switch,
					     RM_SwitchFirstConnection,
					     &curr_conn))
				    != STATUS_OK) {
					error("bridge_get_data: "
					      "RM_SwitchFirstConnection: %s",
					      bridge_err_str(rc));
					goto end_it;
				}
			}
			switch(curr_conn.p1) {
			case RM_PORT_S1:
				curr_conn.p1 = 1;
				break;
			case RM_PORT_S2:
				curr_conn.p1 = 2;
				break;
			case RM_PORT_S4:
				curr_conn.p1 = 4;
				break;
			default:
				error("1 unknown port %d",
				      _port_enum(curr_conn.p1));
				goto end_it;
			}

			switch(curr_conn.p2) {
			case RM_PORT_S0:
				curr_conn.p2 = 0;
				break;
			case RM_PORT_S3:
				curr_conn.p2 = 3;
				break;
			case RM_PORT_S5:
				curr_conn.p2 = 5;
				break;
			default:
				error("2 unknown port %d",
				      _port_enum(curr_conn.p2));
				goto end_it;
			}

			if (curr_conn.p1 == 1 && dim == B) {
				if (ba_mp->used) {
					debug("I have already been to "
					      "this node %c%c%c%c",
					      alpha_num[geo[A]],
					      alpha_num[geo[X]],
					      alpha_num[geo[Y]],
					      alpha_num[geo[Z]]);
					goto end_it;
				}
				ba_mp->used = BA_MP_USED_TRUE;
			}
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("connection going from %d -> %d",
				     curr_conn.p1, curr_conn.p2);

			if (ba_switch->int_wire[curr_conn.p1].used) {
				debug("%c%c%c%c dim %d port %d "
				      "is already in use",
				      alpha_num[geo[A]],
				      alpha_num[geo[X]],
				      alpha_num[geo[Y]],
				      alpha_num[geo[Z]],
				      dim,
				      curr_conn.p1);
				goto end_it;
			}
			ba_switch->int_wire[curr_conn.p1].used = 1;
			ba_switch->int_wire[curr_conn.p1].port_tar
				= curr_conn.p2;

			if (ba_switch->int_wire[curr_conn.p2].used) {
				debug("%c%c%c%c dim %d port %d "
				      "is already in use",
				      alpha_num[geo[A]],
				      alpha_num[geo[X]],
				      alpha_num[geo[Y]],
				      alpha_num[geo[Z]],
				      dim,
				      curr_conn.p2);
				goto end_it;
			}
			ba_switch->int_wire[curr_conn.p2].used = 1;
			ba_switch->int_wire[curr_conn.p2].port_tar
				= curr_conn.p1;
		}
	}
	return results;
end_it:
	list_destroy(results);
	return NULL;
#else
	return NULL;
#endif

}

/* */
extern int validate_coord(uint16_t *coord)
{
	int dim;

	for (dim=0; dim < cluster_dims; dim++) {
		if (coord[dim] >= REAL_DIM_SIZE[dim]) {
			error("got coord %c%c%c%c greater than system dims "
			      "%c%c%c%c",
			      alpha_num[coord[A]],
			      alpha_num[coord[X]],
			      alpha_num[coord[Y]],
			      alpha_num[coord[Z]],
			      alpha_num[REAL_DIM_SIZE[A]],
			      alpha_num[REAL_DIM_SIZE[X]],
			      alpha_num[REAL_DIM_SIZE[Y]],
			      alpha_num[REAL_DIM_SIZE[Z]]);
			return 0;
		} else if (coord[dim] >= DIM_SIZE[dim]) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("got coord %c%c%c%c greater than what "
				     "we are using %c%c%c%c",
				     alpha_num[coord[A]],
				     alpha_num[coord[X]],
				     alpha_num[coord[Y]],
				     alpha_num[coord[Z]],
				     alpha_num[DIM_SIZE[A]],
				     alpha_num[DIM_SIZE[X]],
				     alpha_num[DIM_SIZE[Y]],
				     alpha_num[DIM_SIZE[Z]]);
			return 0;
		}
	}

	return 1;
}

extern char *ba_switch_usage_str(uint16_t usage)
{
	switch (usage) {
	case BG_SWITCH_NONE:
		return "None";
	case BG_SWITCH_WRAPPED_PASS:
		return "WrappedPass";
	case BG_SWITCH_TORUS:
		return "FullTorus";
	case BG_SWITCH_PASS:
		return "Passthrough";
	case BG_SWITCH_WRAPPED:
		return "Wrapped";
	case (BG_SWITCH_OUT | BG_SWITCH_OUT_PASS):
		return "OutLeaving";
	case BG_SWITCH_OUT:
		return "Out";
	case (BG_SWITCH_IN | BG_SWITCH_IN_PASS):
		return "InComming";
	case BG_SWITCH_IN:
		return "In";
	default:
		error("unknown switch usage %u", usage);
		break;
	}
	return "unknown";
}

/*
 * This function is here to check options for rotating and elongating
 * and set up the request based on the count of each option
 */
static int _check_for_options(select_ba_request_t* ba_request)
{
	int temp;
	int set=0;
	uint16_t *geo = NULL;
	ListIterator itr;

	if (ba_request->rotate) {
	rotate_again:
		debug2("Rotating! %d",ba_request->rotate_count);

		if (ba_request->rotate_count==(cluster_dims-1)) {
			temp=ba_request->geometry[A];
			ba_request->geometry[A] = ba_request->geometry[Z];
			ba_request->geometry[Z] = temp;
			ba_request->rotate_count++;
			set=1;

		} else if (ba_request->rotate_count<(cluster_dims*2)) {
			temp=ba_request->geometry[X];
			ba_request->geometry[A] = ba_request->geometry[X];
			ba_request->geometry[X] = ba_request->geometry[Y];
			ba_request->geometry[Y] = ba_request->geometry[Z];
			ba_request->geometry[Z] = temp;
			ba_request->rotate_count++;
			set=1;
		} else
			ba_request->rotate = false;
		if (set) {
			int i;
			for (i = 0; i < cluster_dims; i++)
				if (ba_request->geometry[i] > DIM_SIZE[A]) {
					set = 0;
					goto rotate_again;
				}
			return 1;
		}
	}
	if (ba_request->elongate) {
	elongate_again:
		debug2("Elongating! %d",ba_request->elongate_count);
		ba_request->rotate_count=0;
		ba_request->rotate = true;

		set = 0;
		itr = list_iterator_create(ba_request->elongate_geos);
		for(set=0; set<=ba_request->elongate_count; set++)
			geo = (uint16_t *)list_next(itr);
		list_iterator_destroy(itr);
		if (geo == NULL)
			return 0;
		ba_request->elongate_count++;
		ba_request->geometry[A] = geo[A];
		ba_request->geometry[X] = geo[X];
		ba_request->geometry[Y] = geo[Y];
		ba_request->geometry[Z] = geo[Z];
		if (ba_request->geometry[A]<=DIM_SIZE[A]
		    && ba_request->geometry[X]<=DIM_SIZE[X]
		    && ba_request->geometry[Y]<=DIM_SIZE[Y]
		    && ba_request->geometry[Z]<=DIM_SIZE[Z]) {
			return 1;
		} else
			goto elongate_again;

	}
	return 0;
}

/*
 * grab all the geometries that we can get and append them to the list geos
 */
static int _append_geo(uint16_t *geometry, List geos, int rotate)
{
	ListIterator itr;
	uint16_t *geo_ptr = NULL;
	uint16_t *geo = NULL;
	int temp_geo;
	int i, j;

	if (rotate) {
		for (i = (cluster_dims - 1); i >= 0; i--) {
			for (j = 1; j <= i; j++) {
				if ((geometry[j-1] > geometry[j])
				    && (geometry[j] <= DIM_SIZE[j-i])
				    && (geometry[j-1] <= DIM_SIZE[j])) {
					temp_geo = geometry[j-1];
					geometry[j-1] = geometry[j];
					geometry[j] = temp_geo;
				}
			}
		}
	}
	itr = list_iterator_create(geos);
	while ((geo_ptr = (uint16_t *)list_next(itr)) != NULL) {
		if (geometry[A] == geo_ptr[A]
		    && geometry[X] == geo_ptr[X]
		    && geometry[Y] == geo_ptr[Y]
		    && geometry[Z] == geo_ptr[Z])
			break;

	}
	list_iterator_destroy(itr);

	if (geo_ptr == NULL) {
		geo = (uint16_t *)xmalloc(sizeof(uint16_t)*cluster_dims);
		geo[A] = geometry[A];
		geo[X] = geometry[X];
		geo[Y] = geometry[Y];
		geo[Z] = geometry[Z];
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("adding geo %c%c%c%c",
			     alpha_num[geo[A]], alpha_num[geo[X]],
			     alpha_num[geo[Y]], alpha_num[geo[Z]]);
		list_append(geos, geo);
	}
	return 1;
}

static void _internal_removable_set_mps(int level, int *start,
					int *end, int *coords, bool mark)
{
	ba_mp_t *curr_mp;

	if (level > cluster_dims)
		return;

	if (level < cluster_dims) {
		for (coords[level] = start[level];
		     coords[level] <= end[level];
		     coords[level]++) {
			/* handle the outter dims here */
			_internal_removable_set_mps(
				level+1, start, end, coords, mark);
		}
		return;
	}

	curr_mp = &ba_system_ptr->grid
		[coords[A]][coords[X]][coords[Y]][coords[Z]];
	if (mark) {
		if (curr_mp->used == BA_MP_USED_FALSE)
			curr_mp->used = BA_MP_USED_TEMP;
	} else {
		if (curr_mp->used == BA_MP_USED_TEMP)
			curr_mp->used = BA_MP_USED_FALSE;
	}
}

/*
 * Fill in the paths and extra midplanes we need for the block.
 * Basically copy the x path sent in with the start_list in each C and
 * D dimension filling in every midplane for the block and then
 * completing the C and D wiring, tying the whole block together.
 *
 * IN/OUT results - total list of midplanes after this function
 *        returns successfully.  Should be
 *        an exact copy of the start_list at first.
 * IN start_list - exact copy of results at first, This should only be
 *        a list of midplanes on the B dim.  We will work off this and
 *        the geometry to fill in this wiring for the B dim in all the
 *        C and D coords.
 * IN geometry - What the block looks like
 * IN conn_type - Mesh or Torus
 *
 * RET: 0 on failure 1 on success
 */
static int _fill_in_coords(List results, int level, ba_mp_t *start_mp,
			   ba_mp_t **check_mp, uint16_t *block_start,
			   uint16_t *block_end, uint16_t *pass_end,
			   int *coords)
{
	int dim;
	int count_over = 0;
	uint16_t used = 0;
	ba_mp_t *curr_mp;

	if (level > cluster_dims)
		return -1;

	if (level < cluster_dims) {
		check_mp[level] = start_mp;
		for (coords[level] = block_start[level];
		     coords[level] <= pass_end[level];
		     coords[level]++) {
			/* handle the outter dims here */
			if (_fill_in_coords(
				    results, level+1, start_mp,
				    check_mp, block_start,
				    block_end, pass_end, coords) == -1)
				return -1;
			check_mp[level] = check_mp[level]->next_mp[level];
		}
		return 1;
	}

	curr_mp = &ba_system_ptr->grid
		[coords[A]][coords[X]][coords[Y]][coords[Z]];

	for (dim=0; dim<cluster_dims; dim++) {
		/* If we get over 2 in any dim that we are
		   greater here we are pass anything we need to
		   passthrough, so break.
		*/
		if (check_mp[dim]->used & BA_MP_USED_PASS_BIT) {
			if (curr_mp->coord[dim] > block_end[dim]) {
				count_over++;
				if (count_over > 1)
					break;
			}
			used = check_mp[dim]->used;
		}
	}
	if (count_over > 1) {
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("skipping non-used %s", curr_mp->coord_str);
		return 1;
	}

	for (dim=0; dim<cluster_dims; dim++) {
		int rc;

		/* If we are passing though skip all except the
		   actual passthrough dim.
		*/
		if ((used & BA_MP_USED_PASS_BIT)
		    && (check_mp[dim]->used != used)) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("skipping here %s(%d)",
				     curr_mp->coord_str, dim);
			continue;
		}

		/* if 1 is returned we haven't visited this mp yet,
		   and need to add it to the list
		*/
		if ((rc = _copy_ba_switch(curr_mp, check_mp[dim], dim)) == -1)
			return rc;
		else if (rc == 1)
			list_append(results, curr_mp);
	}
	return 1;
}

static char *_copy_from_main(List main_mps, List ret_list)
{
	ListIterator itr;
	ba_mp_t *ba_mp;
	ba_mp_t *new_mp;
	int dim;
	char *name = NULL;
	hostlist_t hostlist = NULL;

	if (!main_mps || !ret_list)
		return NULL;

	if (!(itr = list_iterator_create(main_mps)))
		fatal("NULL itr returned");
	while ((ba_mp = list_next(itr))) {
		if (!(ba_mp->used & BA_MP_USED_ALTERED)) {
			error("_copy_from_main: it appears we "
			      "have a mp %s added that wasn't altered %d",
			      ba_mp->coord_str, ba_mp->used);
			continue;
		}

		new_mp = ba_copy_mp(ba_mp);
		list_append(ret_list, new_mp);
		/* copy and reset the path */
		memcpy(new_mp->axis_switch, new_mp->alter_switch,
		       sizeof(ba_mp->axis_switch));
		memset(new_mp->alter_switch, 0, sizeof(new_mp->alter_switch));
		if (new_mp->used & BA_MP_USED_PASS_BIT) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("mp %s is used for passthrough",
				     new_mp->coord_str);
			new_mp->used = BA_MP_USED_FALSE;
		} else {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("mp %s is used", new_mp->coord_str);
			new_mp->used = BA_MP_USED_TRUE;
			/* Take this away if we decide we don't want
			   this to setup the main list.
			*/
			ba_mp->used = new_mp->used;
			if (hostlist)
				hostlist_push(hostlist, new_mp->coord_str);
			else
				hostlist = hostlist_create(new_mp->coord_str);
		}

		/* reset the main mp */
		ba_mp->used &= (~BA_MP_USED_ALTERED_PASS);
		memset(ba_mp->alter_switch, 0, sizeof(ba_mp->alter_switch));
		/* Take this away if we decide we don't want
		   this to setup the main list.
		*/
		/* info("got usage of %s %d %d", new_mp->coord_str, */
		/*      new_mp->used, ba_mp->used); */
		for (dim=0; dim<cluster_dims; dim++) {
			ba_mp->axis_switch[dim].usage |=
				new_mp->axis_switch[dim].usage;
			/* info("dim %d is %s", dim, */
			/*      ba_switch_usage_str( */
			/* 	     ba_mp->axis_switch[dim].usage)); */
		}
	}
	list_iterator_destroy(itr);

	if (hostlist) {
		name = hostlist_ranged_string_xmalloc(hostlist);
		hostlist_destroy(hostlist);
		color_count++;
	}

	return name;
}

static char *_reset_altered_mps(List main_mps)
{
	ListIterator itr = NULL;
	ba_mp_t *ba_mp;
	char *name = NULL;
	hostlist_t hostlist = NULL;

	xassert(main_mps);

	if (!(itr = list_iterator_create(main_mps)))
		fatal("got NULL list iterator");
	while ((ba_mp = list_next(itr))) {
		if (!(ba_mp->used & BA_MP_USED_ALTERED)) {
			error("_reset_altered_mps it appears we "
			      "have a mp %s added that wasn't altered",
			      ba_mp->coord_str);
			continue;
		}

		if (ba_mp->used & BA_MP_USED_PASS_BIT) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("mp %s is used for passthrough",
				     ba_mp->coord_str);
		} else {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("mp %s is used", ba_mp->coord_str);
			if (hostlist)
				hostlist_push(hostlist, ba_mp->coord_str);
			else
				hostlist = hostlist_create(ba_mp->coord_str);
		}
		ba_mp->used &= (~BA_MP_USED_ALTERED_PASS);
		memset(ba_mp->alter_switch, 0, sizeof(ba_mp->alter_switch));
	}
	list_iterator_destroy(itr);

	if (hostlist) {
		name = hostlist_ranged_string_xmalloc(hostlist);
		hostlist_destroy(hostlist);
		color_count++;
	}

	return name;
}

#ifdef HAVE_BGQ
/*
 * Copy a path through the wiring of a switch to another switch on a
 * starting port on a dimension.
 *
 * IN/OUT: mps - Local list of midplanes you are keeping track of.  If
 *         you visit any new midplanes a copy from ba_system_grid
 *         will be added to the list.  If NULL the path will be
 *         set in mark_switch of the main virtual system (ba_system_grid).
 * IN: curr_switch - The switch you want to copy the path of
 * IN/OUT: mark_switch - The switch you want to fill in.  On success
 *         this switch will contain a complete path from the curr_switch
 *         starting from the source port.
 * IN: dim - Dimension AXYZ
 *
 * RET: on success 1, on error 0
 */
static int _copy_the_path(List mps, ba_mp_t *start_mp, ba_mp_t *curr_mp,
			  ba_mp_t *mark_mp, int dim)
{
	ba_mp_t *next_mp = NULL;
	ba_mp_t *next_mark_mp = NULL;
	ba_switch_t *axis_switch = &curr_mp->axis_switch[dim];
	ba_switch_t *alter_switch = &curr_mp->alter_switch[dim];
	ba_switch_t *mark_alter_switch = &mark_mp->alter_switch[dim];

	/* Just copy the whole thing over */
	mark_alter_switch->usage = alter_switch->usage;
	if (!(curr_mp->used & BA_MP_USED_ALTERED)) {
		if (mark_alter_switch->usage & BG_SWITCH_PASS_FLAG) {
			curr_mp->used |= BA_MP_USED_ALTERED_PASS;
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("using mp %s(%d) as passthrough "
				     "%s added %s",
				     curr_mp->coord_str, dim,
				     ba_switch_usage_str(
					     axis_switch->usage),
				     ba_switch_usage_str(
					     alter_switch->usage));
		} else {
			curr_mp->used |= BA_MP_USED_ALTERED;
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("_copy_the_path: using mp %s(%d) "
				     "%s added %s",
				     curr_mp->coord_str, dim,
				     ba_switch_usage_str(
					     axis_switch->usage),
				     ba_switch_usage_str(
					     alter_switch->usage));
		}
		list_append(mps, curr_mp);
	} else {
		info("already here for %s", curr_mp->coord_str);
	}
	/* we must be in a mesh! */
	if (!(alter_switch->usage & BG_SWITCH_OUT)) {
		info("_copy_the_path: we are in a mesh returning now.");
		return 1;
	}

	if (!(alter_switch->usage & BG_SWITCH_OUT_PASS)
	    || (alter_switch->usage & BG_SWITCH_PASS_FLAG)) {
		if (!(alter_switch->usage & BG_SWITCH_IN)) {
			error("_copy_the_path: "
			      "we only had an out port set and nothing else");
			return 0;
		}
		/* we just had one midplane */
		return 1;
	}

	next_mp = curr_mp->next_mp[dim];
	if (!next_mp) {
		error("_copy_the_path: We didn't get the next curr_mp!");
		return 0;
	}

	/* follow the path */
	if (!mark_mp->next_mp[dim]) {
		if (!mps) {
			/* If no mps then just get the next mp to fill
			   in from the main system */
			mark_mp->next_mp[dim] = &ba_system_ptr->
				grid[next_mp->coord[A]]
				[next_mp->coord[X]]
				[next_mp->coord[Y]]
				[next_mp->coord[Z]];
		} else {
			ba_mp_t *ba_mp = NULL;
			ListIterator itr = list_iterator_create(mps);
			/* see if we have already been to this mp */
			while ((ba_mp = (ba_mp_t *)list_next(itr)))
				if (memcmp(ba_mp->coord, next_mp->coord,
					   sizeof(ba_mp->coord)))
					break;	/* we found it */

			list_iterator_destroy(itr);
			if (!ba_mp) {
				/* If mp grab a copy and add it to the list */
				ba_mp = ba_copy_mp(&ba_system_ptr->
						   grid[next_mp->coord[A]]
						   [next_mp->coord[X]]
						   [next_mp->coord[Y]]
						   [next_mp->coord[Z]]);
				ba_setup_mp(ba_mp, false);
				list_push(mps, ba_mp);
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
					info("_copy_the_path: "
					     "haven't seen %s adding it",
					     ba_mp->coord_str);
			}
			mark_mp->next_mp[dim] = ba_mp;
		}
	}

	next_mark_mp = mark_mp->next_mp[dim];
	if (!next_mark_mp) {
		error("_copy_the_path: We didn't get the next mark mp's!");
		return 0;
	}

	if (next_mark_mp == start_mp) {
		/* found the end of the line */
		return 1;
	}

	/* Keep going until we reach the end of the line */
	return _copy_the_path(mps, start_mp, next_mp, next_mark_mp, dim);
}
#endif

static int _copy_ba_switch(ba_mp_t *ba_mp, ba_mp_t *orig_mp, int dim)
{
	int rc = 0;
	if (ba_mp->alter_switch[dim].usage != BG_SWITCH_NONE) {
		info("already set %s(%d)", ba_mp->coord_str, dim);
		return 0;

	}
	if ((orig_mp->used & BA_MP_USED_PASS_BIT)
	    || (ba_mp->used & BA_MP_USED_PASS_BIT)) {
		info("here %d %d", orig_mp->alter_switch[dim].usage & BG_SWITCH_PASS_FLAG, ba_mp->alter_switch[dim].usage & BG_SWITCH_PASS_FLAG);
		if (!(orig_mp->alter_switch[dim].usage & BG_SWITCH_PASS_FLAG)) {
			info("skipping %s(%d)", ba_mp->coord_str, dim);
			return 0;
		}
	}

	if (_mp_used(ba_mp, dim)) {
		info("%s used", ba_mp->coord_str);
		return -1;
	}
	info("mapping dim %d of %s(%d) to %s(%d) %d", dim, orig_mp->coord_str, orig_mp->used, ba_mp->coord_str, ba_mp->used, orig_mp->used == BA_MP_USED_ALTERED_PASS);
	if (!(ba_mp->used & BA_MP_USED_ALTERED)) {
		if (switch_overlap(ba_mp->axis_switch[dim].usage,
				   orig_mp->alter_switch[dim].usage)) {
			info("%s switches %d overlapped %s to %s",
			     ba_mp->coord_str, dim,
			     ba_switch_usage_str(
				     ba_mp->alter_switch[dim].usage),
			     ba_switch_usage_str(
				     orig_mp->alter_switch[dim].usage));
			return -1;
		}
		rc = 1;
	}

	/* Just overlap them here so if they are used in a passthough
	   we get it.
	*/
	ba_mp->used |= orig_mp->used;

	info("adding from %s %s to %s",
	     ba_switch_usage_str(orig_mp->alter_switch[dim].usage),
	     orig_mp->coord_str,
	     ba_switch_usage_str(ba_mp->alter_switch[dim].usage));
	ba_mp->alter_switch[dim].usage |= orig_mp->alter_switch[dim].usage;

	return rc;
}

static int _check_deny_pass(int dim)
{
	switch (dim) {
	case A:
		*deny_pass |= PASS_FOUND_A;
		if (*deny_pass & PASS_DENY_A) {
			debug("We don't allow A passthoughs");
			return 1;
		}
		break;
	case X:
		*deny_pass |= PASS_FOUND_X;
		if (*deny_pass & PASS_DENY_X) {
			debug("We don't allow X passthoughs");
			return 1;
		}
		break;
	case Y:
		*deny_pass |= PASS_FOUND_Y;
		if (*deny_pass & PASS_DENY_Y) {
			debug("We don't allow Y passthoughs");
			return 1;
		}
		break;
	case Z:
		*deny_pass |= PASS_FOUND_Z;
		if (*deny_pass & PASS_DENY_Z) {
			debug("We don't allow Z passthoughs");
			return 1;
		}
		break;
	default:
		error("unknown dim %d", dim);
		return 1;
		break;
	}
	return 0;
}

static int _find_path(List mps, ba_mp_t *start_mp, int dim,
		      uint16_t geometry, uint16_t conn_type,
		      uint16_t *block_end, uint16_t *longest)
{
	ba_mp_t *curr_mp = start_mp->next_mp[dim];
	ba_switch_t *axis_switch = NULL;
	ba_switch_t *alter_switch = NULL;
	int count = 1;
	int add = 0;

	if (_mp_used(start_mp, dim))
		return 0;

	//start_mp->used |= BA_MP_USED_START;

	axis_switch = &start_mp->axis_switch[dim];
	alter_switch = &start_mp->alter_switch[dim];
	if (geometry == 1) {
		/* Always check MESH here since we only care about the
		   IN/OUT ports.
		*/
		start_mp->used |= BA_MP_USED_ALTERED;
		if (conn_type == SELECT_TORUS) {
			alter_switch->usage |= BG_SWITCH_WRAPPED;
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("using mp %s(%d) in 1 geo %s added %s",
				     start_mp->coord_str, dim,
				     ba_switch_usage_str(axis_switch->usage),
				     ba_switch_usage_str(alter_switch->usage));
		}
		return 1;
	}

	start_mp->used |= BA_MP_USED_ALTERED;
	alter_switch->usage |= BG_SWITCH_OUT;
	alter_switch->usage |= BG_SWITCH_OUT_PASS;

	while (curr_mp != start_mp) {
		xassert(curr_mp);
		//curr_mp->used |= BA_MP_USED_START;
		axis_switch = &curr_mp->axis_switch[dim];
		alter_switch = &curr_mp->alter_switch[dim];
		if (curr_mp->coord[dim] > *longest)
			*longest = curr_mp->coord[dim];
		/* This should never happen since we got here
		   from an unused mp */
		xassert(!(axis_switch->usage & BG_SWITCH_IN_PASS));
		if ((count < geometry) && !_mp_used(curr_mp, dim)) {
			if (curr_mp->coord[dim] > *block_end)
				*block_end = curr_mp->coord[dim];
			count++;
			if (!(curr_mp->used & BA_MP_USED_ALTERED)) {
				add = 1;
				curr_mp->used |= BA_MP_USED_ALTERED;
			}
			alter_switch->usage |= BG_SWITCH_IN_PASS;
			alter_switch->usage |= BG_SWITCH_IN;
			if ((count < geometry) || (conn_type == SELECT_TORUS)) {
				alter_switch->usage |= BG_SWITCH_OUT;
				alter_switch->usage |= BG_SWITCH_OUT_PASS;
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
					info("using mp %s(%d) %d(%d) "
					     "%s added %s",
					     curr_mp->coord_str, dim,
					     count, geometry,
					     ba_switch_usage_str(
						     axis_switch->usage),
					     ba_switch_usage_str(
						     alter_switch->usage));
			} else if (conn_type == SELECT_MESH) {
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
					info("using mp %s(%d) %d(%d) "
					     "%s added %s",
					     curr_mp->coord_str, dim,
					     count, geometry,
					     ba_switch_usage_str(
						     axis_switch->usage),
					     ba_switch_usage_str(
						     alter_switch->usage));
				if (add)
					list_append(mps, curr_mp);
				return 1;
			}
		} else if (!_mp_out_used(curr_mp, dim)
			   && !_check_deny_pass(dim)) {
			if (!(curr_mp->used & BA_MP_USED_ALTERED)) {
				add = 1;
				curr_mp->used |= BA_MP_USED_ALTERED_PASS;
			}
			alter_switch->usage |= BG_SWITCH_PASS;
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP) {
				if (count == geometry) {
					info("using mp %s(%d) to "
					     "finish torus %s added %s",
					     curr_mp->coord_str, dim,
					     ba_switch_usage_str(
						     axis_switch->usage),
					     ba_switch_usage_str(
						     alter_switch->usage));
				} else {
					info("using mp %s(%d) as passthrough "
					     "%s added %s",
					     curr_mp->coord_str, dim,
					     ba_switch_usage_str(
						     axis_switch->usage),
					     ba_switch_usage_str(
						     alter_switch->usage));
				}
			}
		} else {
			/* we can't use this so return with a nice 0 */
			return 0;
		}

		if (add)
			list_append(mps, curr_mp);
		curr_mp = curr_mp->next_mp[dim];
	}

	if (count != geometry)
		return 0;

	if (curr_mp == start_mp) {
		axis_switch = &curr_mp->axis_switch[dim];
		alter_switch = &curr_mp->alter_switch[dim];
		/* This should never happen since we got here
		   from an unused mp */
		xassert(!(axis_switch->usage & BG_SWITCH_IN_PASS));
		alter_switch->usage |= BG_SWITCH_IN_PASS;
		alter_switch->usage |= BG_SWITCH_IN;
	}

	return 1;
}

/** */
static int _setup_next_mps(ba_mp_t ****grid)
{
	int a;
	ba_mp_t *source = NULL, *target = NULL;
	if (cluster_dims == 1) {
		for(a=0;a<DIM_SIZE[A];a++) {
			source = &grid[a][0][0][0];
			if (a<(DIM_SIZE[A]-1))
				target = &grid[a+1][0][0][0];
			else
				target = &grid[0][0][0][0];
			source->next_mp[A] = target;
		}
	} else {
		int x, y, z;
		for (a = 0; a < DIM_SIZE[A]; a++)
			for (x = 0; x < DIM_SIZE[X]; x++)
				for (y = 0; y < DIM_SIZE[Y]; y++)
					for (z = 0; z < DIM_SIZE[Z]; z++) {
						source = &grid[a][x][y][z];

						if (a < (DIM_SIZE[A]-1))
							target = &grid
								[a+1][x][y][z];
						else
							target = &grid
								[0][x][y][z];
						source->next_mp[A] = target;

						if (x < (DIM_SIZE[X]-1))
							target = &grid
								[a][x+1][y][z];
						else
							target = &grid
								[a][0][y][z];
						source->next_mp[X] = target;

						if (y < (DIM_SIZE[Y]-1))
							target = &grid
								[a][x][y+1][z];
						else
							target = &grid
								[a][x][0][z];
						source->next_mp[Y] = target;

						if (z < (DIM_SIZE[Z]-1))
							target = &grid
								[a][x][y][z+1];
						else
							target = &grid
								[a][x][y][0];
						source->next_mp[Z] = target;
					}
	}
	return 1;
}

static void _create_ba_system(void)
{
	int a,x,y,z;

	ba_system_ptr->grid = (ba_mp_t****)
		xmalloc(sizeof(ba_mp_t***) * DIM_SIZE[A]);
	for (a = 0; a < DIM_SIZE[A]; a++) {
		ba_system_ptr->grid[a] = (ba_mp_t***)
			xmalloc(sizeof(ba_mp_t**) * DIM_SIZE[X]);
		for (x = 0; x < DIM_SIZE[X]; x++) {
			ba_system_ptr->grid[a][x] = (ba_mp_t**)
				xmalloc(sizeof(ba_mp_t*) * DIM_SIZE[Y]);
			for (y = 0; y < DIM_SIZE[Y]; y++) {
				ba_system_ptr->grid[a][x][y] = (ba_mp_t*)
					xmalloc(sizeof(ba_mp_t) * DIM_SIZE[Z]);
				for (z = 0; z < DIM_SIZE[Z]; z++) {
					ba_mp_t *ba_mp = &ba_system_ptr->
						grid[a][x][y][z];
					ba_mp->coord[A] = a;
					ba_mp->coord[X] = x;
					ba_mp->coord[Y] = y;
					ba_mp->coord[Z] = z;
					snprintf(ba_mp->coord_str,
						 sizeof(ba_mp->coord_str),
						 "%c%c%c%c",
						 alpha_num[ba_mp->coord[A]],
						 alpha_num[ba_mp->coord[X]],
						 alpha_num[ba_mp->coord[Y]],
						 alpha_num[ba_mp->coord[Z]]);
					ba_setup_mp(ba_mp, true);
				}
			}
		}
	}

	if (cluster_flags & CLUSTER_FLAG_BGQ)
		_setup_next_mps(ba_system_ptr->grid);
}

/** */
static void _delete_ba_system(void)
{
	int a, x, y;

	if (!ba_system_ptr) {
		return;
	}

	if (ba_system_ptr->grid) {
		for (a=0; a<DIM_SIZE[A]; a++) {
			for (x = 0; x < DIM_SIZE[X]; x++) {
				for (y = 0; y < DIM_SIZE[Y]; y++)
					xfree(ba_system_ptr->grid[a][x][y]);
				xfree(ba_system_ptr->grid[a][x]);
			}
			xfree(ba_system_ptr->grid[a]);
		}
		xfree(ba_system_ptr->grid);
	}
	xfree(ba_system_ptr);
}

/**
 * algorithm for finding match
 */
static int _find_match(select_ba_request_t *ba_request, List results)
{
	int i=0;
	uint16_t start[cluster_dims];
	uint16_t end[cluster_dims];
	char *name=NULL;
	int startx;
	uint16_t *geo_ptr;

	if (!(cluster_flags & CLUSTER_FLAG_BG))
		return 0;

	memset(start, 0, sizeof(start));
	startx = (start[X]-1);

	if (startx == -1)
		startx = DIM_SIZE[X]-1;
	if (ba_request->start_req) {
		for (i = 0; i < cluster_dims; i++) {
			if (ba_request->start[i] >= DIM_SIZE[i])
				return 0;
			start[i] = ba_request->start[i];
		}
	}

	/* set up the geo here */
	if (!(geo_ptr = (uint16_t *)list_peek(ba_request->elongate_geos)))
		return 0;
	ba_request->rotate_count=0;
	ba_request->elongate_count=1;

	for (i = 0; i < cluster_dims; i++)
		ba_request->geometry[i] = geo_ptr[i];

	for (i = 0; i < cluster_dims; i++) {
		end[i] = start[i] + ba_request->geometry[i];
		if (end[i] > DIM_SIZE[i])
			if (!_check_for_options(ba_request))
				return 0;
	}

start_again:
	i = 0;
	if (i == startx)
		i = startx-1;
	while (i != startx) {
		i++;
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("finding %c%c%c%c try %d",
			     alpha_num[ba_request->geometry[A]],
			     alpha_num[ba_request->geometry[X]],
			     alpha_num[ba_request->geometry[Y]],
			     alpha_num[ba_request->geometry[Z]],
			     i);
	new_mp:
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
			info("starting at %c%c%c%c",
			     alpha_num[start[A]],
			     alpha_num[start[X]],
			     alpha_num[start[Y]],
			     alpha_num[start[Z]]);

		if ((name = set_bg_block(results, start,
					 ba_request->geometry,
					 ba_request->conn_type))) {
			ba_request->save_name = name;
			name = NULL;
			return 1;
		}

		if (results) {
			bool is_small = 0;
			if (ba_request->conn_type[0] == SELECT_SMALL)
				is_small = 1;
			remove_block(results, color_count, is_small);
			list_flush(results);
		}

		if (ba_request->start_req)
			goto requested_end;
		//exit(0);
		debug2("trying something else");

		if ((DIM_SIZE[Z]-start[Z]-1) >= ba_request->geometry[Z])
			start[Z]++;
		else {
			start[Z] = 0;
			if ((DIM_SIZE[Y]-start[Y]-1) >= ba_request->geometry[Y])
				start[Y]++;
			else {
				start[Y] = 0;
				if ((DIM_SIZE[X]-start[X]-1)
				    >= ba_request->geometry[X])
					start[X]++;
				else {
					start[X] = 0;
					if ((DIM_SIZE[A]-start[A]-1)
					    >= ba_request->geometry[A])
						start[A]++;
					else {
						if (ba_request->size == 1)
							goto requested_end;
						if (!_check_for_options(
							    ba_request))
							return 0;
						else {
							start[A]=0;
							start[X]=0;
							start[Y]=0;
							start[Z]=0;
							goto start_again;
						}
					}
				}
			}
		}
		goto new_mp;
	}

requested_end:
	debug2("1 can't allocate");

	return 0;
}

/*
 * Used to check if midplane is usable in the block we are creating
 *
 * IN: ba_mp - mp to check if is used
 * IN: dim - dimension we are checking.
  */
static bool _mp_used(ba_mp_t* ba_mp, int dim)
{
	xassert(ba_mp);

	/* if we've used this mp in another block already */
	if (mp_strip_unaltered(ba_mp->used)
	    || ba_mp->axis_switch[dim].usage & BG_SWITCH_WRAPPED) {
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("mp %s used in the %d dim",
			     ba_mp->coord_str, dim);
		return true;
	}
	return false;
}

/*
 * Used to check if we can leave a midplane
 *
 * IN: ba_mp - mp to check if is used
 * IN: dim - dimension we are checking.
 */
static bool _mp_out_used(ba_mp_t* ba_mp, int dim)
{
	xassert(ba_mp);

	/* If the mp is already used just check the PASS_USED. */
	if (ba_mp->axis_switch[dim].usage & BG_SWITCH_PASS_USED) {
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("passthroughs used in the %d dim on mp %s",
			     dim, ba_mp->coord_str);
		return true;
	}

	return false;
}

static void _destroy_geo(void *object)
{
	uint16_t *geo_ptr = (uint16_t *)object;
	xfree(geo_ptr);
}

static int _coord(char coord)
{
	if ((coord >= '0') && (coord <= '9'))
		return (coord - '0');
	if ((coord >= 'A') && (coord <= 'Z'))
		return (coord - 'A');
	return -1;
}
