/*****************************************************************************\
 *  bitstring.h - definitions for bitstring.c, bitmap manipulation functions
 *****************************************************************************
 *  Reimplementation of the functionality of Paul Vixie's bitstring.h macros
 *  from his cron package and later contributed to 4.4BSD.  Little remains,
 *  though interface semantics are preserved in functions noted below.
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

/*
 * A bitstr_t is an array of configurable size words.  The first two words
 * are for internal use.  Word 0 is a magic cookie used to validate that the
 * bitstr_t is properly initialized.  Word 1 is the number of valid bits in
 * the bitstr_t This limts the capacity of a bitstr_t to 4 gigabits if using
 * 32 bit words.
 *
 * bitstrings are zero origin
 *
 * bitstrings are always stored in a little-endian fashion.  In other words,
 * bit "1" is always in the byte of a word at the lowest memory address,
 * regardless of the native architecture endianness.
 */

#ifndef _BITSTRING_H_
#define	_BITSTRING_H_

#include <inttypes.h>

#define BITSTR_SHIFT_WORD8	3
#define BITSTR_SHIFT_WORD64	6
#define BITSTR_MAXVAL           0xffffffffffffffff
#define BITSTR_FMT		PRId64

/* Below are also defined in src/slurm/slurm.h.in.  If it changes please update
 * that as well.
 */
#ifndef   __bitstr_datatypes_defined
#  define __bitstr_datatypes_defined

typedef int64_t bitstr_t;
#define BITSTR_SHIFT 		BITSTR_SHIFT_WORD64

typedef bitstr_t bitoff_t;

#endif

/*
 * internal macros / defs
 */

/* 2 words used for magic cookie and size */
#define BITSTR_OVERHEAD 	2

/* bitstr_t signature in first word */
#define BITSTR_MAGIC 		0x42434445

/* word size */
#define BITSTR_WORD_SIZE	(sizeof(bitstr_t) * 8)
/* max bit position in word */
#define BITSTR_MAXPOS		(BITSTR_WORD_SIZE - 1)

/* compat with Vixie macros */
bitstr_t *bit_alloc(bitoff_t nbits);
int bit_test(bitstr_t *b, bitoff_t bit);
void bit_set(bitstr_t *b, bitoff_t bit);
void bit_clear(bitstr_t *b, bitoff_t bit);
void bit_nclear(bitstr_t *b, bitoff_t start, bitoff_t stop);
void bit_nset(bitstr_t *b, bitoff_t start, bitoff_t stop);
void bit_set_all(bitstr_t *b);
void bit_clear_all(bitstr_t *b);

/* changed interface from Vixie macros */
bitoff_t bit_ffc(bitstr_t *b);
bitoff_t bit_ffs(bitstr_t *b);

/*
 * bit_free() and bit_realloc() are rigged up as macros to be able to
 * manipulate the underlying variable in the same manner as xfree() and
 * xrealloc(). The actual function is implemented as slurm_bit_free() and
 * slurm_bit_realloc(), which also avoids needing to have them exported
 * through slurm_xlator.h.
 */
#define bit_free(__b) slurm_bit_free((bitstr_t **)&(__b))
void slurm_bit_free(bitstr_t **b);
#define bit_realloc(__b, __n) slurm_bit_realloc((bitstr_t **)&(__b), __n)
bitstr_t *slurm_bit_realloc(bitstr_t **b, bitoff_t nbits);

/* new */
bitoff_t bit_nffs(bitstr_t *b, int32_t n);
bitoff_t bit_nffc(bitstr_t *b, int32_t n);
bitoff_t bit_noc(bitstr_t *b, int32_t n, int32_t seed);
bitoff_t bit_size(bitstr_t *b);
void	bit_and(bitstr_t *b1, bitstr_t *b2);
void	bit_and_not(bitstr_t *b1, bitstr_t *b2);
void	bit_not(bitstr_t *b);
void	bit_or(bitstr_t *b1, bitstr_t *b2);
void	bit_or_not(bitstr_t *b1, bitstr_t *b2);
int32_t	bit_set_count(bitstr_t *b);
int32_t	bit_set_count_range(bitstr_t *b, int32_t start, int32_t end);
int32_t	bit_clear_count(bitstr_t *b);
int32_t	bit_clear_count_range(bitstr_t *b, int32_t start, int32_t end);
int32_t	bit_nset_max_count(bitstr_t *b);
bitstr_t *bit_rotate_copy(bitstr_t *b1, int32_t n, bitoff_t nbits);
void	bit_rotate(bitstr_t *b1, int32_t n);
char	*bit_fmt(char *str, int32_t len, bitstr_t *b);
char    *bit_fmt_full(bitstr_t *b);
char    *bit_fmt_range(bitstr_t *b, int offset, int len);
int	bit_unfmt(bitstr_t *b, char *str);
int32_t	*bitfmt2int (char *bit_str_ptr);
char *  inx2bitfmt (int32_t *inx);
int     inx2bitstr(bitstr_t *b, int32_t *inx);
int32_t *bitstr2inx(bitstr_t *b);
char	*bit_fmt_hexmask(bitstr_t *b);
char    *bit_fmt_hexmask_trim(bitstr_t *b);
int 	bit_unfmt_hexmask(bitstr_t *b, const char *str);
char	*bit_fmt_binmask(bitstr_t *b);
void 	bit_unfmt_binmask(bitstr_t *b, const char *str);
bitoff_t bit_ffs_from_bit(bitstr_t *b, bitoff_t bit);
bitoff_t bit_fls(bitstr_t *b);
bitoff_t bit_fls_from_bit(bitstr_t *b, bitoff_t bit);
void	bit_fill_gaps(bitstr_t *b);
int	bit_super_set(bitstr_t *b1, bitstr_t *b2);
int     bit_overlap(bitstr_t *b1, bitstr_t *b2);
int     bit_overlap_any(bitstr_t *b1, bitstr_t *b2);
int     bit_equal(bitstr_t *b1, bitstr_t *b2);
void    bit_copybits(bitstr_t *dest, bitstr_t *src);
bitstr_t *bit_copy(bitstr_t *b);
bitstr_t *bit_pick_cnt(bitstr_t *b, bitoff_t nbits);
bitoff_t bit_get_bit_num(bitstr_t *b, int32_t pos);

/*
 * Move all set bits to the beginning of the bitstring
 *   b (IN)             bitstring to consolidate
 */
void bit_consolidate(bitstr_t *b);

#define FREE_NULL_BITMAP(_X)	\
do {				\
	if (_X)			\
		bit_free (_X);	\
	_X = NULL;		\
} while (0)


#endif /* !_BITSTRING_H_ */
