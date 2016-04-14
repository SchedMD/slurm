/*****************************************************************************\
 *  siphash_str.c - Slurm specific siphash functions
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

#include "src/common/fd.h"
#include "src/common/siphash.h"


/* Use a default value for the key in case initializing from
 * /dev/urandom fails or is forgotten.  */
static uint8_t siphash_key[KEYLEN] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
                                       12, 13, 14, 15 };


/* Initialize the key from /dev/urandom. Should be called once at
 * process startup, before any threads etc. are created.  */
void siphash_init()
{
	int fd = open_cloexec("/dev/urandom", O_RDONLY);
	if (fd == -1)
		return;
	read(fd, siphash_key, KEYLEN);
	close(fd);
}


uint64_t siphash_str(const char* str)
{
	uint8_t out[HASHLEN];
	uint64_t out64;
	const uint8_t *s = (uint8_t *) str;
	siphash(out, s, strlen(str), siphash_key);
	memcpy(&out64, out, HASHLEN);
	return out64;
}
