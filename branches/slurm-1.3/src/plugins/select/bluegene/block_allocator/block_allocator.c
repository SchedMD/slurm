/*****************************************************************************\
 *  block_allocator.c - Assorted functions for layout of bluegene blocks, 
 *	 wiring, mapping for smap, etc.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov>, Danny Auble <da@llnl.gov>
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#define DEBUG_PA
#define BEST_COUNT_INIT 20

/* Global */
bool _initialized = false;
bool _wires_initialized = false;
bool _bp_map_initialized = false;

/* _ba_system is the "current" system that the structures will work
 *  on */
ba_system_t *ba_system_ptr = NULL;
List path = NULL;
List best_path = NULL;
int best_count;
int color_count = 0;
uint16_t *deny_pass = NULL;

/* extern Global */
my_bluegene_t *bg = NULL;
uint16_t ba_deny_pass = 0;
List bp_map_list = NULL;
char letters[62];
char colors[6];
#ifdef HAVE_3D
int DIM_SIZE[BA_SYSTEM_DIMENSIONS] = {0,0,0};
int REAL_DIM_SIZE[BA_SYSTEM_DIMENSIONS] = {0,0,0};
#else
int DIM_SIZE[BA_SYSTEM_DIMENSIONS] = {0};
int REAL_DIM_SIZE[BA_SYSTEM_DIMENSIONS] = {0};
#endif

s_p_options_t bg_conf_file_options[] = {
#ifdef HAVE_BGL
	{"BlrtsImage", S_P_STRING}, 
	{"LinuxImage", S_P_STRING},
	{"RamDiskImage", S_P_STRING},
	{"AltBlrtsImage", S_P_ARRAY, parse_image, NULL}, 
	{"AltLinuxImage", S_P_ARRAY, parse_image, NULL},
	{"AltRamDiskImage", S_P_ARRAY, parse_image, NULL},
#else
	{"CnloadImage", S_P_STRING},
	{"IoloadImage", S_P_STRING},
	{"AltCnloadImage", S_P_ARRAY, parse_image, NULL},
	{"AltIoloadImage", S_P_ARRAY, parse_image, NULL},
#endif
	{"DenyPassthrough", S_P_STRING},
	{"LayoutMode", S_P_STRING},
	{"MloaderImage", S_P_STRING},
	{"BridgeAPILogFile", S_P_STRING},
	{"BridgeAPIVerbose", S_P_UINT16},
	{"BasePartitionNodeCnt", S_P_UINT16},
	{"NodeCardNodeCnt", S_P_UINT16},
	{"Numpsets", S_P_UINT16},
	{"BPs", S_P_ARRAY, parse_blockreq, destroy_blockreq},
	/* these are just going to be put into a list that will be
	   freed later don't free them after reading them */
	{"AltMloaderImage", S_P_ARRAY, parse_image, NULL},
	{NULL}
};

typedef enum {
	BLOCK_ALGO_FIRST,
	BLOCK_ALGO_SECOND
} block_algo_t;

#ifdef HAVE_BG
/** internal helper functions */
#ifdef HAVE_BG_FILES
/** */
static void _bp_map_list_del(void *object);

/** */
static int _port_enum(int port);
#endif /* HAVE_BG_FILES */

/* */
static int _check_for_options(ba_request_t* ba_request);

/* */
static int _append_geo(int *geo, List geos, int rotate);

/* */
static int _fill_in_coords(List results, List start_list, 
			   int *geometry, int conn_type);

/* */
static int _copy_the_path(List nodes, ba_switch_t *curr_switch,
			  ba_switch_t *mark_switch, 
			  int source, int dim);

/* */
static int _find_yz_path(ba_node_t *ba_node, int *first, 
			 int *geometry, int conn_type);
#endif /* HAVE_BG */

#ifndef HAVE_BG_FILES
#ifdef HAVE_3D
/* */
static int _emulate_ext_wiring(ba_node_t ***grid);
#else
/* */
static int _emulate_ext_wiring(ba_node_t *grid);
#endif
#endif

/** */
static void _new_ba_node(ba_node_t *ba_node, int *coord,
			 bool track_down_nodes);
/** */
static int _reset_the_path(ba_switch_t *curr_switch, int source, 
			   int target, int dim);
/** */
static void _create_ba_system(void);
/* */
static void _delete_ba_system(void);
/* */
static void _delete_path_list(void *object);

/* find the first block match in the system */
static int _find_match(ba_request_t* ba_request, List results);

/** */
static bool _node_used(ba_node_t* ba_node, int x_size);

/* */
static void _switch_config(ba_node_t* source, ba_node_t* target, int dim, 
			   int port_src, int port_tar);

/* */
static int _set_external_wires(int dim, int count, ba_node_t* source, 
				ba_node_t* target);

/* */
static char *_set_internal_wires(List nodes, int size, int conn_type);

/* */
static int _find_x_path(List results, ba_node_t *ba_node, int *start,
			int x_size, int found, int conn_type, 
			block_algo_t algo);

/* */
static int _remove_node(List results, int *node_tar);

/* */
static int _find_next_free_using_port_2(ba_switch_t *curr_switch, 
					int source_port, 
					List nodes, int dim, 
					int count);
/* */
/* static int _find_passthrough(ba_switch_t *curr_switch, int source_port,  */
/* 			     List nodes, int dim,  */
/* 			     int count, int highest_phys_x);  */
/* */
static int _finish_torus(ba_switch_t *curr_switch, int source_port, 
			 int dim, int count, int *start);
/* */
static int *_set_best_path();

/* */
static int _set_one_dim(int *start, int *end, int *coord);

/* */
static void _destroy_geo(void *object);


extern char *bg_block_state_string(rm_partition_state_t state)
{
	static char tmp[16];

#ifdef HAVE_BG
	switch (state) {
#ifdef HAVE_BGL
		case RM_PARTITION_BUSY: 
			return "BUSY";
#else
		case RM_PARTITION_REBOOTING: 
			return "REBOOTING";
#endif
		case RM_PARTITION_CONFIGURING:
			return "CONFIG";
		case RM_PARTITION_DEALLOCATING:
			return "DEALLOC";
		case RM_PARTITION_ERROR:
			return "ERROR";
		case RM_PARTITION_FREE:
			return "FREE";
		case RM_PARTITION_NAV:
			return "NAV";
		case RM_PARTITION_READY:
			return "READY";
	}
#endif

	snprintf(tmp, sizeof(tmp), "%d", state);
	return tmp;
}

extern char *ba_passthroughs_string(uint16_t passthrough)
{
	char *pass = NULL;
	if(passthrough & PASS_FOUND_X)
		xstrcat(pass, "X");
	if(passthrough & PASS_FOUND_Y) {
		if(pass)
			xstrcat(pass, ",Y");
		else
			xstrcat(pass, "Y");
	}
	if(passthrough & PASS_FOUND_Z) {
		if(pass)
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
		{"Type", S_P_STRING},
		{"32CNBlocks", S_P_UINT16},
		{"128CNBlocks", S_P_UINT16},
#ifdef HAVE_BGL
		{"Nodecards", S_P_UINT16},
		{"Quarters", S_P_UINT16},
		{"BlrtsImage", S_P_STRING},
		{"LinuxImage", S_P_STRING},
		{"RamDiskImage", S_P_STRING},
#else
		{"16CNBlocks", S_P_UINT16},
		{"64CNBlocks", S_P_UINT16},
		{"256CNBlocks", S_P_UINT16},
		{"CnloadImage", S_P_STRING},
		{"IoloadImage", S_P_STRING},
#endif
		{"MloaderImage", S_P_STRING},
		{NULL}
	};
	s_p_hashtbl_t *tbl;
	char *tmp = NULL;
	blockreq_t *n = NULL;
	hostlist_t hl = NULL;
	char temp[BUFSIZE];

	tbl = s_p_hashtbl_create(block_options);
	s_p_parse_line(tbl, *leftover, leftover);
	if(!value) {
		return 0;
	}
	n = xmalloc(sizeof(blockreq_t));
	hl = hostlist_create(value);
	hostlist_ranged_string(hl, BUFSIZE, temp);
	hostlist_destroy(hl);

	n->block = xstrdup(temp);
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
		n->conn_type = SELECT_TORUS;
	else if(!strcasecmp(tmp,"MESH"))
		n->conn_type = SELECT_MESH;
	else
		n->conn_type = SELECT_SMALL;
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
	if(n) {
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
		{"GROUPS", S_P_STRING},
		{NULL}
	};
	s_p_hashtbl_t *tbl = NULL;
	char *tmp = NULL;
	image_t *n = NULL;
	image_group_t *image_group = NULL;
	int i = 0, j = 0;

	tbl = s_p_hashtbl_create(image_options);
	s_p_parse_line(tbl, *leftover, leftover);
	
	n = xmalloc(sizeof(image_t));
	n->name = xstrdup(value);
	n->def = false;
	debug3("image %s", n->name);
	n->groups = list_create(destroy_image_group_list);
	s_p_get_string(&tmp, "Groups", tbl);
	if(tmp) {
		for(i=0; i<strlen(tmp); i++) {
			if((tmp[i] == ':') || (tmp[i] == ',')) {
				image_group = xmalloc(sizeof(image_group_t));
				image_group->name = xmalloc(i-j+2);
				snprintf(image_group->name,
					 (i-j)+1, "%s", tmp+j);
				image_group->gid =
					gid_from_string(image_group->name);
				debug3("adding group %s %d", image_group->name,
				       image_group->gid);
				list_append(n->groups, image_group);
				j=i;
				j++;
			} 		
		}
		if(j != i) {
			image_group = xmalloc(sizeof(image_group_t));
			image_group->name = xmalloc(i-j+2);
			snprintf(image_group->name, (i-j)+1, "%s", tmp+j);
			image_group->gid = gid_from_string(image_group->name);
			if (image_group->gid == (gid_t) -1) {
				fatal("Invalid bluegene.conf parameter "
				      "Groups=%s", 
				      image_group->name);
			} else {
				debug3("adding group %s %d", image_group->name,
				       image_group->gid);
			}
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
	if(image_group) {
		xfree(image_group->name);
		xfree(image_group);
	}
}

extern void destroy_image(void *ptr)
{
	image_t *n = (image_t *)ptr;
	if(n) {
		xfree(n->name);
		if(n->groups) {
			list_destroy(n->groups);
			n->groups = NULL;
		}
		xfree(n);
	}
}

extern void destroy_ba_node(void *ptr)
{
	ba_node_t *ba_node = (ba_node_t *)ptr;
	if(ba_node) {
		xfree(ba_node);
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
 * IN - avail_node_bitmap: bitmap of usable midplanes.
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
#ifdef HAVE_BG
	float sz=1;
	int geo[BA_SYSTEM_DIMENSIONS] = {0,0,0};
	int i2, i3, picked, total_sz=1 , size2, size3;
	ListIterator itr;
	int checked[8];
	int *geo_ptr;
	int messed_with = 0;
	
	ba_request->save_name= NULL;
	ba_request->rotate_count= 0;
	ba_request->elongate_count = 0;
	ba_request->elongate_geos = list_create(_destroy_geo);
	geo[X] = ba_request->geometry[X];
	geo[Y] = ba_request->geometry[Y];
	geo[Z] = ba_request->geometry[Z];
	if(ba_request->deny_pass == (uint16_t)NO_VAL) 
		ba_request->deny_pass = ba_deny_pass;
	
	deny_pass = &ba_request->deny_pass;
	
	if(geo[X] != (uint16_t)NO_VAL) { 
		for (i=0; i<BA_SYSTEM_DIMENSIONS; i++){
			if ((geo[i] < 1) 
			    ||  (geo[i] > DIM_SIZE[i])){
				error("new_ba_request Error, "
				      "request geometry is invalid %d can't be "
				      "%d, DIMS are %c%c%c", 
				      i,
				      geo[i],
				      alpha_num[DIM_SIZE[X]],
				      alpha_num[DIM_SIZE[Y]],
				      alpha_num[DIM_SIZE[Z]]); 
				return 0;
			}
		}
		_append_geo(geo, ba_request->elongate_geos, 0);
		sz=1;
		for (i=0; i<BA_SYSTEM_DIMENSIONS; i++)
			sz *= ba_request->geometry[i];
		ba_request->size = sz;
		sz=0;
	}
	
	if(ba_request->elongate || sz) {
		sz=1;
		/* decompose the size into a cubic geometry */
		ba_request->rotate= 1;
		ba_request->elongate = 1;		
		
		for (i=0; i<BA_SYSTEM_DIMENSIONS; i++) { 
			total_sz *= DIM_SIZE[i];
			geo[i] = 1;
		}
		
		if(ba_request->size==1) {
			_append_geo(geo, 
				    ba_request->elongate_geos,
				    ba_request->rotate);
			goto endit;
		}
		
		if(ba_request->size<=DIM_SIZE[Y]) {
			geo[X] = 1;
			geo[Y] = ba_request->size;
			geo[Z] = 1;
			sz=ba_request->size;
			_append_geo(geo, 
				    ba_request->elongate_geos, 
				    ba_request->rotate);
		}
		
		i = ba_request->size/4;
		if(!(ba_request->size%2)
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
	
		if(ba_request->size>total_sz || ba_request->size<1) {
			return 0;			
		}
		sz = ba_request->size % (DIM_SIZE[Y] * DIM_SIZE[Z]);
		if(!sz) {
		      i = ba_request->size / (DIM_SIZE[Y] * DIM_SIZE[Z]);
		      geo[X] = i;
		      geo[Y] = DIM_SIZE[Y];
		      geo[Z] = DIM_SIZE[Z];
		      sz=ba_request->size;
		      _append_geo(geo,
				  ba_request->elongate_geos,
				  ba_request->rotate);
		}	
	startagain:		
		picked=0;
		for(i=0;i<8;i++)
			checked[i]=0;
		
		size3=ba_request->size;
		
		for (i=0; i<BA_SYSTEM_DIMENSIONS; i++) {
			total_sz *= DIM_SIZE[i];
			geo[i] = 1;
		}
	
		sz = 1;
		size3=ba_request->size;
		picked=0;
	tryagain:	
		if(size3!=ba_request->size)
			size2=size3;
		else
			size2=ba_request->size;
		//messedup:

		for (i=picked; i<BA_SYSTEM_DIMENSIONS; i++) { 
			if(size2<=1) 
				break;
			sz = size2%DIM_SIZE[i];
			if(!sz) {
				geo[i] = DIM_SIZE[i];	
				size2 /= DIM_SIZE[i];
			} else if (size2 > DIM_SIZE[i]){
				for(i2=(DIM_SIZE[i]-1);i2>1;i2--) {
					/* go through each number to see if 
					   the size is divisable by a smaller 
					   number that is 
					   good in the other dims. */
					if (!(size2%i2) && !checked[i2]) {
						size2 /= i2;
									
						if(i==0)
							checked[i2]=1;
							
						if(i2<DIM_SIZE[i]) 
							geo[i] = i2;
						else {
							goto tryagain;
						}
						if((i2-1)!=1 && 
						   i!=(BA_SYSTEM_DIMENSIONS-1))
							break;
					}		
				}				
				if(i2==1) {
					ba_request->size +=1;
					goto startagain;
				}
						
			} else {
				geo[i] = sz;	
				break;
			}					
		}
		
		if((geo[X]*geo[Y]) <= DIM_SIZE[Y]) {
			ba_request->geometry[X] = 1;
			ba_request->geometry[Y] = geo[X] * geo[Y];
			ba_request->geometry[Z] = geo[Z];
			_append_geo(ba_request->geometry, 
				    ba_request->elongate_geos, 
				    ba_request->rotate);

		}
		if((geo[X]*geo[Z]) <= DIM_SIZE[Y]) {
			ba_request->geometry[X] = 1;
			ba_request->geometry[Y] = geo[Y];
			ba_request->geometry[Z] = geo[X] * geo[Z];	
			_append_geo(ba_request->geometry, 
				    ba_request->elongate_geos, 
				    ba_request->rotate);		
	
		}
		if((geo[X]/2) <= DIM_SIZE[Y]) {
			if(geo[Y] == 1) {
				ba_request->geometry[Y] = geo[X]/2;
				messed_with = 1;
			} else  
				ba_request->geometry[Y] = geo[Y];
			if(!messed_with && geo[Z] == 1) {
				messed_with = 1;
				ba_request->geometry[Z] = geo[X]/2;
			} else
				ba_request->geometry[Z] = geo[Z];
			if(messed_with) {
				messed_with = 0;
				ba_request->geometry[X] = 2;
				_append_geo(ba_request->geometry, 
					    ba_request->elongate_geos, 
					    ba_request->rotate);
			}
		}
		if(geo[X] == DIM_SIZE[X]
		   && (geo[Y] < DIM_SIZE[Y] 
		       || geo[Z] < DIM_SIZE[Z])) {
			if(DIM_SIZE[Y]<DIM_SIZE[Z]) {
				i = DIM_SIZE[Y];
				DIM_SIZE[Y] = DIM_SIZE[Z];
				DIM_SIZE[Z] = i;
			}
			ba_request->geometry[X] = geo[X];
			ba_request->geometry[Y] = geo[Y];
			ba_request->geometry[Z] = geo[Z];
			if(ba_request->geometry[Y] < DIM_SIZE[Y]) {
				i = (DIM_SIZE[Y] - ba_request->geometry[Y]);
				ba_request->geometry[Y] +=i;
			}
			if(ba_request->geometry[Z] < DIM_SIZE[Z]) {
				i = (DIM_SIZE[Z] - ba_request->geometry[Z]);
				ba_request->geometry[Z] +=i;
			}
			for(i = DIM_SIZE[X]; i>0; i--) {
				ba_request->geometry[X]--;
				i2 = (ba_request->geometry[X]
				      * ba_request->geometry[Y]
				      * ba_request->geometry[Z]);
				if(i2 < ba_request->size) {
					ba_request->geometry[X]++;
					messed_with = 1;
					break;
				}					
			}			
			if(messed_with) {
				messed_with = 0;
				_append_geo(ba_request->geometry, 
					    ba_request->elongate_geos, 
					    ba_request->rotate);
			}
		}
		
		_append_geo(geo, 
			    ba_request->elongate_geos, 
			    ba_request->rotate);
	
		/* see if We can find a cube or square root of the 
		   size to make an easy cube */
		for(i=0;i<BA_SYSTEM_DIMENSIONS-1;i++) {
			sz = powf((float)ba_request->size,
				  (float)1/(BA_SYSTEM_DIMENSIONS-i));
			if(pow(sz,(BA_SYSTEM_DIMENSIONS-i))==ba_request->size)
				break;
		}
	
		if(i<BA_SYSTEM_DIMENSIONS-1) {
			/* we found something that looks like a cube! */
			i3=i;
			for (i=0; i<i3; i++) 
				geo[i] = 1;			
			
			for (i=i3; i<BA_SYSTEM_DIMENSIONS; i++)  
				if(sz<=DIM_SIZE[i]) 
					geo[i] = sz;	
				else
					goto endit;
				
			_append_geo(geo, 
				    ba_request->elongate_geos, 
				    ba_request->rotate);
		} 
	}
	
endit:
	itr = list_iterator_create(ba_request->elongate_geos);
	geo_ptr = list_next(itr);
	list_iterator_destroy(itr);
	
	if(geo_ptr == NULL)
		return 0;

	ba_request->elongate_count++;
	ba_request->geometry[X] = geo_ptr[X];
	ba_request->geometry[Y] = geo_ptr[Y];
	ba_request->geometry[Z] = geo_ptr[Z];
	sz=1;
	for (i=0; i<BA_SYSTEM_DIMENSIONS; i++)
		sz *= ba_request->geometry[i];
	ba_request->size = sz;
	
#else
	int geo[BA_SYSTEM_DIMENSIONS] = {0};
	
	ba_request->rotate_count= 0;
	ba_request->elongate_count = 0;
	ba_request->elongate_geos = list_create(_destroy_geo);
	geo[X] = ba_request->geometry[X];
		
	if(geo[X] != NO_VAL) { 
		for (i=0; i<BA_SYSTEM_DIMENSIONS; i++){
			if ((geo[i] < 1) 
			    ||  (geo[i] > DIM_SIZE[i])){
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
			
#endif
	return 1;
}

/**
 * delete a block request 
 */
extern void delete_ba_request(void *arg)
{
	ba_request_t *ba_request = (ba_request_t *)arg;
	if(ba_request) {
		xfree(ba_request->save_name);
		if(ba_request->elongate_geos)
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
	for (i=0; i<BA_SYSTEM_DIMENSIONS; i++){
		debug("%d", ba_request->geometry[i]);
	}
	debug("        size:\t%d", ba_request->size);
	debug("   conn_type:\t%d", ba_request->conn_type);
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
extern void ba_init(node_info_msg_t *node_info_ptr)
{
	int x,y,z;

#ifdef HAVE_3D
	node_info_t *node_ptr = NULL;
	int start, temp;
	char *numeric = NULL;
	int i, j=0;
	slurm_conf_node_t *node = NULL, **ptr_array;
	int count, number;
	int end[BA_SYSTEM_DIMENSIONS];
	
#ifdef HAVE_BG_FILES
	rm_size3D_t bp_size;
	int rc = 0;
#endif /* HAVE_BG_FILES */
	
#endif /* HAVE_3D */

	/* We only need to initialize once, so return if already done so. */
	if (_initialized){
		return;
	}
	
#ifdef HAVE_BG_FILES
	bridge_init();
#endif	
	/* make the letters array only contain letters upper and lower
	 * (62) */
	y = 'A';
	for (x = 0; x < 62; x++) {
		if (y == '[')
			y = 'a';
		else if(y == '{')
			y = '0';
		else if(y == ':')
			y = 'A';
		letters[x] = y;
		y++;
	}

	z=1;
	for (x = 0; x < 6; x++) {
		if(z == 4)
			z++;
		colors[x] = z;				
		z++;
	}
		
	best_count=BEST_COUNT_INIT;

	if(ba_system_ptr)
		_delete_ba_system();
	
	ba_system_ptr = (ba_system_t *) xmalloc(sizeof(ba_system_t));
	
	ba_system_ptr->num_of_proc = 0;
	
	if(node_info_ptr!=NULL) {
#ifdef HAVE_3D
		for (i = 0; i < node_info_ptr->record_count; i++) {
			node_ptr = &node_info_ptr->node_array[i];
			start = 0;
			
			if(!node_ptr->name) {
				DIM_SIZE[X] = 0;
				DIM_SIZE[Y] = 0;
				DIM_SIZE[Z] = 0;
				goto node_info_error;
			}

			numeric = node_ptr->name;
			while (numeric) {
				if (numeric[0] < '0' || numeric[0] > 'Z'
				    || (numeric[0] > '9' 
					&& numeric[0] < 'A')) {
					numeric++;
					continue;
				}
				start = xstrntol(numeric, NULL, 
						 BA_SYSTEM_DIMENSIONS,
						 HOSTLIST_BASE);
				break;
			}
			
			temp = start / (HOSTLIST_BASE * HOSTLIST_BASE);
			if (DIM_SIZE[X] < temp)
				DIM_SIZE[X] = temp;
			temp = (start % (HOSTLIST_BASE * HOSTLIST_BASE))
				/ HOSTLIST_BASE;
			if (DIM_SIZE[Y] < temp)
				DIM_SIZE[Y] = temp;
			temp = start % HOSTLIST_BASE;
			if (DIM_SIZE[Z] < temp)
				DIM_SIZE[Z] = temp;
		}
		DIM_SIZE[X]++;
		DIM_SIZE[Y]++;
		DIM_SIZE[Z]++;
		/* this will probably be reset below */
		REAL_DIM_SIZE[X] = DIM_SIZE[X];
		REAL_DIM_SIZE[Y] = DIM_SIZE[Y];
		REAL_DIM_SIZE[Z] = DIM_SIZE[Z];
#else
		DIM_SIZE[X] = node_info_ptr->record_count;
#endif
		ba_system_ptr->num_of_proc = node_info_ptr->record_count;
	} 
#ifdef HAVE_3D
node_info_error:

	if ((DIM_SIZE[X]==0) || (DIM_SIZE[Y]==0) || (DIM_SIZE[Z]==0)) {
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
				    && (node->nodenames[j+8] == ']' 
					|| node->nodenames[j+8] == ',')
				    && (node->nodenames[j+4] == 'x'
					|| node->nodenames[j+4] == '-')) {
					j+=5;
				} else if((node->nodenames[j] >= '0'
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
						  NULL, BA_SYSTEM_DIMENSIONS,
						  HOSTLIST_BASE);
				
				end[X] = number 
					/ (HOSTLIST_BASE * HOSTLIST_BASE);
				end[Y] = (number 
					  % (HOSTLIST_BASE * HOSTLIST_BASE))
					/ HOSTLIST_BASE;
				end[Z] = (number % HOSTLIST_BASE);
				DIM_SIZE[X] = MAX(DIM_SIZE[X], end[X]);
				DIM_SIZE[Y] = MAX(DIM_SIZE[Y], end[Y]);
				DIM_SIZE[Z] = MAX(DIM_SIZE[Z], end[Z]);
				break;
			}
				
		}
		if ((DIM_SIZE[X]==0) && (DIM_SIZE[Y]==0) && (DIM_SIZE[Z]==0)) 
			info("are you sure you only have 1 midplane? %s",
			      node->nodenames);
		DIM_SIZE[X]++;
		DIM_SIZE[Y]++;
		DIM_SIZE[Z]++;
		/* this will probably be reset below */
		REAL_DIM_SIZE[X] = DIM_SIZE[X];
		REAL_DIM_SIZE[Y] = DIM_SIZE[Y];
		REAL_DIM_SIZE[Z] = DIM_SIZE[Z];
	}
#ifdef HAVE_BG_FILES
	/* sanity check.  We can only request part of the system, but
	   we don't want to allow more than we have. */
	if (have_db2) {
		verbose("Attempting to contact MMCS");
		if ((rc = bridge_get_bg(&bg)) != STATUS_OK) {
			fatal("bridge_get_BG() failed.  This usually means "
			      "there is something wrong with the database.  "
			      "You might want to run slurmctld in daemon "
			      "mode (-D) to see what the real error from "
			      "the api was.  The return code was %d", rc);
			return;
		}
		
		if ((bg != NULL)
		&&  ((rc = bridge_get_data(bg, RM_Msize, &bp_size)) 
		     == STATUS_OK)) {
			verbose("BlueGene configured with "
				"%d x %d x %d base blocks", 
				bp_size.X, bp_size.Y, bp_size.Z);
			REAL_DIM_SIZE[X] = bp_size.X;
			REAL_DIM_SIZE[Y] = bp_size.Y;
			REAL_DIM_SIZE[Z] = bp_size.Z;
			if((DIM_SIZE[X] > bp_size.X)
			   || (DIM_SIZE[Y] > bp_size.Y)
			   || (DIM_SIZE[Z] > bp_size.Z)) {
				fatal("You requested a %c%c%c system, "
				      "but we only have a system of %c%c%c.  "
				      "Change your slurm.conf.",
				      alpha_num[DIM_SIZE[X]],
				      alpha_num[DIM_SIZE[Y]],
				      alpha_num[DIM_SIZE[Z]],
				      alpha_num[bp_size.X],
				      alpha_num[bp_size.Y],
				      alpha_num[bp_size.Z]);
			}
		} else {
			error("bridge_get_data(RM_Msize): %d", rc);	
		}
	}
#endif


	debug("We are using %c x %c x %c of the system.", 
	      alpha_num[DIM_SIZE[X]],
	      alpha_num[DIM_SIZE[Y]],
	      alpha_num[DIM_SIZE[Z]]);
	
#else 
	if (DIM_SIZE[X]==0) {
		debug("Setting default system dimensions");
		DIM_SIZE[X]=100;
	}	
#endif
	if(!ba_system_ptr->num_of_proc)
		ba_system_ptr->num_of_proc = 
			DIM_SIZE[X] 
#ifdef HAVE_3D
			* DIM_SIZE[Y] 
			* DIM_SIZE[Z]
#endif 
			;

	_create_ba_system();

#ifndef HAVE_BG_FILES
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
	int x, y, z, i;
	ba_node_t *source = NULL;
	if(_wires_initialized)
		return;

	for(x=0;x<DIM_SIZE[X];x++) {
		for(y=0;y<DIM_SIZE[Y];y++) {
			for(z=0;z<DIM_SIZE[Z];z++) {
#ifdef HAVE_3D
				source = &ba_system_ptr->grid[x][y][z];
#else
				source = &ba_system_ptr->grid[x];
#endif
				for(i=0; i<NUM_PORTS_PER_NODE; i++) {
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
#ifdef HAVE_BG_FILES	
	_set_external_wires(0,0,NULL,NULL);
	if(!bp_map_list) {
		if(set_bp_map() == -1) {
			return;
		}
	}
#endif
	
	_wires_initialized = true;
	return;
}


/** 
 * destroy all the internal (global) data structs.
 */
extern void ba_fini()
{
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
#ifdef HAVE_BG_FILES
	if(bg)
		bridge_free_bg(bg);

	if (bp_map_list) {
		list_destroy(bp_map_list);
		bp_map_list = NULL;
		_bp_map_initialized = false;
	}
	bridge_fini();
#endif
	_delete_ba_system();
//	debug2("pa system destroyed");
}


/* 
 * set the node in the internal configuration as in, or not in use,
 * along with the current state of the node.
 * 
 * IN ba_node: ba_node_t to update state
 * IN state: new state of ba_node_t
 */
extern void ba_update_node_state(ba_node_t *ba_node, uint16_t state)
{
	uint16_t node_base_state = state & NODE_STATE_BASE;

	if (!_initialized){
		error("Error, configuration not initialized, "
		      "calling ba_init(NULL)");
		ba_init(NULL);
	}

#ifdef HAVE_BG
	debug2("ba_update_node_state: new state of [%c%c%c] is %s", 
	       alpha_num[ba_node->coord[X]], alpha_num[ba_node->coord[Y]],
	       alpha_num[ba_node->coord[Z]], node_state_string(state)); 
#else
	debug2("ba_update_node_state: new state of [%d] is %s", 
	       ba_node->coord[X],
	       node_state_string(state)); 
#endif

	/* basically set the node as used */
	if((node_base_state == NODE_STATE_DOWN)
	   || (ba_node->state & NODE_STATE_DRAIN)) 
		ba_node->used = true;
	else
		ba_node->used = false;
	ba_node->state = state;
}

/* 
 * copy info from a ba_node, a direct memcpy of the ba_node_t
 * 
 * IN ba_node: node to be copied
 * Returned ba_node_t *: copied info must be freed with destroy_ba_node
 */
extern ba_node_t *ba_copy_node(ba_node_t *ba_node)
{
	ba_node_t *new_ba_node = xmalloc(sizeof(ba_node_t));
	
	memcpy(new_ba_node, ba_node, sizeof(ba_node_t));
	return new_ba_node;
}

/* 
 * copy the path of the nodes given
 * 
 * IN nodes List of ba_node_t *'s: nodes to be copied
 * OUT dest_nodes List of ba_node_t *'s: filled in list of nodes
 * wiring.
 * Return on success SLURM_SUCCESS, on error SLURM_ERROR
 */
extern int copy_node_path(List nodes, List *dest_nodes)
{
	int rc = SLURM_ERROR;
	
#ifdef HAVE_BG
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	ba_node_t *ba_node = NULL, *new_ba_node = NULL;
	int dim;
	ba_switch_t *curr_switch = NULL, *new_switch = NULL; 
	
	if(!nodes)
		return SLURM_ERROR;
	if(!*dest_nodes)
		*dest_nodes = list_create(destroy_ba_node);

	itr = list_iterator_create(nodes);
	while((ba_node = list_next(itr))) {
		itr2 = list_iterator_create(*dest_nodes);
		while((new_ba_node = list_next(itr2))) {
			if (ba_node->coord[X] == new_ba_node->coord[X] &&
			    ba_node->coord[Y] == new_ba_node->coord[Y] &&
			    ba_node->coord[Z] == new_ba_node->coord[Z]) 
				break;	/* we found it */
		}
		list_iterator_destroy(itr2);
	
		if(!new_ba_node) {
			debug2("adding %c%c%c as a new node",
			       alpha_num[ba_node->coord[X]], 
			       alpha_num[ba_node->coord[Y]],
			       alpha_num[ba_node->coord[Z]]);
			new_ba_node = ba_copy_node(ba_node);
			_new_ba_node(new_ba_node, ba_node->coord, false);
			list_push(*dest_nodes, new_ba_node);
			
		}
		new_ba_node->used = true;
		for(dim=0;dim<BA_SYSTEM_DIMENSIONS;dim++) {		
			curr_switch = &ba_node->axis_switch[dim];
			new_switch = &new_ba_node->axis_switch[dim];
			if(curr_switch->int_wire[0].used) {
				if(!_copy_the_path(*dest_nodes, 
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
		      "calling ba_init(NULL)");
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
extern int remove_block(List nodes, int new_count)
{
	int dim;
	ba_node_t* ba_node = NULL;
	ba_switch_t *curr_switch = NULL; 
	ListIterator itr;
	
	itr = list_iterator_create(nodes);
	while((ba_node = (ba_node_t*) list_next(itr)) != NULL) {
		ba_node->used = false;
		ba_node->color = 7;
		ba_node->letter = '.';
		for(dim=0;dim<BA_SYSTEM_DIMENSIONS;dim++) {		
			curr_switch = &ba_node->axis_switch[dim];
			if(curr_switch->int_wire[0].used) {
				_reset_the_path(curr_switch, 0, 1, dim);
			}
		}
	}
	list_iterator_destroy(itr);
	if(new_count == -1)
		color_count--;
	else
		color_count=new_count;			
	if(color_count < 0)
		color_count = 0;
	return 1;
}

/* 
 * Admin wants to change something about a previous allocation. 
 * will allow Admin to change previous allocation by giving the 
 * letter code for the allocation and the variable to alter
 * (Not currently used in the system, update this if it is)
 */
extern int alter_block(List nodes, int conn_type)
{
	/* int dim; */
/* 	ba_node_t* ba_node = NULL; */
/* 	ba_switch_t *curr_switch = NULL;  */
/* 	int size=0; */
/* 	char *name = NULL; */
/* 	ListIterator results_i;	 */	

	return SLURM_ERROR;
	/* results_i = list_iterator_create(nodes); */
/* 	while ((ba_node = list_next(results_i)) != NULL) { */
/* 		ba_node->used = false; */
		
/* 		for(dim=0;dim<BA_SYSTEM_DIMENSIONS;dim++) { */
/* 			curr_switch = &ba_node->axis_switch[dim]; */
/* 			if(curr_switch->int_wire[0].used) { */
/* 				_reset_the_path(curr_switch, 0, 1, dim); */
/* 			} */
/* 		} */
/* 		size++; */
/* 	} */
/* 	list_iterator_destroy(results_i); */
/* 	if((name = _set_internal_wires(nodes, size, conn_type)) == NULL) */
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
extern int redo_block(List nodes, int *geo, int conn_type, int new_count)
{
       	ba_node_t* ba_node;
	char *name = NULL;

	ba_node = list_peek(nodes);
	if(!ba_node)
		return SLURM_ERROR;

	remove_block(nodes, new_count);
	list_delete_all(nodes, &empty_null_destroy_list, "");
		
	name = set_bg_block(nodes, ba_node->coord, geo, conn_type);
	if(!name)
		return SLURM_ERROR;
	else {
		xfree(name);
		return SLURM_SUCCESS;
	}
}

/*
 * Used to set a block into a virtual system.  The system can be
 * cleared first and this function sets all the wires and midplanes
 * used in the nodelist given.  The nodelist is a list of ba_node_t's
 * that are already set up.  This is very handly to test if there are
 * any passthroughs used by one block when adding another block that
 * also uses those wires, and neither use any overlapping
 * midplanes. Doing a simple bitmap & will not reveal this.
 *
 * Returns SLURM_SUCCESS if nodelist fits into system without
 * conflict, and SLURM_ERROR if nodelist conflicts with something
 * already in the system.
 */
extern int check_and_set_node_list(List nodes)
{
	int rc = SLURM_ERROR;

#ifdef HAVE_BG
	int i, j;
	ba_switch_t *ba_switch = NULL, *curr_ba_switch = NULL; 
	ba_node_t *ba_node = NULL, *curr_ba_node = NULL;
	ListIterator itr = NULL;

	if(!nodes)
		return rc;

	itr = list_iterator_create(nodes);
	while((ba_node = list_next(itr))) { 
		/* info("checking %c%c%c", */
/* 		     ba_node->coord[X],  */
/* 		     ba_node->coord[Y], */
/* 		     ba_node->coord[Z]); */
					      
		curr_ba_node = &ba_system_ptr->
			grid[ba_node->coord[X]]
			[ba_node->coord[Y]]
			[ba_node->coord[Z]];
		if(ba_node->used && curr_ba_node->used) {
			debug3("I have already been to "
			       "this node %c%c%c",
			       alpha_num[ba_node->coord[X]], 
			       alpha_num[ba_node->coord[Y]],
			       alpha_num[ba_node->coord[Z]]);
			rc = SLURM_ERROR;
			goto end_it;
		}
		
		if(ba_node->used) 
			curr_ba_node->used = true;		
		for(i=0; i<BA_SYSTEM_DIMENSIONS; i++) {
			ba_switch = &ba_node->axis_switch[i];
			curr_ba_switch = &curr_ba_node->axis_switch[i];
			//info("checking dim %d", i);
		
			for(j=0; j<NUM_PORTS_PER_NODE; j++) {
				//info("checking port %d", j);
		
				if(ba_switch->int_wire[j].used 
				   && curr_ba_switch->int_wire[j].used
					&& j != curr_ba_switch->
				   int_wire[j].port_tar) {
					debug3("%c%c%c dim %d port %d "
					       "is already in use to %d",
					       alpha_num[ba_node->coord[X]], 
					       alpha_num[ba_node->coord[Y]],
					       alpha_num[ba_node->coord[Z]], 
					       i,
					       j,
					       curr_ba_switch->
					       int_wire[j].port_tar);
					rc = SLURM_ERROR;
					goto end_it;
				}
				if(!ba_switch->int_wire[j].used)
					continue;

				/* info("setting %c%c%c dim %d port %d -> %d", */
/* 				     alpha_num[ba_node->coord[X]],  */
/* 				     alpha_num[ba_node->coord[Y]], */
/* 				     alpha_num[ba_node->coord[Z]],  */
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
extern char *set_bg_block(List results, int *start, 
			  int *geometry, int conn_type)
{
	char *name = NULL;
	ba_node_t* ba_node = NULL;
	int size = 0;
	int send_results = 0;
	int found = 0;


#ifdef HAVE_3D
	if(start[X]>=DIM_SIZE[X] 
	   || start[Y]>=DIM_SIZE[Y]
	   || start[Z]>=DIM_SIZE[Z])
		return NULL;

	if(geometry[X] <= 0 || geometry[Y] <= 0 || geometry[Z] <= 0) {
		error("problem with geometry %c%c%c, needs to be at least 111",
		      alpha_num[geometry[X]],
		      alpha_num[geometry[Y]],
		      alpha_num[geometry[Z]]);		      
		return NULL;
	}

	size = geometry[X] * geometry[Y] * geometry[Z];
	ba_node = &ba_system_ptr->grid[start[X]][start[Y]][start[Z]];
#else
	if(start[X]>=DIM_SIZE[X])
		return NULL;
	size = geometry[X];
	ba_node = &ba_system_ptr->grid[start[X]];	
#endif
	

	if(!ba_node)
		return NULL;

	if(!results)
		results = list_create(NULL);
	else
		send_results = 1;
	/* This midplane should have already been checked if it was in
	   use or not */
	list_append(results, ba_node);
	if(conn_type >= SELECT_SMALL) {
		/* adding the ba_node and ending */
		ba_node->used = true;
		name = xstrdup_printf("%c%c%c",
				      alpha_num[ba_node->coord[X]],
				      alpha_num[ba_node->coord[Y]],
				      alpha_num[ba_node->coord[Z]]);
		if(ba_node->letter == '.') {
			ba_node->letter = letters[color_count%62];
			ba_node->color = colors[color_count%6];
			debug3("count %d setting letter = %c "
			       "color = %d",
			       color_count,
			       ba_node->letter,
			       ba_node->color);
			color_count++;
		}
		goto end_it; 
	}
	found = _find_x_path(results, ba_node,
			     ba_node->coord, 
			     geometry[X], 
			     1,
			     conn_type, BLOCK_ALGO_FIRST);

	if(!found) {
		debug2("trying less efficient code");
		remove_block(results, color_count);
		list_delete_all(results, &empty_null_destroy_list, "");
		list_append(results, ba_node);
		found = _find_x_path(results, ba_node,
				     ba_node->coord,
				     geometry[X],
				     1,
				     conn_type, BLOCK_ALGO_SECOND);
	}
	if(found) {
#ifdef HAVE_BG
		List start_list = NULL;
		ListIterator itr;

		start_list = list_create(NULL);
		itr = list_iterator_create(results);
		while((ba_node = (ba_node_t*) list_next(itr))) {
			list_append(start_list, ba_node);
		}
		list_iterator_destroy(itr);
		
		if(!_fill_in_coords(results, 
				    start_list, 
				    geometry, 
				    conn_type)) {
			list_destroy(start_list);
			goto end_it;
		}
		list_destroy(start_list);			
#endif		
	} else {
		goto end_it;
	}

	name = _set_internal_wires(results,
				   size,
				   conn_type);
end_it:
	if(!send_results && results) {
		list_destroy(results);
		results = NULL;
	}
	if(name!=NULL) {
		debug2("name = %s", name);
	} else {
		debug2("can't allocate");
		xfree(name);
	}

	return name;	
}

/*
 * Resets the virtual system to a virgin state.  If track_down_nodes is set
 * then those midplanes are not set to idle, but kept in a down state.
 */
extern int reset_ba_system(bool track_down_nodes)
{
	int x;
#ifdef HAVE_3D
	int y, z;
#endif
	int coord[BA_SYSTEM_DIMENSIONS];

	for (x = 0; x < DIM_SIZE[X]; x++) {
#ifdef HAVE_3D
		for (y = 0; y < DIM_SIZE[Y]; y++)
			for (z = 0; z < DIM_SIZE[Z]; z++) {
				coord[X] = x;
				coord[Y] = y;
				coord[Z] = z;
				_new_ba_node(&ba_system_ptr->grid[x][y][z], 
					     coord, track_down_nodes);
			}
#else
		coord[X] = x;
		_new_ba_node(&ba_system_ptr->grid[x], coord, track_down_nodes);

#endif
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
 * Note: Need to call reset_all_removed_bps before starting another
 * allocation attempt after 
 */
extern int removable_set_bps(char *bps)
{
#ifdef HAVE_BG
	int j=0, number;
	int x;
	int y,z;
	int start[BA_SYSTEM_DIMENSIONS];
	int end[BA_SYSTEM_DIMENSIONS];

	if(!bps)
		return SLURM_ERROR;

	while(bps[j] != '\0') {
		if ((bps[j] == '[' || bps[j] == ',')
		    && (bps[j+8] == ']' || bps[j+8] == ',')
		    && (bps[j+4] == 'x' || bps[j+4] == '-')) {
			
			j++;
			number = xstrntol(bps + j, NULL, BA_SYSTEM_DIMENSIONS,
					  HOSTLIST_BASE);
			start[X] = number / (HOSTLIST_BASE * HOSTLIST_BASE);
			start[Y] = (number % (HOSTLIST_BASE * HOSTLIST_BASE))
				/ HOSTLIST_BASE;
			start[Z] = (number % HOSTLIST_BASE);
			j += 4;
			number = xstrntol(bps + j, NULL, 3, HOSTLIST_BASE);
			end[X] = number / (HOSTLIST_BASE * HOSTLIST_BASE);
			end[Y] = (number % (HOSTLIST_BASE * HOSTLIST_BASE))
				/ HOSTLIST_BASE;
			end[Z] = (number % HOSTLIST_BASE);
			j += 3;
			for (x = start[X]; x <= end[X]; x++) {
				for (y = start[Y]; y <= end[Y]; y++) {
					for (z = start[Z]; z <= end[Z]; z++) {
						if(!ba_system_ptr->grid[x][y][z]
						   .used)
							ba_system_ptr->
								grid[x][y][z]
								.used = 2;
					}
				}
			}
			
			if(bps[j] != ',')
				break;
			j--;
		} else if((bps[j] >= '0' && bps[j] <= '9')
			  || (bps[j] >= 'A' && bps[j] <= 'Z')) {
			
			number = xstrntol(bps + j, NULL, BA_SYSTEM_DIMENSIONS,
					  HOSTLIST_BASE);
			x = number / (HOSTLIST_BASE * HOSTLIST_BASE);
			y = (number % (HOSTLIST_BASE * HOSTLIST_BASE))
				/ HOSTLIST_BASE;
			z = (number % HOSTLIST_BASE);
			j+=3;
			if(!ba_system_ptr->grid[x][y][z].used)
				ba_system_ptr->grid[x][y][z].used = 2;
			
			if(bps[j] != ',')
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
 * removable_set_bps, or set_all_bps_except.
 */
extern int reset_all_removed_bps()
{
	int x;

	for (x = 0; x < DIM_SIZE[X]; x++) {
#ifdef HAVE_3D
		int y, z;
		for (y = 0; y < DIM_SIZE[Y]; y++)
			for (z = 0; z < DIM_SIZE[Z]; z++) 
				if(ba_system_ptr->grid[x][y][z].used == 2) {
					ba_system_ptr->grid[x][y][z].used = 0;
				}
#else
		if(ba_system_ptr->grid[x].used == 2)
			ba_system_ptr->grid[x].used = 0;
#endif
	}
	return SLURM_SUCCESS;
}

/*
 * IN: hostlist of midplanes we do not want
 * RET: SLURM_SUCCESS on success, or SLURM_ERROR on error
 *
 * Need to call rest_all_removed_bps before starting another
 * allocation attempt if possible use removable_set_bps since it is
 * faster. It does basically the opposite of this function. If you
 * have to come up with this list though it is faster to use this
 * function than if you have to call bitmap2node_name since that is slow.
 */
extern int set_all_bps_except(char *bps)
{
	int x;
#ifdef HAVE_3D
	int y, z;
#endif
	hostlist_t hl = hostlist_create(bps);
	char *host = NULL, *numeric = NULL;
	int start, temp;

	while((host = hostlist_shift(hl))){
		numeric = host;
		start = 0;
		while (numeric) {
			if (numeric[0] < '0' || numeric[0] > 'Z'
			    || (numeric[0] > '9' 
				&& numeric[0] < 'A')) {
				numeric++;
				continue;
			}
			start = xstrntol(numeric, NULL, 
					 BA_SYSTEM_DIMENSIONS,
					 HOSTLIST_BASE);
			break;
		}
		
		temp = start / (HOSTLIST_BASE * HOSTLIST_BASE);
		x = temp;
#ifdef HAVE_3D
		temp = (start % (HOSTLIST_BASE * HOSTLIST_BASE))
			/ HOSTLIST_BASE;
		y = temp;
		temp = start % HOSTLIST_BASE;
		z = temp;
		if(ba_system_ptr->grid[x][y][z].state != NODE_STATE_IDLE) {
			error("we can't use this node %c%c%c",	
			      alpha_num[x],
			      alpha_num[y],
			      alpha_num[z]);

			return SLURM_ERROR;
		}
		ba_system_ptr->grid[x][y][z].state = NODE_STATE_END;
#else
		if(ba_system_ptr->grid[x].state != NODE_STATE_IDLE) {
			error("we can't use this node %d", x);

			return SLURM_ERROR;
		}
		ba_system_ptr->grid[x].state = NODE_STATE_END;
#endif
		free(host);
	}
	hostlist_destroy(hl);

	for (x = 0; x < DIM_SIZE[X]; x++) {
#ifdef HAVE_3D
		for (y = 0; y < DIM_SIZE[Y]; y++)
			for (z = 0; z < DIM_SIZE[Z]; z++) {
				if(ba_system_ptr->grid[x][y][z].state
				   == NODE_STATE_END) {
					ba_system_ptr->grid[x][y][z].state
						= NODE_STATE_IDLE;
					ba_system_ptr->grid[x][y][z].used = 
						false;
				} else if(!ba_system_ptr->grid[x][y][z].used) {
					ba_system_ptr->grid[x][y][z].used = 2;
				}
			}
#else
		if(ba_system_ptr->grid[x].state == NODE_STATE_END) {
			ba_system_ptr->grid[x].state = NODE_STATE_IDLE;
			ba_system_ptr->grid[x].used = false;
		} else if(!ba_system_ptr->grid[x].used) {
			ba_system_ptr->grid[x].used = 2;
		}
#endif
	}

 	return SLURM_SUCCESS;
}

/*
 * set values of every grid point (used in smap)
 */
extern void init_grid(node_info_msg_t * node_info_ptr)
{
	node_info_t *node_ptr = NULL;
	int x, i = 0;
	uint16_t node_base_state;
	/* For systems with more than 62 active jobs or BG blocks, 
	 * we just repeat letters */

#ifdef HAVE_3D
	int y,z;
	for (x = 0; x < DIM_SIZE[X]; x++)
		for (y = 0; y < DIM_SIZE[Y]; y++)
			for (z = 0; z < DIM_SIZE[Z]; z++) {
				if(node_info_ptr!=NULL) {
					node_ptr = 
						&node_info_ptr->node_array[i];
					node_base_state = node_ptr->node_state 
						& NODE_STATE_BASE;
					ba_system_ptr->grid[x][y][z].color = 7;
					if ((node_base_state 
					     == NODE_STATE_DOWN) || 
					    (node_ptr->node_state &
					     NODE_STATE_DRAIN)) {
						ba_system_ptr->
							grid[x][y][z].color 
							= 0;
						ba_system_ptr->
							grid[x][y][z].letter 
							= '#';
						if(_initialized) {
							ba_update_node_state(
							&ba_system_ptr->
							grid[x][y][z],
							node_ptr->node_state);
						}
					} else {
						ba_system_ptr->grid[x][y][z].
							color = 7;
						ba_system_ptr->grid[x][y][z].
							letter = '.';
					}
					ba_system_ptr->grid[x][y][z].state 
						= node_ptr->node_state;
				} else {
					ba_system_ptr->grid[x][y][z].color = 7;
					ba_system_ptr->grid[x][y][z].letter 
						= '.';
					ba_system_ptr->grid[x][y][z].state = 
						NODE_STATE_IDLE;
				}
				ba_system_ptr->grid[x][y][z].index = i++;
			}
#else
	for (x = 0; x < DIM_SIZE[X]; x++) {
		if(node_info_ptr!=NULL) {
			node_ptr = &node_info_ptr->node_array[i];
			node_base_state = node_ptr->node_state 
				& NODE_STATE_BASE;
			ba_system_ptr->grid[x].color = 7;
			if ((node_base_state == NODE_STATE_DOWN) || 
			    (node_ptr->node_state & NODE_STATE_DRAIN)) {
				ba_system_ptr->grid[x].color = 0;
				ba_system_ptr->grid[x].letter = '#';
				if(_initialized) {
					ba_update_node_state(
						&ba_system_ptr->grid[x],
						node_ptr->node_state);
				}
			} else {
				ba_system_ptr->grid[x].color = 7;
				ba_system_ptr->grid[x].letter = '.';
			}
			ba_system_ptr->grid[x].state = node_ptr->node_state;
		} else {
			ba_system_ptr->grid[x].color = 7;
			ba_system_ptr->grid[x].letter = '.';
			ba_system_ptr->grid[x].state = NODE_STATE_IDLE;
		}
		ba_system_ptr->grid[x].index = i++;
	}
#endif
	return;
}

/*
 * Convert a BG API error code to a string
 * IN inx - error code from any of the BG Bridge APIs
 * RET - string describing the error condition
 */
extern char *bg_err_str(status_t inx)
{
#ifdef HAVE_BG_FILES
	switch (inx) {
	case STATUS_OK:
		return "Status OK";
	case PARTITION_NOT_FOUND:
		return "Partition not found";
	case JOB_NOT_FOUND:
		return "Job not found";
	case BP_NOT_FOUND:
		return "Base partition not found";
	case SWITCH_NOT_FOUND:
		return "Switch not found";
#ifndef HAVE_BGL
	case PARTITION_ALREADY_DEFINED:
		return "Partition already defined";
#endif
	case JOB_ALREADY_DEFINED:
		return "Job already defined";
	case CONNECTION_ERROR:
		return "Connection error";
	case INTERNAL_ERROR:
		return "Internal error";
	case INVALID_INPUT:
		return "Invalid input";
	case INCOMPATIBLE_STATE:
		return "Incompatible state";
	case INCONSISTENT_DATA:
		return "Inconsistent data";
	}
#endif

	return "?";
}

/*
 * Set up the map for resolving
 */
extern int set_bp_map(void)
{
#ifdef HAVE_BG_FILES
	int rc;
	rm_BP_t *my_bp = NULL;
	ba_bp_map_t *bp_map = NULL;
	int bp_num, i;
	char *bp_id = NULL;
	rm_location_t bp_loc;
	int number = 0;

	if(_bp_map_initialized)
		return 1;

	bp_map_list = list_create(_bp_map_list_del);

	if (!have_db2) {
		fatal("Can't access DB2 library, run from service node");
		return -1;
	}

#ifdef HAVE_BGL
	if (!getenv("DB2INSTANCE") || !getenv("VWSPATH")) {
		fatal("Missing DB2INSTANCE or VWSPATH env var.  "
		      "Execute 'db2profile'");
		return -1;
	}
#endif
	
	if (!bg) {
		if((rc = bridge_get_bg(&bg)) != STATUS_OK) {
			error("bridge_get_BG(): %d", rc);
			return -1;
		}
	}
	
	if ((rc = bridge_get_data(bg, RM_BPNum, &bp_num)) != STATUS_OK) {
		error("bridge_get_data(RM_BPNum): %d", rc);
		bp_num = 0;
	}

	for (i=0; i<bp_num; i++) {

		if (i) {
			if ((rc = bridge_get_data(bg, RM_NextBP, &my_bp))
			    != STATUS_OK) {
				error("bridge_get_data(RM_NextBP): %d", rc);
				break;
			}
		} else {
			if ((rc = bridge_get_data(bg, RM_FirstBP, &my_bp))
			    != STATUS_OK) {
				error("bridge_get_data(RM_FirstBP): %d", rc);
				break;
			}
		}
		
		bp_map = (ba_bp_map_t *) xmalloc(sizeof(ba_bp_map_t));
		
		if ((rc = bridge_get_data(my_bp, RM_BPID, &bp_id))
		    != STATUS_OK) {
			xfree(bp_map);
			error("bridge_get_data(RM_BPID): %d", rc);
			continue;
		}

		if(!bp_id) {
			error("No BP ID was returned from database");
			continue;
		}
			
		if ((rc = bridge_get_data(my_bp, RM_BPLoc, &bp_loc))
		    != STATUS_OK) {
			xfree(bp_map);
			error("bridge_get_data(RM_BPLoc): %d", rc);
			continue;
		}
		
		bp_map->bp_id = xstrdup(bp_id);
		bp_map->coord[X] = bp_loc.X;
		bp_map->coord[Y] = bp_loc.Y;
		bp_map->coord[Z] = bp_loc.Z;
		
		number = xstrntol(bp_id+1, NULL,
				  BA_SYSTEM_DIMENSIONS, HOSTLIST_BASE);
/* no longer needed for calculation */
/* 		if(DIM_SIZE[X] > bp_loc.X */
/* 		   && DIM_SIZE[Y] > bp_loc.Y */
/* 		   && DIM_SIZE[Z] > bp_loc.Z) */
/* 			ba_system_ptr->grid */
/* 				[bp_loc.X] */
/* 				[bp_loc.Y] */
/* 				[bp_loc.Z].phys_x = */
/* 				number / (HOSTLIST_BASE * HOSTLIST_BASE); */
		
		list_push(bp_map_list, bp_map);
		
		free(bp_id);		
	}
#endif
	_bp_map_initialized = true;
	return 1;
	
}

/*
 * find a base blocks bg location 
 */
extern int *find_bp_loc(char* bp_id)
{
#ifdef HAVE_BG_FILES
	ba_bp_map_t *bp_map = NULL;
	ListIterator itr;
	char *check = NULL;

	if(!bp_map_list) {
		if(set_bp_map() == -1)
			return NULL;
	}

	check = xstrdup(bp_id);
	/* with BGP they changed the names of the rack midplane action from
	 * R000 to R00-M0 so we now support both formats for each of the
	 * systems */
#ifdef HAVE_BGL
	if(check[3] == '-') {
		if(check[5]) {
			check[3] = check[5];
			check[4] = '\0';
		}
	}
#else
	if(check[3] != '-') {
		xfree(check);
		check = xstrdup_printf("R%c%c-M%c",
				       bp_id[1], bp_id[2], bp_id[3]);
	}
#endif

	itr = list_iterator_create(bp_map_list);
	while ((bp_map = list_next(itr)))  
		if (!strcasecmp(bp_map->bp_id, check)) 
			break;	/* we found it */
	list_iterator_destroy(itr);

	xfree(check);

	if(bp_map != NULL)
		return bp_map->coord;
	else
		return NULL;

#else
	return NULL;
#endif
}

/*
 * find a rack/midplace location 
 */
extern char *find_bp_rack_mid(char* xyz)
{
#ifdef HAVE_BG_FILES
	ba_bp_map_t *bp_map = NULL;
	ListIterator itr;
	int number;
	int coord[BA_SYSTEM_DIMENSIONS];
	int len = strlen(xyz);
	len -= 3;
	if(len<0)
		return NULL;
	number = xstrntol(&xyz[X]+len, NULL,
			  BA_SYSTEM_DIMENSIONS, HOSTLIST_BASE);
	coord[X] = number / (HOSTLIST_BASE * HOSTLIST_BASE);
	coord[Y] = (number % (HOSTLIST_BASE * HOSTLIST_BASE)) / HOSTLIST_BASE;
	coord[Z] = (number % HOSTLIST_BASE);
	if(!bp_map_list) {
		if(set_bp_map() == -1)
			return NULL;
	}
	
	itr = list_iterator_create(bp_map_list);
	while ((bp_map = list_next(itr)) != NULL)
		if (bp_map->coord[X] == coord[X] &&
		    bp_map->coord[Y] == coord[Y] &&
		    bp_map->coord[Z] == coord[Z]) 
			break;	/* we found it */
	
	list_iterator_destroy(itr);
	if(bp_map != NULL)
		return bp_map->bp_id;
	else
		return NULL;

#else
	return NULL;
#endif
}

/*
 * set the used wires in the virtual system for a block from the real system 
 */
extern int load_block_wiring(char *bg_block_id)
{
#ifdef HAVE_BG_FILES
	int rc, i, j;
	rm_partition_t *block_ptr = NULL;
	int cnt = 0;
	int switch_cnt = 0;
	rm_switch_t *curr_switch = NULL;
	rm_BP_t *curr_bp = NULL;
	char *switchid = NULL;
	rm_connection_t curr_conn;
	int dim;
	ba_switch_t *ba_switch = NULL; 
	int *geo = NULL;
	
	debug2("getting info for block %s\n", bg_block_id);
	
	if ((rc = bridge_get_block(bg_block_id,  &block_ptr)) != STATUS_OK) {
		error("bridge_get_block(%s): %s", 
		      bg_block_id, 
		      bg_err_str(rc));
		return SLURM_ERROR;
	}	
	
	if ((rc = bridge_get_data(block_ptr, RM_PartitionSwitchNum,
				  &switch_cnt)) != STATUS_OK) {
		error("bridge_get_data(RM_PartitionSwitchNum): %s",
		      bg_err_str(rc));
		return SLURM_ERROR;
	} 
	if(!switch_cnt) {
		debug3("no switch_cnt");
		if ((rc = bridge_get_data(block_ptr, 
					  RM_PartitionFirstBP, 
					  &curr_bp)) 
		    != STATUS_OK) {
			error("bridge_get_data: "
			      "RM_PartitionFirstBP: %s",
			      bg_err_str(rc));
			return SLURM_ERROR;
		}
		if ((rc = bridge_get_data(curr_bp, RM_BPID, &switchid))
		    != STATUS_OK) { 
			error("bridge_get_data: RM_SwitchBPID: %s",
			      bg_err_str(rc));
			return SLURM_ERROR;
		} 

		geo = find_bp_loc(switchid);	
		if(!geo) {
			error("find_bp_loc: bpid %s not known", switchid);
			return SLURM_ERROR;
		}
		ba_system_ptr->grid[geo[X]][geo[Y]][geo[Z]].used = true;
		return SLURM_SUCCESS;
	}
	for (i=0; i<switch_cnt; i++) {
		if(i) {
			if ((rc = bridge_get_data(block_ptr, 
						  RM_PartitionNextSwitch, 
						  &curr_switch)) 
			    != STATUS_OK) {
				error("bridge_get_data: "
				      "RM_PartitionNextSwitch: %s",
				      bg_err_str(rc));
				return SLURM_ERROR;
			}
		} else {
			if ((rc = bridge_get_data(block_ptr, 
						  RM_PartitionFirstSwitch, 
						  &curr_switch)) 
			    != STATUS_OK) {
				error("bridge_get_data: "
				      "RM_PartitionFirstSwitch: %s",
				      bg_err_str(rc));
				return SLURM_ERROR;
			}
		}
		if ((rc = bridge_get_data(curr_switch, RM_SwitchDim, &dim))
		    != STATUS_OK) { 
			error("bridge_get_data: RM_SwitchDim: %s",
			      bg_err_str(rc));
			return SLURM_ERROR;
		} 
		if ((rc = bridge_get_data(curr_switch, RM_SwitchBPID, 
					  &switchid))
		    != STATUS_OK) { 
			error("bridge_get_data: RM_SwitchBPID: %s",
			      bg_err_str(rc));
			return SLURM_ERROR;
		} 

		geo = find_bp_loc(switchid);
		if(!geo) {
			error("find_bp_loc: bpid %s not known", switchid);
			return SLURM_ERROR;
		}
		
		if ((rc = bridge_get_data(curr_switch, RM_SwitchConnNum, &cnt))
		    != STATUS_OK) { 
			error("bridge_get_data: RM_SwitchBPID: %s",
			      bg_err_str(rc));
			return SLURM_ERROR;
		}
		debug2("switch id = %s dim %d conns = %d", 
		       switchid, dim, cnt);
		ba_switch = &ba_system_ptr->
			grid[geo[X]][geo[Y]][geo[Z]].axis_switch[dim];
		for (j=0; j<cnt; j++) {
			if(j) {
				if ((rc = bridge_get_data(
					     curr_switch, 
					     RM_SwitchNextConnection, 
					     &curr_conn)) 
				    != STATUS_OK) {
					error("bridge_get_data: "
					      "RM_SwitchNextConnection: %s",
					       bg_err_str(rc));
					return SLURM_ERROR;
				}
			} else {
				if ((rc = bridge_get_data(
					     curr_switch, 
					     RM_SwitchFirstConnection,
					     &curr_conn)) 
				    != STATUS_OK) {
					error("bridge_get_data: "
					      "RM_SwitchFirstConnection: %s",
					      bg_err_str(rc));
					return SLURM_ERROR;
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
				return SLURM_ERROR;
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
				return SLURM_ERROR;
			}

			if(curr_conn.p1 == 1 && dim == X) {
				if(ba_system_ptr->
				   grid[geo[X]][geo[Y]][geo[Z]].used) {
					debug("I have already been to "
					      "this node %c%c%c",
					      alpha_num[geo[X]],
					      alpha_num[geo[Y]],
					      alpha_num[geo[Z]]);
					return SLURM_ERROR;
				}
				ba_system_ptr->grid[geo[X]][geo[Y]][geo[Z]].
					used = true;		
			}
			debug3("connection going from %d -> %d",
			      curr_conn.p1, curr_conn.p2);
			
			if(ba_switch->int_wire[curr_conn.p1].used) {
				debug("%c%c%c dim %d port %d "
				      "is already in use",
				      alpha_num[geo[X]],
				      alpha_num[geo[Y]],
				      alpha_num[geo[Z]],
				      dim,
				      curr_conn.p1);
				return SLURM_ERROR;
			}
			ba_switch->int_wire[curr_conn.p1].used = 1;
			ba_switch->int_wire[curr_conn.p1].port_tar 
				= curr_conn.p2;
		
			if(ba_switch->int_wire[curr_conn.p2].used) {
				debug("%c%c%c dim %d port %d "
				      "is already in use",
				      alpha_num[geo[X]],
				      alpha_num[geo[Y]],
				      alpha_num[geo[Z]],
				      dim,
				      curr_conn.p2);
				return SLURM_ERROR;
			}
			ba_switch->int_wire[curr_conn.p2].used = 1;
			ba_switch->int_wire[curr_conn.p2].port_tar 
				= curr_conn.p1;
		}
	}
	return SLURM_SUCCESS;

#else
	return SLURM_ERROR;
#endif
	
}

/*
 * get the used wires for a block out of the database and return the
 * node list
 */
extern List get_and_set_block_wiring(char *bg_block_id)
{
#ifdef HAVE_BG_FILES
	int rc, i, j;
	rm_partition_t *block_ptr = NULL;
	int cnt = 0;
	int switch_cnt = 0;
	rm_switch_t *curr_switch = NULL;
	rm_BP_t *curr_bp = NULL;
	char *switchid = NULL;
	rm_connection_t curr_conn;
	int dim;
	ba_node_t *ba_node = NULL; 
	ba_switch_t *ba_switch = NULL; 
	int *geo = NULL;
	List results = list_create(destroy_ba_node);
	ListIterator itr = NULL;
	
	debug2("getting info for block %s\n", bg_block_id);
	
	if ((rc = bridge_get_block(bg_block_id,  &block_ptr)) != STATUS_OK) {
		error("bridge_get_block(%s): %s", 
		      bg_block_id, 
		      bg_err_str(rc));
		goto end_it;
	}	
	
	if ((rc = bridge_get_data(block_ptr, RM_PartitionSwitchNum,
				  &switch_cnt)) != STATUS_OK) {
		error("bridge_get_data(RM_PartitionSwitchNum): %s",
		      bg_err_str(rc));
		goto end_it;
	} 
	if(!switch_cnt) {
		debug3("no switch_cnt");
		if ((rc = bridge_get_data(block_ptr, 
					  RM_PartitionFirstBP, 
					  &curr_bp)) 
		    != STATUS_OK) {
			error("bridge_get_data: "
			      "RM_PartitionFirstBP: %s",
			      bg_err_str(rc));
			goto end_it;
		}
		if ((rc = bridge_get_data(curr_bp, RM_BPID, &switchid))
		    != STATUS_OK) { 
			error("bridge_get_data: RM_SwitchBPID: %s",
			      bg_err_str(rc));
			goto end_it;
		} 

		geo = find_bp_loc(switchid);	
		if(!geo) {
			error("find_bp_loc: bpid %s not known", switchid);
			goto end_it;
		}
		ba_node = xmalloc(sizeof(ba_node_t));
		list_push(results, ba_node);
		ba_node->coord[X] = geo[X];
		ba_node->coord[Y] = geo[Y];
		ba_node->coord[Z] = geo[Z];
		
		ba_node->used = TRUE;
		return results;
	}
	for (i=0; i<switch_cnt; i++) {
		if(i) {
			if ((rc = bridge_get_data(block_ptr, 
						  RM_PartitionNextSwitch, 
						  &curr_switch)) 
			    != STATUS_OK) {
				error("bridge_get_data: "
				      "RM_PartitionNextSwitch: %s",
				      bg_err_str(rc));
				goto end_it;
			}
		} else {
			if ((rc = bridge_get_data(block_ptr, 
						  RM_PartitionFirstSwitch, 
						  &curr_switch)) 
			    != STATUS_OK) {
				error("bridge_get_data: "
				      "RM_PartitionFirstSwitch: %s",
				      bg_err_str(rc));
				goto end_it;
			}
		}
		if ((rc = bridge_get_data(curr_switch, RM_SwitchDim, &dim))
		    != STATUS_OK) { 
			error("bridge_get_data: RM_SwitchDim: %s",
			      bg_err_str(rc));
			goto end_it;
		} 
		if ((rc = bridge_get_data(curr_switch, RM_SwitchBPID, 
					  &switchid))
		    != STATUS_OK) { 
			error("bridge_get_data: RM_SwitchBPID: %s",
			      bg_err_str(rc));
			goto end_it;
		} 

		geo = find_bp_loc(switchid);
		if(!geo) {
			error("find_bp_loc: bpid %s not known", switchid);
			goto end_it;
		}
		
		if ((rc = bridge_get_data(curr_switch, RM_SwitchConnNum, &cnt))
		    != STATUS_OK) { 
			error("bridge_get_data: RM_SwitchBPID: %s",
			      bg_err_str(rc));
			goto end_it;
		}
		debug2("switch id = %s dim %d conns = %d", 
		       switchid, dim, cnt);
		
		itr = list_iterator_create(results);
		while((ba_node = list_next(itr))) {
			if (ba_node->coord[X] == geo[X] &&
			    ba_node->coord[Y] == geo[Y] &&
			    ba_node->coord[Z] == geo[Z]) 
				break;	/* we found it */
		}
		list_iterator_destroy(itr);
		if(!ba_node) {
			ba_node = xmalloc(sizeof(ba_node_t));
			
			list_push(results, ba_node);
			ba_node->coord[X] = geo[X];
			ba_node->coord[Y] = geo[Y];
			ba_node->coord[Z] = geo[Z];
		}
		ba_switch = &ba_node->axis_switch[dim];
		for (j=0; j<cnt; j++) {
			if(j) {
				if ((rc = bridge_get_data(
					     curr_switch, 
					     RM_SwitchNextConnection, 
					     &curr_conn)) 
				    != STATUS_OK) {
					error("bridge_get_data: "
					      "RM_SwitchNextConnection: %s",
					       bg_err_str(rc));
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
					      bg_err_str(rc));
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

			if(curr_conn.p1 == 1 && dim == X) {
				if(ba_node->used) {
					debug("I have already been to "
					      "this node %c%c%c",
					      alpha_num[geo[X]],
					      alpha_num[geo[Y]],
					      alpha_num[geo[Z]]);
					goto end_it;
				}
				ba_node->used = true;		
			}
			debug3("connection going from %d -> %d",
			      curr_conn.p1, curr_conn.p2);
			
			if(ba_switch->int_wire[curr_conn.p1].used) {
				debug("%c%c%c dim %d port %d "
				      "is already in use",
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
		
			if(ba_switch->int_wire[curr_conn.p2].used) {
				debug("%c%c%c dim %d port %d "
				      "is already in use",
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
extern int validate_coord(int *coord)
{
#ifdef HAVE_BG_FILES
	if(coord[X]>=REAL_DIM_SIZE[X] 
	   || coord[Y]>=REAL_DIM_SIZE[Y]
	   || coord[Z]>=REAL_DIM_SIZE[Z]) {
		error("got coord %c%c%c greater than system dims "
		      "%c%c%c",
		      alpha_num[coord[X]],
		      alpha_num[coord[Y]],
		      alpha_num[coord[Z]],
		      alpha_num[REAL_DIM_SIZE[X]],
		      alpha_num[REAL_DIM_SIZE[Y]],
		      alpha_num[REAL_DIM_SIZE[Z]]);
		return 0;
	}

	if(coord[X]>=DIM_SIZE[X] 
	   || coord[Y]>=DIM_SIZE[Y]
	   || coord[Z]>=DIM_SIZE[Z]) {
		debug4("got coord %c%c%c greater than what we are using "
		       "%c%c%c",
		       alpha_num[coord[X]],
		       alpha_num[coord[Y]],
		       alpha_num[coord[Z]],
		       alpha_num[DIM_SIZE[X]],
		       alpha_num[DIM_SIZE[Y]],
		       alpha_num[DIM_SIZE[Z]]);
		return 0;
	}
#endif
	return 1;
}


/********************* Local Functions *********************/

#ifdef HAVE_BG

#ifdef HAVE_BG_FILES
static void _bp_map_list_del(void *object)
{
	ba_bp_map_t *bp_map = (ba_bp_map_t *)object;
	
	if (bp_map) {
		xfree(bp_map->bp_id);
		xfree(bp_map);		
	}
}

/* translation from the enum to the actual port number */
static int _port_enum(int port)
{
	switch(port) {
	case RM_PORT_S0:
		return 0;
		break;
	case RM_PORT_S1:
		return 1;
		break;
	case RM_PORT_S2:
		return 2;
		break;
	case RM_PORT_S3:
		return 3;
		break;
	case RM_PORT_S4:
		return 4;
		break;
	case RM_PORT_S5:
		return 5;
		break;
	default:
		return -1;
	}
}

#endif

/*
 * This function is here to check options for rotating and elongating
 * and set up the request based on the count of each option
 */
static int _check_for_options(ba_request_t* ba_request) 
{
	int temp;
	int set=0;
	int *geo = NULL;
	ListIterator itr;

	if(ba_request->rotate) {
	rotate_again:
		debug2("Rotating! %d",ba_request->rotate_count);
		
		if (ba_request->rotate_count==(BA_SYSTEM_DIMENSIONS-1)) {
			temp=ba_request->geometry[X];
			ba_request->geometry[X]=ba_request->geometry[Z];
			ba_request->geometry[Z]=temp;
			ba_request->rotate_count++;
			set=1;
		
		} else if(ba_request->rotate_count<(BA_SYSTEM_DIMENSIONS*2)) {
			temp=ba_request->geometry[X];
			ba_request->geometry[X]=ba_request->geometry[Y];
			ba_request->geometry[Y]=ba_request->geometry[Z];
			ba_request->geometry[Z]=temp;
			ba_request->rotate_count++;
			set=1;
		} else 
			ba_request->rotate = false;
		if(set) {
			if(ba_request->geometry[X]<=DIM_SIZE[X] 
			   && ba_request->geometry[Y]<=DIM_SIZE[Y]
			   && ba_request->geometry[Z]<=DIM_SIZE[Z])
				return 1;
			else {
				set = 0;
				goto rotate_again;
			}
		}
	}
	if(ba_request->elongate) {
	elongate_again:
		debug2("Elongating! %d",ba_request->elongate_count);
		ba_request->rotate_count=0;
		ba_request->rotate = true;
		
		set = 0;
		itr = list_iterator_create(ba_request->elongate_geos);
		for(set=0; set<=ba_request->elongate_count; set++)
			geo = list_next(itr);
		list_iterator_destroy(itr);
		if(geo == NULL)
			return 0;
		ba_request->elongate_count++;
		ba_request->geometry[X] = geo[X];
		ba_request->geometry[Y] = geo[Y];
		ba_request->geometry[Z] = geo[Z];
		if(ba_request->geometry[X]<=DIM_SIZE[X] 
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
static int _append_geo(int *geometry, List geos, int rotate) 
{
	ListIterator itr;
	int *geo_ptr = NULL;
	int *geo = NULL;
	int temp_geo;
	int i, j;
	
	if(rotate) {
		for (i = (BA_SYSTEM_DIMENSIONS - 1); i >= 0; i--) {
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
	while ((geo_ptr = list_next(itr)) != NULL) {
		if(geometry[X] == geo_ptr[X]
		   && geometry[Y] == geo_ptr[Y]
		   && geometry[Z] == geo_ptr[Z])
			break;
		
	}
	list_iterator_destroy(itr);
	
	if(geo_ptr == NULL) { 
		geo = xmalloc(sizeof(int)*BA_SYSTEM_DIMENSIONS);
		geo[X] = geometry[X];
		geo[Y] = geometry[Y];
		geo[Z] = geometry[Z];
		debug3("adding geo %c%c%c",
		       alpha_num[geo[X]], alpha_num[geo[Y]],
		       alpha_num[geo[Z]]);
		list_append(geos, geo);
	}
	return 1;
}

/*
 * Fill in the paths and extra midplanes we need for the block.
 * Basically copy the x path sent in with the start_list in each Y anx
 * Z dimension filling in every midplane for the block and then
 * completing the Y and Z wiring, tying the whole block together.
 *
 * IN/OUT results - total list of midplanes after this function
 *        returns successfully.  Should be
 *        an exact copy of the start_list at first.
 * IN start_list - exact copy of results at first, This should only be
 *        a list of midplanes on the X dim.  We will work off this and
 *        the geometry to fill in this wiring for the X dim in all the
 *        Y and Z coords.
 * IN geometry - What the block looks like
 * IN conn_type - Mesh or Torus
 * 
 * RET: 0 on failure 1 on success
 */
static int _fill_in_coords(List results, List start_list,
			   int *geometry, int conn_type)
{
	ba_node_t *ba_node = NULL;
	ba_node_t *check_node = NULL;
	int rc = 1;
	ListIterator itr = NULL;
	int y=0, z=0;
	ba_switch_t *curr_switch = NULL; 
	ba_switch_t *next_switch = NULL; 
	
	if(!start_list || !results)
		return 0;
	/* go through the start_list and add all the midplanes */
	itr = list_iterator_create(start_list);
	while((check_node = (ba_node_t*) list_next(itr))) {		
		curr_switch = &check_node->axis_switch[X];
	
		for(y=0; y<geometry[Y]; y++) {
			if((check_node->coord[Y]+y) 
			   >= DIM_SIZE[Y]) {
				rc = 0;
				goto failed;
			}
			for(z=0; z<geometry[Z]; z++) {
				if((check_node->coord[Z]+z) 
				   >= DIM_SIZE[Z]) {
					rc = 0;
					goto failed;
				}
				ba_node = &ba_system_ptr->grid
					[check_node->coord[X]]
					[check_node->coord[Y]+y]
					[check_node->coord[Z]+z];

				if(ba_node->coord[Y] == check_node->coord[Y]
				   && ba_node->coord[Z] == check_node->coord[Z])
					continue;

				if (!_node_used(ba_node, geometry[X])) {
					debug3("here Adding %c%c%c",
					       alpha_num[ba_node->coord[X]],
					       alpha_num[ba_node->coord[Y]],
					       alpha_num[ba_node->coord[Z]]);
					list_append(results, ba_node);
					next_switch = &ba_node->axis_switch[X];
					
					/* since we are going off the
					 * main system we can send NULL
					 * here
					 */
					_copy_the_path(NULL, curr_switch, 
						       next_switch, 
						       0, X);
				} else {
					rc = 0;
					goto failed;
				}
			}
		}
		
	}
	list_iterator_destroy(itr);
	itr = list_iterator_create(start_list);
	check_node = (ba_node_t*) list_next(itr);
	list_iterator_destroy(itr);
	
	itr = list_iterator_create(results);
	while((ba_node = (ba_node_t*) list_next(itr))) {
		if(!_find_yz_path(ba_node, 
				  check_node->coord, 
				  geometry, 
				  conn_type)){
			rc = 0;
			goto failed;
		}
	}

	if(deny_pass) {
		if((*deny_pass & PASS_DENY_Y)
		   && (*deny_pass & PASS_FOUND_Y)) {
			debug("We don't allow Y passthoughs");
			rc = 0;
		} else if((*deny_pass & PASS_DENY_Z)
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
 * IN/OUT: nodes - Local list of midplanes you are keeping track of.  If
 *         you visit any new midplanes a copy from ba_system_grid  
 *         will be added to the list.  If NULL the path will be
 *         set in mark_switch of the main virtual system (ba_system_grid).  
 * IN: curr_switch - The switch you want to copy the path of
 * IN/OUT: mark_switch - The switch you want to fill in.  On success
 *         this switch will contain a complete path from the curr_switch
 *         starting from the source port.
 * IN: source - source port number (If calling for the first time
 *         should be 0 since we are looking for 1 at the end)
 * IN: dim - Dimension XYZ
 *
 * RET: on success 1, on error 0
 */
static int _copy_the_path(List nodes, ba_switch_t *curr_switch, 
			  ba_switch_t *mark_switch, 
			  int source, int dim)
{
	int *node_tar;
	int *mark_node_tar;
	int *node_curr;
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
	node_curr = curr_switch->ext_wire[0].node_tar;
	node_tar = curr_switch->ext_wire[port_tar].node_tar;
	if(mark_switch->int_wire[source].used)
		debug2("setting dim %d %c%c%c %d-> %c%c%c %d",
		       dim,
		       alpha_num[node_curr[X]],
		       alpha_num[node_curr[Y]],
		       alpha_num[node_curr[Z]],
		       source, 
		       alpha_num[node_tar[X]],
		       alpha_num[node_tar[Y]],
		       alpha_num[node_tar[Z]],
		       port_tar);	
	
	if(port_tar == 1) {
		/* found the end of the line */
		mark_switch->int_wire[1].used = 
			curr_switch->int_wire[1].used;
		mark_switch->int_wire[1].port_tar = 
			curr_switch->int_wire[1].port_tar;
		return 1;
	}
	
	mark_node_tar = mark_switch->ext_wire[port_tar].node_tar;
	port_tar = curr_switch->ext_wire[port_tar].port_tar;
	
	if(node_curr[X] == node_tar[X]
	   && node_curr[Y] == node_tar[Y]
	   && node_curr[Z] == node_tar[Z]) {
		/* We are going to the same node! this should never
		   happen */
		debug4("something bad happened!! "
		       "we are on %c%c%c and are going to it "
		       "from port %d - > %d", 
		       alpha_num[node_curr[X]],
		       alpha_num[node_curr[Y]],
		       alpha_num[node_curr[Z]],
		       port_tar1, port_tar);
		return 0;
	}

	/* see what the next switch is going to be */
	next_switch = &ba_system_ptr->
		grid[node_tar[X]][node_tar[Y]][node_tar[Z]].axis_switch[dim];
	if(!nodes) {
		/* If no nodes then just get the next switch to fill
		   in from the main system */
		next_mark_switch = &ba_system_ptr->
			grid[mark_node_tar[X]]
			[mark_node_tar[Y]]
			[mark_node_tar[Z]]
			.axis_switch[dim];
	} else {
		ba_node_t *ba_node = NULL;
		ListIterator itr = list_iterator_create(nodes);
		/* see if we have already been to this node */
		while((ba_node = list_next(itr))) {
			if (ba_node->coord[X] == mark_node_tar[X] &&
			    ba_node->coord[Y] == mark_node_tar[Y] &&
			    ba_node->coord[Z] == mark_node_tar[Z]) 
				break;	/* we found it */
		}
		list_iterator_destroy(itr);
		if(!ba_node) {
			/* If node grab a copy and add it to the list */
			ba_node = ba_copy_node(&ba_system_ptr->
					       grid[mark_node_tar[X]]
					       [mark_node_tar[Y]]
					       [mark_node_tar[Z]]);
			_new_ba_node(ba_node, mark_node_tar, false);
			list_push(nodes, ba_node);
			debug3("haven't seen %c%c%c adding it",
			       alpha_num[ba_node->coord[X]], 
			       alpha_num[ba_node->coord[Y]],
			       alpha_num[ba_node->coord[Z]]);
		}
		next_mark_switch = &ba_node->axis_switch[dim];
			
	}

	/* Keep going until we reach the end of the line */
	return _copy_the_path(nodes, next_switch, next_mark_switch,
			      port_tar, dim);
}

static int _find_yz_path(ba_node_t *ba_node, int *first, 
			 int *geometry, int conn_type)
{
	ba_node_t *next_node = NULL;
	int *node_tar = NULL;
	ba_switch_t *dim_curr_switch = NULL; 
	ba_switch_t *dim_next_switch = NULL; 
	int i2;
	int count = 0;

	for(i2=1;i2<=2;i2++) {
		if(geometry[i2] > 1) {
			debug3("%d node %c%c%c port 2 -> ",
			       i2,
			       alpha_num[ba_node->coord[X]],
			       alpha_num[ba_node->coord[Y]],
			       alpha_num[ba_node->coord[Z]]);
							       
			dim_curr_switch = &ba_node->axis_switch[i2];
			if(dim_curr_switch->int_wire[2].used) {
				debug4("returning here");
				return 0;
			}
							
			node_tar = dim_curr_switch->ext_wire[2].node_tar;
							
			next_node = &ba_system_ptr->
				grid[node_tar[X]][node_tar[Y]][node_tar[Z]];
			dim_next_switch = &next_node->axis_switch[i2];
			debug3("%c%c%c port 5",
			       alpha_num[next_node->coord[X]],
			       alpha_num[next_node->coord[Y]],
			       alpha_num[next_node->coord[Z]]);
							  
			if(dim_next_switch->int_wire[5].used) {
				debug2("returning here 2");
				return 0;
			}
			debug4("%d %d %d %d",i2, node_tar[i2],
			       first[i2], geometry[i2]);

			/* Here we need to see where we are in
			 * reference to the geo of this dimension.  If
			 * we have not gotten the number we need in
			 * the direction we just go to the next node
			 * with 5 -> 1.  If we have all the midplanes
			 * we need then we go through and finish the
			 * torus if needed
			 */			 
			if(node_tar[i2] < first[i2]) 
				count = node_tar[i2]+(DIM_SIZE[i2]-first[i2]);
			else 
				count = (node_tar[i2]-first[i2]);

			if(count == geometry[i2]) {
				debug4("found end of me %c%c%c",
				       alpha_num[node_tar[X]],
				       alpha_num[node_tar[Y]],
				       alpha_num[node_tar[Z]]);
				if(conn_type == SELECT_TORUS) {
					dim_curr_switch->int_wire[0].used = 1;
					dim_curr_switch->int_wire[0].port_tar
						= 2;
					dim_curr_switch->int_wire[2].used = 1;
					dim_curr_switch->int_wire[2].port_tar
						= 0;
					dim_curr_switch = dim_next_switch;

					if(deny_pass
					   && (node_tar[i2] != first[i2])) {
						if(i2 == 1) 
							*deny_pass |=
								PASS_FOUND_Y;
						else 
							*deny_pass |=
								PASS_FOUND_Z;
					}
					while(node_tar[i2] != first[i2]) {
						debug3("on dim %d at %d "
						       "looking for %d",
						       i2,
						       node_tar[i2],
						       first[i2]);
						
						if(dim_curr_switch->
						   int_wire[2].used) {
							debug3("returning "
							       "here 3");
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
						
						
						node_tar = dim_curr_switch->
							ext_wire[2].node_tar;
						next_node = &ba_system_ptr->
							grid
							[node_tar[X]]
							[node_tar[Y]]
							[node_tar[Z]];
						dim_curr_switch = 
							&next_node->
							axis_switch[i2];
					}
									
					debug3("back to first on dim %d "
					       "at %d looking for %d",
					       i2,
					       node_tar[i2],
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
				if(conn_type == SELECT_TORUS || 
				   (conn_type == SELECT_MESH && 
				    (node_tar[i2] != first[i2]))) {
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
		} else if(geometry[i2] == 1) {
			/* FIX ME: This is put here because we got
			   into a state where the Y dim was not being
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

			dim_curr_switch = &ba_node->axis_switch[i2];
			debug3("%d node %c%c%c port 0 -> 1",
			       i2,
			       alpha_num[ba_node->coord[X]],
			       alpha_num[ba_node->coord[Y]],
			       alpha_num[ba_node->coord[Z]]);
			dim_curr_switch->int_wire[0].used = 1;
			dim_curr_switch->int_wire[0].port_tar = 1;
			dim_curr_switch->int_wire[1].used = 1;
			dim_curr_switch->int_wire[1].port_tar = 0;
		}
	}
	return 1;
}

#endif

#ifndef HAVE_BG_FILES
/** */
#ifdef HAVE_3D
static int _emulate_ext_wiring(ba_node_t ***grid)
#else
static int _emulate_ext_wiring(ba_node_t *grid)
#endif
{
	int x;
	ba_node_t *source = NULL, *target = NULL;

#ifdef HAVE_3D
	int y,z;
	init_wires();
	
	for(x=0;x<DIM_SIZE[X];x++) {
		for(y=0;y<DIM_SIZE[Y];y++) {
			for(z=0;z<DIM_SIZE[Z];z++) {
				source = &grid[x][y][z];
				
				if(x<(DIM_SIZE[X]-1)) {
					target = &grid[x+1][y][z];
				} else
					target = &grid[0][y][z];

				_set_external_wires(X, x, source, 
						    target);
				
				if(y<(DIM_SIZE[Y]-1)) 
					target = &grid[x][y+1][z];
				else 
					target = &grid[x][0][z];
				
				_set_external_wires(Y, y, source, 
						    target);
				if(z<(DIM_SIZE[Z]-1)) 
					target = &grid[x][y][z+1];
				else 
					target = &grid[x][y][0];
				
				_set_external_wires(Z, z, source, 
						    target);
			}
		}
	}
#else
	for(x=0;x<DIM_SIZE[X];x++) {
		source = &grid[x];
		target = &grid[x+1];
		_set_external_wires(X, x, source, 
				    target);
	}
#endif
	return 1;
}
#endif


static int _reset_the_path(ba_switch_t *curr_switch, int source, 
			   int target, int dim)
{
	int *node_tar;
	int *node_curr;
	int port_tar, port_tar1;
	ba_switch_t *next_switch = NULL; 

	if(source < 0 || source > NUM_PORTS_PER_NODE) {
		fatal("source port was %d can only be 0->%d",
		      source, NUM_PORTS_PER_NODE);
	}
	if(target < 0 || target > NUM_PORTS_PER_NODE) {
		fatal("target port was %d can only be 0->%d",
		      target, NUM_PORTS_PER_NODE);
	}
	/*set the switch to not be used */
	if(!curr_switch->int_wire[source].used) {
		debug("I reached the end, the source isn't used");
		return 1;
	}
	curr_switch->int_wire[source].used = 0;
	port_tar = curr_switch->int_wire[source].port_tar;
	if(port_tar < 0 || port_tar > NUM_PORTS_PER_NODE) {
		fatal("port_tar port was %d can only be 0->%d",
		      source, NUM_PORTS_PER_NODE);
	}
	
	port_tar1 = port_tar;
	curr_switch->int_wire[source].port_tar = source;
	curr_switch->int_wire[port_tar].used = 0;
	curr_switch->int_wire[port_tar].port_tar = port_tar;
	if(port_tar==target) {
		return 1;
	}
	/* follow the path */
	node_curr = curr_switch->ext_wire[0].node_tar;
	node_tar = curr_switch->ext_wire[port_tar].node_tar;
	port_tar = curr_switch->ext_wire[port_tar].port_tar;
	if(source == port_tar1) {
		debug("got this bad one %c%c%c %d %d -> %c%c%c %d",
		      alpha_num[node_curr[X]],
		      alpha_num[node_curr[Y]],
		      alpha_num[node_curr[Z]],
		      source,
		      port_tar1,
		      alpha_num[node_tar[X]],
		      alpha_num[node_tar[Y]],
		      alpha_num[node_tar[Z]],
		      port_tar);
		return 0;
	}
	debug4("from %c%c%c %d %d -> %c%c%c %d",
	       alpha_num[node_curr[X]],
	       alpha_num[node_curr[Y]],
	       alpha_num[node_curr[Z]],
	       source,
	       port_tar1,
	       alpha_num[node_tar[X]],
	       alpha_num[node_tar[Y]],
	       alpha_num[node_tar[Z]],
	       port_tar);
	if(node_curr[X] == node_tar[X]
	   && node_curr[Y] == node_tar[Y]
	   && node_curr[Z] == node_tar[Z]) {
		debug4("%d something bad happened!!", dim);
		return 0;
	}
	next_switch = &ba_system_ptr->
		grid[node_tar[X]]
#ifdef HAVE_3D
		[node_tar[Y]]
		[node_tar[Z]]
#endif
		.axis_switch[dim];

	return _reset_the_path(next_switch, port_tar, target, dim);
//	return 1;
}

static void _new_ba_node(ba_node_t *ba_node, int *coord, bool track_down_nodes)
{
	int i,j;
	uint16_t node_base_state = ba_node->state & NODE_STATE_BASE;
	
	if(((node_base_state != NODE_STATE_DOWN)
	   && !(ba_node->state & NODE_STATE_DRAIN)) || !track_down_nodes) 
		ba_node->used = false;

	for (i=0; i<BA_SYSTEM_DIMENSIONS; i++){
		ba_node->coord[i] = coord[i];
		
		for(j=0;j<NUM_PORTS_PER_NODE;j++) {
			ba_node->axis_switch[i].int_wire[j].used = 0;	
			if(i!=X) {
				if(j==3 || j==4) 
					ba_node->axis_switch[i].int_wire[j].
						used = 1;	
			}
			ba_node->axis_switch[i].int_wire[j].port_tar = j;
		}
	}
}

static void _create_ba_system(void)
{
	int x;
	int coord[BA_SYSTEM_DIMENSIONS];
				
#ifdef HAVE_3D
	int y,z;
	ba_system_ptr->grid = (ba_node_t***) 
		xmalloc(sizeof(ba_node_t**) * DIM_SIZE[X]);
#else
	ba_system_ptr->grid = (ba_node_t*) 
		xmalloc(sizeof(ba_node_t) * DIM_SIZE[X]);
#endif
	for (x=0; x<DIM_SIZE[X]; x++) {
#ifdef HAVE_3D
		ba_system_ptr->grid[x] = (ba_node_t**) 
			xmalloc(sizeof(ba_node_t*) * DIM_SIZE[Y]);
		for (y=0; y<DIM_SIZE[Y]; y++) {
			ba_system_ptr->grid[x][y] = (ba_node_t*) 
				xmalloc(sizeof(ba_node_t) * DIM_SIZE[Z]);
			for (z=0; z<DIM_SIZE[Z]; z++){
				coord[X] = x;
				coord[Y] = y;
				coord[Z] = z;
				_new_ba_node(&ba_system_ptr->grid[x][y][z], 
					     coord, true);
			}
		}
#else
		coord[X] = x;
		_new_ba_node(&ba_system_ptr->grid[x], coord, true);
#endif
	}
}

/** */
static void _delete_ba_system(void)
{
#ifdef HAVE_BG
	int x=0;
	int y;
#endif
	if (!ba_system_ptr){
		return;
	}
	
	if(ba_system_ptr->grid) {
#ifdef HAVE_BG
		for (x=0; x<DIM_SIZE[X]; x++) {
			for (y=0; y<DIM_SIZE[Y]; y++)
				xfree(ba_system_ptr->grid[x][y]);
			
			xfree(ba_system_ptr->grid[x]);
		}
#endif
		
		
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
#ifdef HAVE_BG
	int start[BA_SYSTEM_DIMENSIONS] = {0,0,0};
#else
	int start[BA_SYSTEM_DIMENSIONS] = {0};
#endif
	ba_node_t *ba_node = NULL;
	char *name=NULL;
	int startx = (start[X]-1);
	
	if(startx == -1)
		startx = DIM_SIZE[X]-1;
	if(ba_request->start_req) {
		if(ba_request->start[X]>=DIM_SIZE[X] 
#ifdef HAVE_BG
		   || ba_request->start[Y]>=DIM_SIZE[Y]
		   || ba_request->start[Z]>=DIM_SIZE[Z]
#endif
			)
			return 0;
		for(x=0;x<BA_SYSTEM_DIMENSIONS;x++) {
			start[x] = ba_request->start[x];
		}
	}
	x=0;
	
	if(ba_request->geometry[X]>DIM_SIZE[X] 
#ifdef HAVE_3D
	   || ba_request->geometry[Y]>DIM_SIZE[Y]
	   || ba_request->geometry[Z]>DIM_SIZE[Z]
#endif
		)
#ifdef HAVE_BG
		if(!_check_for_options(ba_request))
#endif
			return 0;

#ifdef HAVE_BG
start_again:
#endif
	x=0;
	if(x == startx)
		x = startx-1;
	while(x!=startx) {
		x++;
		debug3("finding %c%c%c try %d",
		       alpha_num[ba_request->geometry[X]],
#ifdef HAVE_3D
		       alpha_num[ba_request->geometry[Y]],
		       alpha_num[ba_request->geometry[Z]],
#endif
		       x);
#ifdef HAVE_3D
	new_node:
#endif
		debug2("starting at %c%c%c",
		       alpha_num[start[X]]
#ifdef HAVE_3D
		       , alpha_num[start[Y]],
		       alpha_num[start[Z]]
#endif
			);
		
		ba_node = &ba_system_ptr->
			grid[start[X]]
#ifdef HAVE_3D
			[start[Y]]
			[start[Z]]
#endif
			;

		if (!_node_used(ba_node, ba_request->geometry[X])) {
			debug3("trying this node %c%c%c %c%c%c %d",
			       alpha_num[start[X]],
			       alpha_num[start[Y]],
			       alpha_num[start[Z]],
			       alpha_num[ba_request->geometry[X]],
			       alpha_num[ba_request->geometry[Y]],
			       alpha_num[ba_request->geometry[Z]], 
			       ba_request->conn_type);
			name = set_bg_block(results,
					    start, 
					    ba_request->geometry, 
					    ba_request->conn_type);
			if(name) {
				ba_request->save_name = xstrdup(name);
				xfree(name);
				return 1;
			}
			
			if(results) {
				remove_block(results, color_count);
				list_delete_all(results,
						&empty_null_destroy_list, "");
			}
			if(ba_request->start_req) 
				goto requested_end;
			//exit(0);
			debug2("trying something else");
			
		}
		
#ifdef HAVE_3D
		
		if((DIM_SIZE[Z]-start[Z]-1)
		   >= ba_request->geometry[Z])
			start[Z]++;
		else {
			start[Z] = 0;
			if((DIM_SIZE[Y]-start[Y]-1)
			   >= ba_request->geometry[Y])
				start[Y]++;
			else {
				start[Y] = 0;
				if ((DIM_SIZE[X]-start[X]-1)
				    >= ba_request->geometry[X])
					start[X]++;
				else {
					if(ba_request->size == 1)
						goto requested_end;
#ifdef HAVE_BG
					if(!_check_for_options(ba_request))
						return 0;
					else {
						start[X]=0;
						start[Y]=0;
						start[Z]=0;
						goto start_again;
					}
#else
					return 0;
#endif
				}
			}
		}
		goto new_node;
#endif
	}							
requested_end:
	debug2("1 can't allocate");
	
	return 0;
}

/* 
 * Used to check if midplane is usable in the block we are creating
 *
 * IN: ba_node - node to check if is used
 * IN: x_size - How big is the block in the X dim used to see if the
 *     wires are full hence making this midplane unusable.
 */
static bool _node_used(ba_node_t* ba_node, int x_size)
{
	ba_switch_t* ba_switch = NULL;
	
	/* if we've used this node in another block already */
	if (!ba_node || ba_node->used) {
		debug3("node %c%c%c used", 
		       alpha_num[ba_node->coord[X]],
		       alpha_num[ba_node->coord[Y]],
		       alpha_num[ba_node->coord[Z]]);
		return true;
	}
	/* Check If we've used this node's switches completely in another 
	   block already.  Right now we are only needing to look at
	   the X dim since it is the only one with extra wires.  This
	   can be set up to do all the dim's if in the future if it is
	   needed. We only need to check this if we are planning on
	   using more than 1 midplane in the block creation */
	if(x_size > 1) {
		/* get the switch of the X Dimension */
		ba_switch = &ba_node->axis_switch[X];
		
		/* If both of these ports are used then the node
		   is in use since there are no more wires we
		   can use since these can not connect to each
		   other they must be connected to the other ports.
		*/
		if(ba_switch->int_wire[3].used && ba_switch->int_wire[5].used) {
			debug3("switch full in the X dim on node %c%c%c!",
			       alpha_num[ba_node->coord[X]],
			       alpha_num[ba_node->coord[Y]],
			       alpha_num[ba_node->coord[Z]]);
			return true;
		}
	}
		
	return false;

}


static void _switch_config(ba_node_t* source, ba_node_t* target, int dim, 
			   int port_src, int port_tar)
{
	ba_switch_t* config = NULL, *config_tar = NULL;
	int i;

	if (!source || !target)
		return;
	
	config = &source->axis_switch[dim];
	config_tar = &target->axis_switch[dim];
	for(i=0;i<BA_SYSTEM_DIMENSIONS;i++) {
		/* Set the coord of the source target node to the target */
		config->ext_wire[port_src].node_tar[i] = target->coord[i];
	
		/* Set the coord of the target back to the source */
		config_tar->ext_wire[port_tar].node_tar[i] = source->coord[i];
	}

	/* Set the port of the source target node to the target */
	config->ext_wire[port_src].port_tar = port_tar;
	
	/* Set the port of the target back to the source */
	config_tar->ext_wire[port_tar].port_tar = port_src;
}

static int _set_external_wires(int dim, int count, ba_node_t* source, 
			       ba_node_t* target)
{
#ifdef HAVE_BG_FILES
#ifdef HAVE_BGL

#define UNDER_POS  7
#define NODE_LEN 5
#define VAL_NAME_LEN 12

#else

#define UNDER_POS  9
#define NODE_LEN 7
#define VAL_NAME_LEN 16

#endif
	int rc;
	int i;
	rm_wire_t *my_wire = NULL;
	rm_port_t *my_port = NULL;
	char *wire_id = NULL;
	int from_port, to_port;
	int wire_num;
	int *coord;
	char from_node[NODE_LEN];
	char to_node[NODE_LEN];

	if (!have_db2) {
		error("Can't access DB2 library, run from service node");
		return -1;
	}
	
	if (!bg) {
		if((rc = bridge_get_bg(&bg)) != STATUS_OK) {
			error("bridge_get_BG(): %d", rc);
			return -1;
		}
	}
		
	if (bg == NULL) 
		return -1;
	
	if ((rc = bridge_get_data(bg, RM_WireNum, &wire_num)) != STATUS_OK) {
		error("bridge_get_data(RM_BPNum): %d", rc);
		wire_num = 0;
	}
	/* find out system wires on each bp */
	
	for (i=0; i<wire_num; i++) {
		
		if (i) {
			if ((rc = bridge_get_data(bg, RM_NextWire, &my_wire))
			    != STATUS_OK) {
				error("bridge_get_data(RM_NextWire): %d", rc);
				break;
			}
		} else {
			if ((rc = bridge_get_data(bg, RM_FirstWire, &my_wire))
			    != STATUS_OK) {
				error("bridge_get_data(RM_FirstWire): %d", rc);
				break;
			}
		}
		if ((rc = bridge_get_data(my_wire, RM_WireID, &wire_id))
		    != STATUS_OK) {
			error("bridge_get_data(RM_FirstWire): %d", rc);
			break;
		}
		
		if(!wire_id) {
			error("No Wire ID was returned from database");
			continue;
		}

		if(wire_id[UNDER_POS] != '_') 
			continue;
		switch(wire_id[0]) {
		case 'X':
			dim = X;
			break;
		case 'Y':
			dim = Y;
			break;
		case 'Z':
			dim = Z;
			break;
		}
		if(strlen(wire_id) < VAL_NAME_LEN) {
			error("Wire_id isn't correct %s",wire_id);
			continue;
		}
		
                memset(&from_node, 0, sizeof(from_node));
                memset(&to_node, 0, sizeof(to_node));
                strncpy(from_node, wire_id+2, NODE_LEN-1);
                strncpy(to_node, wire_id+UNDER_POS+1, NODE_LEN-1);
		free(wire_id);

		if ((rc = bridge_get_data(my_wire, RM_WireFromPort, &my_port))
		    != STATUS_OK) {
			error("bridge_get_data(RM_FirstWire): %d", rc);
			break;
		}
		if ((rc = bridge_get_data(my_port, RM_PortID, &from_port))
		    != STATUS_OK) {
			error("bridge_get_data(RM_PortID): %d", rc);
			break;
		}
		if ((rc = bridge_get_data(my_wire, RM_WireToPort, &my_port))
		    != STATUS_OK) {
			error("bridge_get_data(RM_WireToPort): %d", rc);
			break;
		}
		if ((rc = bridge_get_data(my_port, RM_PortID, &to_port))
		    != STATUS_OK) {
			error("bridge_get_data(RM_PortID): %d", rc);
			break;
		}

		coord = find_bp_loc(from_node);
		if(!coord) {
			error("1 find_bp_loc: bpid %s not known", from_node);
			continue;
		}
		if(!validate_coord(coord))
			continue;

		source = &ba_system_ptr->
			grid[coord[X]][coord[Y]][coord[Z]];
		coord = find_bp_loc(to_node);
		if(!coord) {
			error("2 find_bp_loc: bpid %s not known", to_node);
			continue;
		}
		if(!validate_coord(coord))
			continue;

		target = &ba_system_ptr->
			grid[coord[X]][coord[Y]][coord[Z]];
		_switch_config(source, 
			       target, 
			       dim, 
			       _port_enum(from_port),
			       _port_enum(to_port));	
		
		debug2("dim %d from %c%c%c %d -> %c%c%c %d",
		       dim,
		       alpha_num[source->coord[X]],
		       alpha_num[source->coord[Y]],
		       alpha_num[source->coord[Z]],
		       _port_enum(from_port),
		       alpha_num[target->coord[X]],
		       alpha_num[target->coord[Y]], 
		       alpha_num[target->coord[Z]],
		       _port_enum(to_port));
	}
#else

	_switch_config(source, source, dim, 0, 0);
	_switch_config(source, source, dim, 1, 1);
	if(dim!=X) {
		_switch_config(source, target, dim, 2, 5);
		_switch_config(source, source, dim, 3, 3);
		_switch_config(source, source, dim, 4, 4);
		return 1;
	}
	
#ifdef HAVE_BG
	/* set up x */
	/* always 2->5 of next. If it is the last it will go to the first.*/
	_switch_config(source, target, dim, 2, 5);

	/* set up split x */
	if(DIM_SIZE[X] == 1) {
	} else if(DIM_SIZE[X] == 5) {
		/* 4 X dim fixes for wires */
		switch(count) {
		case 0:
		case 2:
			/* 0th and 2nd node */
			/* Only the 2-5 is used here
			   so nothing else */
			break;
		case 1:
			/* 1st node */
			/* change target to 4th node */
			target = &ba_system_ptr->grid[4]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of 4th */
			_switch_config(source, target, dim, 4, 3);
			break;
		case 3:
			/* 3rd node */
			/* change target to 2th node */
			target = &ba_system_ptr->grid[2]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of 2nd */
			_switch_config(source, target, dim, 4, 3);
			break;
		case 4:
			/* 4th node */
			/* change target to 1st node */
			target = &ba_system_ptr->grid[1]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of 1st */
			_switch_config(source, target, dim, 4, 3);
			
			break;
		default:
			fatal("got %d for a count on a %d X-dim system",
			      count, DIM_SIZE[X]);
			break;
		}
	} else if(DIM_SIZE[X] == 8) {
		switch(count) {
		case 0:
		case 4:
			/* 0 and 4th Node */
			/* nothing */
			break;
		case 1:
		case 5:
			/* 1st Node */
			target = &ba_system_ptr->grid[count-1]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of previous */
			_switch_config(source, target, dim, 4, 3);
			break;	
		case 2:
			/* 2nd Node */
			target = &ba_system_ptr->grid[7]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of last */
			_switch_config(source, target, dim, 4, 3);
			break;
		case 3:
			/* 3rd Node */
			target = &ba_system_ptr->grid[6]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of 6th */
			_switch_config(source, target, dim, 4, 3);
			break;
		case 6:
			/* 6th Node */
			target = &ba_system_ptr->grid[3]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of 3rd */
			_switch_config(source, target, dim, 4, 3);	
			break;
		case 7:
			/* 7th Node */
			target = &ba_system_ptr->grid[2]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of 2nd */
			_switch_config(source, target, dim, 4, 3);	
			break;
		default:
			fatal("got %d for a count on a %d X-dim system",
			      count, DIM_SIZE[X]);
			break;
		}
	} else if(DIM_SIZE[X] == 13) {
		int temp_num = 0;

		switch(count) {
		case 0:
		case 6:
			/* 0 and 6th Node no split */
		case 8:
		case 9:
		case 10:
		case 11:
		case 12:
			/* already taken care of in the next case so
			 * do nothing
			 */
			break;
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
			/* get the node count - 1 then subtract it
			 * from 12 to get the new target and then go
			 * from 3->4 and back again
			 */
			temp_num = 12 - (count - 1);
			if(temp_num < 5) 
				fatal("node %d shouldn't go to %d",
				      count, temp_num);
			
			target = &ba_system_ptr->grid[temp_num]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 3->4 */
			_switch_config(source, target, dim, 3, 4);
			/* and back 3->4 */
			_switch_config(target, source, dim, 3, 4);
			break;
		case 7:
			/* 7th Node */
			target = &ba_system_ptr->grid[count-1]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of previous */
			_switch_config(source, target, dim, 4, 3);
			break;	
		default:
			fatal("got %d for a count on a %d X-dim system",
			      count, DIM_SIZE[X]);
			break;
		}
	} else {
		fatal("We don't have a config to do a BG system with %d "
		      "in the X-dim.", DIM_SIZE[X]);
	}
#else
	if(count == 0)
		_switch_config(source, source, dim, 5, 5);
	else if(count < DIM_SIZE[X]-1)
		_switch_config(source, target, dim, 2, 5);
	else
		_switch_config(source, source, dim, 2, 2);
	_switch_config(source, source, dim, 3, 3);
	_switch_config(source, source, dim, 4, 4);
#endif /* HAVE_BG */
#endif /* HAVE_BG_FILES */
	return 1;
}
				
static char *_set_internal_wires(List nodes, int size, int conn_type)
{
	ba_node_t* ba_node[size+1];
	int count=0, i, set=0;
	int *start = NULL;
	int *end = NULL;
	char *name;
	ListIterator itr;
	hostlist_t hostlist;
	char temp_name[4];

	if(!nodes)
		return NULL;

	name = xmalloc(BUFSIZE);
	hostlist = hostlist_create(NULL);
	itr = list_iterator_create(nodes);
	while((ba_node[count] = list_next(itr))) {
		snprintf(temp_name, sizeof(temp_name), "%c%c%c", 
			 alpha_num[ba_node[count]->coord[X]],
			 alpha_num[ba_node[count]->coord[Y]],
			 alpha_num[ba_node[count]->coord[Z]]);
		debug3("name = %s", temp_name);
		count++;
		hostlist_push(hostlist, temp_name);
	}
	list_iterator_destroy(itr);
		
	start = ba_node[0]->coord;
	end = ba_node[count-1]->coord;	
	hostlist_ranged_string(hostlist, BUFSIZE, name);
	hostlist_destroy(hostlist);

	for(i=0;i<count;i++) {
		if(!ba_node[i]->used) {
			ba_node[i]->used=1;
			if(ba_node[i]->letter == '.') {
				ba_node[i]->letter = letters[color_count%62];
				ba_node[i]->color = colors[color_count%6];
				debug3("count %d setting letter = %c "
				       "color = %d",
				       color_count,
				       ba_node[i]->letter,
				       ba_node[i]->color);
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

	if(conn_type == SELECT_TORUS)
		for(i=0;i<count;i++) {
			_set_one_dim(start, end, ba_node[i]->coord);
		}

	if(set)
		color_count++;		

	return name;
}				

/*
 * Used to find a complete path based on the conn_type for an x dim.
 * When starting to wire a block together this should be called first.
 *
 * IN/OUT: results - contains the number of midplanes we are
 *     potentially going to use in the X dim.  
 * IN: ba_node - current node we are looking at and have already added
 *     to results.
 * IN: start - coordinates of the first midplane (so we know when when
 *     to end with a torus)
 * IN: x_size - How many midplanes are we looking for in the X dim
 * IN: found - count of how many midplanes we have found in the x dim
 * IN: conn_type - MESH or TORUS
 * IN: algo - algorythm to try an allocation by
 *
 * RET: 0 on failure, 1 on success
 */
static int _find_x_path(List results, ba_node_t *ba_node, 
			int *start, int x_size, 
			int found, int conn_type, block_algo_t algo) 
{
	ba_switch_t *curr_switch = NULL; 
	ba_switch_t *next_switch = NULL; 
	
	int port_tar = 0;
	int source_port=0;
	int target_port=1;
	int broke = 0, not_first = 0;
	int ports_to_try[2] = {4, 2};
	int *node_tar = NULL;
	int i = 0;
	ba_node_t *next_node = NULL;
	ba_node_t *check_node = NULL;
/* 	int highest_phys_x = x_size - start[X]; */
/* 	info("highest_phys_x is %d", highest_phys_x); */

	ListIterator itr = NULL;

	if(!ba_node || !results || !start)
		return 0;

	curr_switch = &ba_node->axis_switch[X];

	/* we don't need to go any further */
	if(x_size == 1) {
		curr_switch->int_wire[source_port].used = 1;
		curr_switch->int_wire[source_port].port_tar = target_port;
		curr_switch->int_wire[target_port].used = 1;
		curr_switch->int_wire[target_port].port_tar = source_port;
		return 1;
	}

	if(algo == BLOCK_ALGO_FIRST) {
		ports_to_try[0] = 4;
		ports_to_try[1] = 2;
	} else if(algo == BLOCK_ALGO_SECOND) {
		ports_to_try[0] = 2;
		ports_to_try[1] = 4;
	} else {
		error("Unknown algo %d", algo);
		return 0;
	}			
	
	debug3("Algo(%d) found - %d", algo, found);

	/* Check the 2 ports we can leave though in ports_to_try */
	for(i=0;i<2;i++) {
/* 		info("trying port %d", ports_to_try[i]); */
		/* check to make sure it isn't used */
		if(!curr_switch->int_wire[ports_to_try[i]].used) {
			/* looking at the next node on the switch 
			   and it's port we are going to */
			node_tar = curr_switch->
				ext_wire[ports_to_try[i]].node_tar;
			port_tar = curr_switch->
				ext_wire[ports_to_try[i]].port_tar;
/* 			info("%c%c%c port %d goes to %c%c%c port %d", */
/* 			     alpha_num[ba_node->coord[X]], */
/* 			     alpha_num[ba_node->coord[Y]], */
/* 			     alpha_num[ba_node->coord[Z]], */
/* 			     ports_to_try[i], */
/* 			     alpha_num[node_tar[X]], */
/* 			     alpha_num[node_tar[Y]], */
/* 			     alpha_num[node_tar[Z]], */
/* 			     port_tar); */
			/* check to see if we are back at the start of the
			   block */
			if((node_tar[X] == start[X] 
			    && node_tar[Y] == start[Y] 
			    && node_tar[Z] == start[Z])) {
				broke = 1;
				goto broke_it;
			}
			/* check to see if the port points to itself */
			if((node_tar[X] == ba_node->coord[X]
			    && node_tar[Y] == ba_node->coord[Y]
			    && node_tar[Z] == ba_node->coord[Z])) {
				continue;
			}
			/* check to see if I am going to a place I have
			   already been before */
			itr = list_iterator_create(results);
			while((next_node = list_next(itr))) {
				debug3("Algo(%d) looking at %c%c%c and %c%c%c",
				       algo,
				       alpha_num[next_node->coord[X]],
				       alpha_num[next_node->coord[Y]],
				       alpha_num[next_node->coord[Z]],
				       alpha_num[node_tar[X]],
				       alpha_num[node_tar[Y]],
				       alpha_num[node_tar[Z]]);
				if((node_tar[X] == next_node->coord[X] 
				    && node_tar[Y] == next_node->coord[Y]
				    && node_tar[Z] == next_node->coord[Z])) {
					not_first = 1;
					break;
				}				
			}
			list_iterator_destroy(itr);
			if(not_first && found < DIM_SIZE[X]) {
				debug2("Algo(%d) already been there before",
				       algo);
				not_first = 0;
				continue;
			} 
			not_first = 0;
				
		broke_it:
			next_node = &ba_system_ptr->grid[node_tar[X]]
#ifdef HAVE_3D
				[node_tar[Y]]
				[node_tar[Z]]
#endif
				;
			next_switch = &next_node->axis_switch[X];

 			if((conn_type == SELECT_MESH) && (found == (x_size))) {
				debug2("Algo(%d) we found the end of the mesh",
				       algo);
				return 1;
			}
			debug3("Algo(%d) Broke = %d Found = %d x_size = %d",
			       algo, broke, found, x_size);

			if(broke && (found == x_size)) {
				goto found_path;
			} else if(found == x_size) {
				debug2("Algo(%d) finishing the torus!", algo);

				if(deny_pass && (*deny_pass & PASS_DENY_X)) {
					info("we don't allow passthroughs 1");
					return 0;
				}

				if(best_path)
					list_flush(best_path);
				else
					best_path =
						list_create(_delete_path_list);

				if(path)
					list_flush(path);
				else
					path = list_create(_delete_path_list);
				
				_finish_torus(curr_switch, 0, X, 0, start);

				if(best_count < BEST_COUNT_INIT) {
					debug2("Algo(%d) Found a best path "
					       "with %d steps.",
					       algo, best_count);
					_set_best_path();
					return 1;
				} else {
					return 0;
				}
			} else if(broke) {
				broke = 0;
				continue;
			}

			if (!_node_used(next_node, x_size)) {
#ifdef HAVE_BG
				debug2("Algo(%d) found %d looking at %c%c%c "
				       "%d going to %c%c%c %d",
				       algo,
				       found,
				       alpha_num[ba_node->coord[X]],
				       alpha_num[ba_node->coord[Y]],
				       alpha_num[ba_node->coord[Z]],
				       ports_to_try[i],
				       alpha_num[node_tar[X]],
				       alpha_num[node_tar[Y]],
				       alpha_num[node_tar[Z]],
				       port_tar);
#endif
				itr = list_iterator_create(results);
				while((check_node = list_next(itr))) {
					if((node_tar[X] == check_node->coord[X]
					    && node_tar[Y] == 
					    check_node->coord[Y]
					    && node_tar[Z] == 
					    check_node->coord[Z])) {
						break;
					}
				}
				list_iterator_destroy(itr);
				if(!check_node) {
#ifdef HAVE_BG
					debug2("Algo(%d) add %c%c%c",
					       algo,
					       alpha_num[next_node->coord[X]],
					       alpha_num[next_node->coord[Y]],
					       alpha_num[next_node->coord[Z]]);
#endif					       
					list_append(results, next_node);
				} else {
#ifdef HAVE_BG
					debug2("Algo(%d) Hey this is already "
					       "added %c%c%c",
					       algo,
					       alpha_num[node_tar[X]],
					       alpha_num[node_tar[Y]],
					       alpha_num[node_tar[Z]]);
#endif
					continue;
				}
				found++;

				/* look for the next closest midplane */
				if(!_find_x_path(results, next_node, 
						 start, x_size, 
						 found, conn_type, algo)) {
					_remove_node(results, next_node->coord);
					found--;
					continue;
				} else {
				found_path:
#ifdef HAVE_BG
					debug2("Algo(%d) added node %c%c%c "
					       "%d %d -> %c%c%c %d %d",
					       algo,
					       alpha_num[ba_node->coord[X]],
					       alpha_num[ba_node->coord[Y]],
					       alpha_num[ba_node->coord[Z]],
					       source_port,
					       ports_to_try[i],
					       alpha_num[node_tar[X]],
					       alpha_num[node_tar[Y]],
					       alpha_num[node_tar[Z]],
					       port_tar,
					       target_port);
#endif					
					curr_switch->int_wire[source_port].used
						= 1;
					curr_switch->int_wire
						[source_port].port_tar
						= ports_to_try[i];
					curr_switch->int_wire
						[ports_to_try[i]].used = 1;
					curr_switch->int_wire
						[ports_to_try[i]].port_tar 
						= source_port;
					
					next_switch->int_wire[port_tar].used
						= 1;
					next_switch->int_wire[port_tar].port_tar
						= target_port;
					next_switch->int_wire[target_port].used
						= 1;
					next_switch->int_wire
						[target_port].port_tar
						= port_tar;
					return 1;
				}
			} 			
		}
	}

	if(algo == BLOCK_ALGO_FIRST) {
		debug2("Algo(%d) couldn't find path", algo);
		return 0;
	} else if(algo == BLOCK_ALGO_SECOND) {
#ifdef HAVE_BG
		debug2("Algo(%d) looking for the next free node "
		       "starting at %c%c%c",
		       algo,
		       alpha_num[ba_node->coord[X]],
		       alpha_num[ba_node->coord[Y]],
		       alpha_num[ba_node->coord[Z]]);
#endif
		
		if(best_path)
			list_flush(best_path);
		else
			best_path = list_create(_delete_path_list);
		
		if(path)
			list_flush(path);
		else
			path = list_create(_delete_path_list);
		
		_find_next_free_using_port_2(curr_switch, 0, results, X, 0);
		
		if(best_count < BEST_COUNT_INIT) {
			debug2("Algo(%d) yes found next free %d", algo,
			       best_count);
			node_tar = _set_best_path();

			if(deny_pass && (*deny_pass & PASS_DENY_X)
			   && (*deny_pass & PASS_FOUND_X)) {
				debug("We don't allow X passthoughs.");
				return 0;
			}

			next_node = &ba_system_ptr->grid[node_tar[X]]
#ifdef HAVE_3D
				[node_tar[Y]]
				[node_tar[Z]]
#endif
				;
			
			next_switch = &next_node->axis_switch[X];
			
#ifdef HAVE_BG
			debug2("Algo(%d) found %d looking at %c%c%c "
			       "going to %c%c%c %d",
			       algo, found,
			       alpha_num[ba_node->coord[X]],
			       alpha_num[ba_node->coord[Y]],
			       alpha_num[ba_node->coord[Z]],
			       alpha_num[node_tar[X]],
			       alpha_num[node_tar[Y]],
			       alpha_num[node_tar[Z]],
			       port_tar);
#endif		
			list_append(results, next_node);
			found++;
			if(_find_x_path(results, next_node, 
					start, x_size, found,
					conn_type, algo)) {
				return 1;
			} else {
				found--;
				_reset_the_path(curr_switch, 0, 1, X);
				_remove_node(results, next_node->coord);
				debug2("Algo(%d) couldn't finish "
				       "the path off this one", algo);
			}
		} 
		
		debug2("Algo(%d) couldn't find path", algo);
		return 0;
	}

	error("We got here meaning there is a bad algo, "
	      "but this should never happen algo(%d)", algo);
	return 0;
}

static int _remove_node(List results, int *node_tar)
{
	ListIterator itr;
	ba_node_t *ba_node = NULL;
	
	itr = list_iterator_create(results);
	while((ba_node = (ba_node_t*) list_next(itr))) {
		
#ifdef HAVE_BG
		if(node_tar[X] == ba_node->coord[X] 
		   && node_tar[Y] == ba_node->coord[Y] 
		   && node_tar[Z] == ba_node->coord[Z]) {
			debug2("removing %c%c%c from list",
			       alpha_num[node_tar[X]],
			       alpha_num[node_tar[Y]],
			       alpha_num[node_tar[Z]]);
			list_remove (itr);
			break;
		}
#else
		if(node_tar[X] == ba_node->coord[X]) {
			debug2("removing %d from list",
			       node_tar[X]);
			list_remove (itr);
			break;
		}
#endif
	}
	list_iterator_destroy(itr);
	return 1;
}

static int _find_next_free_using_port_2(ba_switch_t *curr_switch, 
					int source_port, 
					List nodes, 
					int dim, 
					int count) 
{
	ba_switch_t *next_switch = NULL; 
	ba_path_switch_t *path_add = 
		(ba_path_switch_t *) xmalloc(sizeof(ba_path_switch_t));
	ba_path_switch_t *path_switch = NULL;
	ba_path_switch_t *temp_switch = NULL;
	int port_tar;
	int target_port = 0;
	int port_to_try = 2;
	int *node_tar= curr_switch->ext_wire[0].node_tar;
	int *node_src = curr_switch->ext_wire[0].node_tar;
	int used = 0;
	int broke = 0;
	ba_node_t *ba_node = NULL;
	
	ListIterator itr;
	static bool found = false;

	path_add->geometry[X] = node_src[X];
#ifdef HAVE_3D
	path_add->geometry[Y] = node_src[Y];
	path_add->geometry[Z] = node_src[Z];
#endif
	path_add->dim = dim;
	path_add->in = source_port;

	if(count>=best_count)
		goto return_0;
	
	itr = list_iterator_create(nodes);
	while((ba_node = (ba_node_t*) list_next(itr))) {
		
		if(node_tar[X] == ba_node->coord[X] 
#ifdef HAVE_3D
		   && node_tar[Y] == ba_node->coord[Y] 
		   && node_tar[Z] == ba_node->coord[Z] 
#endif
			)
		{
			broke = 1;
			break;
		}
	}
	list_iterator_destroy(itr);
	
	if(!broke && count>0 &&
	   !ba_system_ptr->grid[node_tar[X]]
#ifdef HAVE_3D
	   [node_tar[Y]]
	   [node_tar[Z]]
#endif
	   .used) {
		
#ifdef HAVE_BG
		debug2("this one not found %c%c%c",
		       alpha_num[node_tar[X]],
		       alpha_num[node_tar[Y]],
		       alpha_num[node_tar[Z]]);
#endif		
		broke = 0;
				
		if((source_port%2))
			target_port=1;
		
		list_flush(best_path);
		
		found = true;
		path_add->out = target_port;
		list_push(path, path_add);
		
		itr = list_iterator_create(path);
		while((path_switch = (ba_path_switch_t*) list_next(itr))){
		
			temp_switch = (ba_path_switch_t *) 
				xmalloc(sizeof(ba_path_switch_t));
			 
			temp_switch->geometry[X] = path_switch->geometry[X];
#ifdef HAVE_BG
			temp_switch->geometry[Y] = path_switch->geometry[Y];
			temp_switch->geometry[Z] = path_switch->geometry[Z];
#endif
			temp_switch->dim = path_switch->dim;
			temp_switch->in = path_switch->in;
			temp_switch->out = path_switch->out;
			list_append(best_path, temp_switch);
		}
		list_iterator_destroy(itr);
		best_count = count;
		return 1;
	} 

	used=0;
	if(!curr_switch->int_wire[port_to_try].used) {
		itr = list_iterator_create(path);
		while((path_switch = 
		       (ba_path_switch_t*) list_next(itr))){
				
			if(((path_switch->geometry[X] == node_src[X]) 
#ifdef HAVE_BG
			    && (path_switch->geometry[Y] 
				== node_src[Y])
			    && (path_switch->geometry[Z] 
				== node_tar[Z])
#endif
				   )) {
					
				if( path_switch->out
				    == port_to_try) {
					used = 1;
					break;
				}
			}
		}
		list_iterator_destroy(itr);
			
		if(curr_switch->
		   ext_wire[port_to_try].node_tar[X]
		   == curr_switch->ext_wire[0].node_tar[X]  
#ifdef HAVE_3D
		   && curr_switch->
		   ext_wire[port_to_try].node_tar[Y] 
		   == curr_switch->ext_wire[0].node_tar[Y] 
		   && curr_switch->
		   ext_wire[port_to_try].node_tar[Z] 
		   == curr_switch->ext_wire[0].node_tar[Z]
#endif
			) {
			used = 1;
		}
						
		if(!used) {
			port_tar = curr_switch->
				ext_wire[port_to_try].port_tar;
			node_tar = curr_switch->
				ext_wire[port_to_try].node_tar;
				
			next_switch = &ba_system_ptr->
				grid[node_tar[X]]
#ifdef HAVE_3D
				[node_tar[Y]]
				[node_tar[Z]]
#endif
				.axis_switch[X];
				
			count++;
			path_add->out = port_to_try;
			list_push(path, path_add);
			_find_next_free_using_port_2(next_switch, 
					port_tar, nodes,
					dim, count);
			while((temp_switch = list_pop(path)) != path_add){
				xfree(temp_switch);
				debug3("something here 1");
			}
		}
	}
return_0:
	xfree(path_add);
	return 0;
}

/*
 * Used to tie the end of the block to the start. best_path and path
 * should both be set up before calling this function.
 *
 * IN: curr_switch -
 * IN: source_port - 
 * IN: dim -
 * IN: count -
 * IN: start -
 * 
 * RET: 0 on failure, 1 on success
 *
 * Sets up global variable best_path, and best_count.  On success
 * best_count will be >= BEST_COUNT_INIT you can call _set_best_path
 * to apply this path to the main system (ba_system_ptr)
 */

static int _finish_torus(ba_switch_t *curr_switch, int source_port,
			 int dim, int count, int *start)
{
	ba_switch_t *next_switch = NULL;
	ba_path_switch_t *path_add = xmalloc(sizeof(ba_path_switch_t));
	ba_path_switch_t *path_switch = NULL;
	ba_path_switch_t *temp_switch = NULL;
	int port_tar;
	int target_port=0;
	int ports_to_try[2] = {3,5};
	int *node_tar= curr_switch->ext_wire[0].node_tar;
	int *node_src = curr_switch->ext_wire[0].node_tar;
	int i;
	int used=0;
	ListIterator itr;
	static bool found = false;

	path_add->geometry[X] = node_src[X];
#ifdef HAVE_BG
	path_add->geometry[Y] = node_src[Y];
	path_add->geometry[Z] = node_src[Z];
#endif
	path_add->dim = dim;
	path_add->in = source_port;

	if(count>=best_count) {
		xfree(path_add);
		return 0;
	}
	if(node_tar[X] == start[X]
#ifdef HAVE_BG
	    && node_tar[Y] == start[Y]
	    && node_tar[Z] == start[Z]
#endif
		) {
		
		if((source_port%2))
			target_port=1;
		if(!curr_switch->int_wire[target_port].used) {
			
			list_flush(best_path);
			
			found = true;
			path_add->out = target_port;
			list_push(path, path_add);
			
			itr = list_iterator_create(path);
			while((path_switch = list_next(itr))) {
				
				temp_switch = xmalloc(sizeof(ba_path_switch_t));
				
				temp_switch->geometry[X] =
					path_switch->geometry[X];
#ifdef HAVE_BG
				temp_switch->geometry[Y] =
					path_switch->geometry[Y];
				temp_switch->geometry[Z] =
					path_switch->geometry[Z];
#endif
				temp_switch->dim = path_switch->dim;
				temp_switch->in = path_switch->in;
				temp_switch->out = path_switch->out;
				list_append(best_path,temp_switch);
			}
			list_iterator_destroy(itr);
			best_count = count;
			return 1;
		}
	}
	
	if(source_port==0 || source_port==3 || source_port==5) {
		ports_to_try[0] = 4;
		ports_to_try[1] = 2;
	}
	
	for(i=0;i<2;i++) {
		used=0;
		if(!curr_switch->int_wire[ports_to_try[i]].used) {
			itr = list_iterator_create(path);
			while((path_switch = list_next(itr))){
				
				if(((path_switch->geometry[X] == node_src[X])
#ifdef HAVE_BG
				    && (path_switch->geometry[Y]
					== node_src[Y])
				    && (path_switch->geometry[Z]
					== node_tar[Z])
#endif
					)) {
					if( path_switch->out
					    == ports_to_try[i]) {
						used = 1;
						break;
					}
				}
			}
			list_iterator_destroy(itr);
			if((curr_switch->
			    ext_wire[ports_to_try[i]].node_tar[X] ==
			    curr_switch->ext_wire[0].node_tar[X] &&
			    curr_switch->
			    ext_wire[ports_to_try[i]].node_tar[Y] ==
			    curr_switch->ext_wire[0].node_tar[Y] &&
			    curr_switch->
			    ext_wire[ports_to_try[i]].node_tar[Z] ==
			    curr_switch->ext_wire[0].node_tar[Z])) {
				continue;
			}
			if(!used) {
				port_tar = curr_switch->
					ext_wire[ports_to_try[i]].port_tar;
				node_tar = curr_switch->
					ext_wire[ports_to_try[i]].node_tar;
				
				next_switch = &ba_system_ptr->grid[node_tar[X]]
#ifdef HAVE_3D
					[node_tar[Y]]
					[node_tar[Z]]
#endif
					.axis_switch[dim];
			
				
				count++;
				path_add->out = ports_to_try[i];
				list_push(path, path_add);
				_finish_torus(next_switch, port_tar, 
					      dim, count, start);
				while((temp_switch = list_pop(path))
				      != path_add){
					xfree(temp_switch);
					debug3("something here 3");
				}
			}
		}
       }
       xfree(path_add);
       return 0;
}

/*
 * using best_path set up previously from _finish_torus or
 * _find_next_free_using_port_2.  Will set up the path contained there
 * into the main virtual system.  With will also set the passthrough
 * flag if there was a passthrough used.
 */
static int *_set_best_path()
{
	ListIterator itr;
	ba_path_switch_t *path_switch = NULL;
	ba_switch_t *curr_switch = NULL; 
	int *geo = NULL;

	if(!best_path)
		return NULL;

	itr = list_iterator_create(best_path);
	while((path_switch = (ba_path_switch_t*) list_next(itr))) {
		if(deny_pass && path_switch->in > 1 && path_switch->out > 1) {
			*deny_pass |= PASS_FOUND_X;
			debug2("got a passthrough in X");
		}
#ifdef HAVE_3D
		debug3("mapping %c%c%c %d->%d",
		       alpha_num[path_switch->geometry[X]],
		       alpha_num[path_switch->geometry[Y]],
		       alpha_num[path_switch->geometry[Z]],
		       path_switch->in, path_switch->out);
		if(!geo)
			geo = path_switch->geometry;
		curr_switch = &ba_system_ptr->grid
			[path_switch->geometry[X]]
			[path_switch->geometry[Y]]
			[path_switch->geometry[Z]].  
			axis_switch[path_switch->dim];
#else
		curr_switch = &ba_system_ptr->grid[path_switch->geometry[X]].
			axis_switch[path_switch->dim];
#endif
	
		curr_switch->int_wire[path_switch->in].used = 1;
		curr_switch->int_wire[path_switch->in].port_tar = 
			path_switch->out;
		curr_switch->int_wire[path_switch->out].used = 1;
		curr_switch->int_wire[path_switch->out].port_tar = 
			path_switch->in;
	}
	list_iterator_destroy(itr);

	best_count=BEST_COUNT_INIT;
	return geo;
}

static int _set_one_dim(int *start, int *end, int *coord)
{
	int dim;
	ba_switch_t *curr_switch = NULL; 
	
	for(dim=0;dim<BA_SYSTEM_DIMENSIONS;dim++) {
		if(start[dim]==end[dim]) {
			curr_switch = &ba_system_ptr->grid[coord[X]]
#ifdef HAVE_3D
				[coord[Y]]
				[coord[Z]]
#endif
				.axis_switch[dim];

			if(!curr_switch->int_wire[0].used 
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
	int *geo_ptr = (int *)object;
	xfree(geo_ptr);
}

//#define BUILD_EXE
#ifdef BUILD_EXE
/** */
int main(int argc, char** argv)
{
	ba_request_t *request = (ba_request_t*) xmalloc(sizeof(ba_request_t)); 
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	int debug_level = 6;
	node_info_msg_t *new_node_ptr = NULL;

	List results;
//	List results2;
//	int i,j;
	log_opts.stderr_level  = debug_level;
	log_opts.logfile_level = debug_level;
	log_opts.syslog_level  = debug_level;
	
	log_alter(log_opts, LOG_DAEMON, 
		  "/dev/null");
	
	DIM_SIZE[X]=0;
	DIM_SIZE[Y]=0;
	DIM_SIZE[Z]=0;
	while (slurm_load_node((time_t) NULL, &new_node_ptr, SHOW_ALL)) { 
		
		sleep(10);	/* keep trying to reconnect */
	}
	
	ba_init(new_node_ptr);
	init_wires(NULL);
						
	results = list_create(NULL);
	request->geometry[0] = 1;
	request->geometry[1] = 1;
	request->geometry[2] = 1;
	request->start[0] = 6;
	request->start[1] = 3;
	request->start[2] = 2;
	request->start_req = 1;
//	request->size = 1;
	request->rotate = 0;
	request->elongate = 0;
	request->conn_type = SELECT_TORUS;
	new_ba_request(request);
	print_ba_request(request);
	if(!allocate_block(request, results)) {
       		debug("couldn't allocate %c%c%c",
		       request->geometry[0],
		       request->geometry[1],
		       request->geometry[2]);
	}
	list_destroy(results);

	results = list_create(NULL);
	request->geometry[0] = 2;
	request->geometry[1] = 4;
	request->geometry[2] = 1;
	request->start[0] = 3;
	request->start[1] = 0;
	request->start[2] = 2;
	request->start_req = 1;
//	request->size = 16;
	request->rotate = 0;
	request->elongate = 0;
	request->conn_type = SELECT_TORUS;
	new_ba_request(request);
	print_ba_request(request);
	if(!allocate_block(request, results)) {
       		debug("couldn't allocate %c%c%c",
		       alpha_num[request->geometry[0]],
		       alpha_num[request->geometry[1]],
		       alpha_num[request->geometry[2]]);
	}
	list_destroy(results);

	results = list_create(NULL);
	request->geometry[0] = 2;
	request->geometry[1] = 1;
	request->geometry[2] = 4;
	request->start[0] = 5;
	request->start[1] = 2;
	request->start[2] = 0;
	request->start_req = 1;
	request->rotate = 0;
	request->elongate = 0;
	request->conn_type = SELECT_TORUS;
	new_ba_request(request);
	print_ba_request(request);
	if(!allocate_block(request, results)) {
       		debug("couldn't allocate %c%c%c",
		       alpha_num[request->geometry[0]],
		       alpha_num[request->geometry[1]],
		       alpha_num[request->geometry[2]]);
	}
	list_destroy(results);
	
/* 	results = list_create(NULL); */
/* 	request->geometry[0] = 4; */
/* 	request->geometry[1] = 4; */
/* 	request->geometry[2] = 4; */
/* 	//request->size = 2; */
/* 	request->conn_type = SELECT_TORUS; */
/* 	new_ba_request(request); */
/* 	print_ba_request(request); */
/* 	if(!allocate_block(request, results)) { */
/*        		printf("couldn't allocate %c%c%c\n", */
/* 		       request->geometry[0], */
/* 		       request->geometry[1], */
/* 		       request->geometry[2]); */
/* 	} */

/* 	results = list_create(NULL); */
/* 	request->geometry[0] = 1; */
/* 	request->geometry[1] = 4; */
/* 	request->geometry[2] = 4; */
/* 	//request->size = 2; */
/* 	request->conn_type = SELECT_TORUS; */
/* 	new_ba_request(request); */
/* 	print_ba_request(request); */
/* 	if(!allocate_block(request, results)) { */
/*        		printf("couldn't allocate %c%c%c\n", */
/* 		       request->geometry[0], */
/* 		       request->geometry[1], */
/* 		       request->geometry[2]); */
/* 	} */
	
	int dim,j;
	int x,y,z;
	int startx=0;
	int starty=0;
	int startz=0;
	int endx=DIM_SIZE[X];
	int endy=1;//DIM_SIZE[Y];
	int endz=1;//DIM_SIZE[Z];

	for(x=startx;x<endx;x++) {
		for(y=starty;y<endy;y++) {
			for(z=startz;z<endz;z++) {
				ba_node_t *curr_node = 
					&(ba_system_ptr->grid[x][y][z]);
				info("Node %c%c%c Used = %d Letter = %c",
				     alpha_num[x],alpha_num[y],alpha_num[z],
				     curr_node->used,
				     curr_node->letter);
				for(dim=0;dim<1;dim++) {
					info("Dim %d",dim);
					ba_switch_t *wire =
						&curr_node->axis_switch[dim];
					for(j=0;j<NUM_PORTS_PER_NODE;j++)
						info("\t%d -> %d -> %c%c%c %d "
						     "Used = %d",
						     j, wire->int_wire[j].
						     port_tar,
						     alpha_num[wire->ext_wire[
							     wire->int_wire[j].
							     port_tar].
							       node_tar[X]],
						     alpha_num[wire->ext_wire[
							     wire->int_wire[j].
							     port_tar].
						     node_tar[Y]],
						     alpha_num[wire->ext_wire[
							     wire->int_wire[j].
							     port_tar].
						     node_tar[Z]],
						     wire->ext_wire[
							     wire->int_wire[j].
							     port_tar].
						     port_tar,
						     wire->int_wire[j].used);
				}
			}
		}
	}
	/* list_destroy(results); */

/* 	ba_fini(); */

/* 	delete_ba_request(request); */
	
	return 0;
}


#endif
