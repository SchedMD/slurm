/*****************************************************************************\
 *  block_allocator.c - Assorted functions for layout of bgq blocks,
 *	 wiring, mapping for smap, etc.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov>, Danny Auble <da@llnl.gov>
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

static bool _initialized = false;
static bool _wires_initialized = false;

/* _ba_system is the "current" system that the structures will work
 *  on */
ba_system_t *ba_system_ptr = NULL;
List path = NULL;
List best_path = NULL;
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
List ba_midplane_list = NULL;
char letters[62];
char colors[6];
uint16_t DIM_SIZE[HIGHEST_DIMENSIONS] = {0,0,0,0};
uint16_t REAL_DIM_SIZE[HIGHEST_DIMENSIONS] = {0,0,0,0};

s_p_options_t bg_conf_file_options[] = {
#ifdef HAVE_BGL
	{(char *)"BlrtsImage", S_P_STRING},
	{(char *)"LinuxImage", S_P_STRING},
	{(char *)"RamDiskImage", S_P_STRING},
	{(char *)"AltBlrtsImage", S_P_ARRAY, parse_image, NULL},
	{(char *)"AltLinuxImage", S_P_ARRAY, parse_image, NULL},
	{(char *)"AltRamDiskImage", S_P_ARRAY, parse_image, NULL},
#else
	{(char *)"CnloadImage", S_P_STRING},
	{(char *)"IoloadImage", S_P_STRING},
	{(char *)"AltCnloadImage", S_P_ARRAY, parse_image, NULL},
	{(char *)"AltIoloadImage", S_P_ARRAY, parse_image, NULL},
#endif
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
static int _check_for_options(ba_request_t* ba_request);

/* */
static int _append_geo(uint16_t *geo, List geos, int rotate);

/* */
static int _fill_in_coords(List results, List start_list,
			   uint16_t *geometry, uint16_t *conn_type);

/* */
static int _copy_the_path(List mps, ba_switch_t *curr_switch,
			  ba_switch_t *mark_switch,
			  int source, int dim);

/* */
static int _find_path(ba_mp_t *ba_mp, uint16_t *first,
		      uint16_t *geometry, uint16_t *conn_type);

#ifndef HAVE_BG_FILES
/* */
static int _emulate_ext_wiring(ba_mp_t ****grid);
#endif

/** */
static int _reset_the_path(ba_switch_t *curr_switch, int source,
			   int target, int dim);

/* */
static void _delete_ba_system(void);
/* */
static void _delete_path_list(void *object);

/* find the first block match in the system */
static int _find_match(ba_request_t* ba_request, List results);

/** */
static bool _mp_used(ba_mp_t* ba_mp, int x_size);

/* */
static void _switch_config(ba_mp_t* source, ba_mp_t* target, int dim,
			   int port_src, int port_tar);

/* */
static int _set_external_wires(int dim, int count, ba_mp_t* source,
			       ba_mp_t* target);

/* */
static char *_set_internal_wires(List mps, int size, int conn_type);

/* */
/* static int _find_passthrough(ba_switch_t *curr_switch, int source_port,  */
/* 			     List mps, int dim,  */
/* 			     int count, int highest_phys_x);  */
/* */

/* */
static int _set_one_dim(uint16_t *start, uint16_t *end, uint16_t *coord);

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
#ifdef HAVE_BGL
		{(char *)"Nodecards", S_P_UINT16},
		{(char *)"Quarters", S_P_UINT16},
		{(char *)"BlrtsImage", S_P_STRING},
		{(char *)"LinuxImage", S_P_STRING},
		{(char *)"RamDiskImage", S_P_STRING},
#else
		{(char *)"16CNBlocks", S_P_UINT16},
		{(char *)"64CNBlocks", S_P_UINT16},
		{(char *)"256CNBlocks", S_P_UINT16},
		{(char *)"CnloadImage", S_P_STRING},
		{(char *)"IoloadImage", S_P_STRING},
#endif
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
#ifdef HAVE_BGL
	s_p_get_string(&n->blrtsimage, "BlrtsImage", tbl);
	s_p_get_string(&n->linuximage, "LinuxImage", tbl);
	s_p_get_string(&n->ramdiskimage, "RamDiskImage", tbl);
#else
	s_p_get_string(&n->linuximage, "CnloadImage", tbl);
	s_p_get_string(&n->ramdiskimage, "IoloadImage", tbl);
#endif
	s_p_get_string(&n->mloaderimage, "MloaderImage", tbl);

	s_p_get_string(&tmp, "Type", tbl);
	if (!tmp || !strcasecmp(tmp,"TORUS"))
		n->conn_type[A] = SELECT_TORUS;
	else if (!strcasecmp(tmp,"MESH"))
		n->conn_type[A] = SELECT_MESH;
	else
		n->conn_type[A] = SELECT_SMALL;
	xfree(tmp);

	if (!s_p_get_uint16(&n->small32, "32CNBlocks", tbl)) {
#ifdef HAVE_BGL
		s_p_get_uint16(&n->small32, "Nodecards", tbl);
#else
		;
#endif
	}
	if (!s_p_get_uint16(&n->small128, "128CNBlocks", tbl)) {
#ifdef HAVE_BGL
		s_p_get_uint16(&n->small128, "Quarters", tbl);
#else
		;
#endif
	}

#ifndef HAVE_BGL
	s_p_get_uint16(&n->small16, "16CNBlocks", tbl);
	s_p_get_uint16(&n->small64, "64CNBlocks", tbl);
	s_p_get_uint16(&n->small256, "256CNBlocks", tbl);
#endif

	s_p_hashtbl_destroy(tbl);

	*dest = (void *)n;
	return 1;
}

extern void destroy_blockreq(void *ptr)
{
	blockreq_t *n = (blockreq_t *)ptr;
	if (n) {
		xfree(n->block);
#ifdef HAVE_BGL
		xfree(n->blrtsimage);
#endif
		xfree(n->linuximage);
		xfree(n->mloaderimage);
		xfree(n->ramdiskimage);
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
extern int new_ba_request(ba_request_t* ba_request)
{
	int i=0;
	float sz=1;
	int i2, picked, total_sz=1, size2=0;
	uint16_t *geo_ptr;
	int messed_with = 0;
	int checked[DIM_SIZE[X]];
	uint16_t geo[cluster_dims];

	memset(geo, 0, sizeof(geo));
	ba_request->save_name= NULL;
	ba_request->rotate_count= 0;
	ba_request->elongate_count = 0;
	ba_request->elongate_geos = list_create(_destroy_geo);
	memcpy(geo, ba_request->geometry, sizeof(geo));

	if (ba_request->deny_pass == (uint16_t)NO_VAL)
		ba_request->deny_pass = ba_deny_pass;

	if (!(cluster_flags & CLUSTER_FLAG_BG)) {
		if (geo[X] != (uint16_t)NO_VAL) {
			for (i=0; i<cluster_dims; i++) {
				if ((geo[i] < 1) || (geo[i] > DIM_SIZE[i])) {
					error("new_ba_request Error, "
					      "request geometry is invalid %d",
					      geo[i]);
					return 0;
				}
			}
			ba_request->size = ba_request->geometry[X];
		} else if (ba_request->size) {
			ba_request->geometry[X] = ba_request->size;
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

		if (ba_request->size==1) {
			_append_geo(geo,
				    ba_request->elongate_geos,
				    ba_request->rotate);
			goto endit;
		}

		if (ba_request->size<=DIM_SIZE[Y]) {
			geo[X] = 1;
			geo[Y] = ba_request->size;
			geo[Z] = 1;
			sz=ba_request->size;
			_append_geo(geo,
				    ba_request->elongate_geos,
				    ba_request->rotate);
		}

		i = ba_request->size/4;
		if (!(ba_request->size%2)
		    && i <= DIM_SIZE[Y]
		    && i <= DIM_SIZE[Z]
		    && i*i == ba_request->size) {
			geo[X] = 1;
			geo[Y] = i;
			geo[Z] = i;
			sz=ba_request->size;
			_append_geo(geo,
				    ba_request->elongate_geos,
				    ba_request->rotate);
		}

		if (ba_request->size > total_sz || ba_request->size < 1) {
			return 0;
		}
		sz = ba_request->size % (DIM_SIZE[Y] * DIM_SIZE[Z]);
		if (!sz) {
			i = ba_request->size / (DIM_SIZE[Y] * DIM_SIZE[Z]);
			geo[X] = i;
			geo[Y] = DIM_SIZE[Y];
			geo[Z] = DIM_SIZE[Z];
			sz=ba_request->size;
			if ((geo[X]*geo[Y]*geo[Z]) == ba_request->size)
				_append_geo(geo,
					    ba_request->elongate_geos,
					    ba_request->rotate);
			else
				error("%d I was just trying to add a "
				      "geo of %d%d%d "
				      "while I am trying to request "
				      "%d midplanes",
				      __LINE__, geo[X], geo[Y], geo[Z],
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
			ba_request->geometry[X] = 1;
			ba_request->geometry[Y] = geo[X] * geo[Y];
			ba_request->geometry[Z] = geo[Z];
			_append_geo(ba_request->geometry,
				    ba_request->elongate_geos,
				    ba_request->rotate);

		}
		if ((geo[X]*geo[Z]) <= DIM_SIZE[Y]) {
			ba_request->geometry[X] = 1;
			ba_request->geometry[Y] = geo[Y];
			ba_request->geometry[Z] = geo[X] * geo[Z];
			_append_geo(ba_request->geometry,
				    ba_request->elongate_geos,
				    ba_request->rotate);

		}

		/* Make sure geo[X] is even and then see if we can get
		   it into the C or D dim. */
		if (!(geo[X]%2) && ((geo[X]/2) <= DIM_SIZE[Y])) {
			if (geo[Y] == 1) {
				ba_request->geometry[Y] = geo[X]/2;
				messed_with = 1;
			} else
				ba_request->geometry[Y] = geo[Y];
			if (!messed_with && geo[Z] == 1) {
				messed_with = 1;
				ba_request->geometry[Z] = geo[X]/2;
			} else
				ba_request->geometry[Z] = geo[Z];
			if (messed_with) {
				messed_with = 0;
				ba_request->geometry[X] = 2;
				_append_geo(ba_request->geometry,
					    ba_request->elongate_geos,
					    ba_request->rotate);
			}
		}
		if (geo[X] == DIM_SIZE[X]
		    && (geo[Y] < DIM_SIZE[Y]
			|| geo[Z] < DIM_SIZE[Z])) {
			if (DIM_SIZE[Y]<DIM_SIZE[Z]) {
				i = DIM_SIZE[Y];
				DIM_SIZE[Y] = DIM_SIZE[Z];
				DIM_SIZE[Z] = i;
			}
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

		if ((geo[X]*geo[Y]*geo[Z]) == ba_request->size)
			_append_geo(geo,
				    ba_request->elongate_geos,
				    ba_request->rotate);
		else
			error("%d I was just trying to add a geo of %d%d%d "
			      "while I am trying to request %d midplanes",
			      __LINE__, geo[X], geo[Y], geo[Z],
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
	ba_request_t *ba_request = (ba_request_t *)arg;
	if (ba_request) {
		xfree(ba_request->save_name);
		if (ba_request->elongate_geos)
			list_destroy(ba_request->elongate_geos);
#ifdef HAVE_BGL
		xfree(ba_request->blrtsimage);
#endif
		xfree(ba_request->linuximage);
		xfree(ba_request->mloaderimage);
		xfree(ba_request->ramdiskimage);

		xfree(ba_request);
	}
}

/**
 * print a block request
 */
extern void print_ba_request(ba_request_t* ba_request)
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

	bridge_setup_system(node_info_ptr, cluster_dims);


#ifndef HAVE_BG_FILES
	if (cluster_flags & CLUSTER_FLAG_BGQ)
		_emulate_ext_wiring(ba_system_ptr->grid);
#endif

	path = list_create(_delete_path_list);
	best_path = list_create(_delete_path_list);

	_initialized = true;
	init_grid(node_info_ptr);
}

/* If emulating a system set up a known configuration for wires in a
 * system of the size given.
 * If a real bluegene system, query the system and get all wiring
 * information of the system.
 */
extern void init_wires()
{
	int a, b, c, d, i;
	ba_mp_t *source = NULL;
	if (_wires_initialized)
		return;

	for(a=0;a<DIM_SIZE[A];a++) {
		for(b=0;b<DIM_SIZE[X];b++) {
			for(c=0;c<DIM_SIZE[Y];c++) {
				for(d=0;d<DIM_SIZE[Z];d++) {
					source = &ba_system_ptr->grid
						[a][b][c][d];
					for(i=0; i<NUM_PORTS_PER_NODE; i++) {
						_switch_config(source, source,
							       A, i, i);
						_switch_config(source, source,
							       X, i, i);
						_switch_config(source, source,
							       Y, i, i);
						_switch_config(source, source,
							       Z, i, i);
					}
				}
			}
		}
	}
#ifdef HAVE_BG_FILES
	_set_external_wires(0,0,NULL,NULL);
#endif
	_wires_initialized = true;
	return;
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

	if (path) {
		list_destroy(path);
		path = NULL;
	}
	if (best_path) {
		list_destroy(best_path);
		best_path = NULL;
	}

	bridge_fini();

	_delete_ba_system();
	_initialized = false;
	_wires_initialized = true;
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
	debug2("ba_update_mp_state: new state of [%c%c%c%c] is %s",
	       alpha_num[ba_mp->coord[A]],
	       alpha_num[ba_mp->coord[X]],
	       alpha_num[ba_mp->coord[Y]],
	       alpha_num[ba_mp->coord[Z]],
	       node_state_string(state));
#else
	debug2("ba_update_mp_state: new state of [%d] is %s",
	       ba_mp->coord[A],
	       node_state_string(state));
#endif

	/* basically set the mp as used */
	if ((mp_base_state == NODE_STATE_DOWN)
	    || (mp_flags & (NODE_STATE_DRAIN | NODE_STATE_FAIL)))
		ba_mp->used = true;
	else
		ba_mp->used = false;

	ba_mp->state = state;
}

extern void ba_setup_mp(ba_mp_t *ba_mp, uint16_t *coord, bool track_down_mps)
{
	int i,j;
	uint16_t node_base_state = ba_mp->state & NODE_STATE_BASE;

	if (((node_base_state != NODE_STATE_DOWN)
	     && !(ba_mp->state & NODE_STATE_DRAIN)) || !track_down_mps)
		ba_mp->used = false;

	for (i=0; i<cluster_dims; i++){
		ba_mp->coord[i] = coord[i];

		for(j=0;j<NUM_PORTS_PER_NODE;j++) {
			ba_mp->axis_switch[i].int_wire[j].used = 0;
			ba_mp->axis_switch[i].int_wire[j].port_tar = j;
		}
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

#ifdef HAVE_BG_Q
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
			if (ba_mp->coord[A] == new_ba_mp->coord[A] &&
			    ba_mp->coord[X] == new_ba_mp->coord[X] &&
			    ba_mp->coord[Y] == new_ba_mp->coord[Y] &&
			    ba_mp->coord[Z] == new_ba_mp->coord[Z])
				break;	/* we found it */
		}
		list_iterator_destroy(itr2);

		if (!new_ba_mp) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
				info("adding %c%c%c as a new mp",
				     alpha_num[ba_mp->coord[X]],
				     alpha_num[ba_mp->coord[Y]],
				     alpha_num[ba_mp->coord[Z]]);
			new_ba_mp = ba_copy_mp(ba_mp);
			ba_setup_mp(new_ba_mp, ba_mp->coord, false);
			list_push(*dest_mps, new_ba_mp);

		}
		new_ba_mp->used = true;
		for(dim=0;dim<cluster_dims;dim++) {
			curr_switch = &ba_mp->axis_switch[dim];
			new_switch = &new_ba_mp->axis_switch[dim];
			if (curr_switch->int_wire[0].used) {
				if (!_copy_the_path(*dest_mps,
						    curr_switch, new_switch,
						    0, dim)) {
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
extern int allocate_block(ba_request_t* ba_request, List results)
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
extern int remove_block(List mps, int new_count, int conn_type)
{
	int dim;
	ba_mp_t* curr_ba_mp = NULL;
	ba_mp_t* ba_mp = NULL;
	ba_switch_t *curr_switch = NULL;
	ListIterator itr;

	itr = list_iterator_create(mps);
	while ((curr_ba_mp = (ba_mp_t*) list_next(itr))) {
		/* since the list that comes in might not be pointers
		   to the main list we need to point to that main list */
		ba_mp = &ba_system_ptr->
			grid[curr_ba_mp->coord[A]]
			[curr_ba_mp->coord[X]]
			[curr_ba_mp->coord[Y]]
			[curr_ba_mp->coord[Z]];

		ba_mp->used = false;
		ba_mp->color = 7;
		ba_mp->letter = '.';
		/* Small blocks don't use wires, and only have 1 mp,
		   so just break. */
		if (conn_type == SELECT_SMALL)
			break;
		for(dim=0;dim<cluster_dims;dim++) {
			curr_switch = &ba_mp->axis_switch[dim];
			if (curr_switch->int_wire[0].used) {
				_reset_the_path(curr_switch, 0, 1, dim);
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
					     "this mp %c%c%c %s",
					     alpha_num[ba_mp->coord[X]],
					     alpha_num[ba_mp->coord[Y]],
					     alpha_num[ba_mp->coord[Z]],
					     node_state_string(
						     curr_ba_mp->state));
				rc = SLURM_ERROR;
				goto end_it;
			}
		}

		if (ba_mp->used)
			curr_ba_mp->used = true;
		for(i=0; i<cluster_dims; i++) {
			ba_switch = &ba_mp->axis_switch[i];
			curr_ba_switch = &curr_ba_mp->axis_switch[i];
			//info("checking dim %d", i);

			for(j=0; j<NUM_PORTS_PER_NODE; j++) {
				//info("checking port %d", j);

				if (ba_switch->int_wire[j].used
				    && curr_ba_switch->int_wire[j].used
				    && j != curr_ba_switch->
				    int_wire[j].port_tar) {
					if (ba_debug_flags
					    & DEBUG_FLAG_BG_ALGO_DEEP)
						info("%c%c%c dim %d port %d "
						     "is already in use to %d",
						     alpha_num[ba_mp->
							       coord[X]],
						     alpha_num[ba_mp->
							       coord[Y]],
						     alpha_num[ba_mp->
							       coord[Z]],
						     i,
						     j,
						     curr_ba_switch->
						     int_wire[j].port_tar);
					rc = SLURM_ERROR;
					goto end_it;
				}
				if (!ba_switch->int_wire[j].used)
					continue;

				/* info("setting %c%c%c dim %d port %d -> %d",*/
/* 				     alpha_num[ba_mp->coord[X]],  */
/* 				     alpha_num[ba_mp->coord[Y]], */
/* 				     alpha_num[ba_mp->coord[Z]],  */
/* 				     i, */
/* 				     j, */
/* 				     ba_switch->int_wire[j].port_tar); */
				curr_ba_switch->int_wire[j].used = 1;
				curr_ba_switch->int_wire[j].port_tar
					= ba_switch->int_wire[j].port_tar;
			}
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
	char *name = NULL;
	ba_mp_t* ba_mp = NULL;
	int size = 0;
	int send_results = 0;
	int found = 0;


	if (cluster_dims == 1) {
		if (start[A]>=DIM_SIZE[A])
			return NULL;
		size = geometry[X];
		ba_mp = &ba_system_ptr->grid[start[A]][0][0][0];
	} else {
		if (start[A]>=DIM_SIZE[A]
		    || start[X]>=DIM_SIZE[X]
		    || start[Y]>=DIM_SIZE[Y]
		    || start[Z]>=DIM_SIZE[Z])
			return NULL;

		if (geometry[A] <= 0 || geometry[X] <= 0
		    || geometry[Y] <= 0 || geometry[Z] <= 0) {
			error("problem with geometry %c%c%c%c, needs to be "
			      "at least 1111",
			      alpha_num[geometry[A]],
			      alpha_num[geometry[X]],
			      alpha_num[geometry[Y]],
			      alpha_num[geometry[Z]]);
			return NULL;
		}
		/* info("looking at %d%d%d", geometry[X], */
		/*      geometry[Y], geometry[Z]); */
		size = geometry[A] * geometry[X] * geometry[Y] * geometry[Z];
		ba_mp = &ba_system_ptr->grid
			[start[A]][start[X]][start[Y]][start[Z]];
	}

	if (!ba_mp)
		return NULL;

	if (!results)
		results = list_create(NULL);
	else
		send_results = 1;
	/* This midplane should have already been checked if it was in
	   use or not */
	list_append(results, ba_mp);

	if (conn_type[A] >= SELECT_SMALL) {
		/* adding the ba_mp and ending */
		ba_mp->used = true;
		name = xstrdup_printf("%c%c%c%c",
				      alpha_num[ba_mp->coord[A]],
				      alpha_num[ba_mp->coord[X]],
				      alpha_num[ba_mp->coord[Y]],
				      alpha_num[ba_mp->coord[Z]]);
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

	/* FIXME: THIS NEEDS TO GO FIND THE NODES NOW */

	/**********************************************/

	if (found) {
		if (cluster_flags & CLUSTER_FLAG_BG) {
			List start_list = NULL;
			ListIterator itr;

			start_list = list_create(NULL);
			itr = list_iterator_create(results);
			while ((ba_mp = (ba_mp_t*) list_next(itr))) {
				list_append(start_list, ba_mp);
			}
			list_iterator_destroy(itr);

			if (!_fill_in_coords(results,
					     start_list,
					     geometry,
					     conn_type)) {
				list_destroy(start_list);
				goto end_it;
			}
			list_destroy(start_list);
		}
	} else {
		goto end_it;
	}

	name = _set_internal_wires(results,
				   size,
				   conn_type[A]);
end_it:
	if (!send_results && results) {
		list_destroy(results);
		results = NULL;
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
extern int reset_ba_system(bool track_down_mps)
{
	int a, x, y, z;

	for (a = 0; a < DIM_SIZE[A]; a++)
		for (x = 0; x < DIM_SIZE[X]; x++)
			for (y = 0; y < DIM_SIZE[Y]; y++)
				for (z = 0; z < DIM_SIZE[Z]; z++) {
					ba_mp_t *ba_mp = &ba_system_ptr->
						grid[a][x][y][z];
					ba_setup_mp(ba_mp, ba_mp->coord,
						    track_down_mps);
				}

	return 1;
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
#ifdef HAVE_BG_Q
	int j=0, number;
	int x;
	int y,z;
	int start[cluster_dims];
        int end[cluster_dims];

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
			for (x = start[X]; x <= end[X]; x++) {
				for (y = start[Y]; y <= end[Y]; y++) {
					for (z = start[Z]; z <= end[Z]; z++) {
						if (!ba_system_ptr->
						    grid[x][y][z].used)
							ba_system_ptr->
								grid[a][x][y][z]
								.used = 2;
					}
				}
			}

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
			if (!ba_system_ptr->grid[a][x][y][z].used)
				ba_system_ptr->grid[a][x][y][z].used = 2;

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
	int a, b, c, d;

	for (a = 0; a < DIM_SIZE[A]; a++) {
		for (b = 0; b < DIM_SIZE[X]; b++) {
			for (c = 0; c < DIM_SIZE[Y]; c++)
				for (d = 0; d < DIM_SIZE[Z]; d++)
					if (ba_system_ptr->grid
					    [a][b][c][d].used == 2)
						ba_system_ptr->grid
							[a][b][c][d].used = 0;
		}
	}
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
	int a, b, c, d;
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
		for (b = 0; b < DIM_SIZE[X]; b++)
			for (c = 0; c < DIM_SIZE[Y]; c++)
				for (d = 0; d < DIM_SIZE[Z]; d++) {
					if (ba_system_ptr->grid
					    [a][b][c][d].state
					    & NODE_RESUME) {
						/* clear the bit and mark as unused */
						ba_system_ptr->grid
							[a][b][c][d].state &=
							~NODE_RESUME;
					} else if (!ba_system_ptr->grid
						   [a][b][c][d].used) {
						ba_system_ptr->grid
							[a][b][c][d].used = 2;
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
	ListIterator itr;
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

	itr = list_iterator_create(ba_midplane_list);
	for (a = 0; a <= DIM_SIZE[A]; ++a)
		for (x = 0; x <= DIM_SIZE[X]; ++x)
			for (y = 0; y <= DIM_SIZE[Y]; ++y)
				for (z = 0; z <= DIM_SIZE[Z]; ++z)
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

	if (coord[A] > DIM_SIZE[A]
	    || coord[X] > DIM_SIZE[X]
	    || coord[Y] > DIM_SIZE[Y]
	    || coord[Z] > DIM_SIZE[Z]) {
		error("This location %s is not possible in our system %c%c%c%c",
		      axyz,
 		      alpha_num[DIM_SIZE[A]],
		      alpha_num[DIM_SIZE[X]],
		      alpha_num[DIM_SIZE[Y]],
		      alpha_num[DIM_SIZE[Z]]);
		return NULL;
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
/* extern List get_and_set_block_wiring(char *bg_block_id, */
/* 				     rm_partition_t *block_ptr) */
/* { */
/* #if defined HAVE_BG_FILES && defined HAVE_BG_Q */
/* 	int rc, i, j; */
/* 	int cnt = 0; */
/* 	int switch_cnt = 0; */
/* 	rm_switch_t *curr_switch = NULL; */
/* 	rm_BP_t *curr_mp = NULL; */
/* 	char *switchid = NULL; */
/* 	rm_connection_t curr_conn; */
/* 	int dim; */
/* 	ba_mp_t *ba_mp = NULL; */
/* 	ba_switch_t *ba_switch = NULL; */
/* 	uint16_t *geo = NULL; */
/* 	List results = list_create(destroy_ba_mp); */
/* 	ListIterator itr = NULL; */

/* 	if (ba_debug_flags & DEBUG_FLAG_BG_ALGO) */
/* 		info("getting info for block %s", bg_block_id); */

/* 	if ((rc = bridge_get_data(block_ptr, RM_PartitionSwitchNum, */
/* 				  &switch_cnt)) != STATUS_OK) { */
/* 		error("bridge_get_data(RM_PartitionSwitchNum): %s", */
/* 		      bridge_err_str(rc)); */
/* 		goto end_it; */
/* 	} */
/* 	if (!switch_cnt) { */
/* 		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP) */
/* 			info("no switch_cnt"); */
/* 		if ((rc = bridge_get_data(block_ptr, */
/* 					  RM_PartitionFirstBP, */
/* 					  &curr_mp)) */
/* 		    != STATUS_OK) { */
/* 			error("bridge_get_data: " */
/* 			      "RM_PartitionFirstBP: %s", */
/* 			      bridge_err_str(rc)); */
/* 			goto end_it; */
/* 		} */
/* 		if ((rc = bridge_get_data(curr_mp, RM_BPID, &switchid)) */
/* 		    != STATUS_OK) { */
/* 			error("bridge_get_data: RM_SwitchBPID: %s", */
/* 			      bridge_err_str(rc)); */
/* 			goto end_it; */
/* 		} */

/* 		geo = find_mp_loc(switchid); */
/* 		if (!geo) { */
/* 			error("find_mp_loc: mpid %s not known", switchid); */
/* 			goto end_it; */
/* 		} */
/* 		ba_mp = xmalloc(sizeof(ba_mp_t)); */
/* 		list_push(results, ba_mp); */
/* 		ba_mp->coord[X] = geo[X]; */
/* 		ba_mp->coord[Y] = geo[Y]; */
/* 		ba_mp->coord[Z] = geo[Z]; */

/* 		ba_mp->used = TRUE; */
/* 		return results; */
/* 	} */
/* 	for (i=0; i<switch_cnt; i++) { */
/* 		if (i) { */
/* 			if ((rc = bridge_get_data(block_ptr, */
/* 						  RM_PartitionNextSwitch, */
/* 						  &curr_switch)) */
/* 			    != STATUS_OK) { */
/* 				error("bridge_get_data: " */
/* 				      "RM_PartitionNextSwitch: %s", */
/* 				      bridge_err_str(rc)); */
/* 				goto end_it; */
/* 			} */
/* 		} else { */
/* 			if ((rc = bridge_get_data(block_ptr, */
/* 						  RM_PartitionFirstSwitch, */
/* 						  &curr_switch)) */
/* 			    != STATUS_OK) { */
/* 				error("bridge_get_data: " */
/* 				      "RM_PartitionFirstSwitch: %s", */
/* 				      bridge_err_str(rc)); */
/* 				goto end_it; */
/* 			} */
/* 		} */
/* 		if ((rc = bridge_get_data(curr_switch, RM_SwitchDim, &dim)) */
/* 		    != STATUS_OK) { */
/* 			error("bridge_get_data: RM_SwitchDim: %s", */
/* 			      bridge_err_str(rc)); */
/* 			goto end_it; */
/* 		} */
/* 		if ((rc = bridge_get_data(curr_switch, RM_SwitchBPID, */
/* 					  &switchid)) */
/* 		    != STATUS_OK) { */
/* 			error("bridge_get_data: RM_SwitchBPID: %s", */
/* 			      bridge_err_str(rc)); */
/* 			goto end_it; */
/* 		} */

/* 		geo = find_mp_loc(switchid); */
/* 		if (!geo) { */
/* 			error("find_mp_loc: mpid %s not known", switchid); */
/* 			goto end_it; */
/* 		} */

/* 		if ((rc = bridge_get_data(curr_switch, RM_SwitchConnNum, &cnt)) */
/* 		    != STATUS_OK) { */
/* 			error("bridge_get_data: RM_SwitchBPID: %s", */
/* 			      bridge_err_str(rc)); */
/* 			goto end_it; */
/* 		} */
/* 		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO) */
/* 			info("switch id = %s dim %d conns = %d", */
/* 			     switchid, dim, cnt); */

/* 		itr = list_iterator_create(results); */
/* 		while ((ba_mp = list_next(itr))) { */
/* 			if (ba_mp->coord[X] == geo[X] && */
/* 			    ba_mp->coord[Y] == geo[Y] && */
/* 			    ba_mp->coord[Z] == geo[Z]) */
/* 				break;	/\* we found it *\/ */
/* 		} */
/* 		list_iterator_destroy(itr); */
/* 		if (!ba_mp) { */
/* 			ba_mp = xmalloc(sizeof(ba_mp_t)); */

/* 			list_push(results, ba_mp); */
/* 			ba_mp->coord[X] = geo[X]; */
/* 			ba_mp->coord[Y] = geo[Y]; */
/* 			ba_mp->coord[Z] = geo[Z]; */
/* 		} */
/* 		ba_switch = &ba_mp->axis_switch[dim]; */
/* 		for (j=0; j<cnt; j++) { */
/* 			if (j) { */
/* 				if ((rc = bridge_get_data( */
/* 					     curr_switch, */
/* 					     RM_SwitchNextConnection, */
/* 					     &curr_conn)) */
/* 				    != STATUS_OK) { */
/* 					error("bridge_get_data: " */
/* 					      "RM_SwitchNextConnection: %s", */
/* 					      bridge_err_str(rc)); */
/* 					goto end_it; */
/* 				} */
/* 			} else { */
/* 				if ((rc = bridge_get_data( */
/* 					     curr_switch, */
/* 					     RM_SwitchFirstConnection, */
/* 					     &curr_conn)) */
/* 				    != STATUS_OK) { */
/* 					error("bridge_get_data: " */
/* 					      "RM_SwitchFirstConnection: %s", */
/* 					      bridge_err_str(rc)); */
/* 					goto end_it; */
/* 				} */
/* 			} */
/* 			switch(curr_conn.p1) { */
/* 			case RM_PORT_S1: */
/* 				curr_conn.p1 = 1; */
/* 				break; */
/* 			case RM_PORT_S2: */
/* 				curr_conn.p1 = 2; */
/* 				break; */
/* 			case RM_PORT_S4: */
/* 				curr_conn.p1 = 4; */
/* 				break; */
/* 			default: */
/* 				error("1 unknown port %d", */
/* 				      _port_enum(curr_conn.p1)); */
/* 				goto end_it; */
/* 			} */

/* 			switch(curr_conn.p2) { */
/* 			case RM_PORT_S0: */
/* 				curr_conn.p2 = 0; */
/* 				break; */
/* 			case RM_PORT_S3: */
/* 				curr_conn.p2 = 3; */
/* 				break; */
/* 			case RM_PORT_S5: */
/* 				curr_conn.p2 = 5; */
/* 				break; */
/* 			default: */
/* 				error("2 unknown port %d", */
/* 				      _port_enum(curr_conn.p2)); */
/* 				goto end_it; */
/* 			} */

/* 			if (curr_conn.p1 == 1 && dim == B) { */
/* 				if (ba_mp->used) { */
/* 					debug("I have already been to " */
/* 					      "this node %c%c%c", */
/* 					      alpha_num[geo[X]], */
/* 					      alpha_num[geo[Y]], */
/* 					      alpha_num[geo[Z]]); */
/* 					goto end_it; */
/* 				} */
/* 				ba_mp->used = true; */
/* 			} */
/* 			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP) */
/* 				info("connection going from %d -> %d", */
/* 				     curr_conn.p1, curr_conn.p2); */

/* 			if (ba_switch->int_wire[curr_conn.p1].used) { */
/* 				debug("%c%c%c dim %d port %d " */
/* 				      "is already in use", */
/* 				      alpha_num[geo[X]], */
/* 				      alpha_num[geo[Y]], */
/* 				      alpha_num[geo[Z]], */
/* 				      dim, */
/* 				      curr_conn.p1); */
/* 				goto end_it; */
/* 			} */
/* 			ba_switch->int_wire[curr_conn.p1].used = 1; */
/* 			ba_switch->int_wire[curr_conn.p1].port_tar */
/* 				= curr_conn.p2; */

/* 			if (ba_switch->int_wire[curr_conn.p2].used) { */
/* 				debug("%c%c%c dim %d port %d " */
/* 				      "is already in use", */
/* 				      alpha_num[geo[X]], */
/* 				      alpha_num[geo[Y]], */
/* 				      alpha_num[geo[Z]], */
/* 				      dim, */
/* 				      curr_conn.p2); */
/* 				goto end_it; */
/* 			} */
/* 			ba_switch->int_wire[curr_conn.p2].used = 1; */
/* 			ba_switch->int_wire[curr_conn.p2].port_tar */
/* 				= curr_conn.p1; */
/* 		} */
/* 	} */
/* 	return results; */
/* end_it: */
/* 	list_destroy(results); */
/* 	return NULL; */
/* #else */
/* 	return NULL; */
/* #endif */

/* } */

/* */
extern int validate_coord(uint16_t *coord)
{
	if (coord[A]>=REAL_DIM_SIZE[A]
	    || coord[X]>=REAL_DIM_SIZE[X]
	    || coord[Y]>=REAL_DIM_SIZE[Y]
	    || coord[Z]>=REAL_DIM_SIZE[Z]) {
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
	}

	if (coord[X]>=DIM_SIZE[X]
	    || coord[Y]>=DIM_SIZE[Y]
	    || coord[Z]>=DIM_SIZE[Z]) {
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

	return 1;
}


/*
 * This function is here to check options for rotating and elongating
 * and set up the request based on the count of each option
 */
static int _check_for_options(ba_request_t* ba_request)
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
			ba_request->geometry[A]=ba_request->geometry[Z];
			ba_request->geometry[Z]=temp;
			ba_request->rotate_count++;
			set=1;

		} else if (ba_request->rotate_count<(cluster_dims*2)) {
			temp=ba_request->geometry[X];
			ba_request->geometry[A]=ba_request->geometry[X];
			ba_request->geometry[X]=ba_request->geometry[Y];
			ba_request->geometry[Y]=ba_request->geometry[Z];
			ba_request->geometry[Z]=temp;
			ba_request->rotate_count++;
			set=1;
		} else
			ba_request->rotate = false;
		if (set) {
			if (ba_request->geometry[A]<=DIM_SIZE[A]
			    && ba_request->geometry[X]<=DIM_SIZE[X]
			    && ba_request->geometry[Y]<=DIM_SIZE[Y]
			    && ba_request->geometry[Z]<=DIM_SIZE[Z])
				return 1;
			else {
				set = 0;
				goto rotate_again;
			}
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
static int _fill_in_coords(List results, List start_list,
			   uint16_t *geometry, uint16_t *conn_type)
{
	ba_mp_t *ba_mp = NULL;
	ba_mp_t *check_mp = NULL;
	int rc = 1;
	ListIterator itr = NULL;
	int a=0, b=0, c=0, d=0;
	int ua=0, ub=0, uc=0, ud=0;
	ba_switch_t *curr_switch = NULL;
	ba_switch_t *next_switch = NULL;

	if (!start_list || !results)
		return 0;
	/* go through the start_list and add all the midplanes */
	itr = list_iterator_create(start_list);
	while ((check_mp = (ba_mp_t*) list_next(itr))) {
		curr_switch = &check_mp->axis_switch[A];

		for (a=0; a<geometry[A]; a++) {
			ua = check_mp->coord[A]+a;
			if (ua >= DIM_SIZE[A]) {
				rc = 0;
				goto failed;
			}
			for (b=0; c<geometry[X]; b++) {
				ub = check_mp->coord[X]+b;
				if (ub >= DIM_SIZE[X]) {
					rc = 0;
					goto failed;
				}
				for (c=0; c<geometry[Y]; c++) {
					uc = check_mp->coord[Y]+c;
					if (uc >= DIM_SIZE[Y]) {
						rc = 0;
						goto failed;
					}
					for (d=0; d<geometry[Z]; d++) {
						ud = check_mp->coord[Z]+d;
						if (ud >= DIM_SIZE[Z]) {
							rc = 0;
							goto failed;
						}
						ba_mp = &ba_system_ptr->grid
							[ua][ub][uc][ud];

						if ((ba_mp->coord[Y] == check_mp->coord[Y])
						    && (ba_mp->coord[Z]
							== check_mp->coord[Z]))
							continue;

						if (!_mp_used(ba_mp, geometry[A])) {
							if (ba_debug_flags
							    & DEBUG_FLAG_BG_ALGO_DEEP)
								info("here Adding %c%c%c%c",
								     alpha_num[ba_mp->
									       coord[A]],
								     alpha_num[ba_mp->
									       coord[X]],
								     alpha_num[ba_mp->
									       coord[Y]],
								     alpha_num[ba_mp->
									       coord[Z]]);
							list_append(results, ba_mp);
							next_switch = &ba_mp->axis_switch[A];

							/* since we are going off the
							 * main system we can send NULL
							 * here
							 */
							_copy_the_path(NULL, curr_switch,
								       next_switch,
								       0, A);
						} else {
							rc = 0;
							goto failed;
						}
					}
				}
			}
		}
	}
	list_iterator_destroy(itr);
	itr = list_iterator_create(start_list);
	check_mp = (ba_mp_t*) list_next(itr);
	list_iterator_destroy(itr);

	itr = list_iterator_create(results);
	while ((ba_mp = (ba_mp_t*) list_next(itr))) {
		if (!_find_path(ba_mp, check_mp->coord,
				geometry, conn_type)) {
			rc = 0;
			goto failed;
		}
	}

	if (deny_pass) {
		if ((*deny_pass & PASS_DENY_A)
		    && (*deny_pass & PASS_FOUND_A)) {
			debug("We don't allow A passthoughs");
			rc = 0;
		} else if ((*deny_pass & PASS_DENY_X)
		    && (*deny_pass & PASS_FOUND_X)) {
			debug("We don't allow X passthoughs");
			rc = 0;
		} else if ((*deny_pass & PASS_DENY_Y)
		    && (*deny_pass & PASS_FOUND_Y)) {
			debug("We don't allow Y passthoughs");
			rc = 0;
		} else if ((*deny_pass & PASS_DENY_Z)
			   && (*deny_pass & PASS_FOUND_Z)) {
			debug("We don't allow Z passthoughs");
			rc = 0;
		}
	}

failed:
	list_iterator_destroy(itr);

	return rc;
}

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
 * IN: source - source port number (If calling for the first time
 *         should be 0 since we are looking for 1 at the end)
 * IN: dim - Dimension BCD
 *
 * RET: on success 1, on error 0
 */
static int _copy_the_path(List mps, ba_switch_t *curr_switch,
			  ba_switch_t *mark_switch,
			  int source, int dim)
{
	uint16_t *mp_tar;
	uint16_t *mark_mp_tar;
	uint16_t *mp_curr;
	int port_tar, port_tar1;
	ba_switch_t *next_switch = NULL;
	ba_switch_t *next_mark_switch = NULL;

	/* Copy the source used and port_tar */
	mark_switch->int_wire[source].used =
		curr_switch->int_wire[source].used;
	mark_switch->int_wire[source].port_tar =
		curr_switch->int_wire[source].port_tar;

	port_tar = curr_switch->int_wire[source].port_tar;

	/* Now to the same thing from the other end */
	mark_switch->int_wire[port_tar].used =
		curr_switch->int_wire[port_tar].used;
	mark_switch->int_wire[port_tar].port_tar =
		curr_switch->int_wire[port_tar].port_tar;
	port_tar1 = port_tar;

	/* follow the path */
	mp_curr = curr_switch->ext_wire[0].mp_tar;
	mp_tar = curr_switch->ext_wire[port_tar].mp_tar;
	if (mark_switch->int_wire[source].used)
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
			info("setting dim %d %c%c%c %d-> %c%c%c %d",
			     dim,
			     alpha_num[mp_curr[X]],
			     alpha_num[mp_curr[Y]],
			     alpha_num[mp_curr[Z]],
			     source,
			     alpha_num[mp_tar[X]],
			     alpha_num[mp_tar[Y]],
			     alpha_num[mp_tar[Z]],
			     port_tar);

	if (port_tar == 1) {
		/* found the end of the line */
		mark_switch->int_wire[1].used =
			curr_switch->int_wire[1].used;
		mark_switch->int_wire[1].port_tar =
			curr_switch->int_wire[1].port_tar;
		return 1;
	}

	mark_mp_tar = mark_switch->ext_wire[port_tar].mp_tar;
	port_tar = curr_switch->ext_wire[port_tar].port_tar;

	if (mp_curr[A] == mp_tar[A]
	    && mp_curr[X] == mp_tar[X]
	    && mp_curr[Y] == mp_tar[Y]
	    && mp_curr[Z] == mp_tar[Z]) {
		/* We are going to the same node! this should never
		   happen */
		debug5("something bad happened!! "
		       "we are on %c%c%c%c and are going to it "
		       "from port %d - > %d",
		       alpha_num[mp_curr[A]],
		       alpha_num[mp_curr[X]],
		       alpha_num[mp_curr[Y]],
		       alpha_num[mp_curr[Z]],
		       port_tar1, port_tar);
		return 0;
	}

	/* see what the next switch is going to be */
	next_switch = &ba_system_ptr->
		grid[mp_tar[A]][mp_tar[X]][mp_tar[Y]]
		[mp_tar[Z]].axis_switch[dim];
	if (!mps) {
		/* If no mps then just get the next switch to fill
		   in from the main system */
		next_mark_switch = &ba_system_ptr->
			grid[mark_mp_tar[A]]
			[mark_mp_tar[X]]
			[mark_mp_tar[Y]]
			[mark_mp_tar[Z]]
			.axis_switch[dim];
	} else {
		ba_mp_t *ba_mp = NULL;
		ListIterator itr = list_iterator_create(mps);
		/* see if we have already been to this mp */
		while ((ba_mp = (ba_mp_t *)list_next(itr))) {
			if (ba_mp->coord[A] == mark_mp_tar[A] &&
			    ba_mp->coord[X] == mark_mp_tar[X] &&
			    ba_mp->coord[Y] == mark_mp_tar[Y] &&
			    ba_mp->coord[Z] == mark_mp_tar[Z])
				break;	/* we found it */
		}
		list_iterator_destroy(itr);
		if (!ba_mp) {
			/* If mp grab a copy and add it to the list */
			ba_mp = ba_copy_mp(&ba_system_ptr->
					       grid[mark_mp_tar[A]]
					       [mark_mp_tar[X]]
					       [mark_mp_tar[Y]]
					       [mark_mp_tar[Z]]);
			ba_setup_mp(ba_mp, mark_mp_tar, false);
			list_push(mps, ba_mp);
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("haven't seen %c%c%c%c adding it",
				     alpha_num[ba_mp->coord[A]],
				     alpha_num[ba_mp->coord[X]],
				     alpha_num[ba_mp->coord[Y]],
				     alpha_num[ba_mp->coord[Z]]);
		}
		next_mark_switch = &ba_mp->axis_switch[dim];

	}

	/* Keep going until we reach the end of the line */
	return _copy_the_path(mps, next_switch, next_mark_switch,
			      port_tar, dim);
}

static int _find_path(ba_mp_t *ba_mp, uint16_t *first,
		      uint16_t *geometry, uint16_t *conn_type)
{
	ba_mp_t *next_mp = NULL;
	uint16_t *mp_tar = NULL;
	ba_switch_t *dim_curr_switch = NULL;
	ba_switch_t *dim_next_switch = NULL;
	int i2;
	int count = 0;

	for(i2=A;i2<=Z;i2++) {
		if (geometry[i2] > 1) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("%d mp %c%c%c%c port 2 -> ",
				     i2,
				     alpha_num[ba_mp->coord[A]],
				     alpha_num[ba_mp->coord[X]],
				     alpha_num[ba_mp->coord[Y]],
				     alpha_num[ba_mp->coord[Z]]);

			dim_curr_switch = &ba_mp->axis_switch[i2];
			if (dim_curr_switch->int_wire[2].used) {
				debug5("returning here");
				return 0;
			}

			mp_tar = dim_curr_switch->ext_wire[2].mp_tar;

			next_mp = &ba_system_ptr->
				grid[mp_tar[A]][mp_tar[X]]
				[mp_tar[Y]][mp_tar[Z]];
			dim_next_switch = &next_mp->axis_switch[i2];
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("%c%c%c%c port 5",
				     alpha_num[next_mp->coord[A]],
				     alpha_num[next_mp->coord[X]],
				     alpha_num[next_mp->coord[Y]],
				     alpha_num[next_mp->coord[Z]]);

			if (dim_next_switch->int_wire[5].used) {
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
					info("returning here 2");
				return 0;
			}
			debug5("%d %d %d %d",i2, mp_tar[i2],
			       first[i2], geometry[i2]);

			/* Here we need to see where we are in
			 * reference to the geo of this dimension.  If
			 * we have not gotten the number we need in
			 * the direction we just go to the next mp
			 * with 5 -> 1.  If we have all the midplanes
			 * we need then we go through and finish the
			 * torus if needed
			 */
			if (mp_tar[i2] < first[i2])
				count = mp_tar[i2]+(DIM_SIZE[i2]-first[i2]);
			else
				count = (mp_tar[i2]-first[i2]);

			if (count == geometry[i2]) {
				debug5("found end of me %c%c%c%c",
				       alpha_num[mp_tar[A]],
				       alpha_num[mp_tar[X]],
				       alpha_num[mp_tar[Y]],
				       alpha_num[mp_tar[Z]]);
				if (conn_type[i2] == SELECT_TORUS) {
					dim_curr_switch->int_wire[0].used = 1;
					dim_curr_switch->int_wire[0].port_tar
						= 2;
					dim_curr_switch->int_wire[2].used = 1;
					dim_curr_switch->int_wire[2].port_tar
						= 0;
					dim_curr_switch = dim_next_switch;

					if (deny_pass
					    && (mp_tar[i2] != first[i2])) {
						if (i2 == 1)
							*deny_pass |=
								PASS_FOUND_Y;
						else
							*deny_pass |=
								PASS_FOUND_Z;
					}
					while (mp_tar[i2] != first[i2]) {
						if (ba_debug_flags
						    & DEBUG_FLAG_BG_ALGO_DEEP)
							info("on dim %d at %d "
							     "looking for %d",
							     i2,
							     mp_tar[i2],
							     first[i2]);

						if (dim_curr_switch->
						    int_wire[2].used) {
							if (ba_debug_flags
							    & DEBUG_FLAG_BG_ALGO_DEEP)
								info("returning"
								     " here 3");
							return 0;
						}

						dim_curr_switch->
							int_wire[2].used = 1;
						dim_curr_switch->
							int_wire[2].port_tar
							= 5;
						dim_curr_switch->
							int_wire[5].used
							= 1;
						dim_curr_switch->
							int_wire[5].
							port_tar = 2;


						mp_tar = dim_curr_switch->
							ext_wire[2].mp_tar;
						next_mp = &ba_system_ptr->
							grid
							[mp_tar[A]]
							[mp_tar[X]]
							[mp_tar[Y]]
							[mp_tar[Z]];
						dim_curr_switch =
							&next_mp->
							axis_switch[i2];
					}

					if (ba_debug_flags
					    & DEBUG_FLAG_BG_ALGO_DEEP)
						info("back to first on dim %d "
						     "at %d looking for %d",
						     i2,
						     mp_tar[i2],
						     first[i2]);

					dim_curr_switch->
						int_wire[5].used = 1;
					dim_curr_switch->
						int_wire[5].port_tar
						= 1;
					dim_curr_switch->
						int_wire[1].used
						= 1;
					dim_curr_switch->
						int_wire[1].
						port_tar = 5;
				}

			} else if (count < geometry[i2]) {
				if (conn_type[i2] == SELECT_TORUS ||
				    (conn_type[i2] == SELECT_MESH &&
				     (mp_tar[i2] != first[i2]))) {
					dim_curr_switch->
						int_wire[0].used = 1;
					dim_curr_switch->
						int_wire[0].port_tar
						= 2;
					dim_curr_switch->
						int_wire[2].used
						= 1;
					dim_curr_switch->
						int_wire[2].
						port_tar = 0;

					dim_next_switch->int_wire[5].used
						= 1;
					dim_next_switch->
						int_wire[5].port_tar
						= 1;
					dim_next_switch->
						int_wire[1].used = 1;
					dim_next_switch->
						int_wire[1].port_tar
						= 5;
				}
			} else {
				error("We were only looking for %d "
				      "in the %d dim, but now we have %d",
				      geometry[i2], i2, count);
				return 0;
			}
		} else if ((geometry[i2] == 1)
			   && (conn_type[i2] == SELECT_TORUS)) {
			/* FIB ME: This is put here because we got
			   into a state where the C dim was not being
			   processed correctly.  This will set up the
			   0 -> 1 port correctly.  We should probably
			   find out why this was happening in the
			   first place though.  A reproducer was to
			   have
			   BPs=[310x323] Type=TORUS
			   BPs=[200x233] Type=TORUS
			   BPs=[300x303] Type=TORUS
			   BPs=[100x133] Type=TORUS
			   BPs=[000x033] Type=TORUS
			   BPs=[400x433] Type=TORUS
			   and then add
			   BPs=[330x333] Type=TORUS
			*/

			dim_curr_switch = &ba_mp->axis_switch[i2];
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("%d mp %c%c%c port 0 -> 1",
				     i2,
				     alpha_num[ba_mp->coord[X]],
				     alpha_num[ba_mp->coord[Y]],
				     alpha_num[ba_mp->coord[Z]]);
			dim_curr_switch->int_wire[0].used = 1;
			dim_curr_switch->int_wire[0].port_tar = 1;
			dim_curr_switch->int_wire[1].used = 1;
			dim_curr_switch->int_wire[1].port_tar = 0;
		}
	}
	return 1;
}

#ifndef HAVE_BG_FILES
/** */
static int _emulate_ext_wiring(ba_mp_t ****grid)
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
			_set_external_wires(A, a, source, target);
		}
	} else {
		int b,c,d;
		init_wires();

		for (a=0;a<DIM_SIZE[A];a++)
			for (b=0;b<DIM_SIZE[X];b++)
				for (c=0;c<DIM_SIZE[Y];c++)
					for(d=0;d<DIM_SIZE[Z];d++) {
						source = &grid[a][b][c][d];

						if (a<(DIM_SIZE[A]-1)) {
							target = &grid
								[a+1][b][c][d];
						} else
							target = &grid
								[0][b][c][d];

						_set_external_wires(A, a,
								    source,
								    target);

						if (b<(DIM_SIZE[X]-1)) {
							target = &grid
								[a][b+1][c][d];
						} else
							target = &grid
								[a][0][c][d];

						_set_external_wires(X, b,
								    source,
								    target);

						if (c<(DIM_SIZE[Y]-1))
							target = &grid
								[a][b][c+1][d];
						else
							target = &grid
								[a][b][0][d];

						_set_external_wires(Y, c,
								    source,
								    target);
						if (d<(DIM_SIZE[Z]-1))
							target = &grid
								[a][b][c][d+1];
						else
							target = &grid
								[a][b][c][0];

						_set_external_wires(Z, d,
								    source,
								    target);
					}
	}
	return 1;
}
#endif


static int _reset_the_path(ba_switch_t *curr_switch, int source,
			   int target, int dim)
{
	uint16_t *mp_tar;
	uint16_t *mp_curr;
	int port_tar, port_tar1;
	ba_switch_t *next_switch = NULL;

	if (source < 0 || source > NUM_PORTS_PER_NODE) {
		fatal("source port was %d can only be 0->%d",
		      source, NUM_PORTS_PER_NODE);
	}
	if (target < 0 || target > NUM_PORTS_PER_NODE) {
		fatal("target port was %d can only be 0->%d",
		      target, NUM_PORTS_PER_NODE);
	}
	/*set the switch to not be used */
	if (!curr_switch->int_wire[source].used) {
		/* This means something overlapping the removing block
		   already cleared this, or the path just never was
		   complete in the first place. */
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
			info("I reached the end, the source isn't used");
		return 1;
	}
	curr_switch->int_wire[source].used = 0;
	port_tar = curr_switch->int_wire[source].port_tar;
	if (port_tar < 0 || port_tar > NUM_PORTS_PER_NODE) {
		fatal("port_tar port was %d can only be 0->%d",
		      source, NUM_PORTS_PER_NODE);
	}

	port_tar1 = port_tar;
	curr_switch->int_wire[source].port_tar = source;
	curr_switch->int_wire[port_tar].used = 0;
	curr_switch->int_wire[port_tar].port_tar = port_tar;
	if (port_tar==target) {
		return 1;
	}
	/* follow the path */
	mp_curr = curr_switch->ext_wire[0].mp_tar;
	mp_tar = curr_switch->ext_wire[port_tar].mp_tar;
	port_tar = curr_switch->ext_wire[port_tar].port_tar;
	if (source == port_tar1) {
		debug("got this bad one %c%c%c%c %d %d -> %c%c%c%c %d",
		      alpha_num[mp_curr[A]],
		      alpha_num[mp_curr[X]],
		      alpha_num[mp_curr[Y]],
		      alpha_num[mp_curr[Z]],
		      source,
		      port_tar1,
		      alpha_num[mp_tar[A]],
		      alpha_num[mp_tar[X]],
		      alpha_num[mp_tar[Y]],
		      alpha_num[mp_tar[Z]],
		      port_tar);
		return 0;
	}
	debug5("from %c%c%c%c %d %d -> %c%c%c%c %d",
	       alpha_num[mp_curr[A]],
	       alpha_num[mp_curr[X]],
	       alpha_num[mp_curr[Y]],
	       alpha_num[mp_curr[Z]],
	       source,
	       port_tar1,
	       alpha_num[mp_tar[A]],
	       alpha_num[mp_tar[X]],
	       alpha_num[mp_tar[Y]],
	       alpha_num[mp_tar[Z]],
	       port_tar);
	if (mp_curr[A] == mp_tar[A]
	    && mp_curr[X] == mp_tar[X]
	    && mp_curr[Y] == mp_tar[Y]
	    && mp_curr[Z] == mp_tar[Z]) {
		debug5("%d something bad happened!!", dim);
		return 0;
	}
	next_switch = &ba_system_ptr->
		grid[mp_tar[A]][mp_tar[X]]
		[mp_tar[Y]][mp_tar[Z]].axis_switch[dim];

	return _reset_the_path(next_switch, port_tar, target, dim);
//	return 1;
}

/** */
static void _delete_ba_system(void)
{
	int a, b, c;

	if (!ba_system_ptr) {
		return;
	}

	if (ba_system_ptr->grid) {
		for (a=0; a<DIM_SIZE[A]; a++) {
			for (b=0; b<DIM_SIZE[X]; b++) {
				for (c=0; c<DIM_SIZE[Y]; c++)
					xfree(ba_system_ptr->grid[a][b][c]);
				xfree(ba_system_ptr->grid[a][b]);
			}
			xfree(ba_system_ptr->grid[a]);
		}
		xfree(ba_system_ptr->grid);
	}
	xfree(ba_system_ptr);
}

static void _delete_path_list(void *object)
{
	ba_path_switch_t *path_switch = (ba_path_switch_t *)object;

	if (path_switch) {
		xfree(path_switch);
	}
	return;
}

/**
 * algorithm for finding match
 */
static int _find_match(ba_request_t *ba_request, List results)
{
	int x=0;
	uint16_t start[cluster_dims];
	ba_mp_t *ba_mp = NULL;
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
		for(x=0;x<cluster_dims;x++) {
			if (ba_request->start[x]>=DIM_SIZE[x])
				return 0;
			start[x] = ba_request->start[x];
		}
	}
	x=0;

	/* set up the geo here */
	if (!(geo_ptr = (uint16_t *)list_peek(ba_request->elongate_geos)))
		return 0;
	ba_request->rotate_count=0;
	ba_request->elongate_count=1;
	ba_request->geometry[A] = geo_ptr[A];
	ba_request->geometry[X] = geo_ptr[X];
	ba_request->geometry[Y] = geo_ptr[Y];
	ba_request->geometry[Z] = geo_ptr[Z];

	if (ba_request->geometry[A]>DIM_SIZE[A]
	    || ba_request->geometry[X]>DIM_SIZE[X]
	    || ba_request->geometry[Y]>DIM_SIZE[Y]
	    || ba_request->geometry[Z]>DIM_SIZE[Z])
		if (!_check_for_options(ba_request))
			return 0;

start_again:
	x=0;
	if (x == startx)
		x = startx-1;
	while (x!=startx) {
		x++;
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("finding %c%c%c%c try %d",
			     alpha_num[ba_request->geometry[A]],
			     alpha_num[ba_request->geometry[X]],
			     alpha_num[ba_request->geometry[Y]],
			     alpha_num[ba_request->geometry[Z]],
			     x);
	new_mp:
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
			info("starting at %c%c%c%c",
			     alpha_num[start[A]],
			     alpha_num[start[X]],
			     alpha_num[start[Y]],
			     alpha_num[start[Z]]);

		ba_mp = &ba_system_ptr->grid
			[start[A]][start[X]][start[Y]][start[Z]];

		if (!_mp_used(ba_mp, ba_request->geometry[X])) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("trying this mp %c%c%c%c %c%c%c%c %d",
				     alpha_num[start[A]],
				     alpha_num[start[X]],
				     alpha_num[start[Y]],
				     alpha_num[start[Z]],
				     alpha_num[ba_request->geometry[A]],
				     alpha_num[ba_request->geometry[X]],
				     alpha_num[ba_request->geometry[Y]],
				     alpha_num[ba_request->geometry[Z]],
				     ba_request->conn_type[A]);
			name = set_bg_block(results,
					    start,
					    ba_request->geometry,
					    ba_request->conn_type);
			if (name) {
				ba_request->save_name = xstrdup(name);
				xfree(name);
				return 1;
			}

			if (results) {
				remove_block(results, color_count,
					     ba_request->conn_type[A]);
				list_delete_all(results,
						&empty_null_destroy_list,
						(void *)"");
			}
			if (ba_request->start_req)
				goto requested_end;
			//exit(0);
			debug2("trying something else");

		}

		if ((DIM_SIZE[Z]-start[Z]-1)
		    >= ba_request->geometry[Z])
			start[Z]++;
		else {
			start[Z] = 0;
			if ((DIM_SIZE[Y]-start[Y]-1)
			    >= ba_request->geometry[Y])
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
 * IN: x_size - How big is the block in the B dim used to see if the
 *     wires are full hence making this midplane unusable.
 */
static bool _mp_used(ba_mp_t* ba_mp, int x_size)
{
	ba_switch_t* ba_switch = NULL;
	/* if we've used this mp in another block already */
	if (!ba_mp || ba_mp->used) {
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("mp %c%c%c%c used",
			     alpha_num[ba_mp->coord[A]],
			     alpha_num[ba_mp->coord[X]],
			     alpha_num[ba_mp->coord[Y]],
			     alpha_num[ba_mp->coord[Z]]);
		return true;
	}
	/* Check If we've used this mp's switches completely in another
	   block already.  Right now we are only needing to look at
	   the B dim since it is the only one with extra wires.  This
	   can be set up to do all the dim's if in the future if it is
	   needed. We only need to check this if we are planning on
	   using more than 1 midplane in the block creation */
	if (x_size > 1) {
		/* get the switch of the B Dimension */
		ba_switch = &ba_mp->axis_switch[X];

		/* If this port is used then the mp
		   is in use since there are no more wires we
		   can use since these can not connect to each
		   other they must be connected to the other ports.
		*/
		if (ba_switch->int_wire[3].used) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("switch full in the B "
				     "dim on mp %c%c%c%c!",
				     alpha_num[ba_mp->coord[A]],
				     alpha_num[ba_mp->coord[X]],
				     alpha_num[ba_mp->coord[Y]],
				     alpha_num[ba_mp->coord[Z]]);
			return true;
		}
	}

	return false;

}


static void _switch_config(ba_mp_t* source, ba_mp_t* target, int dim,
			   int port_src, int port_tar)
{
	ba_switch_t* config = NULL, *config_tar = NULL;
	int i;

	if (!source || !target)
		return;

	config = &source->axis_switch[dim];
	config_tar = &target->axis_switch[dim];
	for(i=0;i<cluster_dims;i++) {
		/* Set the coord of the source target mp to the target */
		config->ext_wire[port_src].mp_tar[i] = target->coord[i];

		/* Set the coord of the target back to the source */
		config_tar->ext_wire[port_tar].mp_tar[i] = source->coord[i];
	}

	/* Set the port of the source target mp to the target */
	config->ext_wire[port_src].port_tar = port_tar;

	/* Set the port of the target back to the source */
	config_tar->ext_wire[port_tar].port_tar = port_src;
}

static int _set_external_wires(int dim, int count, ba_mp_t* source,
			       ba_mp_t* target)
{
	return 1;
}

static char *_set_internal_wires(List mps, int size, int conn_type)
{
	ba_mp_t* ba_mp[size+1];
	int count=0, i, set=0;
	uint16_t *start = NULL;
	uint16_t *end = NULL;
	char *name = NULL;
	ListIterator itr;
	hostlist_t hostlist;
	char temp_name[4];

	if (!mps)
		return NULL;

	hostlist = hostlist_create(NULL);
	itr = list_iterator_create(mps);
	while ((ba_mp[count] = (ba_mp_t *)list_next(itr))) {
		snprintf(temp_name, sizeof(temp_name), "%c%c%c%c",

			 alpha_num[ba_mp[count]->coord[A]],
			 alpha_num[ba_mp[count]->coord[X]],
			 alpha_num[ba_mp[count]->coord[Y]],
			 alpha_num[ba_mp[count]->coord[Z]]);
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("name = %s", temp_name);
		count++;
		hostlist_push(hostlist, temp_name);
	}
	list_iterator_destroy(itr);

	start = ba_mp[0]->coord;
	end = ba_mp[count-1]->coord;
	name = hostlist_ranged_string_xmalloc(hostlist);
	hostlist_destroy(hostlist);

	for (i=0;i<count;i++) {
		if (!ba_mp[i]->used) {
			ba_mp[i]->used=1;
			if (ba_mp[i]->letter == '.') {
				ba_mp[i]->letter = letters[color_count%62];
				ba_mp[i]->color = colors[color_count%6];
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
					info("count %d setting letter = %c "
					     "color = %d",
					     color_count,
					     ba_mp[i]->letter,
					     ba_mp[i]->color);
				set=1;
			}
		} else {
			debug("No network connection to create "
			      "bgblock containing %s", name);
			debug("Use smap to define bgblocks in "
			      "bluegene.conf");
			xfree(name);
			return NULL;
		}
	}

	if (conn_type == SELECT_TORUS)
		for (i=0;i<count;i++) {
			_set_one_dim(start, end, ba_mp[i]->coord);
		}

	if (set)
		color_count++;

	return name;
}

static int _set_one_dim(uint16_t *start, uint16_t *end, uint16_t *coord)
{
	int dim;
	ba_switch_t *curr_switch = NULL;

	for(dim=0;dim<cluster_dims;dim++) {
		if (start[dim]==end[dim]) {
			curr_switch = &ba_system_ptr->grid
				[coord[A]][coord[X]]
				[coord[Y]][coord[Z]].axis_switch[dim];

			if (!curr_switch->int_wire[0].used
			    && !curr_switch->int_wire[1].used) {
				curr_switch->int_wire[0].used = 1;
				curr_switch->int_wire[0].port_tar = 1;
				curr_switch->int_wire[1].used = 1;
				curr_switch->int_wire[1].port_tar = 0;
			}
		}
	}
	return 1;
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
