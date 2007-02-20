/*****************************************************************************\
 *  bitstring.h - definitions for bitstring.c, bitmap manipulation functions
 *****************************************************************************
 *  Reimplementation of the functionality of Paul Vixie's bitstring.h macros
 *  from his cron package and later contributed to 4.4BSD.  Little remains, 
 *  though interface semantics are preserved in functions noted below.
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

/*
 * A bitstr_t is an array of configurable size words.  The first two words 
 * are for internal use.  Word 0 is a magic cookie used to validate that the 
 * bitstr_t is properly initialized.  Word 1 is the number of valid bits in 
 * the bitstr_t This limts the capacity of a bitstr_t to 4 gigabits if using
 * 32 bit words.
 * 
 * bitstrings are zero origin
 */

#ifndef _BITSTRING_H_
#define	_BITSTRING_H_

#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif  /* HAVE_INTTYPES_H */
#else	/* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif  /*  HAVE_CONFIG_H */

#define BITSTR_SHIFT_WORD8	3
#define BITSTR_SHIFT_WORD32	5
#define BITSTR_SHIFT_WORD64	6

#ifdef USE_64BIT_BITSTR
typedef uint64_t bitstr_t;
#define BITSTR_SHIFT 		BITSTR_SHIFT_WORD64
#else
typedef uint32_t bitstr_t;
#define BITSTR_SHIFT 		BITSTR_SHIFT_WORD32
#endif

typedef bitstr_t bitoff_t;

/* 
 * internal macros / defs 
 */

/* 2 words used for magic cookie and size */
#define BITSTR_OVERHEAD 	2 	

/* bitstr_t signature in first word */
#define BITSTR_MAGIC 		0x42434445
#define BITSTR_MAGIC_STACK	0x42434446 /* signature if on stack */

/* max bit position in word */
#define BITSTR_MAXPOS		(sizeof(bitstr_t)*8 - 1) 

/* word of the bitstring bit is in */
#define	_bit_word(bit) 		(((bit) >> BITSTR_SHIFT) + BITSTR_OVERHEAD)

/* address of the byte containing bit */
#define _bit_byteaddr(name, bit) \
	((char *)((name) + BITSTR_OVERHEAD) + ((bit) >> BITSTR_SHIFT_WORD8))

/* mask for the bit within its word */
#define	_bit_mask(bit) 		((bitstr_t)1 << ((bit)&BITSTR_MAXPOS))

/* number of bits actually allocated to a bitstr */
#define _bitstr_bits(name) 	((name)[1])

/* magic cookie stored here */
#define _bitstr_magic(name) 	((name)[0])

/* words in a bitstring of nbits bits */
#define	_bitstr_words(nbits)	\
	((((nbits) + BITSTR_MAXPOS) >> BITSTR_SHIFT) + BITSTR_OVERHEAD)

/* check signature */
#define _assert_bitstr_valid(name) do { \
	assert((name) != NULL); \
	assert(_bitstr_magic(name) == BITSTR_MAGIC \
			    || _bitstr_magic(name) == BITSTR_MAGIC_STACK); \
} while (0)

/* check bit position */
#define _assert_bit_valid(name,bit) do { \
	assert((bit) >= 0); \
	assert((bit) < _bitstr_bits(name)); 	\
} while (0)

/* 
 * external macros 
 */

/* allocate a bitstring on the stack */
/* XXX bit_decl does not check if nbits overflows word 1 */
#define	bit_decl(name, nbits) \
	(name)[_bitstr_words(nbits)] = { BITSTR_MAGIC_STACK, (nbits) }

/* compat with Vixie macros */
bitstr_t *bit_alloc(bitoff_t nbits);
int bit_test(bitstr_t *b, bitoff_t bit);
void bit_set(bitstr_t *b, bitoff_t bit);
void bit_clear(bitstr_t *b, bitoff_t bit);
void bit_nclear(bitstr_t *b, bitoff_t start, bitoff_t stop);
void bit_nset(bitstr_t *b, bitoff_t start, bitoff_t stop);

/* changed interface from Vixie macros */
bitoff_t bit_ffc(bitstr_t *b);
bitoff_t bit_ffs(bitstr_t *b);

/* new */
bitoff_t bit_nffs(bitstr_t *b, int n);
bitoff_t bit_nffc(bitstr_t *b, int n);
bitoff_t bit_noc(bitstr_t *b, int n, int seed);
void	bit_free(bitstr_t *b);
bitstr_t *bit_realloc(bitstr_t *b, bitoff_t nbits);
bitoff_t bit_size(bitstr_t *b);
void	bit_and(bitstr_t *b1, bitstr_t *b2);
void	bit_not(bitstr_t *b);
void	bit_or(bitstr_t *b1, bitstr_t *b2);
int	bit_set_count(bitstr_t *b);
int	bit_clear_count(bitstr_t *b);
char	*bit_fmt(char *str, int len, bitstr_t *b);
int 	bit_unfmt(bitstr_t *b, char *str);
bitoff_t bit_fls(bitstr_t *b);
void	bit_fill_gaps(bitstr_t *b);
int	bit_super_set(bitstr_t *b1, bitstr_t *b2);
int     bit_equal(bitstr_t *b1, bitstr_t *b2);
void    bit_copybits(bitstr_t *dest, bitstr_t *src);
bitstr_t *bit_copy(bitstr_t *b);
bitstr_t *bit_pick_cnt(bitstr_t *b, bitoff_t nbits);
int 	*bitfmt2int (char *bit_str_ptr);
#endif /* !_BITSTRING_H_ */
