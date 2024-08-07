/*****************************************************************************\
 *  parsers.h - Slurm data parsing handlers
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include "api.h"
#include "src/interfaces/data_parser.h"
#include "src/slurmrestd/openapi.h"

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
	FLAG_BIT_TYPE_REMOVED, /* flag removed but needs to still parse correct */
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
	const char *description;
	bool hidden; /* hide from OpenAPI spec generation */
	uint16_t deprecated; /* protocol version when deprecated */
} flag_bit_t;

typedef enum {
	PARSER_MODEL_INVALID = 0, /* aka not initialized */
	PARSER_MODEL_ARRAY, /* parser array to parse every field in a struct */
	PARSER_MODEL_ARRAY_LINKED_FIELD, /* link to parser in a parser array */
	PARSER_MODEL_ARRAY_LINKED_EXPLODED_FLAG_ARRAY_FIELD, /* link to parser in a parser array of exploded flag array */
	PARSER_MODEL_ARRAY_SKIP_FIELD, /* parser to mark field as not being parsed in a parser array */
	PARSER_MODEL_ARRAY_REMOVED_FIELD, /* parser to mark field as placeholder for field already removed from struct */

	PARSER_MODEL_SIMPLE, /* parser for single field in struct */
	PARSER_MODEL_COMPLEX, /* parser for uses multiple fields in struct */
	PARSER_MODEL_FLAG_ARRAY, /* parser for list of flags */
	PARSER_MODEL_LIST, /* parser for list_t's */
	PARSER_MODEL_PTR, /* parser for pointer */
	PARSER_MODEL_NT_PTR_ARRAY, /* parser for NULL terminated array of pointers */
	/* NT_ARRAY objects must not require an special initializer */
	PARSER_MODEL_NT_ARRAY, /* parser for NULL terminated array of objects */
	PARSER_MODEL_REMOVED, /* parser for removed types */

	/*
	 * Alias for another parser.
	 *
	 * Only for use in maintaining the same OAS name when a new parser name
	 * is needed in newer plugins.
	 */
	PARSER_MODEL_ALIAS,

	PARSER_MODEL_MAX /* place holder */
} parser_model_t;

#define MAGIC_PARSER 0xa3bafa05

typedef void *(*parser_new_func_t)(void);
/* must be compatible with ListDelF */
typedef void (*parser_free_func_t)(void *ptr);

typedef struct parser_s {
	int magic; /* MAGIC_PARSER */
	parser_model_t model;

	/* common model properties ------------------------------------------ */
	type_t type;
	char *type_string; /* stringified DATA_PARSE enum */
	const char *obj_desc; /* description of object */
	char *obj_type_string; /* stringified C type */
	openapi_type_format_t obj_openapi; /* OpenAPI format for object */
	ssize_t size; /* size of target obj */
	parser_new_func_t new; /* function to create new instance of obj being pointed at */
	parser_free_func_t free; /* function to release instance of obj being pointed at */
	uint16_t deprecated; /* protocol version when deprecated */

	/* Linked model properties ------------------------------------------ */
	char *field_name; /* name of field in struct if there is a ptr_offset */
	uint8_t field_name_overloads; /* number of other parsers using same field name */
	char *key; /* path of field key in dictionary */
	ssize_t ptr_offset; /* offset from parent object or NO_VAL */
	bool required;

	/* Alias model properties ----------------------------------------- */
	type_t alias_type;

	/* Pointer model properties ----------------------------------------- */
	type_t pointer_type;
	bool allow_null_pointer; /* leave destination as null type when source pointer is NULL while dumping */

	/* NULL terminated array of pointers model properties --------------- */
	type_t array_type;

	/* Flag array model properties -------------------------------------- */
	const flag_bit_t *flag_bit_array;
	uint8_t flag_bit_array_count; /* number of entries in flag_bit_array */
	bool single_flag; /* false to be list or true to be string for 1 flag */

	/* List model properties -------------------------------------------- */
	type_t list_type;

	/* Array model properties ------------------------------------------- */
	const parser_t *const fields; /* pointer to array of parsers for each field */
	const size_t field_count; /* number of fields in fields array */

	/* Simple and Complex model properties ------------------------------ */
	int (*dump)(const parser_t *const parser, void *src, data_t *dst,
		    args_t *args);
	int (*parse)(const parser_t *const parser, void *dst, data_t *src,
		     args_t *args, data_t *parent_path);
	need_t needs;
} parser_t;

/*
 * Called at startup to run any setup of parsers and testing
 */
extern void parsers_init(void);

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

/*
 * Resolve aliased or pointer model parsers to final unaliased parser
 */
extern const parser_t *unalias_parser(const parser_t *parser);

extern void get_parsers(const parser_t **parsers_ptr, int *count_ptr);

#endif
