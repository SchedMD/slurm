/*****************************************************************************
 *  parse_config.h - parse any slurm.conf-like configuration file
 *
 *  NOTE: when you see the prefix "s_p_", think "slurm parser".
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

#ifndef _PARSE_CONFIG_H
#define _PARSE_CONFIG_H

#include <stdint.h>

typedef struct s_p_values s_p_values_t;
typedef s_p_values_t * s_p_hashtbl_t;

typedef enum slurm_parser_enum {
	S_P_STRING,
	S_P_LONG,
	S_P_UINT16,
	S_P_UINT32,
	S_P_POINTER,
	S_P_ARRAY,
	S_P_BOOLEAN
} slurm_parser_enum_t;

typedef struct conf_file_options {
	char *key;
	slurm_parser_enum_t type;
	int (*handler)(void **, slurm_parser_enum_t,
		       const char *, const char *, const char *);
	void (*destroy)(void *);
} s_p_options_t;


s_p_hashtbl_t *s_p_hashtbl_create(struct conf_file_options options[]);
void s_p_hashtbl_destroy(s_p_hashtbl_t *hashtbl);

void s_p_parse_file(s_p_hashtbl_t *hashtbl, char *filename);
void s_p_parse_line(s_p_hashtbl_t *hashtbl, const char *line);

/*
 * s_p_get_string
 *
 * Caller is responsible for freeing returned string pointer with xfree().
 */
int s_p_get_string(const s_p_hashtbl_t *hashtbl, const char *key, char **str);

int s_p_get_long(const s_p_hashtbl_t *hashtbl, const char *key, long *num);
int s_p_get_uint16(const s_p_hashtbl_t *hashtbl, const char *key,
		   uint16_t *num);
int s_p_get_uint32(const s_p_hashtbl_t *hashtbl, const char *key,
		   uint32_t *num);
int s_p_get_pointer(const s_p_hashtbl_t *hashtbl, const char *key, void **ptr);
int s_p_get_array(const s_p_hashtbl_t *hashtbl, const char *key,
		  void **ptr_array[], int *count);
int s_p_get_boolean(const s_p_hashtbl_t *hashtbl, const char *key,
		    bool *num);

/*
 * Given an "options" array, print the current values of all
 * options in supplied hash table "hashtbl".
 *
 * Primarily for debugging purposes.
 */
void s_p_dump_values(const s_p_hashtbl_t *hashtbl,
		     const s_p_options_t options[]);


#endif /* !_PARSE_CONFIG_H */
