/*****************************************************************************\
 *  xutf.c - Unicode handler
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

#include <stddef.h>

#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

#include "src/common/xutf.h"

/* The code point and byte checks below rely on the signedness and width here */
_Static_assert((utf_code_t) -1 > 0, "utf_code_t must be unsigned");
_Static_assert(sizeof(utf_code_t) == 4, "utf_code_t must be 32 bits");
_Static_assert((utf8_t) -1 > 0, "utf8_t must be unsigned");
_Static_assert(sizeof(utf8_t) == 1, "utf8_t must be 8 bits");

/*
 * Alias the non-prefixed exported functions to slurm_-prefixed names so the
 * serializer plugin links against them via slurm_xlator.h. The macro-backed
 * helpers are already slurm_-prefixed and need no alias.
 */
strong_alias(utf_encoding_scheme_to_string,
	     slurm_utf_encoding_scheme_to_string);
strong_alias(utf8_strlen, slurm_utf8_strlen);
strong_alias(utf8_ndup, slurm_utf8_ndup);
strong_alias(utf8_dup, slurm_utf8_dup);
strong_alias(utf_read_encoding_schema, slurm_utf_read_encoding_schema);
strong_alias(utf8_get_loggable, slurm_utf8_get_loggable);
strong_alias(utf16_to_coding, slurm_utf16_to_coding);
strong_alias(utf16_from_coding, slurm_utf16_from_coding);

/*
 * Logging here is excessive and called multiple times for every single byte
 * which makes even a simple if() expensive. We are disabling the checks here
 * outside of developer mode instead of trying to cache or reduce the costs
 * here. The logging is being kept as bugs here are extremely hard to track down
 * without them though and can easily pop up given the nature of the utf8
 * characters involved.
 */
#ifndef NDEBUG
#define DEBUG_LOG_ENABLED true
#else
#define DEBUG_LOG_ENABLED false
#endif

#define LOG(fmt, ...) \
	do { \
		if (DEBUG_LOG_ENABLED && \
		    (slurm_conf.debug_flags & DEBUG_FLAG_DATA) && \
		    (get_log_level() >= LOG_LEVEL_DEBUG5)) \
			log_flag(DATA, "%s: " fmt, __func__, ##__VA_ARGS__); \
	} while (false)

static const struct {
	utf_encoding_schemes_t schema;
	char *string;
} utf_schemas[] = {
	{ UTF_INVALID, "INVALID" },        { UTF_UNKNOWN_ENCODING, "UNKNOWN" },
	{ UTF_8_ENCODING, "UTF-8" },       { UTF_16BE_ENCODING, "UTF-16BE" },
	{ UTF_16LE_ENCODING, "UTF-16LE" }, { UTF_32BE_ENCODING, "UTF-32BE" },
	{ UTF_32LE_ENCODING, "UTF-32LE" }, { UTF_INVALID_MAX, "INVALID" },
};

static const struct {
	utf_code_t utf; /* utf code */
	utf_code_t sub; /* utf code for replacement character */
} utf_subs[] = {
	/*
	 * Values from Control Pictures:
	 *	https://www.unicode.org/charts/PDF/U2400.pdf
	 */
	{ 0x000000, 0x2400 }, /* NUL (null) */
	{ 0x000001, 0x2401 }, /* SOH (start of heading) */
	{ 0x000002, 0x2402 }, /* STX (start of text) */
	{ 0x000003, 0x2403 }, /* ETX (end of text) */
	{ 0x000004, 0x2404 }, /* EOT (end of transmission) */
	{ 0x000005, 0x2405 }, /* ENQ (enquiry) */
	{ 0x000006, 0x2406 }, /* ACK (acknowledge) */
	{ 0x000007, 0x2407 }, /* BEL (bell) */
	{ 0x000008, 0x2408 }, /* BS  (backspace) */
	{ 0x000009, 0x2409 }, /* TAB (horizontal tab) */
	{ 0x00000A, 0x240A }, /* LF  (NL line feed, new line) */
	{ 0x00000B, 0x240B }, /* VT  (vertical tab) */
	{ 0x00000C, 0x240C }, /* FF  (NP form feed, new page) */
	{ 0x00000D, 0x240D }, /* CR  (carriage return) */
	{ 0x00000E, 0x240E }, /* SO  (shift out) */
	{ 0x00000F, 0x240F }, /* SI  (shift in) */
	{ 0x000010, 0x2410 }, /* DLE (data link escape) */
	{ 0x000011, 0x2411 }, /* DC1 (device control 1) */
	{ 0x000012, 0x2412 }, /* DC2 (device control 2) */
	{ 0x000013, 0x2413 }, /* DC3 (device control 3) */
	{ 0x000014, 0x2414 }, /* DC4 (device control 4) */
	{ 0x000015, 0x2415 }, /* NAK (negative acknowledge) */
	{ 0x000016, 0x2416 }, /* SYN (synchronous idle) */
	{ 0x000017, 0x2417 }, /* ETB (end of trans. block) */
	{ 0x000018, 0x2418 }, /* CAN (cancel) */
	{ 0x000019, 0x2419 }, /* EM  (end of medium) */
	{ 0x00001A, 0x241A }, /* SUB (substitute) */
	{ 0x00001B, 0x241B }, /* ESC (escape) */
	{ 0x00001C, 0x241C }, /* FS  (file separator) */
	{ 0x00001D, 0x241D }, /* GS  (group separator) */
	{ 0x00001E, 0x241E }, /* RS  (record separator) */
	{ 0x00001F, 0x241F }, /* US  (unit separator) */
	{ 0x000020, 0x2420 }, /* SPACE */
	{ 0x00007F, 0x2421 }, /* DEL */
	{ 0x000085, 0x23CE }, /* next line */
	{ 0x00200E, 0x2AAA }, /* LRM (left to right mark) */
	{ 0x00200F, 0x2AAB }, /* RLM (right to left mark) */
	{ 0x00202A, 0x2AAA }, /* left-to-right embedding */
	{ 0x00202B, 0x2AAB }, /* right-to-left embedding */
	{ 0x00202C, 0x2AA4 }, /* pop directional formatting */
	{ 0x00202D, 0x2AAA }, /* left-to-right override */
	{ 0x00202E, 0x2AAB }, /* right-to-left override */
};

extern size_t utf8_strlen(const utf8_t *str)
{
	return strlen((const char *) str);
}

extern utf8_t *utf8_ndup(const utf8_t *src, size_t n)
{
	return (utf8_t *) xstrndup((const char *) src, n);
}

extern utf8_t *utf8_dup(const utf8_t *src)
{
	return (utf8_t *) xstrdup((const char *) src);
}

extern const char *utf_encoding_scheme_to_string(utf_encoding_schemes_t schema)
{
	for (int i = 0; i < ARRAY_SIZE(utf_schemas); i++)
		if (utf_schemas[i].schema == schema)
			return utf_schemas[i].string;

	xassert(false);
	return "INVALID";
}

extern utf_code_t utf8_get_loggable(const utf_code_t utf)
{
	/* swap out explicit replacements */
	for (int i = 0; i < ARRAY_SIZE(utf_subs); i++) {
		if (utf == utf_subs[i].utf)
			return utf_subs[i].sub;
		else if (utf_subs[i].utf > utf)
			break;
	}

	if (utf_is_valid(utf))
		return UTF_REPLACEMENT_CODE;

	if (utf8_is_newline(utf))
		return UTF_RETURN_SYMBOL_CODE;

	if (utf8_is_space(utf))
		return UTF_SPACE_REPLACEMENT_CODE;

	if (utf8_is_control(utf))
		return UTF_CONTROL_REPLACEMENT_CODE;

	return utf;
}

extern bool slurm_is_utf8_control(utf_code_t utf)
{
	static const utf_code_t codes[] = {
		0x34F, /* combining grapheme joiner */
		0x61C, /* arabic letter mark */
		0xE0001, /* language tag (deprecated) */
		0x115F, /* hangul jamo block */
		0x1160, /* hangul jungseong filler */
		0x3164, /* hangul filler */
	};

#ifndef NDEBUG
	/*
	 * Wrapping macro will have already check ASCII so just verify it didn't
	 * miss a character. This is more for documentation of the characters
	 * than anything else.
	 */

	/*
	 * Unicode 15.0.0:
	 *	There are 65 code points set aside in the Unicode Standard for
	 *	compatibility with the C0 and C1 control codes defined in the
	 *	ISO/IEC 2022 framework. The ranges of these code points are
	 *	U+0000..U+001F, U+007F, and U+0080..U+009F
	 */
	xassert(utf > 0x8);
	/*
	 * We are just going to pretend these are not control codes:
	 *	TAB (horizontal tab)
	 *	LF  (NL line feed, new line)
	 *	VT  (vertical tab)
	 *	FF  (NP form feed, new page)
	 *	CR  (carriage return)
	 */
	xassert(!((utf >= 0xE) && (utf <= 0x1F)));

	/* DEL */
	xassert(utf != 0x7f);

	xassert(utf > UTF_ASCII_MAX_CODE);
#endif

	/* PAD (Padding Character) -> APPLICATION PROGRAM COMMAND */
	if ((utf >= 0x80) && (utf <= 0x9F))
		return true;

	/* ZERO WIDTH NON-JOINER -> RIGHT-TO-LEFT MARK */
	if ((utf >= 0x200C) && (utf <= 0x200F))
		return true;
	/* left-to-right embedding -> right-to-left override */
	if ((utf >= 0x202A) && (utf <= 0x202E))
		return true;
	/* word joiner -> nominal digit shapes (deprecated) */
	if ((utf >= 0x2060) && (utf <= 0x206F))
		return true;
	/* interlinear annotation anchor -> interlinear annotation terminator */
	if ((utf >= 0xFFF9) && (utf <= 0xFFFB))
		return true;

	for (int i = 0; i < ARRAY_SIZE(codes); i++)
		if (utf == codes[i])
			return true;

	return false;
}

extern bool slurm_is_utf8_newline(utf_code_t utf)
{
	static const utf_code_t codes[] = {
		0x0085, /* NEL (next line) */
		0x2028, /* LS (line separator) */
		0x2029, /* PS (paragraph separator) */
	};

	/*
	 * Unicode 15.0.0: 5.8 Newline Guidelines gives these as
	 * newlines:
	 *	CR (carriage return)
	 *	LF (line feed)
	 *	CRLF (carriage return and line feed)
	 *	NEL (next line)
	 *	VT (vertical tab)
	 *	FF (form feed)
	 *	LS (line separator)
	 *	PS (paragraph separator)
	 */

#ifndef NDEBUG
	/*
	 * Wrapping macro will have already check ASCII so just verify it didn't
	 * miss a character. This is more for documentation of the characters
	 * than anything else.
	 */

	/* LF (line feed) -> CR (carriage return) */
	xassert(!((utf >= 0xA) && (utf <= 0xD)));
	/* FS (file separator) -> US (unit separator) */
	xassert(!((utf >= 0x1c) && (utf <= 0x1f)));

	xassert(utf > UTF_ASCII_MAX_CODE);
#endif

	for (int i = 0; i < ARRAY_SIZE(codes); i++)
		if (utf == codes[i])
			return true;

	return false;
}

extern bool slurm_is_utf8_space(utf_code_t utf)
{
	static const utf_code_t high_codes[] = {
		0x00A0, /* no-break space */
		0x1680, /* ogham space mark */
		0x180E, /* mongolian vowel separator */
		0x202F, /* narrow no-break space */
		0x205F, /* medium mathematical space */
		0x2800, /* braille pattern blank */
		0x3000, /* ideographic space */
		0xFFA0, /* halfwidth hangful filler */
	};

	/*
	 * Avoid is_space() to avoid locale causing this character list to
	 * change but instead follow Unicode standard.  We also need to be able
	 * to handle multibyte UTF8 characters so a single char is not enough.
	 *
	 * Unicode 15.0.0: Table 6-2. Unicode Space Characters
	 *	U+0020 space
	 *	U+00A0 no-break space
	 *	U+1680 ogham space mark
	 *	U+2000 en quad
	 *	U+2001 em quad
	 *	U+2002 en space
	 *	U+2003 em space
	 *	U+2004 three-per-em space
	 *	U+2005 four-per-em space
	 *	U+2006 six-per-em space
	 *	U+2007 figure space
	 *	U+2008 punctuation space
	 *	U+2009 thin space
	 *	U+200A hair space
	 *	U+202F narrow no-break space
	 *	U+205F medium mathematical space
	 *	U+3000 ideographic space
	 */

	if (utf < 0)
		return false;

#ifndef NDEBUG
	/*
	 * Wrapping macro will have already check ASCII so just verify it didn't
	 * miss a character. This is more for documentation of the characters
	 * than anything else.
	 */

	/* horizontal tab */
	xassert(utf != 0x09);
	/* space */
	xassert(utf != 0x20);

	xassert(utf > UTF_ASCII_MAX_CODE);
#endif

	/* en quad -> zero width space */
	if ((utf >= 0x2000) && (utf <= 0x200B))
		return true;

	for (int i = 0; i < ARRAY_SIZE(high_codes); i++)
		if (utf == high_codes[i])
			return true;

	return false;
}

extern int slurm_is_utf_valid(utf_code_t utf)
{
	/*
	 * Check against invalid UTF codes but try to be as fast as possible as
	 * this function is called while checking every character.
	 *
	 * The Unicode standard doesn't keep a list of the valid codespaces in a
	 * single page or table but has them scattered about the the entire
	 * standard. Each check there is based on the utf8 byte ranges and then
	 * the relevant invalid areas in those. Its split up to try to do the
	 * checks only once and only when relevant.
	 */

	/*
	 * Wrapping macro will eave already check ASCII so just verify it didn't
	 * miss a character
	 */
	xassert(!((utf > 0) && (utf <= UTF_ASCII_MAX_CODE)));

	if (utf == 0x0) {
		/*
		 * Unicode 15.0.0:
		 *	It is acceptable for a conforming implementation: To
		 *	support only a subset of the Unicode characters.
		 *
		 * UTF allows for U+0 but leaves the implementation to reject or
		 * allow any specific UTF code. Allowing U+0 will leave the door
		 * open for too many possible avenues of attack against Slurm.
		 * NULL terminated strings are the standard string type used
		 * everywhere in Slurm with a few exceptions of mainly buf_t and
		 * serializers. The 4 byte utf-8 codes will be the slowest since
		 * that codespace has the most invalid ranges.
		 */
		return ESLURM_UTF_NULL_CODE;
	}

	if ((utf < 0) || (utf > 0x10FFFF))
		/* outside of UTF codespace */
		return ESLURM_UTF_INVALID_CODE;

	if (((utf & 0xFFFE) == 0xFFFE) || ((utf >= 0xFDD0) && (utf <= 0xFDEF)))
		/*
		 * Reject noncharacters in any plane.
		 *
		 * Unicode 15.0.0:
		 *	D14 Noncharacter: A code point that is permanently
		 *	reserved for internal use. Noncharacters consist of the
		 *	values U+nFFFE and U+nFFFF (where n is from 0 to 1016)
		 *	and the values U+FDD0..U+FDEF.
		 *
		 * (utf & 0xFFFE) == 0xFFFE matches both U+nFFFE and U+nFFFF for
		 * every plane n.
		 */
		return ESLURM_UTF_NONCHARACTER_CODE;

	if (utf <= 0xD7FF) { /* imply (utf > 0) && */
		/* 1-3 byte */
		return SLURM_SUCCESS;
	} else if (utf <= 0xFFFF) { /* imply (utf >= 0xD800) && */
		if ((utf >= 0xD800) && (utf <= 0xDFFF))
			/*
			 * Reject surrogate code points only used for UTF-16.
			 *
			 * Unicode 15.0.0:
			 *	D71 High-surrogate code point: A Unicode code
			 *	point in the range U+D800 to U+DBFF.
			 *	D73 Low-surrogate code point: A Unicode code
			 *	point in the range U+DC00 to U+DFFF.
			 */
			return ESLURM_UTF16_SURROGATE_CODE;
		if ((utf >= 0xE000) && (utf <= 0xF8FF))
			/*
			 * Reject private use only codes
			 *
			 * Unicode 15.0.0:
			 *	D49 Private-use code point: Code points in the
			 *	ranges U+E000..U+F8FF
			 */
			return ESLURM_UTF_PRIVATE_CODE;
		if ((utf >= 0xFFF0) && (utf <= 0xFFF8))
			/*
			 * Unicode 15.0.0:
			 *	The nine unassigned Unicode code points in the
			 *	range U+FFF0..U+FFF8 are reserved for special
			 *	character definitions.
			 */
			return ESLURM_UTF_RESERVED_CODE;

		/* 1-3 bytes */
		return SLURM_SUCCESS;
	} else if (utf <= 0x0FFFFF) { /* imply (utf >= 0x10000) && */
		if ((utf >= 0xF0000) && (utf <= 0xFFFFD))
			/*
			 * Reject private use only codes
			 *
			 * Unicode 15.0.0:
			 *	D49 Private-use code point: Code points in the
			 *	ranges U+E000..U+F8FF, U+F0000..U+FFFFD, and
			 *	U+100000..U+10FFFD.
			 */
			return ESLURM_UTF_PRIVATE_CODE;

		/* 4 byte */
		return SLURM_SUCCESS;
	} else if (utf <= 0x10FFFF) { /* imply (utf >= 0x100000) && */
		if ((utf >= 0x100000) && (utf <= 0x10FFFD))
			/*
			 * Reject private use only codes
			 *
			 * Unicode 15.0.0:
			 *	D49 Private-use code point: Code points in the
			 *	ranges U+E000..U+F8FF, U+F0000..U+FFFFD, and
			 *	U+100000..U+10FFFD.
			 */
			return ESLURM_UTF_PRIVATE_CODE;

		/* 4 byte */
		return SLURM_SUCCESS;
	}

	/* should never get here as we missed some bits above */
	xassert(false);
	return ESLURM_UTF_INVALID_CODE;
}

extern int utf16_to_coding(const utf16_t high, const utf16_t low,
			   utf_code_t *utf_ptr)
{
	xassert(utf_ptr);

	if (low) {
		if (!utf16_is_high_surrogate(high) ||
		    !utf16_is_low_surrogate(low)) {
			*utf_ptr = UTF_REPLACEMENT_CODE;
			return ESLURM_UTF16_SURROGATE_CODE;
		}

		*utf_ptr = 0x00010000;
		*utf_ptr += (high - 0xD800) * 0x0400;
		*utf_ptr += low - 0xDC00;
	} else {
		*utf_ptr = high;
	}

	return utf_is_valid(*utf_ptr);
}

extern int utf16_from_coding(const utf_code_t utf, utf16_t *high_ptr,
			     utf16_t *low_ptr)
{
	int rc = -1;

	xassert(high_ptr);
	xassert(low_ptr);

	if ((rc = utf_is_valid(utf))) {
		*high_ptr = '\0';
		*low_ptr = '\0';
		return rc;
	}

	if (utf <= 0xFFFF) {
		/* BMP scalar: single code unit, no low surrogate */
		*high_ptr = utf;
		*low_ptr = 0;
	} else {
		/* Supplementary plane: inverse of utf16_to_coding() */
		const utf_code_t v = utf - 0x00010000;

		*high_ptr = 0xD800 + (v / 0x0400);
		*low_ptr = 0xDC00 + (v % 0x0400);
	}

	return SLURM_SUCCESS;
}

extern utf_encoding_schemes_t utf_read_encoding_schema(const utf8_t *src,
						       const utf8_t *end,
						       int *bytes_ptr)
{
	static const utf8_t utf8[] = UTF8_BYTE_ORDER_MARK_SEQ;
	static const utf8_t utf16be[] = UTF16BE_BYTE_ORDER_MARK_SEQ;
	static const utf8_t utf16le[] = UTF16LE_BYTE_ORDER_MARK_SEQ;
	static const utf8_t utf32be[] = UTF32BE_BYTE_ORDER_MARK_SEQ;
	static const utf8_t utf32le[] = UTF32LE_BYTE_ORDER_MARK_SEQ;
	const ptrdiff_t bytes = (end - src);

	xassert(src);
	xassert(end);
	xassert(bytes >= 0);
	xassert(bytes_ptr);

	if (bytes <= 0) {
		*bytes_ptr = 0;
		return UTF_UNKNOWN_ENCODING;
	}

	if ((sizeof(utf8) <= bytes) && !memcmp(utf8, src, sizeof(utf8))) {
		*bytes_ptr = sizeof(utf8);
		return UTF_8_ENCODING;
	}

	/*
	 * Check the 4-byte UTF-32 BOMs before the 2-byte UTF-16 BOMs: the
	 * UTF-32LE BOM (FF FE 00 00) begins with the UTF-16LE BOM (FF FE), so
	 * testing UTF-16 first would shadow UTF-32LE detection.
	 */
	if ((sizeof(utf32be) <= bytes) &&
	    !memcmp(utf32be, src, sizeof(utf32be))) {
		*bytes_ptr = sizeof(utf32be);
		return UTF_32BE_ENCODING;
	}

	if ((sizeof(utf32le) <= bytes) &&
	    !memcmp(utf32le, src, sizeof(utf32le))) {
		*bytes_ptr = sizeof(utf32le);
		return UTF_32LE_ENCODING;
	}

	if ((sizeof(utf16be) <= bytes) &&
	    !memcmp(utf16be, src, sizeof(utf16be))) {
		*bytes_ptr = sizeof(utf16be);
		return UTF_16BE_ENCODING;
	}

	if ((sizeof(utf16le) <= bytes) &&
	    !memcmp(utf16le, src, sizeof(utf16le))) {
		*bytes_ptr = sizeof(utf16le);
		return UTF_16LE_ENCODING;
	}

	*bytes_ptr = 0;
	return UTF_UNKNOWN_ENCODING;
}

extern int slurm_write_utf8_character(const utf_code_t utf, utf8_t *dst,
				      int *bytes_ptr, bool log)
{
	int rc = EINVAL;

	xassert(bytes_ptr);
	xassert(dst);

	if (!dst || !bytes_ptr)
		return EINVAL;

	if ((rc = utf_is_valid(utf))) {
		/*
		 * Unicode 15.0.0 D92:
		 * Any UTF-8 byte sequence that does not match the patterns
		 * listed in Table 3-7 is ill-formed.
		 *
		 * Unicode 15.0.0 3.9.2:
		 * replaces almost every byte of an ill-formed UTF-8 sequence
		 * with one U+FFFD
		 */
		if (log)
			LOG("replacing invalid U+%06" PRIx32 ": %s", utf,
			    slurm_strerror(rc));
		return slurm_write_utf8_character(UTF_REPLACEMENT_CODE, dst,
						  bytes_ptr, log);
	} else if ((utf >= 0) && (utf <= 0x7F)) {
		/* UTF8 below 128 is same as ASCII */
		dst[0] = utf & 0x7F;
		*bytes_ptr = 1;

		if (log)
			LOG("converted U+%06" PRIx32 " to 0x%" PRIx8, utf,
			    dst[0]);
	} else if ((utf >= 0x80) && (utf <= 0x7FF)) {
		/*
		 * UTF8 shifts and masks each byte for every code point
		 * 110xxxxx	10xxxxxx
		 */
		dst[0] = 0xc0 | (0x1f & (utf >> 6));
		dst[1] = 0x80 | (0x3f & (utf >> 0));
		*bytes_ptr = 2;

		if (log)
			LOG("converted U+%06" PRIx32 " to 0x%" PRIx8
			    " 0x%" PRIx8,
			    utf, dst[0], dst[1]);
	} else if ((utf >= 0x800) && (utf <= 0xFFFF)) {
		/*
		 * UTF8 shifts and masks each byte for every code point
		 * 1110xxxx	10xxxxxx	10xxxxxx
		 */
		dst[0] = 0xe0 | (0x0f & (utf >> 12));
		dst[1] = 0x80 | (0x3f & (utf >> 6));
		dst[2] = 0x80 | (0x3f & (utf >> 0));
		*bytes_ptr = 3;

		if (log)
			LOG("converted U+%06" PRIx32 " to 0x%" PRIx8
			    " 0x%" PRIx8 " 0x%" PRIx8,
			    utf, dst[0], dst[1], dst[2]);
	} else if ((utf >= 0x10000) && (utf <= 0x10FFFF)) {
		/*
		 * UTF8 shifts and masks each byte for every code point
		 * 11110xxx	10xxxxxx	10xxxxxx	10xxxxxx
		 */
		dst[0] = 0xf0 | (0x07 & (utf >> 18));
		dst[1] = 0x80 | (0x3f & (utf >> 12));
		dst[2] = 0x80 | (0x3f & (utf >> 6));
		dst[3] = 0x80 | (0x3f & (utf >> 0));
		*bytes_ptr = 4;

		if (log)
			LOG("converted U+%06" PRIx32 " to 0x%" PRIx8
			    " 0x%" PRIx8 " 0x%" PRIx8 " 0x%" PRIx8,
			    utf, dst[0], dst[1], dst[2], dst[3]);
	} else {
		if (log)
			fatal_abort("should never happen");
		else
			abort();
	}

	return SLURM_SUCCESS;
}

extern int slurm_read_utf8_character(const utf8_t *src, const utf8_t *end,
				     utf_code_t *utf_ptr, int *bytes_ptr,
				     bool check_valid)
{
	int rc = EINVAL;
	const ptrdiff_t remaining = end - src;
	int64_t utf = 0;
	int bytes = 0;

	xassert(src);
	xassert(end);
	xassert(src <= end);
	xassert(remaining >= 0);
	xassert(bytes_ptr);

	if (!src || !end || (remaining <= 0)) {
		*bytes_ptr = 0;
		*utf_ptr = '\0';
		return EINVAL;
	}

	if ((*src >= 0) && (*src <= 0x7F)) {
		/* ASCII: 0xxxxxxx */

		bytes = 1;
		utf = *src;
	} else if ((*src & 0x80) == 0x00) {
		/* 0xxxxxxx */

		bytes = 1;
		utf = *src & 0x7f;
	} else if ((*src & 0xe0) == 0xc0) {
		/* 110xxxxx	10xxxxxx */

		if (remaining < 2) {
			rc = ESLURM_UTF8_READ_ILLEGAL_TERMINATION;
			bytes = 1;
			goto reject;
		}

		if ((src[1] & 0xc0) != 0x80) {
			rc = ESLURM_UTF8_INVALID_BYTE_2;
			goto reject;
		}

		bytes = 2;
		utf = ((src[0] & 0x1f) << 6) | (src[1] & 0x3f);

		/*
		 * Reject overlong encodings: a code point encoded in more bytes
		 * than the minimum required for its value is malformed UTF-8
		 * (RFC 3629 mandates the shortest form) and a classic way to
		 * smuggle characters such as '/', '.' or '"' past a byte-level
		 * filter.
		 */
		if (utf < 0x80) {
			rc = ESLURM_UTF8_INVALID_READ;
			goto reject;
		}

	} else if ((*src & 0xf0) == 0xe0) {
		/* 1110xxxx	10xxxxxx	10xxxxxx */

		if (remaining < 3) {
			rc = ESLURM_UTF8_READ_ILLEGAL_TERMINATION;
			bytes = 1;
			goto reject;
		}

		if ((src[1] & 0xc0) != 0x80) {
			rc = ESLURM_UTF8_INVALID_BYTE_2;
			bytes = 1;
			goto reject;
		}
		if ((src[2] & 0xc0) != 0x80) {
			rc = ESLURM_UTF8_INVALID_BYTE_3;
			bytes = 2;
			goto reject;
		}

		bytes = 3;
		utf = ((src[0] & 0x0f) << 12) | ((src[1] & 0x3f) << 6) |
		      (src[2] & 0x3f);

		if (utf < 0x800) {
			rc = ESLURM_UTF8_INVALID_READ;
			goto reject;
		}
	} else if ((*src & 0xf8) == 0xf0) {
		/* 11110xxx	10xxxxxx	10xxxxxx	10xxxxxx */

		if (remaining < 4) {
			rc = ESLURM_UTF8_READ_ILLEGAL_TERMINATION;
			bytes = 1;
			goto reject;
		}

		if ((src[1] & 0xc0) != 0x80) {
			rc = ESLURM_UTF8_INVALID_BYTE_2;
			bytes = 1;
			goto reject;
		}
		if ((src[2] & 0xc0) != 0x80) {
			rc = ESLURM_UTF8_INVALID_BYTE_3;
			bytes = 2;
			goto reject;
		}
		if ((src[3] & 0xc0) != 0x80) {
			rc = ESLURM_UTF8_INVALID_BYTE_4;
			bytes = 3;
			goto reject;
		}

		bytes = 4;
		utf = ((src[0] & 0x07) << 18) | ((src[1] & 0x3f) << 12) |
		      ((src[2] & 0x3f) << 6) | (src[3] & 0x3f);

		if (utf < 0x10000) {
			rc = ESLURM_UTF8_INVALID_READ;
			goto reject;
		}
	} else {
		rc = ESLURM_UTF8_INVALID_READ;
		goto reject;
	}

	/* utf8 is restricted to 4 bytes in Unicode 15 */
	xassert((bytes >= 0) && (bytes <= 4));

	/* check against invalid UTF codes */
	if (check_valid && (rc = utf_is_valid(utf)))
		goto reject;

	rc = SLURM_SUCCESS;
reject:
	if (rc) {
		/*
		 * Unicode 15.0 3.9: U+FFFD Substitution of Maximal Subparts:
		 *  Maximal subpart of an ill-formed subsequence: The longest
		 *  code unit subsequence starting at an unconvertible offset
		 *  that is either:
		 *	a. the initial subsequence of a well-formed
		 *  code unit sequence, or
		 *	b. a subsequence of length one.
		 */
		utf = UTF_REPLACEMENT_CODE;
		if (!bytes)
			bytes = 1;
	}

	*bytes_ptr = bytes;
	*utf_ptr = utf;
	return rc;
}
