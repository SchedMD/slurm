/*****************************************************************************\
 *  bitstring.c - bitmap manipulation functions
 *****************************************************************************
 *  See comments about origin, limitations, and internal structure in
 *  bitstring.h.
 *
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jim Garlick <garlick@llnl.gov>, Morris Jette <jette1@llnl.gov>
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

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/bitstring.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/* word of the bitstring bit is in */
#define	_bit_word(bit) 		(((bit) >> BITSTR_SHIFT) + BITSTR_OVERHEAD)

/* address of the byte containing bit */
#define _bit_byteaddr(name, bit) \
	((char *)((name) + BITSTR_OVERHEAD) + ((bit) >> BITSTR_SHIFT_WORD8))

/* mask for the bit within its word */
#ifdef SLURM_BIGENDIAN
#define	_bit_mask(bit) ((bitstr_t)1 << (BITSTR_MAXPOS - ((bit)&BITSTR_MAXPOS)))
#else
#define	_bit_mask(bit) ((bitstr_t)1 << ((bit)&BITSTR_MAXPOS))
#endif

/* mask for less significant bits within their word*/
#ifdef SLURM_BIGENDIAN
#define _bit_nmask(n) \
	((~((bitstr_t) 0)) << ((BITSTR_MAXPOS + 1) - ((n) & BITSTR_MAXPOS)))
#else
#define _bit_nmask(n) \
	(((bitstr_t) 1 << ((n) & BITSTR_MAXPOS)) - 1)
#endif

/* number of bits actually allocated to a bitstr */
#define _bitstr_bits(name) 	((name)[1])

/* magic cookie stored here */
#define _bitstr_magic(name) 	((name)[0])

/* words in a bitstring of nbits bits */
#define	_bitstr_words(nbits)	\
	((((nbits) + BITSTR_MAXPOS) >> BITSTR_SHIFT) + BITSTR_OVERHEAD)

/* check signature */
#define _assert_bitstr_valid(name) do { \
	xassert((name) != NULL); \
	xassert(_bitstr_magic(name) == BITSTR_MAGIC); \
} while (0)

/* check bit position */
#define _assert_bit_valid(name,bit) do { \
	xassert((bit) >= 0); \
	xassert((bit) < _bitstr_bits(name)); 	\
} while (0)


/* Ensure valid bitmap size, prevent overflow in buffer size calculation */
#define _assert_valid_size(bit) do {	\
	xassert((bit) > 0);		\
	xassert((bit) <= 0x40000000); 	\
} while (0)

/*
 * Define slurm-specific aliases for use by plugins, see slurm_xlator.h
 * for details.
 */
strong_alias(bit_alloc,		slurm_bit_alloc);
strong_alias(bit_test,		slurm_bit_test);
strong_alias(bit_set,		slurm_bit_set);
strong_alias(bit_clear,		slurm_bit_clear);
strong_alias(bit_nclear,	slurm_bit_nclear);
strong_alias(bit_nset,		slurm_bit_nset);
strong_alias(bit_set_all,	slurm_bit_set_all);
strong_alias(bit_clear_all,	slurm_bit_clear_all);
strong_alias(bit_ffc,		slurm_bit_ffc);
strong_alias(bit_ffs,		slurm_bit_ffs);
strong_alias(bit_size,		slurm_bit_size);
strong_alias(bit_and,		slurm_bit_and);
strong_alias(bit_not,		slurm_bit_not);
strong_alias(bit_or,		slurm_bit_or);
strong_alias(bit_set_count,	slurm_bit_set_count);
strong_alias(bit_set_count_range, slurm_bit_set_count_range);
strong_alias(bit_clear_count,	slurm_bit_clear_count);
strong_alias(bit_clear_count_range, slurm_bit_clear_count_range);
strong_alias(bit_nset_max_count,slurm_bit_nset_max_count);
strong_alias(bit_rotate_copy,	slurm_bit_rotate_copy);
strong_alias(bit_rotate,	slurm_bit_rotate);
strong_alias(bit_fmt,		slurm_bit_fmt);
strong_alias(bit_fmt_full,	slurm_bit_fmt_full);
strong_alias(bit_unfmt,		slurm_bit_unfmt);
strong_alias(bitfmt2int,	slurm_bitfmt2int);
strong_alias(bit_fmt_hexmask,	slurm_bit_fmt_hexmask);
strong_alias(bit_fmt_hexmask_trim, slurm_bit_fmt_hexmask_trim);
strong_alias(bit_unfmt_hexmask,	slurm_bit_unfmt_hexmask);
strong_alias(bit_fmt_binmask,	slurm_bit_fmt_binmask);
strong_alias(bit_unfmt_binmask,	slurm_bit_unfmt_binmask);
strong_alias(bit_fls,		slurm_bit_fls);
strong_alias(bit_fill_gaps,	slurm_bit_fill_gaps);
strong_alias(bit_super_set,	slurm_bit_super_set);
strong_alias(bit_overlap,	slurm_bit_overlap);
strong_alias(bit_overlap_any,	slurm_bit_overlap_any);
strong_alias(bit_equal,		slurm_bit_equal);
strong_alias(bit_copy,		slurm_bit_copy);
strong_alias(bit_pick_cnt,	slurm_bit_pick_cnt);
strong_alias(bit_nffc,		slurm_bit_nffc);
strong_alias(bit_noc,		slurm_bit_noc);
strong_alias(bit_nffs,		slurm_bit_nffs);
strong_alias(bit_copybits,	slurm_bit_copybits);
strong_alias(bit_get_bit_num,	slurm_bit_get_bit_num);
strong_alias(bit_get_pos_num,	slurm_bit_get_pos_num);

#ifdef SLURM_BIGENDIAN
static const char* hexmask_lookup[256] = {
	"00",	"80",	"40",	"C0",	"20",	"A0",	"60",	"E0",
	"10",	"90",	"50",	"D0",	"30",	"B0",	"70",	"F0",
	"08",	"88",	"48",	"C8",	"28",	"A8",	"68",	"E8",
	"18",	"98",	"58",	"D8",	"38",	"B8",	"78",	"F8",
	"04",	"84",	"44",	"C4",	"24",	"A4",	"64",	"E4",
	"14",	"94",	"54",	"D4",	"34",	"B4",	"74",	"F4",
	"0C",	"8C",	"4C",	"CC",	"2C",	"AC",	"6C",	"EC",
	"1C",	"9C",	"5C",	"DC",	"3C",	"BC",	"7C",	"FC",
	"02",	"82",	"42",	"C2",	"22",	"A2",	"62",	"E2",
	"12",	"92",	"52",	"D2",	"32",	"B2",	"72",	"F2",
	"0A",	"8A",	"4A",	"CA",	"2A",	"AA",	"6A",	"EA",
	"1A",	"9A",	"5A",	"DA",	"3A",	"BA",	"7A",	"FA",
	"06",	"86",	"46",	"C6",	"26",	"A6",	"66",	"E6",
	"16",	"96",	"56",	"D6",	"36",	"B6",	"76",	"F6",
	"0E",	"8E",	"4E",	"CE",	"2E",	"AE",	"6E",	"EE",
	"1E",	"9E",	"5E",	"DE",	"3E",	"BE",	"7E",	"FE",
	"01",	"81",	"41",	"C1",	"21",	"A1",	"61",	"E1",
	"11",	"91",	"51",	"D1",	"31",	"B1",	"71",	"F1",
	"09",	"89",	"49",	"C9",	"29",	"A9",	"69",	"E9",
	"19",	"99",	"59",	"D9",	"39",	"B9",	"79",	"F9",
	"05",	"85",	"45",	"C5",	"25",	"A5",	"65",	"E5",
	"15",	"95",	"55",	"D5",	"35",	"B5",	"75",	"F5",
	"0D",	"8D",	"4D",	"CD",	"2D",	"AD",	"6D",	"ED",
	"1D",	"9D",	"5D",	"DD",	"3D",	"BD",	"7D",	"FD",
	"03",	"83",	"43",	"C3",	"23",	"A3",	"63",	"E3",
	"13",	"93",	"53",	"D3",	"33",	"B3",	"73",	"F3",
	"0B",	"8B",	"4B",	"CB",	"2B",	"AB",	"6B",	"EB",
	"1B",	"9B",	"5B",	"DB",	"3B",	"BB",	"7B",	"FB",
	"07",	"87",	"47",	"C7",	"27",	"A7",	"67",	"E7",
	"17",	"97",	"57",	"D7",	"37",	"B7",	"77",	"F7",
	"0F",	"8F",	"4F",	"CF",	"2F",	"AF",	"6F",	"EF",
	"1F",	"9F",	"5F",	"DF",	"3F",	"BF",	"7F",	"FF",
};
#else
static const char* hexmask_lookup[256] = {
	"00",	"01",	"02",	"03",	"04",	"05",	"06",	"07",
	"08",	"09",	"0A",	"0B",	"0C",	"0D",	"0E",	"0F",
	"10",	"11",	"12",	"13",	"14",	"15",	"16",	"17",
	"18",	"19",	"1A",	"1B",	"1C",	"1D",	"1E",	"1F",
	"20",	"21",	"22",	"23",	"24",	"25",	"26",	"27",
	"28",	"29",	"2A",	"2B",	"2C",	"2D",	"2E",	"2F",
	"30",	"31",	"32",	"33",	"34",	"35",	"36",	"37",
	"38",	"39",	"3A",	"3B",	"3C",	"3D",	"3E",	"3F",
	"40",	"41",	"42",	"43",	"44",	"45",	"46",	"47",
	"48",	"49",	"4A",	"4B",	"4C",	"4D",	"4E",	"4F",
	"50",	"51",	"52",	"53",	"54",	"55",	"56",	"57",
	"58",	"59",	"5A",	"5B",	"5C",	"5D",	"5E",	"5F",
	"60",	"61",	"62",	"63",	"64",	"65",	"66",	"67",
	"68",	"69",	"6A",	"6B",	"6C",	"6D",	"6E",	"6F",
	"70",	"71",	"72",	"73",	"74",	"75",	"76",	"77",
	"78",	"79",	"7A",	"7B",	"7C",	"7D",	"7E",	"7F",
	"80",	"81",	"82",	"83",	"84",	"85",	"86",	"87",
	"88",	"89",	"8A",	"8B",	"8C",	"8D",	"8E",	"8F",
	"90",	"91",	"92",	"93",	"94",	"95",	"96",	"97",
	"98",	"99",	"9A",	"9B",	"9C",	"9D",	"9E",	"9F",
	"A0",	"A1",	"A2",	"A3",	"A4",	"A5",	"A6",	"A7",
	"A8",	"A9",	"AA",	"AB",	"AC",	"AD",	"AE",	"AF",
	"B0",	"B1",	"B2",	"B3",	"B4",	"B5",	"B6",	"B7",
	"B8",	"B9",	"BA",	"BB",	"BC",	"BD",	"BE",	"BF",
	"C0",	"C1",	"C2",	"C3",	"C4",	"C5",	"C6",	"C7",
	"C8",	"C9",	"CA",	"CB",	"CC",	"CD",	"CE",	"CF",
	"D0",	"D1",	"D2",	"D3",	"D4",	"D5",	"D6",	"D7",
	"D8",	"D9",	"DA",	"DB",	"DC",	"DD",	"DE",	"DF",
	"E0",	"E1",	"E2",	"E3",	"E4",	"E5",	"E6",	"E7",
	"E8",	"E9",	"EA",	"EB",	"EC",	"ED",	"EE",	"EF",
	"F0",	"F1",	"F2",	"F3",	"F4",	"F5",	"F6",	"F7",
	"F8",	"F9",	"FA",	"FB",	"FC",	"FD",	"FE",	"FF",
};
#endif

/*
 * Allocate a bitstring.
 *   nbits (IN)		valid bits in new bitstring, initialized to all clear
 *   RETURN		new bitstring
 */
bitstr_t *bit_alloc(bitoff_t nbits)
{
	bitstr_t *new;

	_assert_valid_size(nbits);
	new = xmalloc(_bitstr_words(nbits) * sizeof(bitstr_t));

	_bitstr_magic(new) = BITSTR_MAGIC;
	_bitstr_bits(new) = nbits;
	return new;
}

/*
 * Reallocate a bitstring (expand or contract size).
 *   b (IN)		pointer to old bitstring
 *   nbits (IN)		valid bits in new bitstr
 *   RETURN		new bitstring
 */
bitstr_t *slurm_bit_realloc(bitstr_t **b, bitoff_t nbits)
{
	_assert_bitstr_valid(*b);
	_assert_valid_size(nbits);

	xrecalloc(*b, _bitstr_words(nbits), sizeof(bitstr_t));

	_assert_bitstr_valid(*b);
	_bitstr_bits(*b) = nbits;

	return *b;
}

/*
 * Free a bitstr.
 *   b (IN/OUT)	bitstr to be freed
 */
void slurm_bit_free(bitstr_t **b)
{
	xassert(*b);
	xassert(_bitstr_magic(*b) == BITSTR_MAGIC);
	_bitstr_magic(*b) = 0;
	xfree(*b);
}

/*
 * Return the number of possible bits in a bitstring.
 *   b (IN)		bitstring to check
 *   RETURN		number of bits allocated
 */
bitoff_t
bit_size(bitstr_t *b)
{
	_assert_bitstr_valid(b);
	return _bitstr_bits(b);
}

/*
 * Is bit N of bitstring b set?
 *   b (IN)		bitstring to test
 *   bit (IN)		bit position to test
 *   RETURN		1 if bit set, 0 if clear
 */
int
bit_test(bitstr_t *b, bitoff_t bit)
{
	_assert_bitstr_valid(b);
	_assert_bit_valid(b, bit);
	return ((b[_bit_word(bit)] & _bit_mask(bit)) ? 1 : 0);
}

/*
 * Set bit N of bitstring.
 *   b (IN)		target bitstring
 *   bit (IN)		bit position to set
 */
void
bit_set(bitstr_t *b, bitoff_t bit)
{
	_assert_bitstr_valid(b);
	_assert_bit_valid(b, bit);
	b[_bit_word(bit)] |= _bit_mask(bit);
}

/*
 * Clear bit N of bitstring
 *   b (IN)		target bitstring
 *   bit (IN)		bit position to clear
 */
void
bit_clear(bitstr_t *b, bitoff_t bit)
{
	_assert_bitstr_valid(b);
	_assert_bit_valid(b, bit);
	b[_bit_word(bit)] &= ~_bit_mask(bit);
}

/*
 * Set bits start ... stop in bitstring
 *   b (IN)		target bitstring
 *   start (IN)		starting (low numbered) bit position
 *   stop (IN)		ending (higher numbered) bit position
 */
void
bit_nset(bitstr_t *b, bitoff_t start, bitoff_t stop)
{
	_assert_bitstr_valid(b);
	_assert_bit_valid(b, start);
	_assert_bit_valid(b, stop);

	while (start <= stop && start % 8 > 0) 	     /* partial first byte? */
		bit_set(b, start++);
	while (stop >= start && (stop+1) % 8 > 0)    /* partial last byte? */
		bit_set(b, stop--);
	if (stop > start) {                          /* now do whole bytes */
		xassert((stop-start+1) % 8 == 0);
		memset(_bit_byteaddr(b, start), 0xff, (stop-start+1) / 8);
	}
}

/*
 * Clear bits start ... stop in bitstring
 *   b (IN)		target bitstring
 *   start (IN)		starting (low numbered) bit position
 *   stop (IN)		ending (higher numbered) bit position
 */
void
bit_nclear(bitstr_t *b, bitoff_t start, bitoff_t stop)
{
	_assert_bitstr_valid(b);
	_assert_bit_valid(b, start);
	_assert_bit_valid(b, stop);

	while (start <= stop && start % 8 > 0) 	/* partial first byte? */
		bit_clear(b, start++);
	while (stop >= start && (stop+1) % 8 > 0)/* partial last byte? */
		bit_clear(b, stop--);
	if (stop > start) {			/* now do whole bytes */
		xassert((stop-start+1) % 8 == 0);
		memset(_bit_byteaddr(b, start), 0, (stop-start+1) / 8);
	}
}

/*
 * Set all bits in bitstring
 *   b (IN)		target bitstring
 */
void
bit_set_all(bitstr_t *b)
{
	bit_nset(b, 0, bit_size(b)-1);
}

/*
 * Clear all bits in bitstring
 *   b (IN)		target bitstring
 */
void
bit_clear_all(bitstr_t *b)
{
	bit_nclear(b, 0, bit_size(b)-1);
}

/*
 * Find first bit clear in bitstring.
 *   b (IN)		bitstring to search
 *   nbits (IN)		number of bits to search
 *   RETURN      	resulting bit position (-1 if none found)
 */
bitoff_t
bit_ffc(bitstr_t *b)
{
	bitoff_t bit = 0, value = -1;

	_assert_bitstr_valid(b);

	while (bit < _bitstr_bits(b) && value == -1) {
		int32_t word = _bit_word(bit);

		if (b[word] == BITSTR_MAXVAL) {
			bit += sizeof(bitstr_t)*8;
			continue;
		}
		while (bit < _bitstr_bits(b) && _bit_word(bit) == word) {
			if (!bit_test(b, bit)) {
				value = bit;
				break;
			}
			bit++;
		}
	}
	return value;
}

/* Find the first n contiguous bits clear in b.
 *   b (IN)             bitstring to search
 *   n (IN)             number of bits needed
 *   RETURN             position of first bit in range (-1 if none found)
 */
bitoff_t
bit_nffc(bitstr_t *b, int32_t n)
{
	bitoff_t value = -1;
	bitoff_t bit;
	int32_t cnt = 0;

	_assert_bitstr_valid(b);
	xassert(n > 0 && n < _bitstr_bits(b));

	for (bit = 0; bit < _bitstr_bits(b); bit++) {
		if (bit_test(b, bit)) {		/* fail */
			cnt = 0;
		} else {
			cnt++;
			if (cnt >= n) {
				value = bit - (cnt - 1);
				break;
			}
		}
	}

	return value;
}

/* Find n contiguous bits clear in b starting at some offset.
 *   b (IN)             bitstring to search
 *   n (IN)             number of bits needed
 *   seed (IN)          position at which to begin search
 *   RETURN             position of first bit in range (-1 if none found)
 */
bitoff_t
bit_noc(bitstr_t *b, int32_t n, int32_t seed)
{
	bitoff_t value = -1;
	bitoff_t bit;
	int32_t cnt = 0;

	_assert_bitstr_valid(b);
	xassert(n > 0 && n <= _bitstr_bits(b));

	if ((seed + n) >= _bitstr_bits(b))
		seed = _bitstr_bits(b);	/* skip offset test, too small */

	for (bit = seed; bit < _bitstr_bits(b); bit++) {	/* start at offset */
		if (bit_test(b, bit)) {		/* fail */
			cnt = 0;
		} else {
			cnt++;
			if (cnt >= n) {
				value = bit - (cnt - 1);
				return value;
			}
		}
	}

	cnt = 0;	/* start at beginning */
	for (bit = 0; bit < _bitstr_bits(b); bit++) {
		if (bit_test(b, bit)) {		/* fail */
			if (bit >= seed)
				break;
			cnt = 0;
		} else {
			cnt++;
			if (cnt >= n) {
				value = bit - (cnt - 1);
				return value;
			}
		}
	}

	return -1;
}

/* Find the first n contiguous bits set in b.
 *   b (IN)             bitstring to search
 *   n (IN)             number of bits needed
 *   RETURN             position of first bit in range (-1 if none found)
 */
bitoff_t
bit_nffs(bitstr_t *b, int32_t n)
{
	bitoff_t value = -1;
	bitoff_t bit;
	int32_t cnt = 0;

	_assert_bitstr_valid(b);
	xassert(n > 0 && n <= _bitstr_bits(b));

	for (bit = 0; bit <= _bitstr_bits(b) - n; bit++) {
		if (!bit_test(b, bit)) {	/* fail */
			cnt = 0;
		} else {
			cnt++;
			if (cnt >= n) {
				value = bit - (cnt - 1);
				break;
			}
		}
	}

	return value;
}

/*
 * Find first bit set in b.
 *   b (IN)		bitstring to search
 *   RETURN 		resulting bit position (-1 if none found)
 */
bitoff_t
bit_ffs(bitstr_t *b)
{
	bitoff_t bit = 0, value = -1;

	_assert_bitstr_valid(b);

	while (bit < _bitstr_bits(b) && value == -1) {
		int32_t word = _bit_word(bit);

		if (b[word] == 0) {
			bit += sizeof(bitstr_t)*8;
			continue;
		}
#if HAVE___BUILTIN_CLZLL && (defined SLURM_BIGENDIAN)
		value = bit + __builtin_clzll(b[word]);
#elif HAVE___BUILTIN_CTZLL && (!defined SLURM_BIGENDIAN)
		value = bit + __builtin_ctzll(b[word]);
#else
		while (bit < _bitstr_bits(b) && _bit_word(bit) == word) {
			if (bit_test(b, bit)) {
				value = bit;
				break;
			}
			bit++;
		}
#endif
	}
	if (value < _bitstr_bits(b))
		return value;
	else
		return -1;
}

/*
 * Find last bit set in b.
 *   b (IN)		bitstring to search
 *   RETURN 		resulting bit position (-1 if none found)
 */
bitoff_t
bit_fls(bitstr_t *b)
{
	bitoff_t bit, value = -1;
	int32_t word;

	_assert_bitstr_valid(b);

	if (_bitstr_bits(b) == 0)	/* empty bitstring */
		return -1;

	bit = _bitstr_bits(b) - 1;	/* zero origin */

	while (bit >= 0 && 		/* test partial words */
		(_bit_word(bit) == _bit_word(bit + 1))) {
		if (bit_test(b, bit)) {
			value = bit;
			break;
		}
		bit--;
	}
	while (bit >= 0 && value == -1) {	/* test whole words */
		word = _bit_word(bit);
		if (b[word] == 0) {
			bit -= sizeof(bitstr_t) * 8;
			continue;
		}
#if HAVE___BUILTIN_CTZLL && (defined SLURM_BIGENDIAN)
		value = bit - __builtin_ctzll(b[word]);
#elif HAVE___BUILTIN_CLZLL && (!defined SLURM_BIGENDIAN)
		value = bit - __builtin_clzll(b[word]);
#else
		while (bit >= 0) {
			if (bit_test(b, bit)) {
				value = bit;
				break;
			}
			bit--;
		}
#endif
	}
	return value;
}

/*
 * set all bits between the first and last bits set (i.e. fill in the gaps
 *	to make set bits contiguous)
 */
void
bit_fill_gaps(bitstr_t *b)
{
	bitoff_t first, last;

	_assert_bitstr_valid(b);

	first = bit_ffs(b);
	if (first == -1)
		return;

	last = bit_fls(b);
	bit_nset(b, first, last);

	return;
}

/*
 * return 1 if all bits set in b1 are also set in b2, 0 0therwise
 */
int
bit_super_set(bitstr_t *b1, bitstr_t *b2)
{
	bitoff_t bit;

	_assert_bitstr_valid(b1);
	_assert_bitstr_valid(b2);
	xassert(_bitstr_bits(b1) == _bitstr_bits(b2));

	for (bit = 0; bit < _bitstr_bits(b1); bit += sizeof(bitstr_t) * 8) {
		if (b1[_bit_word(bit)] != (b1[_bit_word(bit)] &
		                           b2[_bit_word(bit)])) {
			bitstr_t mask;
			if ((bit + sizeof(bitstr_t) * 8) <= _bitstr_bits(b1))
				return 0;
			mask = _bit_nmask(_bitstr_bits(b1));
			if ((b1[_bit_word(bit)] & mask) != (b1[_bit_word(bit)] &
							    b2[_bit_word(bit)] &
							    mask))
				return 0;
		}

	}

	return 1;
}

/*
 * return 1 if b1 and b2 are identical, 0 otherwise
 */
extern int
bit_equal(bitstr_t *b1, bitstr_t *b2)
{
	bitoff_t bit;

	_assert_bitstr_valid(b1);
	_assert_bitstr_valid(b2);

	if (_bitstr_bits(b1) != _bitstr_bits(b2))
		return 0;

	for (bit = 0; bit < _bitstr_bits(b1); bit += sizeof(bitstr_t)*8) {
		if (b1[_bit_word(bit)] != b2[_bit_word(bit)])
			return 0;
	}

	return 1;
}



/*
 * b1 &= b2 as many bits as both bitstr_t have
 *   b1 (IN/OUT)	first string
 *   b2 (IN)		second bitstring
 */
void
bit_and(bitstr_t *b1, bitstr_t *b2)
{
	bitoff_t bit, bit_cnt;

	_assert_bitstr_valid(b1);
	_assert_bitstr_valid(b2);

	bit_cnt = MIN(_bitstr_bits(b1), _bitstr_bits(b2));
	for (bit = 0; bit < bit_cnt; bit += sizeof(bitstr_t)*8)
		b1[_bit_word(bit)] &= b2[_bit_word(bit)];
}

/*
 * b1 &= ~b2 as many bits as both bitstr_t have
 * b1 (IN/OUT)
 * b2 (IN)
 */
void bit_and_not(bitstr_t *b1, bitstr_t *b2)
{
	bitoff_t bit, bit_cnt;

	_assert_bitstr_valid(b1);
	_assert_bitstr_valid(b2);

	bit_cnt = MIN(_bitstr_bits(b1), _bitstr_bits(b2));
	for (bit = 0; bit < bit_cnt; bit += sizeof(bitstr_t)*8)
		b1[_bit_word(bit)] &= ~b2[_bit_word(bit)];
}

/*
 * b1 = ~b1		one's complement
 *   b1 (IN/OUT)	first bitmap
 */
void
bit_not(bitstr_t *b)
{
	bitoff_t bit;

	_assert_bitstr_valid(b);

	for (bit = 0; bit < _bitstr_bits(b); bit += sizeof(bitstr_t)*8)
		b[_bit_word(bit)] = ~b[_bit_word(bit)];
}

/*
 * b1 |= b2 as many bits as both bitstr_t have
 *   b1 (IN/OUT)	first bitmap
 *   b2 (IN)		second bitmap
 */
void
bit_or(bitstr_t *b1, bitstr_t *b2)
{
	bitoff_t bit, bit_cnt;

	_assert_bitstr_valid(b1);
	_assert_bitstr_valid(b2);

	bit_cnt = MIN(_bitstr_bits(b1), _bitstr_bits(b2));
	for (bit = 0; bit < bit_cnt; bit += sizeof(bitstr_t)*8)
		b1[_bit_word(bit)] |= b2[_bit_word(bit)];
}

/*
 * b1 |= ~b2 as many bits as both bitstr_t have
 * b1 (IN/OUT)
 * b2 (IN)
 */
void bit_or_not(bitstr_t *b1, bitstr_t *b2)
{
	bitoff_t bit, bit_cnt;

	_assert_bitstr_valid(b1);
	_assert_bitstr_valid(b2);

	bit_cnt = MIN(_bitstr_bits(b1), _bitstr_bits(b2));
	for (bit = 0; bit < bit_cnt; bit += sizeof(bitstr_t)*8)
		b1[_bit_word(bit)] |= ~b2[_bit_word(bit)];
}

/*
 * return a copy of the supplied bitmap
 */
bitstr_t *
bit_copy(bitstr_t *b)
{
	bitstr_t *new;
	int32_t newsize_bits;
	size_t len = 0;  /* Number of bytes to memcpy() */

	_assert_bitstr_valid(b);

	newsize_bits  = bit_size(b);
	len = (_bitstr_words(newsize_bits) - BITSTR_OVERHEAD)*sizeof(bitstr_t);
	new = bit_alloc(newsize_bits);
	if (new)
		memcpy(&new[BITSTR_OVERHEAD], &b[BITSTR_OVERHEAD], len);

	return new;
}

void
bit_copybits(bitstr_t *dest, bitstr_t *src)
{
	int32_t len;

	_assert_bitstr_valid(dest);
	_assert_bitstr_valid(src);
	xassert(bit_size(src) == bit_size(dest));

	len = (_bitstr_words(bit_size(src)) - BITSTR_OVERHEAD)*sizeof(bitstr_t);
	memcpy(&dest[BITSTR_OVERHEAD], &src[BITSTR_OVERHEAD], len);
}

#ifdef HAVE___BUILTIN_POPCOUNTLL
#define hweight __builtin_popcountll
#else
/*
 * Returns the hamming weight (i.e. the number of bits set) in a word.
 * NOTE: This routine borrowed from Linux 4.9 <tools/lib/hweight.c>.
 */
static uint64_t
hweight(uint64_t w)
{
        w -= (w >> 1) & 0x5555555555555555ul;
        w =  (w & 0x3333333333333333ul) + ((w >> 2) & 0x3333333333333333ul);
        w =  (w + (w >> 4)) & 0x0f0f0f0f0f0f0f0ful;
        return (w * 0x0101010101010101ul) >> 56;
}
#endif

/*
 * Count the number of bits set in bitstring.
 *   b (IN)		bitstring to check
 *   RETURN		count of set bits
 */
int32_t
bit_set_count(bitstr_t *b)
{
	int32_t count = 0;
	bitoff_t bit, bit_cnt;
	int32_t word_size = sizeof(bitstr_t) * 8;

	_assert_bitstr_valid(b);

	bit_cnt = _bitstr_bits(b);
	for (bit = 0; (bit + word_size) <= bit_cnt; bit += word_size) {
		count += hweight(b[_bit_word(bit)]);
	}
	if (bit < bit_cnt) {
		uint64_t mask = _bit_nmask(bit_cnt);
		count += hweight(b[_bit_word(bit)] & mask);
	}
	return count;
}

/*
 * Count the number of bits set in a range of bitstring.
 *   b (IN)		bitstring to check
 *   start (IN) first bit to check
 *   end (IN)	last bit to check+1
 *   RETURN		count of set bits
 */
int32_t
bit_set_count_range(bitstr_t *b, int32_t start, int32_t end)
{
	int32_t count = 0, eow;
	bitoff_t bit;
	const int32_t word_size = sizeof(bitstr_t) * 8;

	_assert_bitstr_valid(b);
	_assert_bit_valid(b,start);

	end = MIN(end, _bitstr_bits(b));
	/* end of word */
	eow = (((start + BITSTR_MAXPOS) >> BITSTR_SHIFT) << BITSTR_SHIFT);

	bit = start;
	if ((start < eow) && (eow <= end)) {
		uint64_t mask = ~_bit_nmask(start);
		count += hweight(b[_bit_word(bit)] & mask);
		bit = eow;
	} else if (eow > start) {
		uint64_t mask = ~_bit_nmask(start);
		mask &= _bit_nmask(end);
		count += hweight(b[_bit_word(bit)] & mask);
		bit = eow;
	}
	for (; (bit + word_size) <= end ; bit += word_size) {
		count += hweight(b[_bit_word(bit)]);
	}
	if (bit < end) {
		uint64_t mask = _bit_nmask(end);
		count += hweight(b[_bit_word(bit)] & mask);
	}

	return count;
}

static int32_t _bit_overlap_internal(bitstr_t *b1, bitstr_t *b2, bool count_it)
{
	int32_t count = 0;
	int64_t anded;
	bitoff_t bit, bit_cnt;
	int32_t word_size = sizeof(bitstr_t) * 8;

	_assert_bitstr_valid(b1);
	_assert_bitstr_valid(b2);
	xassert(_bitstr_bits(b1) == _bitstr_bits(b2));

	bit_cnt = _bitstr_bits(b1);
	for (bit = 0; bit < bit_cnt; bit += word_size) {
		if ((bit + word_size - 1) >= bit_cnt)
			break;
		anded = b1[_bit_word(bit)] & b2[_bit_word(bit)];
		if (count_it)
			count += hweight(anded);
		else if (anded)
			return 1;
	}

	if (bit < bit_cnt) {
		uint64_t mask = _bit_nmask(bit_cnt);
		anded = b1[_bit_word(bit)] & b2[_bit_word(bit)] & mask;
		if (count_it)
			count += hweight(anded);
		else if (anded)
			return 1;
	}

	return count;
}

/*
 * return number of bits set in b1 that are also set in b2, 0 if no overlap
 */
extern int32_t bit_overlap(bitstr_t *b1, bitstr_t *b2)
{
	return _bit_overlap_internal(b1, b2, 1);
}

/*
 * return 1 if there is at least one bit set in b1 that is also set in b2, 0 if
 * no overlap
 */
extern int32_t bit_overlap_any(bitstr_t *b1, bitstr_t *b2)
{
	return _bit_overlap_internal(b1, b2, 0);
}

/*
 * Count the number of bits clear in bitstring.
 *   b (IN)		bitstring to check
 *   RETURN		count of clear bits
 */
int32_t
bit_clear_count(bitstr_t *b)
{
	_assert_bitstr_valid(b);
	return (_bitstr_bits(b) - bit_set_count(b));
}

/*
 * Count the number of bits clear in a range of bitstring.
 *   b (IN)		bitstring to check
 *   start (IN) first bit to check
 *   end (IN)	last bit to check+1
 *   RETURN		count of set bits
 */
int32_t
bit_clear_count_range(bitstr_t *b, int32_t start, int32_t end)
{
	_assert_bitstr_valid(b);
	int diff = end - start;

	if (diff < 1)
		return 0;

	return (diff - bit_set_count_range(b, start, end));
}

/* Return the count of the largest number of contiguous bits set in b.
 *   b (IN)             bitstring to search
 *   RETURN             the largest number of contiguous bits set in b
 */
int32_t
bit_nset_max_count(bitstr_t *b)
{
	bitoff_t bit;
	int32_t  cnt = 0;
	int32_t  maxcnt = 0;
	uint32_t bitsize;

	_assert_bitstr_valid(b);
	bitsize = _bitstr_bits(b);

	for (bit = 0; bit < bitsize; bit++) {
		if (!bit_test(b, bit)) {	/* no longer continuous */
			cnt = 0;
		} else {
			cnt++;
			if (cnt > maxcnt) {
				maxcnt = cnt;
			}
		}
		if (cnt == 0 && ((bitsize - bit) < maxcnt)) {
		    	break;			/* already found max */
		}
	}

	return maxcnt;
}

/*
 * rotate b1 by n bits returning a rotated copy
 *   b1 (IN)		bitmap to rotate
 *   n  (IN)		rotation distance (+ = rotate left, - = rotate right)
 *   nbits (IN)		size of the new copy (in which the rotation occurs)
 *				note: nbits must be >= bit_size(b1)
 *   RETURN		new rotated bitmap
 */
bitstr_t *
bit_rotate_copy(bitstr_t *b1, int32_t n, bitoff_t nbits)
{
	bitoff_t bit, dst;
	bitstr_t *new;
	bitoff_t bitsize;
	bitoff_t deltasize, wrapbits;

	_assert_bitstr_valid(b1);
	bitsize = bit_size(b1);
	xassert(nbits >= bitsize);
	deltasize = nbits - bitsize;

	/* normalize n to a single positive rotation */
	n = n % nbits;
	if (n < 0) {
		n += nbits;
	}

	wrapbits = 0;	/* number of bits that will wrap around */
	if (n > deltasize) {
		wrapbits = n - deltasize;
	}

	new = bit_alloc(nbits);
	bit_nclear(new,0,nbits-1);

	/* bits shifting up */
	for (bit = 0; bit < (bitsize-wrapbits); bit++) {
		if (bit_test(b1, bit))
			bit_set(new, bit+n);
	}
	/* continue bit into wrap-around bits, if any */
	for (dst = 0; bit < bitsize; bit++, dst++) {
		if (bit_test(b1, bit))
			bit_set(new, dst);
	}
	return(new);
}

/*
 * rotate b1 by n bits
 *   b1 (IN/OUT)	bitmap to rotate
 *   n  (IN)		rotation distance (+ = rotate left, - = rotate right)
 */
void
bit_rotate(bitstr_t *b1, int32_t n)
{
	uint32_t bitsize;
	bitstr_t *new;

	if (n == 0)
		return;

	_assert_bitstr_valid(b1);
	bitsize = bit_size(b1);

	new = bit_rotate_copy(b1, n, bitsize);
	bit_copybits(b1, new);
	bit_free(new);
}

/*
 * build a bitmap containing the first nbits of b which are set
 */
bitstr_t *
bit_pick_cnt(bitstr_t *b, bitoff_t nbits)
{
	bitoff_t bit = 0, new_bits, count = 0;
	bitstr_t *new;
	int32_t word_size = sizeof(bitstr_t) * 8;

	_assert_bitstr_valid(b);

	if (_bitstr_bits(b) < nbits)
		return NULL;

	new = bit_alloc(bit_size(b));
	if (new == NULL)
		return NULL;

	while ((bit < _bitstr_bits(b)) && (count < nbits)) {
		int32_t word = _bit_word(bit);

		if (b[word] == 0) {
			bit += word_size;
			continue;
		}

		new_bits = hweight(b[word]);
		if (((count + new_bits) <= nbits) &&
		    ((bit + word_size - 1) < _bitstr_bits(b))) {
			new[word] = b[word];
			count += new_bits;
			bit += word_size;
			continue;
		}
		while ((bit < _bitstr_bits(b)) && (count < nbits)) {
			if (bit_test(b, bit)) {
				bit_set(new, bit);
				count++;
			}
			bit++;
		}
	}
	if (count < nbits) {
		bit_free (new);
		new = NULL;
	}

	return new;
}

/*
 * Convert to range string format, e.g. 0-5,42
 */
char *bit_fmt(char *str, int32_t len, bitstr_t *b)
{
	int32_t word;
	char *comma = "";
	bitoff_t bit;

	_assert_bitstr_valid(b);
	xassert(len > 0);
	*str = '\0';
	for (bit = 0; bit < _bitstr_bits(b); ) {
		word = _bit_word(bit);
		if (b[word] == 0) {
			bit += sizeof(bitstr_t)*8;
			continue;
		}

		if (bit_test(b, bit)) {
			int32_t ret, size;
			bitoff_t start = bit;

			while (bit+1 < _bitstr_bits(b) && bit_test(b, bit+1)) {
				bit++;
			}
			size = strlen(str);
			if (bit == start) {	/* add single bit position */
				ret = snprintf(str + size, len - size,
				               "%s%"BITSTR_FMT"", comma, start);
			} else { 		/* add bit position range */
				ret = snprintf(str + size, len - size,
				               "%s%"BITSTR_FMT"-%"BITSTR_FMT"",
					       comma, start, bit);
			}
			comma = ",";

			xassert(ret != -1);
			if (ret == -1)
				error("failed to write to string -- this should never happen");
		}
		bit++;
	}
	return str;
}

/*
 * Convert to range string format, e.g. 0-5,42 with no length restriction
 * Call xfree() on return value to avoid memory leak
 */
char *bit_fmt_full(bitstr_t *b)
{
	int32_t word;
	bitoff_t start, bit;
	char *str = NULL, *comma = "";
	_assert_bitstr_valid(b);

	for (bit = 0; bit < _bitstr_bits(b); ) {
		word = _bit_word(bit);
		if (b[word] == 0) {
			bit += sizeof(bitstr_t)*8;
			continue;
		}

		if (bit_test(b, bit)) {
			start = bit;
			while (bit+1 < _bitstr_bits(b) && bit_test(b, bit+1)) {
				bit++;
			}
			if (bit == start)	/* add single bit position */
				xstrfmtcat(str, "%s%"BITSTR_FMT"",
					   comma, start);
			else 			/* add bit position range */
				xstrfmtcat(str, "%s%"BITSTR_FMT"-%"BITSTR_FMT,
					   comma, start, bit);
			comma = ",";
		}
		bit++;
	}

	return str;
}

/*
 * Convert to range string format, e.g. 0-5,42 with no length restriction
 * offset IN - location of bit zero
 * len IN - number of bits to test
 * Call xfree() on return value to avoid memory leak
 */
char *bit_fmt_range(bitstr_t *b, int offset, int len)
{
	int32_t word;
	bitoff_t start, fini_bit, bit;
	char *str = NULL, *comma = "";
	_assert_bitstr_valid(b);

	fini_bit = MIN(_bitstr_bits(b), offset + len);
	for (bit = offset; bit < fini_bit; ) {
		word = _bit_word(bit);
		if (b[word] == 0) {
			bit += sizeof(bitstr_t) * 8;
			continue;
		}

		if (bit_test(b, bit)) {
			start = bit;
			while ((bit + 1 < fini_bit) && bit_test(b, bit + 1)) {
				bit++;
			}
			if (bit == start) {	/* add single bit position */
				xstrfmtcat(str, "%s%"BITSTR_FMT"",
					   comma, (start - offset));
			} else {		/* add bit position range */
				xstrfmtcat(str, "%s%"BITSTR_FMT"-%"BITSTR_FMT,
					   comma, (start - offset),
					   (bit - offset));
			}
			comma = ",";
		}
		bit++;
	}

	return str;
}

/*
 * Convert range string format, e.g. "0-5,42" to bitmap
 * Ret 0 on success, -1 on error
 */
int
bit_unfmt(bitstr_t *b, char *str)
{
	int32_t *intvec;
	int rc = 0;

	_assert_bitstr_valid(b);
	if (!str || str[0] == '\0')	/* no bits set */
		return rc;
	intvec = bitfmt2int(str);
	if (intvec == NULL)
		return -1;
	rc = inx2bitstr(b, intvec);
	xfree(intvec);
	return rc;
}

/*
 * bitfmt2int - convert a string describing bitmap (output from bit_fmt,
 *	e.g. "0-30,45,50-60") into an array of integer (start/end) pairs
 *	terminated by -1 (e.g. "0, 30, 45, 45, 50, 60, -1")
 *	Also supports the "1-17:4" step format ("1, 5, 9, 13, 17, -1").
 * input: bitmap string as produced by bitstring.c : bitfmt
 * output: an array of integers
 * NOTE: the caller must xfree the returned memory
 */
int32_t *bitfmt2int(char *bit_str_ptr)
{
	int32_t *bit_int_ptr, i, bit_inx, size, sum, start_val;
	char *tmp = NULL;
	int32_t start_task_id = -1;
	int32_t end_task_id = -1;
	int32_t step = -1;

	if (bit_str_ptr == NULL)
		return NULL;
	if (!(xstrchr(bit_str_ptr, ':'))) {
		size = strlen(bit_str_ptr) + 1;
		/* more than enough space */
		bit_int_ptr = xmalloc(sizeof(int32_t) * (size * 2 + 1));
		bit_inx = sum = 0;
		start_val = -1;
		for (i = 0; i < size; i++) {
			if (bit_str_ptr[i] >= '0' &&
			    bit_str_ptr[i] <= '9') {
				sum = (sum * 10) + (bit_str_ptr[i] - '0');
			} else if (bit_str_ptr[i] == '-') {
				start_val = sum;
				sum = 0;
			} else if (bit_str_ptr[i] == ',' ||
				   bit_str_ptr[i] == '\0') {
				if (i == 0)
					break;
				if (start_val == -1)
					start_val = sum;
				bit_int_ptr[bit_inx++] = start_val;
				bit_int_ptr[bit_inx++] = sum;
				start_val = -1;
				sum = 0;
			}
		}
		xassert(bit_inx < (size * 2 + 1));
	} else {	/* handle step format */
		start_task_id = strtol(bit_str_ptr, &tmp, 10);
		if (*tmp != '-')
			return NULL;
		end_task_id = strtol(tmp + 1, &tmp, 10);
		if (*tmp != ':')
			return NULL;
		step = strtol(tmp + 1, &tmp, 10);
		if (*tmp != '\0')
			return NULL;
		if (end_task_id < start_task_id || step <= 0)
			return NULL;

		size = ((end_task_id - start_task_id) / step) + 1;
		bit_int_ptr = xmalloc(sizeof(int32_t) * (size * 2 + 1));
		bit_inx = 0;
		for(i = start_task_id; i < end_task_id; i += step) {
			bit_int_ptr[bit_inx++] = i;     /* start of pair */
			bit_int_ptr[bit_inx++] = i;     /* end of pair */
		}
	}
	bit_int_ptr[bit_inx] = -1;
	return bit_int_ptr;
}

/*
 * intbitfmt - convert a array of integer (start/end) pairs
 *	terminated by -1 (e.g. "0, 30, 45, 45, 50, 60, -1") to a
 *	string describing bitmap (output from bit_fmt, e.g. "0-30,45,50-60")
 * input: int array
 * output: char *
 * NOTE: the caller must xfree the returned memory
 */
char *
inx2bitfmt (int32_t *inx)
{
	int32_t j = 0;
	char *bit_char_ptr = NULL;

	if (inx == NULL)
		return NULL;

	while (inx[j] >= 0) {
		if (bit_char_ptr)
			xstrfmtcat(bit_char_ptr, ",%d-%d", inx[j], inx[j+1]);
		else
			xstrfmtcat(bit_char_ptr, "%d-%d", inx[j], inx[j+1]);
		j += 2;
	}

	return bit_char_ptr;
}

int inx2bitstr(bitstr_t *b, int32_t *inx)
{
	int32_t *p, bit_cnt;
	int rc = 0;

	xassert(b);
	xassert(inx);

	bit_cnt = _bitstr_bits(b);
	if (bit_cnt > 0)
		bit_nclear(b, 0, bit_cnt - 1);
	for (p = inx; *p != -1; p += 2) {
		if ((*p < 0) || (*p >= bit_cnt) ||
		    (*(p + 1) < 0) || (*(p + 1) >= bit_cnt)) {
			rc = -1;
			break;
		}
		bit_nset(b, *p, *(p + 1));
	}
	return rc;
}

/*
 * convert a bitstring to inx format
 * returns an xmalloc()'d array of int32_t that must be xfree()'d
 */
int32_t *bitstr2inx(bitstr_t *b)
{
	bitoff_t start, bit, pos = 0;
	int32_t *bit_inx;

	if (!b) {
		bit_inx = xmalloc(sizeof(int32_t));
		bit_inx[0] = -1;
		return bit_inx;
	}

	/* worst case: every other bit set, resulting in an array of length
	 * bitstr_bits(b) + 1 (if an odd number of elements)
	 * + 1 (for trailing -1) */
	bit_inx = xmalloc_nz(sizeof(int32_t) * (_bitstr_bits(b) + 2));

	for (bit = 0; bit < _bitstr_bits(b); ) {
		/* skip past empty words */
		if (!b[_bit_word(bit)]) {
			bit += sizeof(bitstr_t) * 8;
			continue;
		}

		if (bit_test(b, bit)) {
			start = bit;
			while (bit + 1 < _bitstr_bits(b)
			       && bit_test(b, bit + 1))
				bit++;
			bit_inx[pos++] = start;
			bit_inx[pos++] = bit;
		}
		bit++;
	}
	/* terminate array with -1 */
	bit_inx[pos] = -1;

	return bit_inx;
}

/* If trim_output is true, strip off leading zeros from result. */
static char *_bit_fmt_hexmask(bitstr_t *bitmap, bool trim_output)
{
	char *retstr, *ptr;
	char current;
	const int32_t word_size = sizeof(bitstr_t) * 8;
	bitoff_t i, j, word;
	bitoff_t bitsize;

	if (trim_output)
		bitsize = bit_fls(bitmap) + 1;
	else
		bitsize = bit_size(bitmap);

	if (!bitsize)
		return xstrdup("0x0");

	/* 4 bits per ASCII '0'-'F' */
	bitoff_t charsize = (bitsize + 3) / 4;

	retstr = xmalloc(charsize + 3);

	retstr[0] = '0';
	retstr[1] = 'x';
	retstr[charsize + 2] = '\0';
	ptr = &retstr[charsize + 1];

	for (i = 0; i < bitsize;) {
		if (i + word_size <= bitsize) {
			word = _bit_word(i);
			for (j = 0; j < sizeof(bitstr_t); j++) {
				uint8_t idx = ((uint8_t *)(&(bitmap[word])))[j];
				*ptr = hexmask_lookup[idx][1];
				ptr--;
				*ptr = hexmask_lookup[idx][0];
				ptr--;
			}
			i += word_size;
		} else {
			current = 0;
			if (                 bit_test(bitmap,i++))
				current |= 0x1;
			if ((i < bitsize) && bit_test(bitmap,i++))
				current |= 0x2;
			if ((i < bitsize) && bit_test(bitmap,i++))
				current |= 0x4;
			if ((i < bitsize) && bit_test(bitmap,i++))
				current |= 0x8;

			if (current <= 9) {
				current += '0';
			} else {
				current += 'A' - 10;
			}
			*ptr-- = current;
		}
	}

	return retstr;
}

/* bit_fmt_hexmask
 *
 * Given a bitstr_t, allocate and return a string in the form of:
 *                         "0x0123ABC\0"
 *                            ^     ^
 *                            |     |
 *                           MSB   LSB
 *   bitmap (IN)  bitmap to format
 *   RETURN       formatted string
 */
char *bit_fmt_hexmask(bitstr_t *bitmap)
{
	return _bit_fmt_hexmask(bitmap, false);
}

/* bit_fmt_hexmask_trim
 *
 * Same as bit_fmt_hexmask() except leading zeros are stripped from the output.
 */
char *bit_fmt_hexmask_trim(bitstr_t *bitmap)
{
	return _bit_fmt_hexmask(bitmap, true);
}

/* bit_unfmt_hexmask
 *
 * Given a hex mask string "0x0123ABC\0", convert to a bitstr_t *
 *                            ^     ^
 *                            |     |
 *                           MSB   LSB
 *   bitmap (OUT)  bitmap to update
 *   str (IN)      hex mask string to unformat
 *   RET: 0 on success, -1 on error
 */
int bit_unfmt_hexmask(bitstr_t * bitmap, const char* str)
{
	int32_t bit_index = 0, len;
	int rc = 0;
	const char *curpos;
	bitstr_t current;
	bitoff_t bitsize;

	if (!bitmap || !str)
		return -1;

	len = strlen(str);
	bitsize = bit_size(bitmap);
	curpos = str + len - 1;

	bit_nclear(bitmap, 0, bitsize - 1);
	if (xstrncmp(str, "0x", 2) == 0) {	/* Bypass 0x */
		str += 2;
	}

	while (curpos >= str) {
		current = (bitoff_t) *curpos;
		if (isxdigit(current)) {	/* valid hex digit */
			if (isdigit(current)) {
				current -= '0';
			} else {
				current = toupper(current);
				current -= 'A' - 10;
			}
		} else {			/* not a valid hex digit */
		    	current = 0;
			rc = -1;
			break;
		}

		if (bit_index + 3 < bitsize && !(bit_index % 4)) {
#ifdef SLURM_BIGENDIAN
			current = (((current & 1) << 3) |
				   ((current & 2) << 1) |
				   ((current & 4) >> 1) |
				   ((current & 8) >> 3));
			current = (current << ((BITSTR_MAXPOS -
					        (bit_index & BITSTR_MAXPOS)) -
					       3));
			bitmap[_bit_word(bit_index)] |= current;
#else
			bitmap[_bit_word(bit_index)] |=
				(current & 0xf) << (bit_index & BITSTR_MAXPOS);
#endif
			curpos--;
			bit_index += 4;
			continue;
		}
		if (current & 1) {
			if (bit_index < bitsize)
				bit_set(bitmap, bit_index);
			else {
				rc = -1;
				break;
			}
		}
		if (current & 2) {
			if ((bit_index + 1) < bitsize)
				bit_set(bitmap, bit_index + 1);
			else {
				rc = -1;
				break;
			}
		}
		if (current & 4) {
			if ((bit_index + 2) < bitsize)
				bit_set(bitmap, bit_index + 2);
			else {
				rc = -1;
				break;
			}
		}
		if (current & 8) {
			if ((bit_index + 3) < bitsize)
				bit_set(bitmap, bit_index + 3);
			else {
				rc = -1;
				break;
			}
		}
		curpos--;
		bit_index += 4;
	}
	return rc;
}

/* bit_fmt_binmask
 *
 * Given a bitstr_t, allocate and return a binary string in the form of:
 *                            "0001010\0"
 *                             ^     ^
 *                             |     |
 *                            MSB   LSB
 *   bitmap (IN)  bitmap to format
 *   RETURN       formatted string
 */
char * bit_fmt_binmask(bitstr_t * bitmap)
{
	char *retstr, *ptr;
	char current;
	bitoff_t i;
	bitoff_t bitsize = bit_size(bitmap);

	/* 1 bits per ASCII '0'-'1' */
	bitoff_t charsize = bitsize;

	retstr = xmalloc(charsize + 1);

	retstr[charsize] = '\0';
	ptr = &retstr[charsize - 1];
	for (i=0; i < bitsize;) {
		current = 0;
		if (bit_test(bitmap,i++)) current |= 0x1;
		current += '0';
		*ptr-- = current;
	}

	return retstr;
}

/* bit_unfmt_binmask
 *
 * Given a binary mask string "0001010\0", convert to a bitstr_t *
 *                             ^     ^
 *                             |     |
 *                            MSB   LSB
 *   bitmap (OUT)  bitmap to update
 *   str (IN)      hex mask string to unformat
 */
void bit_unfmt_binmask(bitstr_t * bitmap, const char* str)
{
	int32_t bit_index = 0, len = strlen(str);
	const char *curpos = str + len - 1;
	char current;
	bitoff_t bitsize = bit_size(bitmap);

	bit_nclear(bitmap, 0, bitsize - 1);
	while (curpos >= str) {
		current = (int32_t) *curpos;
		current -= '0';
		if ((current & 1) && (bit_index   < bitsize))
			bit_set(bitmap, bit_index);
		curpos--;
		bit_index++;
	}
}

/* Find the bit set at pos (0 - bitstr_bits) in bitstr b.
 *   b (IN)             bitstring to search
 *   pos (IN)           bit to search for
 *   RETURN             number bit is set in bitstring (-1 on error)
 */

bitoff_t
bit_get_bit_num(bitstr_t *b, int32_t pos)
{
	bitoff_t bit;
	int32_t cnt = 0;
	bitoff_t bit_cnt;

	_assert_bitstr_valid(b);
	bit_cnt = _bitstr_bits(b);
	xassert(pos <= bit_cnt);

	for (bit = 0; bit < bit_cnt; bit++) {
		if (bit_test(b, bit)) {	/* we got one */
			if (cnt == pos)
				break;
			cnt++;
		}
	}

	if (bit >= bit_cnt)
		bit = -1;

	return bit;
}

/* Find want nth the bit pos is set in bitstr b.
 *   b (IN)             bitstring to search
 *   pos (IN)           bit to search to
 *   RETURN             number bit is set in bitstring (-1 on error)
 */

int32_t
bit_get_pos_num(bitstr_t *b, bitoff_t pos)
{
	bitoff_t bit;
	int32_t cnt = -1;
#ifndef NDEBUG
	bitoff_t bit_cnt;
#endif

	_assert_bitstr_valid(b);
#ifndef NDEBUG
	bit_cnt = _bitstr_bits(b);
	xassert(pos <= bit_cnt);
#endif

	if (!bit_test(b, pos)) {
		error("bit %"BITSTR_FMT" not set", pos);
		return cnt;
	}
	for (bit = 0; bit <= pos; bit++) {
		if (bit_test(b, bit)) {	/* we got one */
			cnt++;
		}
	}

	return cnt;
}

void bit_consolidate(bitstr_t *b)
{
	int set_count = bit_set_count(b);

	if (set_count && (set_count < bit_size(b))) {
		bit_nclear(b, set_count, bit_size(b) - 1);
		bit_nset(b, 0, set_count - 1);
	}
}
