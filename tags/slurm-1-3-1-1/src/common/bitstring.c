/*****************************************************************************\
 *  bitstring.c - bitmap manipulation functions
 *****************************************************************************
 *  See comments about origin, limitations, and internal structure in 
 *  bitstring.h.
 *
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jim Garlick <garlick@llnl.gov>, Morris Jette <jette1@llnl.gov>
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

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
strong_alias(bit_nset_max_count,slurm_bit_nset_max_count);
strong_alias(int_and_set_count,	slurm_int_and_set_count);
strong_alias(bit_rotate_copy,	slurm_bit_rotate_copy);
strong_alias(bit_rotate,	slurm_bit_rotate);
strong_alias(bit_fmt,		slurm_bit_fmt);
strong_alias(bit_unfmt,		slurm_bit_unfmt);
strong_alias(bitfmt2int,	slurm_bitfmt2int);
strong_alias(bit_fmt_hexmask,	slurm_bit_fmt_hexmask);
strong_alias(bit_unfmt_hexmask,	slurm_bit_unfmt_hexmask);
strong_alias(bit_fmt_binmask,	slurm_bit_fmt_binmask);
strong_alias(bit_unfmt_binmask,	slurm_bit_unfmt_binmask);
strong_alias(bit_fls,		slurm_bit_fls);
strong_alias(bit_fill_gaps,	slurm_bit_fill_gaps);
strong_alias(bit_super_set,	slurm_bit_super_set);
strong_alias(bit_overlap,	slurm_bit_overlap);
strong_alias(bit_equal,		slurm_bit_equal);
strong_alias(bit_copy,		slurm_bit_copy);
strong_alias(bit_pick_cnt,	slurm_bit_pick_cnt);
strong_alias(bit_nffc,		slurm_bit_nffc);
strong_alias(bit_noc,		slurm_bit_noc);
strong_alias(bit_nffs,		slurm_bit_nffs);
strong_alias(bit_copybits,	slurm_bit_copybits);
strong_alias(bit_get_bit_num,	slurm_bit_get_bit_num);
strong_alias(bit_get_pos_num,	slurm_bit_get_pos_num);

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

/* Find the first n contiguous bits clear in b.
 *   b (IN)             bitstring to search
 *   n (IN)             number of bits needed
 *   RETURN             position of first bit in range (-1 if none found)
 */
bitoff_t
bit_nffc(bitstr_t *b, int n)
{
	bitoff_t value = -1;
	bitoff_t bit;
	int cnt = 0;

	_assert_bitstr_valid(b);
	assert(n > 0 && n < _bitstr_bits(b));

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
bit_noc(bitstr_t *b, int n, int seed)
{
	bitoff_t value = -1;
	bitoff_t bit;
	int cnt = 0;

	_assert_bitstr_valid(b);
	assert(n > 0 && n <= _bitstr_bits(b));

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
bit_nffs(bitstr_t *b, int n)
{
	bitoff_t value = -1;
	bitoff_t bit;
	int cnt = 0;

	_assert_bitstr_valid(b);
	assert(n > 0 && n <= _bitstr_bits(b));

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

void
bit_copybits(bitstr_t *dest, bitstr_t *src)
{
	int len;

	_assert_bitstr_valid(dest);
	_assert_bitstr_valid(src);
	assert(bit_size(src) == bit_size(dest));

	len = (_bitstr_words(bit_size(src)) - BITSTR_OVERHEAD)*sizeof(bitstr_t);
	memcpy(&dest[BITSTR_OVERHEAD], &src[BITSTR_OVERHEAD], len);
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
 * return number of bits set in b1 that are also set in b2, 0 if no overlap
 */
extern int
bit_overlap(bitstr_t *b1, bitstr_t *b2)
{
	int count = 0;
	bitoff_t bit;
	
	_assert_bitstr_valid(b1);
	_assert_bitstr_valid(b2);
	assert(_bitstr_bits(b1) == _bitstr_bits(b2));

	for (bit = 0; bit < _bitstr_bits(b1); bit += sizeof(bitstr_t)*8) 
		count += hweight(b1[_bit_word(bit)] & b2[_bit_word(bit)]);

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

/* Return the count of the largest number of contiguous bits set in b.
 *   b (IN)             bitstring to search
 *   RETURN             the largest number of contiguous bits set in b
 */
int
bit_nset_max_count(bitstr_t *b)
{
	bitoff_t bit;
	int cnt = 0;
	int maxcnt = 0;
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
 * And an integer vector and a bitstring and sum the elements corresponding
 * to set entries in b2: SUM(i1 & b2)
 * Note: if b2 is longer than i1, then elements are wrapped into i1
 *   i1 (IN)		integer vector
 *   b2 (IN)		bitstring
 */
int
int_and_set_count(int *i1, int ilen, bitstr_t *b2) {
	bitoff_t bit;
	int sum;

	_assert_bitstr_valid(b2);

	sum = 0;
	for (bit = 0; bit < _bitstr_bits(b2); bit++) {
	    	if (bit_test(b2, bit))
			sum += i1[bit % ilen];
	}
	return(sum);
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
bit_rotate_copy(bitstr_t *b1, int n, bitoff_t nbits) {
	bitoff_t bit, dst;
	bitstr_t *new;
	bitoff_t bitsize;
	bitoff_t deltasize, wrapbits;

	_assert_bitstr_valid(b1);
	bitsize = bit_size(b1);
	assert(nbits >= bitsize);
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
bit_rotate(bitstr_t *b1, int n) {
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

int
bit_unfmt(bitstr_t *b, char *str)
{
	int *intvec, *p, rc = 0; 

	_assert_bitstr_valid(b);
	intvec = bitfmt2int(str);
	if (intvec == NULL) 
		return -1;

	bit_nclear(b, 0, _bitstr_bits(b) - 1);
	for (p = intvec; *p != -1; p += 2) {
		if ((*p < 0) || (*p >= _bitstr_bits(b))
		||  (*(p + 1) < 0) || (*(p + 1) >= _bitstr_bits(b))) {
			rc = -1;
			break;
		}
		bit_nset(b, *p, *(p + 1));		
	}

	xfree(intvec);
	return rc;
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
	bit_int_ptr = xmalloc ( sizeof (int) * 
			(size * 2 + 1));	/* more than enough space */

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
	assert(bit_inx < (size*2+1));
	bit_int_ptr[bit_inx] = -1;
	return bit_int_ptr;
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
char * bit_fmt_hexmask(bitstr_t * bitmap)
{
	char *retstr, *ptr;
	char current;
	bitoff_t i;
	bitoff_t bitsize = bit_size(bitmap);

	/* 4 bits per ASCII '0'-'F' */
	bitoff_t charsize = (bitsize + 3) / 4;

	retstr = xmalloc(charsize + 3);

	retstr[0] = '0';  
	retstr[1] = 'x';  
	retstr[charsize + 2] = '\0';
	ptr = &retstr[charsize + 1];
	for (i=0; i < bitsize;) {
		current = 0;
		if (                 bit_test(bitmap,i++)) current |= 0x1;
		if ((i < bitsize) && bit_test(bitmap,i++)) current |= 0x2;
		if ((i < bitsize) && bit_test(bitmap,i++)) current |= 0x4;
		if ((i < bitsize) && bit_test(bitmap,i++)) current |= 0x8;
		if (current <= 9) {
			current += '0';
		} else {
			current += 'A' - 10;
		}
		*ptr-- = current;
	}

	return retstr;
}

/* bit_unfmt_hexmask
 *
 * Given a hex mask string "0x0123ABC\0", convert to a bitstr_t *
 *                            ^     ^
 *                            |     |
 *                           MSB   LSB
 *   bitmap (OUT)  bitmap to update
 *   str (IN)      hex mask string to unformat
 */
int bit_unfmt_hexmask(bitstr_t * bitmap, const char* str) 
{
	int bit_index = 0, len = strlen(str);
	int rc = 0;
	const char *curpos = str + len - 1;
	char current;
	bitoff_t bitsize = bit_size(bitmap);

	if (strncmp(str, "0x", 2) == 0) {	/* Bypass 0x */
		str += 2;
		len -= 2;
	}

	while(curpos >= str) {
		current = (int) *curpos; 
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
		}

		if ((current & 1) && (bit_index   < bitsize))
			bit_set(bitmap, bit_index);
		if ((current & 2) && (bit_index+1 < bitsize))
			bit_set(bitmap, bit_index+1); 
		if ((current & 4) && (bit_index+2 < bitsize))
			bit_set(bitmap, bit_index+2);
		if ((current & 8) && (bit_index+3 < bitsize))
			bit_set(bitmap, bit_index+3);
		len--;
		curpos--;
		bit_index+=4;
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
int bit_unfmt_binmask(bitstr_t * bitmap, const char* str) 
{
	int bit_index = 0, len = strlen(str);
	const char *curpos = str + len - 1;
	char current;
	bitoff_t bitsize = bit_size(bitmap);

	while(curpos >= str) {
		current = (int) *curpos; 
		current -= '0';
		if ((current & 1) && (bit_index   < bitsize))
			bit_set(bitmap, bit_index);
		len--;
		curpos--;
		bit_index++;
	}
	return 0;
}

/* Find the bit set at pos (0 - bitstr_bits) in bitstr b.
 *   b (IN)             bitstring to search
 *   pos (IN)           bit to search for
 *   RETURN             number bit is set in bitstring (-1 on error)
 */

bitoff_t
bit_get_bit_num(bitstr_t *b, int pos)
{
	bitoff_t bit;
	int cnt = 0;
	bitoff_t bit_cnt;
	
	_assert_bitstr_valid(b);
	bit_cnt = _bitstr_bits(b);
	assert(pos <= bit_cnt);

	for (bit = 0; bit < bit_cnt; bit++) {
		if (bit_test(b, bit)) {	/* we got one */
			if(cnt == pos)
				break;			
			cnt++;			
		}
	}

	if(bit >= bit_cnt)
		bit = -1;

	return bit;
}

/* Find want nth the bit pos is set in bitstr b.
 *   b (IN)             bitstring to search
 *   pos (IN)           bit to search to
 *   RETURN             number bit is set in bitstring (-1 on error)
 */

int
bit_get_pos_num(bitstr_t *b, bitoff_t pos)
{
	bitoff_t bit;
	int cnt = -1;
	bitoff_t bit_cnt;
	
	_assert_bitstr_valid(b);
	bit_cnt = _bitstr_bits(b);
	assert(pos <= bit_cnt);
	
	if (!bit_test(b, pos)) {
		error("bit %d not set", pos);
		return cnt;
	}
	for (bit = 0; bit <= pos; bit++) {
		if (bit_test(b, bit)) {	/* we got one */
			cnt++;			
		}
	}

	return cnt;
}

