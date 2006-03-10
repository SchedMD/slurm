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
	S_P_IGNORE = 0,
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

/*
 * Returns 1 if the line is parsed cleanly, and 0 otherwise.
 */
int s_p_parse_line(s_p_hashtbl_t *hashtbl, const char *line);

/*
 * s_p_get_string
 *
 * Search for a key in a s_p_hashtbl_t with value of type
 * string.  If the key is found and has a set value, the
 * value is retuned in "str".
 *
 * OUT str - pointer to a copy of the string value
 *           (caller is resonsible for freeing str with xfree())
 * IN key - hash table key.
 * IN hashtbl - hash table created by s_p_hashtbl_create()
 *
 * Returns 1 when a value was set for "key" during parsing and "str"
 *   was successfully set, otherwise returns 0;
 *
 * NOTE: Caller is responsible for freeing the returned string with xfree!
 */
int s_p_get_string(char **str, const char *key, const s_p_hashtbl_t *hashtbl);

/*
 * s_p_get_long
 *
 * Search for a key in a s_p_hashtbl_t with value of type
 * long.  If the key is found and has a set value, the
 * value is retuned in "num".
 *
 * OUT num - pointer to a long where the value is returned
 * IN key - hash table key
 * IN hashtbl - hash table created by s_p_hashtbl_create()
 *
 * Returns 1 when a value was set for "key" during parsing and "num"
 *   was successfully set, otherwise returns 0;
 */
int s_p_get_long(long *num, const char *key, const s_p_hashtbl_t *hashtbl);

/*
 * s_p_get_uint16
 *
 * Search for a key in a s_p_hashtbl_t with value of type
 * uint16.  If the key is found and has a set value, the
 * value is retuned in "num".
 *
 * OUT num - pointer to a uint16_t where the value is returned
 * IN key - hash table key
 * IN hashtbl - hash table created by s_p_hashtbl_create()
 *
 * Returns 1 when a value was set for "key" during parsing and "num"
 *   was successfully set, otherwise returns 0;
 */
int s_p_get_uint16(uint16_t *num, const char *key,
		   const s_p_hashtbl_t *hashtbl);

/*
 * s_p_get_uint32
 *
 * Search for a key in a s_p_hashtbl_t with value of type
 * uint32.  If the key is found and has a set value, the
 * value is retuned in "num".
 *
 * OUT num - pointer to a uint32_t where the value is returned
 * IN key - hash table key
 * IN hashtbl - hash table created by s_p_hashtbl_create()
 *
 * Returns 1 when a value was set for "key" during parsing and "num"
 *   was successfully set, otherwise returns 0;
 */
int s_p_get_uint32(uint32_t *num, const char *key,
		   const s_p_hashtbl_t *hashtbl);

/*
 * s_p_get_pointer
 *
 * Search for a key in a s_p_hashtbl_t with value of type
 * pointer.  If the key is found and has a set value, the
 * value is retuned in "ptr".
 *
 * OUT num - pointer to a void pointer where the value is returned
 * IN key - hash table key
 * IN hashtbl - hash table created by s_p_hashtbl_create()
 *
 * Returns 1 when a value was set for "key" during parsing and "ptr"
 *   was successfully set, otherwise returns 0;
 */
int s_p_get_pointer(void **ptr, const char *key, const s_p_hashtbl_t *hashtbl);

/*
 * s_p_get_array
 *
 * Most s_p_ data types allow a key to appear only once in a file
 * (s_p_parse_file) or line (s_p_parse_line).  S_P_ARRAY is the exception.
 *
 * S_P_ARRAY allows a key to appear any number of times.  Each time
 * a particular key is found the value array grows by one element, and
 * that element contains a pointer to the newly parsed value.  You can
 * think of this as being an array of S_P_POINTER types.
 *
 * OUT num - pointer to a void pointer-pointer where the value is returned
 * IN key - hash table key
 * IN hashtbl - hash table created by s_p_hashtbl_create()
 *
 * Returns 1 when a value was set for "key" during parsing and "ptr"
 *   was successfully set, otherwise returns 0;
 */
int s_p_get_array(void **ptr_array[], int *count, 
		  const char *key, const s_p_hashtbl_t *hashtbl);

/*
 * s_p_get_boolean
 *
 * Search for a key in a s_p_hashtbl_t with value of type
 * boolean.  If the key is found and has a set value, the
 * value is retuned in "flag".
 *
 * OUT flag - pointer to a bool where the value is returned
 * IN key - hash table key
 * IN hashtbl - hash table created by s_p_hashtbl_create()
 *
 * Returns 1 when a value was set for "key" during parsing and "num"
 *   was successfully set, otherwise returns 0;
 */
int s_p_get_boolean(bool *flag, const char *key, const s_p_hashtbl_t *hashtbl);

/*
 * Given an "options" array, print the current values of all
 * options in supplied hash table "hashtbl".
 *
 * Primarily for debugging purposes.
 */
void s_p_dump_values(const s_p_hashtbl_t *hashtbl,
		     const s_p_options_t options[]);


#endif /* !_PARSE_CONFIG_H */
