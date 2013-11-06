/*****************************************************************************\
 *  bg_read_config.h
 *
 *****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#ifndef _BG_READ_CONFIG_H_
#define _BG_READ_CONFIG_H_

#include "src/common/list.h"
#include "src/common/xmalloc.h"
#include "src/common/read_config.h"
#include "src/common/parse_spec.h"

/* structure filled in from reading bluegene.conf file for specifying
 * images */
typedef struct {
	bool def;                      /* Whether image is the default
					  image or not */
	List groups;                   /* list of groups able to use
					* the image contains
					* image_group_t's */
	char *name;                    /* Name of image */
} image_t;

typedef struct {
	char *name;
	gid_t gid;
} image_group_t;

extern void destroy_image_group_list(void *ptr);
extern void destroy_image(void *ptr);

/* Parse a block request from the bluegene.conf file */
extern int parse_blockreq(void **dest, slurm_parser_enum_t type,
			  const char *key, const char *value,
			  const char *line, char **leftover);

/* Parse imagine information from blugene.conf file */
extern int parse_image(void **dest, slurm_parser_enum_t type,
		       const char *key, const char *value,
		       const char *line, char **leftover);

extern int read_bg_conf(void);
extern s_p_hashtbl_t *config_make_tbl(char *filename);

#endif
