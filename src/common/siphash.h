/*****************************************************************************\
 *  siphash.h - Headers for the siphash 2-4 hash functions
 *****************************************************************************
 *  Copyright (C) 2016 Janne Blomqvist
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
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

/* Siphash home page https://131002.net/siphash/
 *
 * Reference implementation of the algorithm in file siphash24.c taken from
 * https://raw.githubusercontent.com/veorq/SipHash/master/siphash24.c
 *
 * Slurm specific utility functions in siphash_slurm.c.
 *
 * Siphash is a keyed hash function that is performance competitive
 * with non-cryptograhic hash functions. It's used as the default hash
 * function for hash tables e.g. in Perl, Python, Rust. It should be
 * relatively secure, in the sense of being collision resistant,
 * preventing stuff like hash flooding DDOS attacks.
 */

#ifndef _SIPHASH_H
#define _SIPHASH_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define KEYLEN 16
#define HASHLEN 8


/* The core algorithm, though the interface is a bit cumbersome. See
 * siphash_str() for a simple to use thing for string hashing.  */
int siphash(uint8_t *out, const uint8_t *in, uint64_t inlen, const uint8_t *k);

/* Initialize the siphash key.  */
void siphash_init(void);

/* Hash a string.  */
uint64_t siphash_str(const char* str);

#endif /* _SIPHASH_H */
