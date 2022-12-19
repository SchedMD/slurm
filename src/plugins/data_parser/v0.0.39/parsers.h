/*****************************************************************************\
 *  parsers.h - Slurm data parsing handlers
 *****************************************************************************
 *  Copyright (C) 2022 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef DATA_PARSER_PARSERS
#define DATA_PARSER_PARSERS

#include "src/interfaces/data_parser.h"
#include "api.h"

typedef data_parser_type_t type_t;
typedef struct parser_s parser_t;

typedef enum {
	NEED_NONE = 0, /* parser has no pre-reqs for data */
	NEED_AUTH = SLURM_BIT(0),
	NEED_TRES = SLURM_BIT(1),
	NEED_QOS = SLURM_BIT(2),
	NEED_ASSOC = SLURM_BIT(3),
} need_t;

typedef enum {
	FLAG_BIT_TYPE_INVALID = 0, /* aka not initialized */
	FLAG_BIT_TYPE_EQUAL, /* entire masked value must match for flag */
	FLAG_BIT_TYPE_BIT, /* only need bit(s) to match */
	FLAG_BIT_TYPE_MAX /* place holder */
} flag_bit_type_t;

#define MAGIC_FLAG_BIT 0xa11a3a05

typedef struct {
	int magic; /* MAGIC_FLAG_BIT */
	char *name;
	flag_bit_type_t type;
	uint64_t mask; /* avoid changing any bits not in mask */
	size_t mask_size;
	char *mask_name;
	uint64_t value; /* bits set by flag */
	char *flag_name;
	size_t flag_size;
} flag_bit_t;

typedef enum {
	FLAG_TYPE_INVALID = 0, /* aka not a flag */
	FLAG_TYPE_NONE, /* not applicable aka not a flag */
	FLAG_TYPE_BIT_ARRAY, /* array of bit flags */
	FLAG_TYPE_BOOL, /* set a bool using offset */
	FLAG_TYPE_MAX /* place holder */
} flag_type_t;

#define MAGIC_PARSER 0xa3bafa05

typedef struct parser_s {
	int magic; /* MAGIC_PARSER */
	type_t type;
	/* field is not to be parser and dumped */
	bool skip;
	bool required;
	/* offset from parent object - for fields in structs */
	ssize_t ptr_offset; /* set to NO_VAL if there is no offset */
	char *field_name; /* name of field in struct if there is a ptr_offset */
	/* path of field key in dictionary */
	char *key; /* set to NULL if this is simple object */
	need_t needs;
	ssize_t size; /* size of target obj */
	char *obj_type_string; /* stringified C type */
	char *type_string; /* stringified DATA_PARSE enum */

	/* flag specific properties */
	flag_type_t flag;
	char *flag_name;
	const flag_bit_t *flag_bit_array;
	uint8_t flag_bit_array_count; /* number of entries in flag_bit_array */

	/* set if is a List of given type */
	type_t list_type;
	ListDelF list_del_func;
	/*
	 * function to create object to add to List for this parser
	 * IN parser - current list parser
	 * IN size - ptr to size. set with the size of the new object
	 * RET ptr to obj
	 */
	void *(*list_new_func)(const parser_t *const parser, ssize_t *size);

	/* parser is for a struct and has child fields to parse */
	const parser_t *const fields;
	const size_t field_count; /* number of fields in fields array */

	/* parser has functions to handle parsing and dumping */
	int (*parse)(const parser_t *const parser, void *dst, data_t *src,
		     args_t *args, data_t *parent_path);
	int (*dump)(const parser_t *const parser, void *src, data_t *dst,
		    args_t *args);
} parser_t;

/*
 * Called at startup to run any setup of parsers and testing
 */
extern void parsers_init();

#ifndef NDEBUG
extern void check_parser_funcname(const parser_t *const parser,
				  const char *func_name);
#define check_parser(parser) check_parser_funcname(parser, __func__)

/*
 * Verify that the parser is sliced parser from an array.
 *
 * Allow enforcement that certain parsers should only ever be in an parser array
 * and never as a directly reference-able parser (such as flags)
 */
extern void verify_parser_sliced_funcname(const parser_t *const parser,
					  const char *func, const char *file,
					  int line);
#define verify_parser_sliced(parser) \
	verify_parser_sliced_funcname(parser, __func__, __FILE__, __LINE__)

/*
 * Verify that the parser is not a sliced parser from an array. Parsers inside
 * of arrays are only meant to act as link and definition of where the offset is
 * to the field. Directly referencing them is always an bug. Use
 * find_parser_by_type() to find the correct parser for that type instead.
 */
extern void verify_parser_not_sliced_funcname(const parser_t *const parser,
					      const char *func,
					      const char *file, int line);
#define verify_parser_not_sliced(parser) \
	verify_parser_not_sliced_funcname(parser, __func__, __FILE__, __LINE__)
#else
#define check_parser(parser) {}
#define verify_parser_not_sliced(parser) {}
#define verify_parser_sliced(parser) {}
#endif

extern const parser_t *const find_parser_by_type(type_t type);

#endif
