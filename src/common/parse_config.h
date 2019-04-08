/*****************************************************************************
 *  parse_config.h - parse any slurm.conf-like configuration file
 *
 *  NOTE: when you see the prefix "s_p_", think "slurm parser".
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifndef _PARSE_CONFIG_H
#define _PARSE_CONFIG_H

#include <stdint.h>
#include "slurm/slurm.h"
#include "src/common/pack.h"

/*
 * This slurm file parser provides a method for parsing a file
 * for key-value pairs of the form "key = value".  This parser can be used
 * for any slurm-like configuration file, not just slurm.conf.  If you are
 * looking for code specific to slurm.conf, look in
 * src/common/slurm_conf.[hc].
 *
 * In the parsed file, any amount of white-space is allowed between the
 * key, equal-sign, and value.  The parser handles comments, line
 * continuations, and escaped characters automatically.  Double-quotes can
 * be used to surround an entire value if white-space is needed within
 * a value string.
 *
 * A comment begins with a "#" and ends at the end of the line. A line
 * continuation is a "\" character at the end of the line (only white-space
 * may follow the "\").  A line continuation tells the parser to
 * concatenate the following line with the current line.
 *
 * To include a literal "\" or "#" character in a file, it can be escaped
 * by a preceding "\".
 *
 * Double-quotes CANNOT be escaped, and they must surround the entire value
 * string, they cannot be used within some substring of a value string.
 * An empty string can be specified with doubles quotes: Apple="".
 *
 * To use this parser, first construct an array of s_p_options_t structures.
 * Only the "key" string needs to be non-zero.  Zero or NULL are valid
 * defaults for type, handler, and destroy, which conventiently allows
 * then to be left out in any static initializations of options arrays. For
 * instance:
 *
 *	s_p_options_t options[] = {{"Apples", S_P_UINT16},
 *	                           {"Oranges"},
 *	                           {NULL}};
 *
 * In this example, the handler and destroy functions for the "Apples" key
 * are NULL pointers, and for key "Oranges" even the type is zero.  A zero
 * type is equivalent to specifying type S_P_IGNORE.
 *
 * Once an s_p_options_t array is defined, it is converted into a slurm
 * parser hash table structure with the s_p_hashtbl_create() function.
 * The s_p_hashtbl_t thus returned can be given to the s_p_parse_file()
 * function to parse a file, and fill in the s_p_hashtbl_t structure with
 * the values found in the file.  Values for keys can then be retrieved
 * from the s_p_hashtbl_t with the functions with names beginning with
 * "s_p_get_", e.g. s_p_get_boolean(), s_p_get_string(), s_p_get_uint16(),
 * etc.
 *
 * Valid types
 * -----------
 *
 * S_P_IGNORE - Any instance of specified key and associated value in a file
 *	will be allowed, but the value will not be stored and will not
 *	be retirevable from the s_p_hashtbl_t.
 * S_P_STRING - The value for a given key will be saved in string form. no
 *      conversions will be performed on the value, unless it is used as a
 *      nested definition inside a S_P_EXPLINE definition. (see S_P_EXPLINE)
 * S_P_PLAIN_STRING - The value for a given key will be saved in string form,
 *      no conversions will be performed on the value.
 * S_P_LONG - The value for a given key must be a valid
 *	string representation of a long integer (as determined by strtol()),
 *	otherwise an error will be raised.
 * S_P_UINT16 - The value for a given key must be a valid
 *	string representation of an unsigned 16-bit integer.
 * S_P_UINT32 - The value for a given key must be be a valid
 *	string representation of an unsigned 32-bit integer.
 * S_P_UINT64 - The value for a given key must be be a valid
 *	string representation of an unsigned 64-bit integer.
 * S_P_POINTER - The parser makes no assumption about the type of the value.
 *    	The s_p_get_pointer() function will return a pointer to the
 *	s_p_hashtbl_t's internal copy of the value.  By default, the value
 *	will simply be the string representation of the value found in the file.
 *	This differs from S_P_STRING in that s_p_get_string() returns a COPY
 *	of the value which must be xfree'ed by the user.  The pointer
 *	returns by s_p_get_pointer() must NOT be freed by the user.
 *  	It is intended that normally S_P_POINTER with be used in conjunction
 *	with "handler" and "destroy" functions to implement a custom type.
 * S_P_ARRAY - This (and S_P_IGNORE, which does not record the fact that it
 *	has seen the key previously) is the only type which allows its key to
 * 	appear multiple times in a file.  With any other type (except
 *	S_P_IGNORE), an error will be raised when a key is seen more than
 *	once in a file.
 *	S_P_ARRAY works mostly the same as S_P_POINTER, except that it builds
 *	an array of pointers to the found values.
 * S_P_LINE - This type avoids to write custom handlers by directly providing
 *      the capability to express nested s_p_options_t structs into an
 *      s_p_options_t. As with S_P_ARRAY, it allows its key to appear multiple
 *      times in a file.
 *      It can be seen as an advanced version of the S_P_ARRAY type enabling
 *      to return an array of s_p_hashtable_t containing the sub-elements as
 *      described in the nested s_p_options_t.
 *      No custom handlers are supported with S_P_LINE. An example of S_P_LINE
 *      usage would be :
 *      s_p_options_t entity_options[] = {
 *         {"Entity", S_P_STRING}
 *         {"CoordX", S_P_UINT32},
 *         {"CoordY", S_P_UINT32},
 *         {"CoordZ", S_P_UINT32},
 *         {NULL}
 *      };
 *      s_p_options_t options[] = {
 *         {"Entity", S_P_LINE, NULL, NULL, entity_options},
 *         {NULL}
 *      };
 *      The s_p_get_line() function will return the array of hashtables
 *      corresponding to the "Entity" entries found in the configuration file.
 *      Note that "Entity=%key% ..." lines sharing the same master "key"
 *      will be automatically merged into the same hashtable enabling to split
 *      the definition over multiple lines without having to use the '\'
 *      delimiter.
 *      The following example shows the content the previously defined
 *      s_p_options_t would handle :
 *      -----
 *      Entity=node[0-3] CoordX=0
 *      Entity=node[0-3] CoordY=2
 *      Entity=node[4-7] CoordX=1
 *      Entity=node[4-7] CoordY=2
 *      -----
 *      This file would provide a hashtables array containing 2 elements with
 *      the following master keys :
 *      - node[0-3]
 *      - node[4-7]
 *      /!\ WARNING: do not specify the same struct as suboption or /!\
 *      /!\ an infinite loop will occur.                            /!\
 * * S_P_EXPLINE - This type is an extended version of the S_P_LINE type that
 *      add the capability to expand the hostlist formated elements when
 *      possible in order to reduce the number of lines required to parse some
 *      complex configurations. The values associated to the key of the
 *      S_P_EXPLINE will then be automatically expanded in order to return one
 *      hashtable element per associated value.
 *      Replacing the S_P_LINE with an S_P_EXPLINE in the previous example with :
 *      s_p_options_t options[] = {
 *         {"Entity", S_P_EXPLINE, NULL, NULL, entity_options},
 *         {NULL}
 *      };
 *      would then enable to automatically manage conf files like :
 *      -----
 *      Entity=node[0-3] CoordX=0
 *      Entity=node[0-3] CoordY=2 CoordZ=[10-13]
 *      Entity=node[4-7] CoordX=1
 *      Entity=node[4-7] CoordY=2 CoordZ=[10-13]
 *      -----
 *      The s_p_get_expline() function will in this example returns an array of
 *      eight elements, having the master key set to the following values :
 *      - node0
 *      - node1
 *      ...
 *      Note that in case a particular option string must not be expanded but
 *      still used within an S_P_EXPLINE definition, it will have to be
 *      expressed as a S_P_PLAIN_STRING instead of a basic S_P_STRING. Indeed,
 *      S_P_STRING are automatically expanded using hostlist related functions.
 *      An example of such a situation would be something like :
 *      s_p_options_t entity_options[] = {
 *         {"Entity", S_P_STRING}
 *         {"Enclosed", S_P_PLAIN_STRING},
 *         {NULL}
 *      };
 *      s_p_options_t options[] = {
 *         {"Entity", S_P_EXPLINE, NULL, NULL, entity_options},
 *         {NULL}
 *      };
 *      -----
 *      Entity=switch[0-1] Enclosed=flake[0-17]
 *      Entity=switch[2-3] Enclosed=flake[18-35]
 *      -----
 *      /!\ WARNING: do not specify the same struct as suboption or /!\
 *      /!\ an infinite loop will occur.                            /!\
 *
 * Handlers and destructors
 * ------------------------
 *
 * Any key specified in an s_p_options_t array can have function callbacks for
 * a "handler" function and a "destroy" function.  The prototypes for each
 * are available below in the typedef of s_p_options_t.
 *
 * The "handler" function is given the "key" string, "value" string, and a
 * pointer to the remainder of the "line" on which the key-value pair was found
 * (this is the line after the parser has removed comments and concatenated
 * continued lines).  The handler can transform the value any way it desires,
 * and then return a pointer to the newly allocated value data in the "data"
 * pointer.  The handler shall also return int the "leftover" parameter a
 * pointer into the "line" buffer designating the start of any leftover,
 * unparsed characters in the string.  The return code from "handler" must be
 * -1 if the value is invalid, 0 if the value is valid but no value will be set
 * for "data" (the parser will not flag this key as already seen, and the
 * destroy() function will not be called during s_p_hashtbl_destroy()),
 * and 1 if "data" is set.
 *
 * If the "destroy" function is set for a key, and the parser will mark the key
 * as "seen" during parsing, then it will pass the pointer to the value data
 * to the "destroy" function when s_p_hashtbl_destroy() is called.  If
 * a key was "seen" during parsing, but the "destroy" function is NULL,
 * s_p_hashtbl_destroy() will call xfree() on the data pointer.
 */

typedef struct s_p_values s_p_values_t;
typedef s_p_values_t * s_p_hashtbl_t;

typedef enum slurm_parser_enum {
	S_P_IGNORE = 0,
	S_P_STRING,
	S_P_LONG,
	S_P_UINT16,
	S_P_UINT32,
	S_P_UINT64,
	S_P_POINTER,
	S_P_ARRAY,
	S_P_BOOLEAN,
	S_P_LINE,
	S_P_EXPLINE,
	S_P_PLAIN_STRING /* useful only within S_P_EXPLINE */,
	S_P_FLOAT,
	S_P_DOUBLE,
	S_P_LONG_DOUBLE

} slurm_parser_enum_t;

/*
 * Standard Slurm conf files use key=value elements.
 * slurm_parser_operator_t extends that concept to cover additionnal
 * use cases like :
 *        key+=value
 *        key-=value
 *        key*=value
 *        key/=value
 *
 * this feature is for now dedicated to the layouts framework. It enables
 * to have advanced modifications of entities reusing the traditional
 * Slurm parser with the new operator information to manage updates.
 *
 */
typedef enum slurm_parser_operator {
	S_P_OPERATOR_SET = 0,
	S_P_OPERATOR_ADD,
	S_P_OPERATOR_SUB,
	S_P_OPERATOR_MUL,
	S_P_OPERATOR_DIV,
	S_P_OPERATOR_SET_IF_MIN,
	S_P_OPERATOR_SET_IF_MAX,
	S_P_OPERATOR_AVG
} slurm_parser_operator_t;

typedef struct conf_file_options {
	char *key;
	slurm_parser_enum_t type;
	int (*handler)(void **data, slurm_parser_enum_t type,
		       const char *key, const char *value,
		       const char *line, char **leftover);
	void (*destroy)(void *data);
	struct conf_file_options* line_options;
} s_p_options_t;


s_p_hashtbl_t *s_p_hashtbl_create(const struct conf_file_options options[]);
void s_p_hashtbl_destroy(s_p_hashtbl_t *hashtbl);

/* Returns SLURM_SUCCESS if file was opened and parse correctly
 * OUT hash_val - cyclic redundancy check (CRC) character-wise value
 *                of file.
 * IN ignore_new - do not treat unrecognized keywords as a fatal error,
 *                 print debug() message and continue
 */
int s_p_parse_file(s_p_hashtbl_t *hashtbl, uint32_t *hash_val, char *filename,
		   bool ignore_new);

/* Returns SLURM_SUCCESS if buffer was opened and parse correctly.
 * buffer must be a valid Buf bufferonly containing strings.The parsing
 * stops at the first non string content extracted.
 * OUT hash_val - cyclic redundancy check (CRC) character-wise value
 *                of file.
 * IN ignore_new - do not treat unrecognized keywords as a fatal error,
 *                 print debug() message and continue
 */
int s_p_parse_buffer(s_p_hashtbl_t *hashtbl, uint32_t *hash_val,
		     Buf buffer, bool ignore_new);

/*
 * Returns 1 if the line is parsed cleanly, and 0 otherwise.
 */
int s_p_parse_pair(s_p_hashtbl_t *hashtbl, const char *key, const char *value);

/*
 * Returns 1 if the line is parsed cleanly, and 0 otherwise.
 * Set the operator of the updated s_p_values_t to the provided one.
 */
int s_p_parse_pair_with_op(s_p_hashtbl_t *hashtbl, const char *key,
			   const char *value, slurm_parser_operator_t opt);

/*
 * Returns 1 if the line is parsed cleanly, and 0 otherwise.
 */
int s_p_parse_line(s_p_hashtbl_t *hashtbl, const char *line, char **leftover);

/*
 * s_p_hashtbl_merge
 * 
 * Merge the contents of two s_p_hashtbl_t data structures. Anything in
 * from_hashtbl that does not also appear in to_hashtbl is transfered to it.
 * This is intended primary to support multiple lines of DEFAULT configuration
 * information and preserve the old default values while adding new defaults.
 *
 * IN from_hashtbl - Source of old data
 * IN to_hashtbl - Destination for old data (if new value not already set)
 */
void s_p_hashtbl_merge(s_p_hashtbl_t *to_hashtbl, s_p_hashtbl_t *from_hashtbl);

/* Like s_p_hashtbl_merge, but if for a key, data exists in both tables, data
 * is swapped.
 */
void s_p_hashtbl_merge_override(s_p_hashtbl_t *to_hashtbl,
				s_p_hashtbl_t *from_hashtbl);

/*
 * Mainly to enable a generic set of option to be merged with a specific set
 * of options.
 */
void s_p_hashtbl_merge_keys(s_p_hashtbl_t *to_hashtbl,
			    s_p_hashtbl_t *from_hashtbl);

int s_p_parse_line_complete(s_p_hashtbl_t *hashtbl,
		const char* key, const char* value,
		const char *line, char **leftover);

/*
 * s_p_parse_line_expanded
 *
 * Parse a whole line of data and generate an array of s_p_hashtable. This
 * function is meant to be used inside a custom handler of a (left most) key.
 *
 * This function can be used in a custom handler, but in general, use of
 * S_P_*LINE is prefered.
 *
 * IN hashtbl - hash table template of a final line after expansion,
 *		types and custom handlers are used after line has been
 *		expanded. They will parse values as if the line were not
 *		expandable and were written only with one value by key.
 *		This hash table must contains the master key definition
 *		(left most key of the line).
 * OUT data - resulting hashtables array
 * OUT data_count - number of resulting hashtables in the array
 * IN key - the master key (left most key of the line)
 * IN value - the value attached to the master key (which will be converted
 *	      with s_p_parse_pair thanks to hashtbl)
 * IN line - only used for logging
 * IN leftover - used by s_p_parse_line
 */
int s_p_parse_line_expanded(const s_p_hashtbl_t *hashtbl,
		s_p_hashtbl_t*** data, int* data_count,
		const char* key, const char* value,
		const char *line, char **leftover);

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
 * s_p_get_uint64
 *
 * Search for a key in a s_p_hashtbl_t with value of type
 * uint64.  If the key is found and has a set value, the
 * value is retuned in "num".
 *
 * OUT num - pointer to a uint64_t where the value is returned
 * IN key - hash table key
 * IN hashtbl - hash table created by s_p_hashtbl_create()
 *
 * Returns 1 when a value was set for "key" during parsing and "num"
 *   was successfully set, otherwise returns 0;
 */
int s_p_get_uint64(uint64_t *num, const char *key,
		   const s_p_hashtbl_t *hashtbl);

/*
 * s_p_get_float
 *
 * Search for a key in a s_p_hashtbl_t with value of type
 * float.  If the key is found and has a set value, the
 * value is retuned in "num".
 *
 * OUT num - pointer to a float where the value is returned
 * IN key - hash table key
 * IN hashtbl - hash table created by s_p_hashtbl_create()
 *
 * Returns 1 when a value was set for "key" during parsing and "num"
 *   was successfully set, otherwise returns 0;
 */
int s_p_get_float(float *num, const char *key,
		  const s_p_hashtbl_t *hashtbl);

/*
 * s_p_get_double
 *
 * Search for a key in a s_p_hashtbl_t with value of type
 * double.  If the key is found and has a set value, the
 * value is retuned in "num".
 *
 * OUT num - pointer to a double where the value is returned
 * IN key - hash table key
 * IN hashtbl - hash table created by s_p_hashtbl_create()
 *
 * Returns 1 when a value was set for "key" during parsing and "num"
 *   was successfully set, otherwise returns 0;
 */
int s_p_get_double(double *num, const char *key,
		   const s_p_hashtbl_t *hashtbl);

/*
 * s_p_get_long_double
 *
 * Search for a key in a s_p_hashtbl_t with value of type
 * long double.  If the key is found and has a set value, the
 * value is retuned in "num".
 *
 * OUT num - pointer to a long double where the value is returned
 * IN key - hash table key
 * IN hashtbl - hash table created by s_p_hashtbl_create()
 *
 * Returns 1 when a value was set for "key" during parsing and "num"
 *   was successfully set, otherwise returns 0;
 */
int s_p_get_long_double(long double *num, const char *key,
			const s_p_hashtbl_t *hashtbl);

/*
 * s_p_get_operator
 *
 * Search for a key in a s_p_hashtbl_t and return the operator
 * associated with that key in the configuration file. The operator
 * is one of the slurm_parser_operator_t enum possible values.
 *
 * OUT operator - pointer to a slurm_parser_operator_t where the
 *     operator is returned
 * IN key - hash table key
 * IN hashtbl - hash table created by s_p_hashtbl_create()
 *
 * Returns 1 when a operator was set for "key" during parsing and
 *     "operator" was successfully set, otherwise returns 0;
 */
int s_p_get_operator(slurm_parser_operator_t *opt, const char *key,
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

/** works like s_p_get_array but each item of the array is a s_p_hashtbl_t */
int s_p_get_line(s_p_hashtbl_t **ptr_array[], int *count,
		  const char *key, const s_p_hashtbl_t *hashtbl);

/** works like s_p_get_array but each item of the array is a s_p_hashtbl_t */
int s_p_get_expline(s_p_hashtbl_t **ptr_array[], int *count,
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


/*
 * Given an "options" array, pack the key, type of options along with values and
 * op of the hashtbl.
 *
 * Primarily for sending a table across the network so you don't have to read a
 * file in.
 */
extern Buf s_p_pack_hashtbl(const s_p_hashtbl_t *hashtbl,
			   const s_p_options_t options[],
			   const uint32_t cnt);

/*
 * Given a buffer, unpack key, type, op and value into a hashtbl.
 */
extern s_p_hashtbl_t *s_p_unpack_hashtbl(Buf buffer);

/*
 * copy options onto the end of full_options
 * IN/OUT full_options
 * IN options
 * IN/OUT full_options_cnt
 *
 * Used if the full set of options are not available from one location.
 */
extern void transfer_s_p_options(s_p_options_t **full_options,
				 s_p_options_t *options,
				 int *full_options_cnt);

#endif /* !_PARSE_CONFIG_H */
