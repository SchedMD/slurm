/*****************************************************************************\
 *  data.h - generic data_t structures
 *****************************************************************************
 *  Copyright (C) 2019 SchedMD LLC.
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

/*
 * The data_t struct exists to provide a generic and type safe way to work with
 * complex data types in a tree. All data_t pointers and helpers are re-entrant
 * but not thread safe.
 *
 * data_t ptr always has a root instance created by data_new() and cleaned up by
 * data_free(). Do not use data_free() on a child data_t ptr as it will
 * eventually cause a double free error. All the helper functions exist to
 * manipulate the data_t struct. Never directly edit the contents of the data_t
 * struct without one of the helpers. Most helper calls will return a data_t
 * pointer which is a child of the existing tree and will be cleaned up by the
 * root.
 *
 * To use data_t, data_init() must be called before anything else. It is safe to
 * call this multiple times but not advised before every usage as it may be
 * slow. data_init() is designed to allow calls for a specific plugin
 * requirement which will not prevent loading of other plugins by the other
 * calls to data_init(). data_fini() needs to be called after all data
 * operations are complete is only for testing for memory leaks.
 *
 * data_t has very *strict* typing that is based on JSON. All of the possible
 * types are in data_type_t. The caller is required to verify the type of the
 * data_t pointer is correct before calling the helper function to retrieve the
 * contents. If the source data is provided by a user (or is just unknown), then
 * one of the many data_*convert*() functions must be used to ensure that the
 * data_t pointer is of the correct type. These convert functions will generally
 * allow conversion betwen all of the types except DICT and LIST (as converting
 * between them is not well defined).
 *
 * There are helpers to iterate over all members of LIST and DICT data_t
 * pointers. These are all function pointer based helpers that will call a given
 * function pointer on each item being iterated over. The return value is
 * operational command allowing the function pointer to inform the caller how to
 * proceed. There are purposefully no iterators for data_t pointers to avoid any
 * form of dangling pointers.
 *
 * data_t uses a plugin interface for serialization of the data to common
 * formats. These plugins require 3rd party libraries and may not have been
 * compiled. Any code should expect this possiblity as data_init() will fail if
 * the plugin is not found.
 *
 * Example usage:
 *
 * //Global init requiring JSON serializer
 * if (data_init(MIME_TYPE_JSON_PLUGIN, NULL)) fatal("failed");
 * //Create root data entry:
 * data_t *ex = data_new();
 * //Set data entry to be a dictionary type
 * data_set_dict(ex);
 * //Set key test1 to be string "test1 value"
 * data_set_string(data_key_set(ex, "test1"), "test1 value");
 * //Set key test2 to be integer 12345
 * data_set_int(data_key_set(ex, "test2"), 12345);
 * // serialize into JSON string
 * char *json = NULL;
 * data_g_serialize(&json, ex, MIME_TYPE_JSON, DATA_SER_FLAGS_PRETTY);
 * // cleanup the example
 * FREE_NULL_DATA(ex);
 * // log the json
 * debug("example json: %s", json);
 * // deserialise the JSON back into data_t
 * data_g_deserialize(&ex, json, strlen(json), MIME_TYPE_JSON);
 * xfree(json);
 * // verify contents
 * xassert(data_get_type(ex) == DATA_TYPE_DICT);
 * xassert(!xstrcmp(data_get_string(data_key_get(ex, "test1"), "test1 value"));
 * xassert(data_get_int(data_key_get(ex, "test2") == 12345);
 * // cleanup tree
 * FREE_NULL_DATA(ex);
 * // release all global memory and plugins
 * data_fini();
 */

#ifndef _DATA_H
#define _DATA_H

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

#include "src/common/list.h"
#include "src/common/plugrack.h"

/*
 * The possible types of data.
 *
 * Each one is based on ECMA types.
 */
typedef enum {
	DATA_TYPE_NONE = 0, /* invalid or unknown type */
	DATA_TYPE_NULL, /* ECMA-262:4.3.13 NULL type */
	DATA_TYPE_LIST, /* ECMA-262:22.1 Array Object (ordered list) */
	DATA_TYPE_DICT, /* ECMA-262:23.1 Map Object (dictionary) */
	DATA_TYPE_INT_64, /*  64bit signed integer
				This exists as an convenient storage type.
				ECMA does not have an integer primitive.
				ECMA-262:7.1.4 ToInteger() returns approx
				this value with some rounding. */
	DATA_TYPE_STRING, /* ECMA-262:4.3.18 String type */
	DATA_TYPE_FLOAT, /* ECMA-262:6.1.6 Number type */
	DATA_TYPE_BOOL, /* ECMA-262:4.3.15 Boolean type */
	DATA_TYPE_MAX /* only for bounds checking */
} data_type_t;

/* convert type to readable string */
extern char *data_type_to_string(data_type_t type);

/* opaque type for list_u and dict_u */
typedef struct data_list_s data_list_t;

/*
 * Opaque data struct to hold generic data.
 * data is based on the JSON data type and has the same types.
 * data forms a tree structure.
 * please avoid direct access of this struct and only use access functions.
 * the nature of this struct may change at any time, only pass around pointers
 * created from data_new().
 */
typedef struct {
	int magic;
	data_type_t type;
	union { /* append "_u" to every type to avoid reserved words */
		data_list_t *list_u;
		data_list_t *dict_u;
		int64_t int_u;
		char *string_u;
		double float_u;
		bool bool_u;
	} data;
} data_t;

/*
 * Enum to control how the foreach will
 * handle the result of a function call
 */
typedef enum {
	DATA_FOR_EACH_INVALID = 0,
	DATA_FOR_EACH_CONT, /* continue for each processing */
	DATA_FOR_EACH_DELETE, /* delete item */
	DATA_FOR_EACH_STOP, /* stop for each processing */
	DATA_FOR_EACH_FAIL, /* stop for each processing due to failure */
	DATA_FOR_EACH_MAX, /* assertion only value on max value */
} data_for_each_cmd_t;

/*
 *  Function prototype for operating on each item in a list typed data_t.
 *  Returns command requested for processing
 */
typedef data_for_each_cmd_t (*DataListForF) (data_t *data, void *arg);
/*
 *  Function prototype for operating on each item in a list typed data_t.
 *  Returns command requested for processing
 */
typedef data_for_each_cmd_t (*DataListForFConst) (const data_t *data, void *arg);

/*
 *  Function prototype for operating on each item in a dictionary typed data_t.
 *  Returns command requested for processing
 */
typedef data_for_each_cmd_t (*DataDictForF) (const char *key, data_t *data, void *arg);

/*
 *  Function prototype for operating on each item in a dictionary typed data_t.
 *  Returns command requested for processing
 */
typedef data_for_each_cmd_t (*DataDictForFConst) (const char *key, const data_t *data, void *arg);

/*
 * Initialize static structs needed by data functions and
 * 	load serializer plugins
 *
 * It is safe to call this function multiple times with different plugins as
 * they will be loaded only once.
 *
 * IN plugins - comma delimited list of plugins or "list"
 * 	pass NULL to load all found or "" to load none of them
 *
 * IN listf - function to call if plugins="list" (may be NULL)
 * RET SLURM_SUCCESS or error
 */
extern int data_init(const char *plugins, plugrack_foreach_t listf);
/*
 * Cleanup global memory used by data_t helpers and unload all plugins
 *
 * WARNING: must be called only once after all data commands complete
 */
extern void data_fini(void);

/*
 * Create new data struct.
 * 	must call FREE_NULL_DATA() against resultant.
 * 	do not xfree().
 * RET data structure or will abort()
 */
extern data_t *data_new(void);
/*
 * safely and recursively frees all parts of data struct.
 * 	Try to use FREE_NULL_DATA() instead.
 * IN data structure to free
 * */
extern void data_free(data_t *data);

#define FREE_NULL_DATA(_X)             \
	do {                           \
		if (_X)                \
			data_free(_X); \
		_X = NULL;             \
	} while (0)

/*
 * Get data type enum.
 * IN data structure to examine
 * RET enum value of type.
 * 	returns DATA_TYPE_NONE if data is NULL.
 */
extern data_type_t data_get_type(const data_t *data);

/*
 * Set data to float type with given value.
 * IN data structure to modify
 * IN value value to set
 * RET data ptr or NULL on error
 */
extern data_t *data_set_float(data_t *data, double value);

/*
 * Set data to NULL type (no value possible)
 * IN data structure to modify
 * RET data ptr or NULL on error
 */
extern data_t *data_set_null(data_t *data);

/*
 * Set data to bool type with given value.
 * IN data structure to modify
 * IN value value to set
 * RET data ptr or NULL on error
 */
extern data_t *data_set_bool(data_t *data, bool value);

/*
 * Set data to signed 64 bit integer type with given value.
 * IN data structure to modify
 * IN value value to set
 * RET data ptr or NULL on error
 */
extern data_t *data_set_int(data_t *data, int64_t value);

/*
 * Set data to string type with given value.
 * IN data structure to modify
 * IN value value to set
 * RET data ptr or NULL on error
 */
extern data_t *data_set_string(data_t *data, const char *value);

/*
 * Set data to string type with given value.
 * IN data structure to modify
 * IN value value to set (takes ownership of value and will xfree())
 * RET data ptr or NULL on error
 */
extern data_t *data_set_string_own(data_t *data, char *value);

/*
 * Set data to string type with given formatted value.
 * IN data structure to modify
 * IN fmt - printf format field
 */
#define data_set_string_fmt(data, fmt, ...)          \
	do {                                         \
		char *str = NULL;                    \
		xstrfmtcat(str, fmt, ##__VA_ARGS__); \
		if (!data_set_string_own(data, str)) \
			xfree(str);                  \
	} while (0)

/*
 * Detect data type and if possible, change to correct type.
 * WARNING: command is currently only useful for to/from DATA_TYPE_STRING.
 * WARNING: Does not work on dict or list types
 * IN data structure to try to type and convert
 * IN match try to detect this type only
 * 	or DATA_TYPE_NONE to try automatic matching
 * RET new data type or DATA_TYPE_NONE on no change
 */
extern data_type_t data_convert_type(data_t *data, const data_type_t match);

/*
 * Recursively apply data_convert_type() to entire data tree
 * IN data structure to try to type and convert
 * IN match try to detect this type only
 * 	or DATA_TYPE_NONE to try automatic matching
 * RET count of data nodes converted (may be 0)
 */
extern size_t data_convert_tree(data_t *data, const data_type_t match);

/*
 * Get data as floating point number
 * IN data data to retrieve as a double
 * RET NAN (if not a float type) or value
 * WARNING: only use this function if type already known as DATA_TYPE_FLOAT
 */
extern double data_get_float(const data_t *data);

/*
 * Get data as Boolean
 * IN data data to retrieve as a bool
 * RET false (if not a bool type) or value
 * WARNING: only use this function if type already known as DATA_TYPE_BOOL
 */
extern bool data_get_bool(const data_t *data);

/*
 * Get data as bool and force conversion if possible.
 * IN data data to convert to bool and set to bool if successful
 * IN ptr_buffer ptr to set if bool conversion is successful
 * RET SLURM_SUCCESS or error
 */
extern int data_get_bool_converted(data_t *data, bool *ptr_buffer);

/*
 * Get data as bool and force conversion if possible in clone if required
 * IN data data to convert to bool
 * IN ptr_buffer ptr to set if bool value is successful
 * RET SLURM_SUCCESS or error
 */
extern int data_copy_bool_converted(const data_t *data, bool *ptr_buffer);

/*
 * Get data as int
 * IN data data to retrieve as a int64_t
 * RET false (if not a int type) or value
 * WARNING: only use this function if type already known as DATA_TYPE_INT_64
 */
extern int64_t data_get_int(const data_t *data);

/*
 * Get data as int and force conversion if possible.
 * IN data data to convert to int
 * IN ptr_buffer ptr to set if string conversion is successful
 * RET SLURM_SUCCESS or error
 */
extern int data_get_int_converted(const data_t *d, int64_t *ptr_buffer);

/*
 * Get data as string
 * IN data data to convert into a string
 * RET data string or NULL on failure
 */
extern char *data_get_string(data_t *data);

/*
 * Get const data as string
 * IN data data to convert into a string
 * RET data string or NULL on failure
 */
extern const char *data_get_string_const(const data_t *data);

/*
 * Get data as string and force conversion if possible.
 * IN data data to convert to string
 * IN ptr_buffer ptr to set if string conversion is successful (must xfree()) or
 * will be set to NULL
 * RET SLURM_SUCCESS or error
 */
extern int data_get_string_converted(const data_t *d, char **ptr_buffer);

/*
 * Set data as type dictionary.
 * 	Warning: this must be called before adding key entries.
 * IN data data to set as dictionary type
 * RET ptr to data
 */
extern data_t *data_set_dict(data_t *data);

/*
 * Set data as type list.
 * 	Warning: this must be called before adding list entries.
 * IN data data to set as list type
 * RET ptr to data
 */
extern data_t *data_set_list(data_t *data);

/*
 *  For each item in data list [d], invokes the function [f] with [arg].
 *
 *  Returns a count of the number of items on which [f] returned
 *  DATA_FOR_EACH_CONT.
 *
 *  Will process per return of [f]
 */
extern int data_list_for_each(data_t *d, DataListForF f, void *arg);

/*
 *  For each item in data list [d], invokes the function [f] with [arg].
 *
 *  Returns a count of the number of items on which [f] returned
 *  DATA_FOR_EACH_CONT.
 *
 *  Will process per return of [f]
 */
extern int data_list_for_each_const(const data_t *d, DataListForFConst f, void *arg);

/*
 *  For each item in data dictionary [d], invokes the function [f] with [arg].
 *
 *  Returns a count of the number of items on which [f] returned
 *  DATA_FOR_EACH_CONT.
 *
 *  Will process per return of [f]
 */
extern int data_dict_for_each(data_t *d, DataDictForF f, void *arg);

/*
 *  For each item in data dictionary [d], invokes the function [f] with [arg].
 *
 *  Returns a count of the number of items on which [f] returned
 *  DATA_FOR_EACH_CONT.
 *
 *  Will process per return of [f]
 */
extern int data_dict_for_each_const(const data_t *d, DataDictForFConst f, void *arg);

/*
 * Get number of entities in dictionary
 * IN data data object to count dictionary entities
 * RET cardinality of data (may return 0 for empty dictionary)
 */
extern size_t data_get_dict_length(const data_t *data);

/*
 * Append NULL data to end of list
 * IN data data object (list type only) to append a new data object
 * RET new data object at end of list
 */
extern data_t *data_list_append(data_t *data);

/*
 * Append NULL data to start of list
 * IN data data object (list type only) to prepend a new data object
 * RET new data object at start of list
 */
extern data_t *data_list_prepend(data_t *data);

/*
 * Copy and join array of data into a single list.
 * IN data - array of data objects (list type only) to copy/merge into a single
 * data list. Last entry must be NULL.
 * IN flatten_lists - if data is a list, add items in list as if there were
 * separate data items in data. Will only flatten 1 level.
 * RET new data object with merged lists.
 */
extern data_t *data_list_join(const data_t **data, bool flatten_lists);

/*
 * Get number of entities in list
 * IN data data object to count list entities
 * RET cardinality of data (may return 0 for empty list)
 */
extern size_t data_get_list_length(const data_t *data);

/*
 * Get data entry with given key (from constant data).
 * IN data constant data object to find entity with given key string
 * IN key string of key to find
 * RET ptr to data or NULL if it doesn't exist.
 */
extern const data_t *data_key_get_const(const data_t *data, const char *key);

/*
 * Get data entry with given key
 * IN data data object to find entity with given key string
 * IN key string of key to find
 * RET ptr to data or NULL if it doesn't exist.
 */
extern data_t *data_key_get(data_t *data, const char *key);

/*
 * Get data entry with given integer key
 * IN data data object to find entity with given key string
 * IN key string of key to find
 * RET ptr to data or NULL if it doesn't exist.
 */
extern data_t *data_key_get_int(data_t *data, int64_t key);

/*
 * Create data entry with given key.
 * Use this as a quick way to add or update a given key.
 *
 * IN data data object to find entity with given key string
 * IN key string of key to find
 * RET ptr to new data object or existing data object with given key
 */
extern data_t *data_key_set(data_t *data, const char *key);

/*
 * set key using integer instead of string.
 * Same functionality as data_key_set().
 *
 * IN data data object to set entity with given key
 * IN key integer key of data to set (converted to string)
 * RET ptr to new data object or existing data object with given key
 */
extern data_t *data_key_set_int(data_t *data, const int64_t key);

/*
 * remove and free data at given key
 *
 * IN data data object to unset entity with given key
 * IN key key of entity to remove
 * RET true if key found and removed, false if not found
 */
extern bool data_key_unset(data_t *data, const char *key);

/*
 * attempt to match two data by type and values
 * Warning: order matters & lists must not contain list/dict entries
 * IN data - data source a to check
 * IN path - data source b to verify a against
 * IN mask - if true, if b entry is NULL, then ignore value in entry in a
 * RET true on match, false otherwise
 */
extern bool data_check_match(const data_t *a, const data_t *b, bool mask);

/*
 * resolve out path based on data object full of dictionary keys
 * IN data dictionary of dictionaries to search
 * IN path /key/path/to/value to search
 * RET ptr to data of value or NULL if not found
 */
extern data_t *data_resolve_dict_path(data_t *data, const char *path);

/*
 * resolve out path based on data object full of dictionary keys
 * IN data dictionary of dictionaries to search
 * IN path /key/path/to/value to search
 * RET const ptr to data of value or NULL if not found
 */
extern const data_t *data_resolve_dict_path_const(const data_t *data,
						   const char *path);

/*
 * create (or resolve) out path based on data object full of dictionary keys
 * IN data dictionary of dictionaries to search
 * IN path /key/path/to/value to search and create if required
 * RET ptr to data of value or NULL on error (mainly non-dictionary in path)
 */
extern data_t *data_define_dict_path(data_t *data, const char *path);

/*
 * retrieve data from given dictionary path of type string
 * IN data dictionary to traverse
 * IN path path to find
 * IN ptr_buffer ptr to buffer to set with string ptr (you must xfree())
 * 	will xfree if not null
 * RET SLURM_SUCCESS if found and copied or error
 */
extern int data_retrieve_dict_path_string(const data_t *data, const char *path,
					  char **ptr_buffer);

/*
 * retrieve data from given dictionary path of type bool
 * IN data dictionary to traverse
 * IN path path to find
 * IN ptr_buffer ptr to set (true or false)
 * RET SLURM_SUCCESS if found and copied or error
 */
extern int data_retrieve_dict_path_bool(const data_t *data, const char *path,
					bool *ptr_buffer);

/*
 * retrieve data from given dictionary path of type integer
 * IN data dictionary to traverse
 * IN path path to find
 * IN ptr_buffer ptr to set with integer
 * RET SLURM_SUCCESS if found and copied or error
 */
extern int data_retrieve_dict_path_int(const data_t *data, const char *path,
				       int64_t *ptr_buffer);

/*
 * deep copy entire data tree from src to dest
 * IN dest destination data to overwrite
 * IN src source data to deep copy
 * RET ptr to dest or NULL on error
 */
extern data_t *data_copy(data_t *dest, const data_t *src);

typedef enum {
	DATA_SER_FLAGS_NONE = 0, /* defaults to compact currently */
	DATA_SER_FLAGS_COMPACT = 1 << 1,
	DATA_SER_FLAGS_PRETTY = 1 << 2,
} data_serializer_flags_t;

/*
 * Define common MIME types to make it easier for serializer callers.
 *
 * WARNING: There is no guarantee that plugins for these types
 * will be loaded at any given time.
 */
#define MIME_TYPE_YAML "application/x-yaml"
#define MIME_TYPE_YAML_PLUGIN "serializer/yaml"
#define MIME_TYPE_JSON "application/json"
#define MIME_TYPE_JSON_PLUGIN "serializer/json"
#define MIME_TYPE_URL_ENCODED "application/x-www-form-urlencoded"
#define MIME_TYPE_URL_ENCODED_PLUGIN "serializer/url-encoded"

/*
 * Serialize data in src into string dest
 * IN/OUT dest - ptr to NULL string ptr to set with output data.
 * 	caller must xfree(dest) if set.
 * IN src - populated data ptr to serialize
 * IN mime_type - serialize data into the given mime_type
 * IN flags - optional flags to specify to serilzier to change presentation of
 * 	data
 * RET SLURM_SUCCESS or error
 */
extern int data_g_serialize(char **dest, const data_t *src,
			    const char *mime_type,
			    data_serializer_flags_t flags);

/*
 * Deserialize string in src into data dest
 * IN/OUT dest - ptr to NULL data ptr to set with output data.
 * 	caller must FREE_NULL_DATA(dest) if set.
 * IN src - string to deserialize
 * IN length - number of bytes in src
 * IN mime_type - deserialize data using given mime_type
 * RET SLURM_SUCCESS or error
 */
extern int data_g_deserialize(data_t **dest, const char *src, size_t length,
			      const char *mime_type);

/*
 * Check if there is a plugin loaded that can handle the requested mime type
 * RET ptr to best matching mime type or NULL if none can match
 */
extern const char *data_resolve_mime_type(const char *mime_type);

#endif /* _DATA_H */
