/*****************************************************************************
 *  parse_config.h - definitions parsing any configuration file
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

typedef struct s_c_values s_c_values_t;
typedef s_c_values_t * s_c_hashtbl_t;

typedef enum slurm_conf_enum {
	S_C_STRING,
	S_C_LONG,
	S_C_POINTER,
	S_C_ARRAY
} slurm_conf_enum_t;

typedef struct conf_file_options {
	char *key;
	slurm_conf_enum_t type;
	int (*handler)(void **, slurm_conf_enum_t,
		       const char *, const char *, const char *);
	void (*destroy)(void *);
} s_c_options_t;


s_c_hashtbl_t *s_c_hashtbl_create(struct conf_file_options options[]);
void s_c_hashtbl_destroy(s_c_hashtbl_t *hashtbl);

void s_c_parse_file(s_c_hashtbl_t *hashtbl, char *filename);
void s_c_parse_line(s_c_hashtbl_t *hashtbl, const char *line);

int s_c_get_string(const s_c_hashtbl_t *hashtbl, const char *key, char **str);
int s_c_get_long(const s_c_hashtbl_t *hashtbl, const char *key, long *num);
int s_c_get_pointer(const s_c_hashtbl_t *hashtbl, const char *key, void **ptr);
int s_c_get_array(const s_c_hashtbl_t *hashtbl, const char *key,
		  void **ptr_array[], int *count);

/*
 * Given an "options" array, print the current values of all
 * options in supplied hash table "hashtbl".
 *
 * Primarily for debugging purposes.
 */
void s_c_dump_values(const s_c_hashtbl_t *hashtbl,
		     const struct conf_file_options options[]);


#endif /* !_PARSE_CONFIG_H */
