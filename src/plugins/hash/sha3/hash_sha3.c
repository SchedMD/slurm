/*****************************************************************************\
 *  hash_sha3.c -  SHA-3 hash plugin
*****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#define _GNU_SOURCE

#include "src/common/slurm_xlator.h"

#include "src/common/log.h"
#include "src/interfaces/hash.h"

#include "src/plugins/hash/common_xkcp/KeccakHash.h"

/* Required Slurm plugin symbols: */
const char plugin_name[] = "SHA-3 hash plugin";
const char plugin_type[] = "hash/sha3";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/* Required for hash plugins: */
const uint32_t plugin_id = HASH_PLUGIN_SHA3;

extern int init(void)
{
	debug("%s: %s loaded", __func__, plugin_name);

	return SLURM_SUCCESS;
}

extern void fini(void)
{
	debug("%s: unloading %s", __func__, plugin_name);
}

extern int hash_p_compute(char *input, int len, char *custom_str, int cs_len,
			  slurm_hash_t *hash)
{
	Keccak_HashInstance hi;

	if (Keccak_HashInitialize_SHA3_256(&hi))
		return SLURM_ERROR;

	if (Keccak_HashUpdate(&hi, (BitSequence *) input, (len * 8)))
		return SLURM_ERROR;

	/*
	 * SHA-3 does not support the "customization string" directly.
	 * Append it so it's included in the resulting hash.
	 */
	if (cs_len)
		if (Keccak_HashUpdate(&hi, (BitSequence *) custom_str, (cs_len * 8)))
			return SLURM_ERROR;

	if (Keccak_HashFinal(&hi, (BitSequence*) hash->hash))
		return SLURM_ERROR;

	hash->type = HASH_PLUGIN_SHA3;

	return (sizeof(hash->hash));
}
