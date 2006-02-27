/*****************************************************************************\
 *  slurm_config.c - slurm.conf reader
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>.
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif


/* #include "src/common/slurm_protocol_defs.h" */
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xassert.h"
/* #include "src/common/slurm_rlimits_info.h" */
#include "src/common/slurm_config.h"

#include <slurm/slurm.h>

int parse_nodename(void **dest, slurm_parser_enum_t type,
		   const char *key, const char *value, const char *line)
{
	s_p_hashtbl_t *hashtbl;

	hashtbl = s_p_hashtbl_create(slurm_nodename_options);
	s_p_parse_line(hashtbl, line);
	s_p_dump_values(hashtbl, slurm_nodename_options);

	*dest = (void *)hashtbl;

	return 0;
}

void destroy_nodename(void *ptr)
{
	s_p_hashtbl_destroy((s_p_hashtbl_t *)ptr);
}

int parse_partitionname(void **dest, slurm_parser_enum_t type,
		   const char *key, const char *value, const char *line)
{
	s_p_hashtbl_t *hashtbl;

	hashtbl = s_p_hashtbl_create(slurm_partition_options);
	s_p_parse_line(hashtbl, line);
	s_p_dump_values(hashtbl, slurm_partition_options);

	*dest = (void *)hashtbl;

	return 0;
}

void destroy_partitionname(void *ptr)
{
	s_p_hashtbl_destroy((s_p_hashtbl_t *)ptr);
}

void read_slurm_conf_init(void) {
	s_p_hashtbl_t *hashtbl;

	hashtbl = s_p_hashtbl_create(slurm_conf_options);
	s_p_parse_file(hashtbl, "/home/morrone/slurm.conf");
	s_p_dump_values(hashtbl, slurm_conf_options);
	s_p_hashtbl_destroy(hashtbl);
}

