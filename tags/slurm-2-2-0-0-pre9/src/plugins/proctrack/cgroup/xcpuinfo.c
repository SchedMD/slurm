/*****************************************************************************\
 *  xcpuinfo.c - cpuinfo related primitives
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
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
#   include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>
#include "src/common/log.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmd/slurmd/get_mach_stat.h"

#include "xcpuinfo.h"

bool     initialized = false;      
uint16_t procs, sockets, cores, threads;
uint16_t block_map_size;
uint16_t *block_map, *block_map_inv;

int _ranges_conv(char* lrange,char** prange,int mode);

/* for testing purpose */
/* uint16_t block_map_size=8; */
/* uint16_t block_map[] = { 0, 4, 1, 5, 3, 7, 2, 6}; */
/* uint16_t block_map_inv[] = { 0, 2, 6, 4, 1, 3, 7, 5}; */
/* xcpuinfo_abs_to_mac("0,2,4,6",&mach); */
/* xcpuinfo_mac_to_abs(mach,&abs); */

int
xcpuinfo_init()
{
	if ( initialized )
		return XCPUINFO_SUCCESS;

	if ( get_procs(&procs) )
		return XCPUINFO_ERROR;
	
	if ( get_cpuinfo(procs,&sockets,&cores,&threads,
			 &block_map_size,&block_map,&block_map_inv) )
		return XCPUINFO_ERROR;

	initialized = true ;

	return XCPUINFO_SUCCESS;
}

int
xcpuinfo_fini()
{
	if ( ! initialized )
		return XCPUINFO_SUCCESS;

	initialized = false ;
	procs = sockets = cores = threads = 0;
	block_map_size = 0;
	xfree(block_map);
	xfree(block_map_inv);

	return XCPUINFO_SUCCESS;
}

int
xcpuinfo_abs_to_mac(char* lrange,char** prange)
{
	return _ranges_conv(lrange,prange,0);
}

int
xcpuinfo_mac_to_abs(char* lrange,char** prange)
{
	return _ranges_conv(lrange,prange,1);
}


/* 
 * set to 1 each element of already allocated map of size 
 * map_size if they are present in the input range
 */
int
_range_to_map(char* range,uint16_t *map,uint16_t map_size)
{
	int bad_nb=0;
	int num_fl=0;
	int con_fl=0;
	int last=0;

	char *dup;
	char *p;
	char *s=NULL;

	uint16_t start=0,end=0,i;

	/* duplicate input range */
	dup = xstrdup(range);
	p = dup;
	while ( ! last ) {
		if ( isdigit(*p) ) {
			if ( !num_fl ) {
				num_fl++;
				s=p;
			}
		}
		else if ( *p == '-' ) {
			if ( s && num_fl ) {
				*p = '\0';
				start = (uint16_t) atoi(s);
				con_fl=1;
				num_fl=0;
				s=NULL;
			}
		}
		else if ( *p == ',' || *p == '\0') {
			if ( *p == '\0' )
				last = 1;
			if ( s && num_fl ) {
				*p = '\0';
				end = (uint16_t) atoi(s);
				if ( !con_fl )
					start = end ;
				con_fl=2;
				num_fl=0;
				s=NULL;
			}
		}
		else {
			bad_nb++;
			break;
		}
		if ( con_fl == 2 ) {
			for( i = start ; i <= end && i < map_size ; i++) {
				map[i]=1;
			}
			con_fl=0;
		}
		p++;
	}

	xfree(dup);

	if ( bad_nb > 0 ) {
		/* bad format for input range */
		return XCPUINFO_ERROR;
	}

	return XCPUINFO_SUCCESS;
}


/*
 * allocate and build a range of ids using an input map
 * having printable element set to 1
 */
int
_map_to_range(uint16_t *map,uint16_t map_size,char** prange)
{
	size_t len;
	int num_fl=0;
	int con_fl=0;

	char id[12];
	char *str;

	uint16_t start=0,end=0,i;

	str = xstrdup("");
	for ( i = 0 ; i < map_size ; i++ ) {

		if ( map[i] ) {
			num_fl=1;
			end=i;
			if ( !con_fl ) {
				start=end;
				con_fl=1;
			}
		}
		else if ( num_fl ) {
			if ( start < end ) {
				sprintf(id,"%u-%u,",start,end);
				xstrcat(str,id);
			}
			else {
				sprintf(id,"%u,",start);
				xstrcat(str,id);
			}
			con_fl = num_fl = 0;
		}
	}
	if ( num_fl ) {
		if ( start < end ) {
			sprintf(id,"%u-%u,",start,end);
			xstrcat(str,id);
		}
		else {
			sprintf(id,"%u,",start);
			xstrcat(str,id);
		}
	}

	len = strlen(str);
	if ( len > 0 ) {
		str[len-1]='\0';
	}

	if ( prange != NULL )
		*prange = str;
	else
		xfree(str);

	return XCPUINFO_SUCCESS;
}

/*
 * convert a range into an other one according to 
 * a modus operandi being 0 or 1 for abstract to machine
 * or machine to abstract representation of cores
 */
int
_ranges_conv(char* lrange,char** prange,int mode)
{
	int fstatus;
	int i;
	uint16_t *amap;
	uint16_t *map;
	uint16_t *map_out;

	/* init internal data if not already done */
	if ( xcpuinfo_init() != XCPUINFO_SUCCESS )
		return XCPUINFO_ERROR;

	if ( mode ) {
		/* machine to abstract conversion */
		amap = block_map_inv;
	}
	else {
		/* abstract to machine conversion */
		amap = block_map;
	}

	/* allocate map for local work */
	map = (uint16_t*) xmalloc(block_map_size*sizeof(uint16_t));
	map_out = (uint16_t*) xmalloc(block_map_size*sizeof(uint16_t));

	/* extract the input map */
	fstatus = _range_to_map(lrange,map,block_map_size);
	if ( fstatus ) {
		goto exit;
	}

	/* do the conversion (see src/slurmd/slurmd/get_mach_stat.c) */
	for( i = 0 ; i < block_map_size ; i++) {
		if ( map[i] )
			map_out[amap[i]]=1;
	}

	/* build the ouput range */
	fstatus = _map_to_range(map_out,block_map_size,prange);

exit:
	xfree(map);
	xfree(map_out);
	return fstatus;
}
