/*****************************************************************************\
 *  bitstring.c - bitmap manipulation functions
 *****************************************************************************
 *  See comments about origin, limitations, and internal structure in 
 *  bitstring.h.
 *
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jim Garlick <garlick@llnl.gov>, Moe Jette <jette1@llnl.gov>
 *  UCRL-CODE-2002-040.
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

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "src/common/bitstring.h"
#include "src/common/macros.h"
#include "src/common/xmalloc.h"

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
strong_alias(bit_ffc,		slurm_bit_ffc);
strong_alias(bit_ffs,		slurm_bit_ffs);
strong_alias(bit_free,		slurm_bit_free);
strong_alias(bit_realloc,	slurm_bit_realloc);
strong_alias(bit_size,		slurm_bit_size);
strong_alias(bit_and,		slurm_bit_and);
strong_alias(bit_not,		slurm_bit_not);
strong_alias(bit_or,		slurm_bit_or);
strong_alias(bit_set_count,	slurm_bit_set_count);
strong_alias(bit_clear_count,	slurm_bit_clear_count);
strong_alias(bit_fmt,		slurm_bit_fmt);
strong_alias(bit_fls,		slurm_bit_fls);
strong_alias(bit_fill_gaps,	slurm_bit_fill_gaps);
strong_alias(bit_super_set,	slurm_bit_super_set);
strong_alias(bit_copy,		slurm_bit_copy);
strong_alias(bit_pick_cnt,	slurm_bit_pick_cnt);
strong_alias(bitfmt2int,	slurm_bitfmt2int);

/* 
 * Allocate a bitstring.
 *   nbits (IN)		valid bits in new bitstring, initialized to all clear
 *   RETURN		new bitstring
 */
bitstr_t *
bit_alloc(bitoff_t nbits)
{
	bitstr_t *new;

	new = (bitstr_t *)calloc(_bitstr_words(nbits), sizeof(bitstr_t));
	if (new) {
		_bitstr_magic(new) = BITSTR_MAGIC;
		_bitstr_bits(new) = nbits;
	}
	return new;
}

/* 
 * Reallocate a bitstring (expand or contract size).
 *   b (IN)		pointer to old bitstring 
 *   nbits (IN)		valid bits in new bitstr
 *   RETURN		new bitstring
 */
bitstr_t *
bit_realloc(bitstr_t *b, bitoff_t nbits)
{
	bitoff_t obits;
	bitstr_t *new = NULL;
       
	_assert_bitstr_valid(b);
	obits = _bitstr_bits(b);
	new = realloc(b, _bitstr_words(nbits) * sizeof(bitstr_t));
	if (new) {
		_assert_bitstr_valid(new);
		_bitstr_bits(new) = nbits;
		if (nbits > obits)
			bit_nclear(new, obits, nbits - 1);
	}
	return new;
}

/* 
 * Free a bitstr.
 *   b (IN/OUT)	bitstr to be freed
 */ 
void
bit_free(bitstr_t *b)
{
	assert(b);
	assert(_bitstr_magic(b) == BITSTR_MAGIC);
	_bitstr_magic(b) = 0;
	free(b);
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
		assert((stop-start+1) % 8 == 0);
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
		assert((stop-start+1) % 8 == 0);
		memset(_bit_byteaddr(b, start), 0, (stop-start+1) / 8);
	}
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
		int word = _bit_word(bit);

		if (b[word] == BITSTR_MAXPOS) {
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
		int word = _bit_word(bit);

		if (b[word] == 0) {
			bit += sizeof(bitstr_t)*8;
			continue;
		}
		while (bit < _bitstr_bits(b) && _bit_word(bit) == word) {
			if (bit_test(b, bit)) {
				value = bit;
				break;
			}
			bit++;
		}
	}
	return value;
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
	int word;

	_assert_bitstr_valid(b);

	if (_bitstr_bits(b) == 0)	/* empty bitstring */
		return -1;

	bit = _bitstr_bits(b) - 1;	/* zero origin */
	while (bit >= 0 && 		/* test partitial words */
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
		while (bit >= 0) {
			if (bit_test(b, bit)) {
				value = bit;
				break;
			}
			bit--;
		}
	}
	return value;
}

/* 
 * set all bits between the first and last bits set (i.e. fill in the gaps 
 *	to make set bits contiguous)
 */
void
bit_fill_gaps(bitstr_t *b) {
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
bit_super_set(bitstr_t *b1, bitstr_t *b2)  {
	bitoff_t bit;

	_assert_bitstr_valid(b1);
	_assert_bitstr_valid(b2);
	assert(_bitstr_bits(b1) == _bitstr_bits(b2));

	for (bit = 0; bit < _bitstr_bits(b1); bit += sizeof(bitstr_t)*8) {
		if (b1[_bit_word(bit)] != (b1[_bit_word(bit)] & 
		                           b2[_bit_word(bit)]))
			return 0;
	}

	return 1;
}

/*
 * b1 &= b2
 *   b1 (IN/OUT)	first string
 *   b2 (IN)		second bitstring
 */
void
bit_and(bitstr_t *b1, bitstr_t *b2) {
	bitoff_t bit;

	_assert_bitstr_valid(b1);
	_assert_bitstr_valid(b2);
	assert(_bitstr_bits(b1) == _bitstr_bits(b2));

	for (bit = 0; bit < _bitstr_bits(b1); bit += sizeof(bitstr_t)*8)
		b1[_bit_word(bit)] &= b2[_bit_word(bit)];
}

/* 
 * b1 = ~b1		one's complement
 *   b1 (IN/OUT)	first bitmap
 */
void
bit_not(bitstr_t *b) {
	bitoff_t bit;

	_assert_bitstr_valid(b);

	for (bit = 0; bit < _bitstr_bits(b); bit += sizeof(bitstr_t)*8)
		b[_bit_word(bit)] = ~b[_bit_word(bit)];
}

/* 
 * b1 |= b2
 *   b1 (IN/OUT)	first bitmap
 *   b2 (IN)		second bitmap
 */
void
bit_or(bitstr_t *b1, bitstr_t *b2) {
	bitoff_t bit;

	_assert_bitstr_valid(b1);
	_assert_bitstr_valid(b2);
	assert(_bitstr_bits(b1) == _bitstr_bits(b2));

	for (bit = 0; bit < _bitstr_bits(b1); bit += sizeof(bitstr_t)*8)
		b1[_bit_word(bit)] |= b2[_bit_word(bit)];
}

/* 
 * return a copy of the supplied bitmap
 */
bitstr_t *
bit_copy(bitstr_t *b)
{
	bitstr_t *new;
	int newsize_bits;
	size_t len = 0;  /* Number of bytes to memcpy() */

	_assert_bitstr_valid(b);

	newsize_bits  = bit_size(b);
	len = (_bitstr_words(newsize_bits) - BITSTR_OVERHEAD)*sizeof(bitstr_t);
	new = bit_alloc(newsize_bits);
	if (new)
		memcpy(&new[BITSTR_OVERHEAD], &b[BITSTR_OVERHEAD], len);

	return new;
}

#if !defined(USE_64BIT_BITSTR)
/*
 * Returns the hamming weight (i.e. the number of bits set) in a word.
 * NOTE: This routine borrowed from Linux 2.4.9 <linux/bitops.h>.
 */
static uint32_t
hweight(uint32_t w)
{
	uint32_t res;
       
	res = (w   & 0x55555555) + ((w >> 1)    & 0x55555555);
	res = (res & 0x33333333) + ((res >> 2)  & 0x33333333);
	res = (res & 0x0F0F0F0F) + ((res >> 4)  & 0x0F0F0F0F);
	res = (res & 0x00FF00FF) + ((res >> 8)  & 0x00FF00FF);
	res = (res & 0x0000FFFF) + ((res >> 16) & 0x0000FFFF);

	return res;
}
#else
/*
 * A 64 bit version crafted from 32-bit one borrowed above.
 */
static uint64_t
hweight(uint64_t w)
{
	uint64_t res;
       
	res = (w   & 0x5555555555555555) + ((w >> 1)    & 0x5555555555555555);
	res = (res & 0x3333333333333333) + ((res >> 2)  & 0x3333333333333333);
	res = (res & 0x0F0F0F0F0F0F0F0F) + ((res >> 4)  & 0x0F0F0F0F0F0F0F0F);
	res = (res & 0x00FF00FF00FF00FF) + ((res >> 8)  & 0x00FF00FF00FF00FF);
	res = (res & 0x0000FFFF0000FFFF) + ((res >> 16) & 0x0000FFFF0000FFFF);
	res = (res & 0x00000000FFFFFFFF) + ((res >> 32) & 0x00000000FFFFFFFF);

	return res;
}
#endif /* !USE_64BIT_BITSTR */

/*
 * Count the number of bits set in bitstring.
 *   b (IN)		bitstring to check
 *   RETURN		count of set bits 
 */
int
bit_set_count(bitstr_t *b)
{
	int count = 0;
	bitoff_t bit;

	_assert_bitstr_valid(b);

	for (bit = 0; bit < _bitstr_bits(b); bit += sizeof(bitstr_t)*8)
		count += hweight(b[_bit_word(bit)]);

	return count;
}

/*
 * Count the number of bits clear in bitstring.
 *   b (IN)		bitstring to check
 *   RETURN		count of clear bits 
 */
int
bit_clear_count(bitstr_t *b)
{
	_assert_bitstr_valid(b);
	return (_bitstr_bits(b) - bit_set_count(b));
}

/*
 * build a bitmap containing the first nbits of b which are set
 */
bitstr_t *
bit_pick_cnt(bitstr_t *b, bitoff_t nbits) {
	bitoff_t bit = 0, new_bits, count = 0;
	bitstr_t *new;

	_assert_bitstr_valid(b);

	if (_bitstr_bits(b) < nbits)
		return NULL;

	new = bit_alloc(bit_size(b));
	if (new == NULL)
		return NULL;

	while ((bit < _bitstr_bits(b)) && (count < nbits)) {
		int word = _bit_word(bit);

		if (b[word] == 0) {
			bit += sizeof(bitstr_t)*8;
			continue;
		}

		new_bits = hweight(b[word]);
		if ((count + new_bits) <= nbits) {
			new[word] = b[word];
			count += new_bits;
			bit += sizeof(bitstr_t)*8;
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
 * XXX the relationship between stdint types and "unsigned [long] long"
 * types is architecture/compiler dependent, so this may have to be tweaked.
 */
#ifdef	USE_64BIT_BITSTR
#define BITSTR_RANGE_FMT	"%llu-%llu,"
#define BITSTR_SINGLE_FMT	"%llu,"
#else
#define BITSTR_RANGE_FMT	"%u-%u,"
#define BITSTR_SINGLE_FMT	"%u,"
#endif

/*
 * Convert to range string format, e.g. 0-5,42
 */
char *
bit_fmt(char *str, int len, bitstr_t *b)
{
	int count = 0, ret, word;
	bitoff_t start, bit;

	_assert_bitstr_valid(b);
	assert(len > 0);
	*str = '\0';
	for (bit = 0; bit < _bitstr_bits(b); ) {
		word = _bit_word(bit);
		if (b[word] == 0) {
			bit += sizeof(bitstr_t)*8;
			continue;
		}

		if (bit_test(b, bit)) {
			count++;
			start = bit;
			while (bit+1 < _bitstr_bits(b) && bit_test(b, bit+1))
				bit++;
			if (bit == start)	/* add single bit position */
				ret = snprintf(str+strlen(str), 
				               len-strlen(str),
				               BITSTR_SINGLE_FMT, start);
			else 			/* add bit position range */
				ret = snprintf(str+strlen(str), 
				               len-strlen(str),
				               BITSTR_RANGE_FMT, start, bit);
			assert(ret != -1);
		}
		bit++;
	}
	if (count > 0)
		str[strlen(str) - 1] = '\0'; 	/* zap trailing comma */
	if (count > 1) { 			/* add braces */
		assert(strlen(str) + 3 < len);
		memmove(str + 1, str, strlen(str));
		str[0] = '[';
		strcat(str, "]");
	} 
	return str;
}


/*
 * bitfmt2int - convert a string describing bitmap (output from bit_fmt, 
 *	e.g. "0-30,45,50-60") into an array of integer (start/end) pairs 
 *	terminated by -1 (e.g. "0, 30, 45, 45, 50, 60, -1")
 * input: bitmap string as produced by bitstring.c : bitfmt
 * output: an array of integers
 * NOTE: the caller must xfree the returned memory
 */
int *
bitfmt2int (char *bit_str_ptr) 
{
	int *bit_int_ptr, i, bit_inx, size, sum, start_val;

	if (bit_str_ptr == NULL) 
		return NULL;
	size = strlen (bit_str_ptr) + 1;
	bit_int_ptr = xmalloc ( sizeof (int *) * 
			(size * 2 + 1));	/* more than enough space */
	if (bit_int_ptr == NULL)
		return NULL;

	bit_inx = sum = 0;
	start_val = -1;
	for (i = 0; i < size; i++) {
		if (bit_str_ptr[i] >= '0' &&
		    bit_str_ptr[i] <= '9'){
			sum = (sum * 10) + (bit_str_ptr[i] - '0');
		}

		else if (bit_str_ptr[i] == '-') {
			start_val = sum;
			sum = 0;
		}

		else if (bit_str_ptr[i] == ',' || 
		         bit_str_ptr[i] == (char) NULL) {
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
	assert(bit_inx < (size*2+1));
	bit_int_ptr[bit_inx] = -1;
	return bit_int_ptr;
}

