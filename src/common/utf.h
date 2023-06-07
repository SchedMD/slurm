/*****************************************************************************\
 *  utf.h - UTF-8 handlers
 *****************************************************************************
 *  Copyright (C) 2023 SchedMD LLC.
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

#ifndef _UTF_H
#define _UTF_H

/* Numeric UTF character coding */
typedef int32_t utf_code_t;

/* character for utf8 strings */
typedef uint8_t utf8_t;

typedef enum {
	UTF_INVALID = 0,
	UTF_UNKNOWN_ENCODING,
	UTF_8_ENCODING,
	UTF_16BE_ENCODING,
	UTF_16LE_ENCODING,
	UTF_32BE_ENCODING,
	UTF_32LE_ENCODING,
	UTF_INVALID_MAX
} utf_encoding_schemes_t;

/* convert schema to string for logging */
const char *utf_encoding_scheme_to_string(utf_encoding_schemes_t schema);

/* Marks string as a UTF format  */
#define UTF_BYTE_ORDER_MARK_CODE ((utf_code_t) 0xFEFF)
/* Byte sequence to mark stream as UTF-8 */
#define UTF8_BYTE_ORDER_MARK_SEQ { 0xEF, 0xBB, 0xBF }
/* Byte sequence to mark stream as UTF-16 Big Endian */
#define UTF16BE_BYTE_ORDER_MARK_SEQ { 0xFE, 0xFF }
/* Byte sequence to mark stream as UTF-16 Little Endian */
#define UTF16LE_BYTE_ORDER_MARK_SEQ { 0xFF, 0xFE }
/* Byte sequence to mark stream as UTF-32 Big Endian */
#define UTF32BE_BYTE_ORDER_MARK_SEQ { 0x00, 0x00, 0xFE, 0xFF }
/* Byte sequence to mark stream as UTF-32 Little Endian */
#define UTF32LE_BYTE_ORDER_MARK_SEQ { 0xFE, 0xFF, 0x00, 0x00 }

/* UTF8 code to replace any kind of vertical spacing */
#define UTF_RETURN_SYMBOL_CODE ((utf_code_t) 0x23CE)
/* UTF8 code to replace an invalid character (or code sequence) */
#define UTF_REPLACEMENT_CODE ((utf_code_t) 0xFFFD)
/* UTF8 code to replace a space character */
#define UTF_SPACE_REPLACEMENT_CODE ((utf_code_t) 0x00B7)
/* Byte sequence for UTF replacement space character */
#define UTF8_SPACE_REPLACEMENT_SEQ { 0xC2, 0xB7 }
/* UTF8 code to replace a control code */
#define UTF_CONTROL_REPLACEMENT_CODE ((utf_code_t) 0x2426)
/* Byte sequence for UTF control replacement code */
#define UTF8_CONTROL_REPLACEMENT_SEQ { 0xE2, 0x90, 0xA6 }

/* Printf replacement for UTF code */
#define UTF8_PRINTF "U+%06"PRIx32

/* Any char below value is considered ASCII and can be safely treated as such */
#define UTF_ASCII_MAX_CODE 0x7F

/*
 * Max number of bytes in string required to hold single UTF-8 character
 * including NULL character.
 */
#define UTF8_CHAR_MAX_BYTES 5

/*
 * Read string for BOM to determine if format is given explicitly
 * IN src - ptr to string to read
 * IN end - ptr to end of string
 * RET SLURM_SUCCESS or error
 */
extern utf_encoding_schemes_t read_utf_encoding_schema(const utf8_t *src,
						       const utf8_t *end);

/*
 * Read single UTF-8 character
 * IN src - ptr to string to read
 * IN end - ptr to end of string
 * IN/OUT utf_ptr - ptr to populate with utf code (or -1 on error)
 * IN/OUT bytes_ptr - ptr to populate with number of bytes of character (or 0 on
 *	error)
 * RET SLURM_SUCCESS or error
 */
extern int read_utf8_character(const utf8_t *src, const utf8_t *end,
			       utf_code_t *utf_ptr, int *bytes_ptr);

/*
 * Set dst with multibyte UTF-8 character
 * IN utf - utf code
 * IN dst - ptr to string to populate
 * 	dst will not have \0 set after character
 * 	dst must be atleast UTF8_CHAR_MAX_BYTES characters long
 * RET SLURM_SUCCESS or error (invalid utf input)
 */
extern int write_utf8_character(const utf_code_t utf, utf8_t *dst,
				bool write_null_terminator);

/*
 * Get number of bytes for a given UTF coding
 * IN utf - utf code
 * RET 1-4 or -1 on invalid utf code
 */
extern int get_utf8_byte_count(const utf_code_t utf);

/*
 * Resolves loggable character for any given utf code
 * IN utf - utf code
 * RET loggable utf code
 */
extern utf_code_t get_utf8_loggable(const utf_code_t utf);

/*
 * Is UTF code considered a newline
 * IN utf - utf code
 * RET true if utf is a newline
 */
extern bool is_utf8_newline(utf_code_t utf);

/*
 * Is UTF code considered a horizontal space
 * IN utf - utf code
 * RET true if utf is a newline
 */
extern bool is_utf8_space(utf_code_t utf);

/*
 * Is UTF code considered whitespace
 * IN utf - utf code
 * RET true if utf is a whitespace
 */
extern bool is_utf8_whitespace(utf_code_t utf);

/*
 * Is UTF code considered control character
 * IN utf - utf code
 * RET true if utf is a control character
 */
extern bool is_utf8_control(utf_code_t utf);

/*
 * Is UTF code valid (aka not illformed)
 * IN utf - utf code
 * RET SLURM_SUCCESS if valid or error if illformed
 */
extern int is_utf_valid(utf_code_t utf);

#endif /* _UTF_H */
