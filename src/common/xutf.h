/*****************************************************************************\
 *  xutf.h - Unicode handler
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#ifndef _XUTF_H
#define _XUTF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Numeric UTF character coding */
typedef uint32_t utf_code_t;

/* character for utf16 strings */
typedef uint16_t utf16_t;

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
extern const char *utf_encoding_scheme_to_string(utf_encoding_schemes_t schema);

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
#define UTF32LE_BYTE_ORDER_MARK_SEQ { 0xFF, 0xFE, 0x00, 0x00 }

/* UTF8 code to replace any kind of vertical spacing */
#define UTF_RETURN_SYMBOL_CODE ((utf_code_t) 0x23CE)
/* UTF8 code to replace an invalid character (or code sequence) */
#define UTF_REPLACEMENT_CODE ((utf_code_t) 0xFFFD)
/* UTF8 code to replace a space character */
#define UTF_SPACE_REPLACEMENT_CODE ((utf_code_t) 0x00B7)
/* UTF8 code to replace a control code */
#define UTF_CONTROL_REPLACEMENT_CODE ((utf_code_t) 0x2426)

/* Any char below value is considered ASCII and can be safely treated as such */
#define UTF_ASCII_MAX_CODE 0x7F

/*
 * Max number of bytes in string required to hold single UTF-8 character
 * including NULL character.
 */
#define UTF8_CHAR_MAX_BYTES 5

/*
 * Length in bytes of a NUL-terminated UTF-8 string.
 * Warning: this is counting bytes in the string until '\0', not the number of
 * utf8 characters.
 */
extern size_t utf8_strlen(const utf8_t *str);

/*
 * xstrndup() wrapper for utf8_t strings.
 * Warning: this is counting bytes in the string until '\0', not the number of
 * utf8 characters.
 */
extern utf8_t *utf8_ndup(const utf8_t *src, size_t n);

/*
 * xstrdup() wrapper for utf8_t strings
 * Warning: this is counting bytes in the string until '\0', not the number of
 * utf8 characters.
 */
extern utf8_t *utf8_dup(const utf8_t *src);

/*
 * Read string for BOM to determine if format is given explicitly
 * IN src - ptr to string to read
 * IN end - ptr to end of string
 * IN/OUT bytes_ptr - Populate with number of bytes read
 * RET detected encoding scheme or UTF_UNKNOWN_ENCODING if there is no BOM
 */
extern utf_encoding_schemes_t utf_read_encoding_schema(const utf8_t *src,
						       const utf8_t *end,
						       int *bytes_ptr);

/*
 * Resolves loggable character for any given utf code
 * IN utf - utf code
 * RET loggable utf code
 */
extern utf_code_t utf8_get_loggable(const utf_code_t utf);

/*
 * Is UTF code considered a newline
 * IN utf - utf code
 * RET true if utf is a newline
 *
 * Warning: call utf8_is_newline() directly instead
 */
extern bool slurm_is_utf8_newline(utf_code_t utf);

/*
 * Is UTF code considered a newline
 *	Use macro to check ASCII directly to avoid function call cost
 * IN utf - utf code
 * RET true if utf is a newline
 */
#define utf8_is_newline(utf) \
	(((utf) <= UTF_ASCII_MAX_CODE) ? \
		 ((((utf) >= 0xA) && ((utf) <= 0xD)) || \
		  (((utf) >= 0x1c) && ((utf) <= 0x1f))) : \
		 slurm_is_utf8_newline(utf))

/*
 * Is UTF code considered a horizontal space
 * IN utf - utf code
 * RET true if utf is horizontal whitespace
 *
 * Warning: Call utf8_is_space() directly instead
 */
extern bool slurm_is_utf8_space(utf_code_t utf);

/*
 * Is UTF code considered a horizontal space
 *	Use macro to check ASCII directly to avoid function call cost
 * IN utf - utf code
 * RET true if utf is horizontal whitespace
 */
#define utf8_is_space(utf) \
	(((utf) <= UTF_ASCII_MAX_CODE) ? \
		 (((utf) == 0x09) || ((utf) == 0x20)) : \
		 slurm_is_utf8_space(utf))

/*
 * Is UTF code considered whitespace
 *	Use macro to inline and allow compiler to avoid duplicate compares
 * IN utf - utf code
 * RET true if utf is a whitespace
 */
#define utf8_is_whitespace(utf) (utf8_is_space(utf) || utf8_is_newline(utf))

/*
 * Is UTF code considered control character
 * IN utf - utf code
 * RET true if utf is a control character
 *
 * Warning: Call utf8_is_control() directly instead
 */
extern bool slurm_is_utf8_control(utf_code_t utf);

/*
 * Is UTF code considered control character
 *	Use macro to inline and allow compiler to avoid duplicate compares
 * IN utf - utf code
 * RET true if utf is a control character
 */
#define utf8_is_control(utf) \
	(((utf) <= UTF_ASCII_MAX_CODE) ? \
		 (((utf) <= 0x8) || (((utf) >= 0xE) && ((utf) <= 0x1F)) || \
		  ((utf) == 0x7f)) : \
		 slurm_is_utf8_control(utf))

/*
 * Is UTF code valid (aka not illformed)
 * IN utf - utf code
 * RET SLURM_SUCCESS if valid or error if illformed
 *
 * Warning: call the utf_is_valid() macro directly instead
 */
extern int slurm_is_utf_valid(utf_code_t utf);

/*
 * Is UTF code valid (aka not illformed)
 *	Use macro to check ASCII directly to avoid function call cost
 * IN utf - utf code
 * RET SLURM_SUCCESS if valid or error if illformed
 */
#define utf_is_valid(utf) \
	((((utf) > 0) && ((utf) <= UTF_ASCII_MAX_CODE)) ? \
		 0 : \
		 slurm_is_utf_valid(utf))

/*
 * Combine and validate two UTF-16 code units into UTF character coding
 *
 * Represented via in JSON:
 *	\uXXXX
 *	\uXXXX\uXXXX
 *
 * Example:
 *	G clef character (U+1D11E) is \uD834\uDD1E
 *
 * IN high - high surrogate UTF16 code or only code
 * IN low - low surrogate UTF16 code or 0 if not a surrogate
 * IN utf_ptr - pointer to populate with character coding
 * RET
 *	SLURM_SUCCESS - UTF16 converted into UTF character coding
 *	ESLURM_UTF16_SURROGATE_CODE - Unpaired UTF16 surrogate
 *	error - Any other utf_is_valid() error return
 */
extern int utf16_to_coding(const utf16_t high, const utf16_t low,
			   utf_code_t *utf_ptr);

/*
 * Split a UTF character coding into one or two UTF-16 code units
 *
 * Inverse of utf16_to_coding(). Represented in JSON:
 *	\uXXXX
 *	\uXXXX\uXXXX
 *
 * Example:
 *	G clef character (U+1D11E) is 𝄞
 *
 * IN utf - UTF character coding
 * IN high_ptr - pointer to populate with the high surrogate or only code unit
 * IN low_ptr - pointer to populate with the low surrogate, or 0 if utf is a
 *	Basic Multilingual Plane scalar (no surrogate pair required)
 * RET
 *	SLURM_SUCCESS - utf split into UTF-16 code unit(s)
 *	error - Any utf_is_valid() error return (includes surrogate and
 *		out-of-range code points)
 */
extern int utf16_from_coding(const utf_code_t utf, utf16_t *high_ptr,
			     utf16_t *low_ptr);

/*
 * Is UTF16 code considered a high surrogate
 * IN utf - utf code
 * RET true if utf is a high surrogate character
 */
#define utf16_is_high_surrogate(utf) (((utf) >= 0xD800) && ((utf) <= 0xDBFF))

/*
 * Is UTF16 code considered a low surrogate
 * IN utf - utf code
 * RET true if utf is a low surrogate character
 */
#define utf16_is_low_surrogate(utf) (((utf) >= 0xDC00) && ((utf) <= 0xDFFF))

/*
 * Read single UTF-8 character
 * IN src - ptr to string to read
 * IN end - ptr to end of string
 * IN/OUT utf_ptr - ptr to populate with utf code or UTF_REPLACEMENT_CODE on
 *	error
 * IN/OUT bytes_ptr - ptr to populate with number of bytes of character
 * IN check_valid - check if resultant utf code is a valid code, otherwise limit
 *	sanity checking to unparsable codes
 * RET SLURM_SUCCESS or error
 *
 * WARNING: always call utf8_read_character() macro directly
 */
extern int slurm_read_utf8_character(const utf8_t *src, const utf8_t *end,
				     utf_code_t *utf_ptr, int *bytes_ptr,
				     bool check_valid);

/*
 * Read single UTF-8 character
 *	Use macro to bypass entire function call if the src character is within
 *	the ASCII range and avoid paying all the function calls if not needed.
 *	This favors reading ASCII efficiency at the cost of another if when
 *	handling non-ASCII characters.
 * IN src - ptr to string to read
 * IN end - ptr to end of string
 * IN/OUT utf_ptr - ptr to populate with utf code or UTF_REPLACEMENT_CODE or \0
 * IN/OUT bytes_ptr - ptr to populate with number of bytes of character read
 * IN check_valid - check if resultant utf code is a valid code, otherwise limit
 *	sanity checking to unparsable codes
 * RET SLURM_SUCCESS or error
 */
#define utf8_read_character(src, end, utf_ptr, bytes_ptr, check_valid) \
	(((src) && ((src) < (end)) && ((src)[0] > 0) && \
	  ((src)[0] <= UTF_ASCII_MAX_CODE)) ? \
		 ((*(utf_ptr) = (src)[0]), (*(bytes_ptr) = 1), \
		  SLURM_SUCCESS) : \
		 slurm_read_utf8_character((src), (end), (utf_ptr), \
					   (bytes_ptr), (check_valid)))

/*
 * Set dst with multibyte UTF-8 character
 * IN utf - utf code
 * IN dst - ptr to string to populate
 *	dst will not have \0 set after character
 *	dst must be at least UTF8_CHAR_MAX_BYTES characters long
 * IN/OUT bytes_ptr - number of UTF-8 bytes written (1-4)
 * IN log - True to log results or False to avoid all logging
 * RET SLURM_SUCCESS or error
 *
 * WARNING: use utf8_write_character() macro instead of calling directly
 */
extern int slurm_write_utf8_character(const utf_code_t utf, utf8_t *dst,
				      int *bytes_ptr, bool log);

/*
 * Set dst with multibyte UTF-8 character
 *	Macro sets an ASCII character directly to avoid the slower call to
 *	slurm_write_utf8_character(). The compiler folds this for compile-time
 *	constant ASCII inputs and short-circuits the function call for runtime
 *	ASCII characters. This results in a noticeable performance bonus.
 *
 * IN utf - utf code
 * IN dst - ptr to string to populate
 *	dst will not have \0 set after character
 *	dst must be at least UTF8_CHAR_MAX_BYTES characters long
 * IN/OUT bytes_ptr - number of UTF-8 bytes written (1-4)
 * RET SLURM_SUCCESS or error
 */
#define utf8_write_character(utf, dst, bytes_ptr) \
	((((utf) > 0) && ((utf) <= UTF_ASCII_MAX_CODE)) ? \
		 ((((utf8_t *) (dst))[0] = (utf)), (*(bytes_ptr) = 1), \
		  SLURM_SUCCESS) : \
		 slurm_write_utf8_character((utf), (dst), (bytes_ptr), true))

#endif /* _XUTF_H */
