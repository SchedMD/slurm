/*****************************************************************************\
 **  kvs.c - KVS manipulation functions
 *****************************************************************************
 *  Copyright (C) 2011-2012 National University of Defense Technology.
 *  Written by Hongjia Cao <hjcao@nudt.edu.cn>.
 *  All rights reserved.
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

#include <stdlib.h>
#include <unistd.h>

#include "kvs.h"
#include "setup.h"
#include "tree.h"
#include "pmi.h"

#define MAX_RETRIES 5

/* for fence */
int tasks_to_wait = 0;
int children_to_wait = 0;
int kvs_seq = 1; /* starting from 1 */
int waiting_kvs_resp = 0;


/* bucket of key-value pairs */
typedef struct kvs_bucket {
	char **pairs;
	uint32_t count;
	uint32_t size;
} kvs_bucket_t;

static kvs_bucket_t *kvs_hash = NULL;
static uint32_t hash_size = 0;

static char *temp_kvs_buf = NULL;
static int temp_kvs_cnt = 0;
static int temp_kvs_size = 0;

static int no_dup_keys = 0;

#define TASKS_PER_BUCKET 8
#define TEMP_KVS_SIZE_INC 2048

#define KEY_INDEX(i) (i * 2)
#define VAL_INDEX(i) (i * 2 + 1)
#define HASH(key) ( _hash(key) % hash_size)

inline static uint32_t
_hash(char *key)
{
	int len, i;
	uint32_t hash = 0;
	uint8_t shift;

	len = strlen(key);
	for (i = 0; i < len; i ++) {
		shift = (uint8_t)(hash >> 24);
		hash = (hash << 8) | (uint32_t)(shift ^ (uint8_t)key[i]);
	}
	return hash;
}

extern int
temp_kvs_init(void)
{
	uint16_t cmd;
	uint32_t nodeid, num_children, size;
	buf_t *buf = NULL;

	xfree(temp_kvs_buf);
	temp_kvs_cnt = 0;
	temp_kvs_size = TEMP_KVS_SIZE_INC;
	temp_kvs_buf = xmalloc(temp_kvs_size);

	/* put the tree cmd here to simplify message sending */
	if (in_stepd()) {
		cmd = TREE_CMD_KVS_FENCE;
	} else {
		cmd = TREE_CMD_KVS_FENCE_RESP;
	}

	buf = init_buf(1024);
	pack16(cmd, buf);
	if (in_stepd()) {
		nodeid = job_info.nodeid;
		/* XXX: TBC */
		num_children = tree_info.num_children + 1;

		pack32(nodeid, buf); /* from_nodeid */
		packstr(tree_info.this_node, buf); /* from_node */
		pack32(num_children, buf); /* num_children */
		pack32(kvs_seq, buf);
	} else {
		pack32(kvs_seq, buf);
	}
	size = get_buf_offset(buf);
	if (temp_kvs_cnt + size > temp_kvs_size) {
		temp_kvs_size += TEMP_KVS_SIZE_INC;
		xrealloc(temp_kvs_buf, temp_kvs_size);
	}
	memcpy(&temp_kvs_buf[temp_kvs_cnt], get_buf_data(buf), size);
	temp_kvs_cnt += size;
	FREE_NULL_BUFFER(buf);

	tasks_to_wait = 0;
	children_to_wait = 0;

	return SLURM_SUCCESS;
}

extern int
temp_kvs_add(char *key, char *val)
{
	buf_t *buf;
	uint32_t size;

	if ( key == NULL || val == NULL )
		return SLURM_SUCCESS;

	buf = init_buf(PMI2_MAX_KEYLEN + PMI2_MAX_VALLEN + 2 * sizeof(uint32_t));
	packstr(key, buf);
	packstr(val, buf);
	size = get_buf_offset(buf);
	if (temp_kvs_cnt + size > temp_kvs_size) {
		temp_kvs_size += TEMP_KVS_SIZE_INC;
		xrealloc(temp_kvs_buf, temp_kvs_size);
	}
	memcpy(&temp_kvs_buf[temp_kvs_cnt], get_buf_data(buf), size);
	temp_kvs_cnt += size;
	FREE_NULL_BUFFER(buf);

	return SLURM_SUCCESS;
}

extern int temp_kvs_merge(buf_t *buf)
{
	char *data;
	uint32_t offset, size;

	size = remaining_buf(buf);
	if (size == 0) {
		return SLURM_SUCCESS;
	}
	data = get_buf_data(buf);
	offset = get_buf_offset(buf);

	if (temp_kvs_cnt + size > temp_kvs_size) {
		temp_kvs_size += size;
		xrealloc(temp_kvs_buf, temp_kvs_size);
	}
	memcpy(&temp_kvs_buf[temp_kvs_cnt], &data[offset], size);
	temp_kvs_cnt += size;

	return SLURM_SUCCESS;
}

extern int
temp_kvs_send(void)
{
	int rc = SLURM_ERROR, retry = 0;
	unsigned int delay = 1;
	char *nodelist = NULL;

	if (!in_stepd())	/* srun */
		nodelist = xstrdup(job_info.step_nodelist);
	else if (tree_info.parent_node)
		nodelist = xstrdup(tree_info.parent_node);

	/* cmd included in temp_kvs_buf */
	kvs_seq++; /* expecting new kvs after now */

	while (1) {
		if (retry == 1)
			verbose("failed to send temp kvs, rc=%d, retrying", rc);

		if (nodelist)
			/* srun or non-first-level stepds */
			rc = slurm_forward_data(&nodelist,
						tree_sock_addr,
						temp_kvs_cnt,
						temp_kvs_buf);
		else		/* first level stepds */
			rc = tree_msg_to_srun(temp_kvs_cnt, temp_kvs_buf);

		if (rc == SLURM_SUCCESS)
			break;

		if (++retry >= MAX_RETRIES)
			break;
		/* wait, in case parent stepd / srun not ready */
		sleep(delay);
		delay *= 2;
	}
	temp_kvs_init();	/* clear old temp kvs */

	xfree(nodelist);

	return rc;
}

/**************************************************************/

extern int
kvs_init(void)
{
	debug3("mpi/pmi2: in kvs_init");

	hash_size = ((job_info.ntasks + TASKS_PER_BUCKET - 1) / TASKS_PER_BUCKET);

	kvs_hash = xmalloc(hash_size * sizeof(kvs_bucket_t));

	if (getenv(PMI2_KVS_NO_DUP_KEYS_ENV))
		no_dup_keys = 1;

	return SLURM_SUCCESS;
}

/*
 * returned value is not dup-ed
 */
extern char *
kvs_get(char *key)
{
	kvs_bucket_t *bucket;
	char *val = NULL;
	int i;

	debug3("mpi/pmi2: in kvs_get, key=%s", key);

	bucket = &kvs_hash[HASH(key)];
	if (bucket->count > 0) {
		for(i = 0; i < bucket->count; i ++) {
			if (! xstrcmp(key, bucket->pairs[KEY_INDEX(i)])) {
				val = bucket->pairs[VAL_INDEX(i)];
				break;
			}
		}
	}

	debug3("mpi/pmi2: out kvs_get, val=%s", val);

	return val;
}

extern int
kvs_put(char *key, char *val)
{
	kvs_bucket_t *bucket;
	int i;

	debug3("mpi/pmi2: in kvs_put");

	bucket = &kvs_hash[HASH(key)];

	if (! no_dup_keys) {
		for (i = 0; i < bucket->count; i ++) {
			if (! xstrcmp(key, bucket->pairs[KEY_INDEX(i)])) {
				/* replace the k-v pair */
				xfree(bucket->pairs[VAL_INDEX(i)]);
				bucket->pairs[VAL_INDEX(i)] = xstrdup(val);
				debug("mpi/pmi2: put kvs %s=%s", key, val);
				return SLURM_SUCCESS;
			}
		}
	}
	if (bucket->count * 2 >= bucket->size) {
		bucket->size += (TASKS_PER_BUCKET * 2);
		xrealloc(bucket->pairs, bucket->size * sizeof(char *));
	}
	/* add the k-v pair */
	i = bucket->count;
	bucket->pairs[KEY_INDEX(i)] = xstrdup(key);
	bucket->pairs[VAL_INDEX(i)] = xstrdup(val);
	bucket->count ++;

	debug3("mpi/pmi2: put kvs %s=%s", key, val);
	return SLURM_SUCCESS;
}

extern int
kvs_clear(void)
{
	kvs_bucket_t *bucket;
	int i, j;

	for (i = 0; i < hash_size; i ++){
		bucket = &kvs_hash[i];
		for (j = 0; j < bucket->count; j ++) {
			xfree (bucket->pairs[KEY_INDEX(j)]);
			xfree (bucket->pairs[VAL_INDEX(j)]);
		}
	}
	xfree(kvs_hash);

	return SLURM_SUCCESS;
}
