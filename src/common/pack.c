/****************************************************************************\
 *  pack.c - lowest level un/pack functions
 *  NOTE: The memory buffer will expand as needed using xrealloc()
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jim Garlick <garlick@llnl.gov>,
 *             Morris Jette <jette1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
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
\****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>

#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/xmalloc.h"

/*
 * Define slurm-specific aliases for use by plugins, see slurm_xlator.h
 * for details.
 */
strong_alias(create_buf,	slurm_create_buf);
strong_alias(free_buf,		slurm_free_buf);
strong_alias(grow_buf,		slurm_grow_buf);
strong_alias(init_buf,		slurm_init_buf);
strong_alias(xfer_buf_data,	slurm_xfer_buf_data);
strong_alias(pack_time,		slurm_pack_time);
strong_alias(unpack_time,	slurm_unpack_time);
strong_alias(packdouble,	slurm_packdouble);
strong_alias(unpackdouble,	slurm_unpackdouble);
strong_alias(pack64,		slurm_pack64);
strong_alias(unpack64,		slurm_unpack64);
strong_alias(pack32,		slurm_pack32);
strong_alias(unpack32,		slurm_unpack32);
strong_alias(pack16,		slurm_pack16);
strong_alias(unpack16,		slurm_unpack16);
strong_alias(pack8,		slurm_pack8);
strong_alias(unpack8,		slurm_unpack8);
strong_alias(pack16_array,      slurm_pack16_array);
strong_alias(unpack16_array,    slurm_unpack16_array);
strong_alias(pack32_array,	slurm_pack32_array);
strong_alias(unpack32_array,	slurm_unpack32_array);
strong_alias(packmem,		slurm_packmem);
strong_alias(unpackmem,		slurm_unpackmem);
strong_alias(unpackmem_ptr,	slurm_unpackmem_ptr);
strong_alias(unpackmem_xmalloc,	slurm_unpackmem_xmalloc);
strong_alias(unpackmem_malloc,	slurm_unpackmem_malloc);
strong_alias(packstr_array,	slurm_packstr_array);
strong_alias(unpackstr_array,	slurm_unpackstr_array);
strong_alias(packmem_array,	slurm_packmem_array);
strong_alias(unpackmem_array,	slurm_unpackmem_array);

/* Basic buffer management routines */
/* create_buf - create a buffer with the supplied contents, contents must
 * be xalloc'ed */
Buf create_buf(char *data, int size)
{
	Buf my_buf;

	if (size > MAX_BUF_SIZE) {
		error("%s: Buffer size limit exceeded (%u > %u)",
		      __func__, size, MAX_BUF_SIZE);
		return NULL;
	}

	my_buf = xmalloc_nz(sizeof(struct slurm_buf));
	my_buf->magic = BUF_MAGIC;
	my_buf->size = size;
	my_buf->processed = 0;
	my_buf->head = data;

	return my_buf;
}

/* free_buf - release memory associated with a given buffer */
void free_buf(Buf my_buf)
{
	assert(my_buf->magic == BUF_MAGIC);
	xfree(my_buf->head);
	xfree(my_buf);
}

/* Grow a buffer by the specified amount */
void grow_buf (Buf buffer, int size)
{
	if ((buffer->size + size) > MAX_BUF_SIZE) {
		error("%s: Buffer size limit exceeded (%u > %u)",
		      __func__, (buffer->size + size), MAX_BUF_SIZE);
		return;
	}

	buffer->size += size;
	xrealloc_nz(buffer->head, buffer->size);
}

/* init_buf - create an empty buffer of the given size */
Buf init_buf(int size)
{
	Buf my_buf;

	if (size > MAX_BUF_SIZE) {
		error("%s: Buffer size limit exceeded (%u > %u)",
		      __func__, size, MAX_BUF_SIZE);
		return NULL;
	}
	if (size <= 0)
		size = BUF_SIZE;
	my_buf = xmalloc_nz(sizeof(struct slurm_buf));
	my_buf->magic = BUF_MAGIC;
	my_buf->size = size;
	my_buf->processed = 0;
	my_buf->head = xmalloc_nz(sizeof(char)*size);
	return my_buf;
}

/* xfer_buf_data - return a pointer to the buffer's data and release the
 * buffer's structure */
void *xfer_buf_data(Buf my_buf)
{
	void *data_ptr;

	assert(my_buf->magic == BUF_MAGIC);
	data_ptr = (void *) my_buf->head;
	xfree(my_buf);
	return data_ptr;
}

/*
 * Given a time_t in host byte order, promote it to int64_t, convert to
 * network byte order, store in buffer and adjust buffer acc'd'ngly
 */
void pack_time(time_t val, Buf buffer)
{
	int64_t n64 = HTON_int64((int64_t) val);

	if (remaining_buf(buffer) < sizeof(n64)) {
		if ((buffer->size + BUF_SIZE) > MAX_BUF_SIZE) {
			error("%s: Buffer size limit exceeded (%u > %u)",
			      __func__, (buffer->size + BUF_SIZE),
			      MAX_BUF_SIZE);
			return;
		}
		buffer->size += BUF_SIZE;
		xrealloc_nz(buffer->head, buffer->size);
	}

	memcpy(&buffer->head[buffer->processed], &n64, sizeof(n64));
	buffer->processed += sizeof(n64);
}

int unpack_time(time_t * valp, Buf buffer)
{
	int64_t n64;

	if (remaining_buf(buffer) < sizeof(n64))
		return SLURM_ERROR;

	memcpy(&n64, &buffer->head[buffer->processed], sizeof(n64));
	buffer->processed += sizeof(n64);
	*valp = (time_t) NTOH_int64(n64);
	return SLURM_SUCCESS;
}


/*
 * Given a double, multiple by FLOAT_MULT and then
 * typecast to a uint64_t in host byte order, convert to network byte order
 * store in buffer, and adjust buffer counters.
 */
void 	packdouble(double val, Buf buffer)
{
	uint64_t nl;
	union {
		double d;
		uint64_t u;
	} uval;

	 /* The 0.5 is here to round off.  We have found on systems going out
	  * more than 15 decimals will mess things up, but this corrects it. */
	uval.d =  (val * FLOAT_MULT);
	nl =  HTON_uint64(uval.u);
	if (remaining_buf(buffer) < sizeof(nl)) {
		if ((buffer->size + BUF_SIZE) > MAX_BUF_SIZE) {
			error("%s: Buffer size limit exceeded (%u > %u)",
			      __func__, (buffer->size + BUF_SIZE),
			      MAX_BUF_SIZE);
			return;
		}
		buffer->size += BUF_SIZE;
		xrealloc_nz(buffer->head, buffer->size);
	}

	memcpy(&buffer->head[buffer->processed], &nl, sizeof(nl));
	buffer->processed += sizeof(nl);
}

/*
 * Given a buffer containing a network byte order 64-bit integer,
 * typecast as double, and  divide by FLOAT_MULT
 * store a host double at 'valp', and adjust buffer counters.
 */
int	unpackdouble(double *valp, Buf buffer)
{
	uint64_t nl;
	union {
		double d;
		uint64_t u;
	} uval;

	if (remaining_buf(buffer) < sizeof(nl))
		return SLURM_ERROR;

	memcpy(&nl, &buffer->head[buffer->processed], sizeof(nl));
	buffer->processed += sizeof(nl);

	uval.u = NTOH_uint64(nl);
	*valp = uval.d / FLOAT_MULT;

	return SLURM_SUCCESS;
}

/*
 * Given a 64-bit integer in host byte order, convert to network byte order
 * store in buffer, and adjust buffer counters.
 */
void pack64(uint64_t val, Buf buffer)
{
	uint64_t nl =  HTON_uint64(val);

	if (remaining_buf(buffer) < sizeof(nl)) {
		if ((buffer->size + BUF_SIZE) > MAX_BUF_SIZE) {
			error("%s: Buffer size limit exceeded (%u > %u)",
			      __func__, (buffer->size + BUF_SIZE),
			      MAX_BUF_SIZE);
			return;
		}
		buffer->size += BUF_SIZE;
		xrealloc_nz(buffer->head, buffer->size);
	}

	memcpy(&buffer->head[buffer->processed], &nl, sizeof(nl));
	buffer->processed += sizeof(nl);
}

/*
 * Given a buffer containing a network byte order 64-bit integer,
 * store a host integer at 'valp', and adjust buffer counters.
 */
int unpack64(uint64_t * valp, Buf buffer)
{
	uint64_t nl;
	if (remaining_buf(buffer) < sizeof(nl))
		return SLURM_ERROR;

	memcpy(&nl, &buffer->head[buffer->processed], sizeof(nl));
	*valp = NTOH_uint64(nl);
	buffer->processed += sizeof(nl);
	return SLURM_SUCCESS;
}

/*
 * Given a 32-bit integer in host byte order, convert to network byte order
 * store in buffer, and adjust buffer counters.
 */
void pack32(uint32_t val, Buf buffer)
{
	uint32_t nl = htonl(val);

	if (remaining_buf(buffer) < sizeof(nl)) {
		if ((buffer->size + BUF_SIZE) > MAX_BUF_SIZE) {
			error("%s: Buffer size limit exceeded (%u > %u)",
			      __func__, (buffer->size + BUF_SIZE),
			      MAX_BUF_SIZE);
			return;
		}
		buffer->size += BUF_SIZE;
		xrealloc_nz(buffer->head, buffer->size);
	}

	memcpy(&buffer->head[buffer->processed], &nl, sizeof(nl));
	buffer->processed += sizeof(nl);
}

/*
 * Given a buffer containing a network byte order 32-bit integer,
 * store a host integer at 'valp', and adjust buffer counters.
 */
int unpack32(uint32_t * valp, Buf buffer)
{
	uint32_t nl;
	if (remaining_buf(buffer) < sizeof(nl))
		return SLURM_ERROR;

	memcpy(&nl, &buffer->head[buffer->processed], sizeof(nl));
	*valp = ntohl(nl);
	buffer->processed += sizeof(nl);
	return SLURM_SUCCESS;
}

/* Given a *uint16_t, it will pack an array of size_val */
void pack16_array(uint16_t * valp, uint32_t size_val, Buf buffer)
{
	uint32_t i = 0;

	pack32(size_val, buffer);

	for (i = 0; i < size_val; i++) {
		pack16(*(valp + i), buffer);
	}
}

/* Given a int ptr, it will unpack an array of size_val
 */
int unpack16_array(uint16_t ** valp, uint32_t * size_val, Buf buffer)
{
	uint32_t i = 0;

	if (unpack32(size_val, buffer))
		return SLURM_ERROR;

	*valp = xmalloc_nz((*size_val) * sizeof(uint16_t));
	for (i = 0; i < *size_val; i++) {
		if (unpack16((*valp) + i, buffer))
			return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

/* Given a *uint32_t, it will pack an array of size_val */
void pack32_array(uint32_t * valp, uint32_t size_val, Buf buffer)
{
	uint32_t i = 0;

	pack32(size_val, buffer);

	for (i = 0; i < size_val; i++) {
		pack32(*(valp + i), buffer);
	}
}

/* Given a int ptr, it will unpack an array of size_val
 */
int unpack32_array(uint32_t ** valp, uint32_t * size_val, Buf buffer)
{
	uint32_t i = 0;

	if (unpack32(size_val, buffer))
		return SLURM_ERROR;

	*valp = xmalloc_nz((*size_val) * sizeof(uint32_t));
	for (i = 0; i < *size_val; i++) {
		if (unpack32((*valp) + i, buffer))
			return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

/* Given a *uint64_t, it will pack an array of size_val */
void pack64_array(uint64_t * valp, uint32_t size_val, Buf buffer)
{
	uint32_t i = 0;

	pack32(size_val, buffer);

	for (i = 0; i < size_val; i++) {
		pack64(*(valp + i), buffer);
	}
}

/* Given a int ptr, it will unpack an array of size_val
 */
int unpack64_array(uint64_t ** valp, uint32_t * size_val, Buf buffer)
{
	uint32_t i = 0;

	if (unpack32(size_val, buffer))
		return SLURM_ERROR;

	*valp = xmalloc_nz((*size_val) * sizeof(uint64_t));
	for (i = 0; i < *size_val; i++) {
		if (unpack64((*valp) + i, buffer))
			return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

/*
 * Given a 16-bit integer in host byte order, convert to network byte order,
 * store in buffer and adjust buffer counters.
 */
void pack16(uint16_t val, Buf buffer)
{
	uint16_t ns = htons(val);

	if (remaining_buf(buffer) < sizeof(ns)) {
		if ((buffer->size + BUF_SIZE) > MAX_BUF_SIZE) {
			error("%s: Buffer size limit exceeded (%u > %u)",
			      __func__, (buffer->size + BUF_SIZE),
			      MAX_BUF_SIZE);
			return;
		}
		buffer->size += BUF_SIZE;
		xrealloc_nz(buffer->head, buffer->size);
	}

	memcpy(&buffer->head[buffer->processed], &ns, sizeof(ns));
	buffer->processed += sizeof(ns);
}

/*
 * Given a buffer containing a network byte order 16-bit integer,
 * store a host integer at 'valp', and adjust buffer counters.
 */
int unpack16(uint16_t * valp, Buf buffer)
{
	uint16_t ns;

	if (remaining_buf(buffer) < sizeof(ns))
		return SLURM_ERROR;

	memcpy(&ns, &buffer->head[buffer->processed], sizeof(ns));
	*valp = ntohs(ns);
	buffer->processed += sizeof(ns);
	return SLURM_SUCCESS;
}

/*
 * Given a 8-bit integer in host byte order, convert to network byte order
 * store in buffer, and adjust buffer counters.
 */
void pack8(uint8_t val, Buf buffer)
{
	if (remaining_buf(buffer) < sizeof(uint8_t)) {
		if ((buffer->size + BUF_SIZE) > MAX_BUF_SIZE) {
			error("%s: Buffer size limit exceeded (%u > %u)",
			      __func__, (buffer->size + BUF_SIZE),
			      MAX_BUF_SIZE);
			return;
		}
		buffer->size += BUF_SIZE;
		xrealloc_nz(buffer->head, buffer->size);
	}

	memcpy(&buffer->head[buffer->processed], &val, sizeof(uint8_t));
	buffer->processed += sizeof(uint8_t);
}

/*
 * Given a buffer containing a network byte order 8-bit integer,
 * store a host integer at 'valp', and adjust buffer counters.
 */
int unpack8(uint8_t * valp, Buf buffer)
{
	if (remaining_buf(buffer) < sizeof(uint8_t))
		return SLURM_ERROR;

	memcpy(valp, &buffer->head[buffer->processed], sizeof(uint8_t));
	buffer->processed += sizeof(uint8_t);
	return SLURM_SUCCESS;
}

/*
 * Given a pointer to memory (valp) and a size (size_val), convert
 * size_val to network byte order and store at buffer followed by
 * the data at valp. Adjust buffer counters.
 */
void packmem(char *valp, uint32_t size_val, Buf buffer)
{
	uint32_t ns = htonl(size_val);

	if (size_val > MAX_PACK_MEM_LEN) {
		error("%s: Buffer to be packed is too large (%u > %u)",
		      __func__, size_val, MAX_PACK_MEM_LEN);
		return;
	}
	if (remaining_buf(buffer) < (sizeof(ns) + size_val)) {
		if ((buffer->size + size_val + BUF_SIZE) > MAX_BUF_SIZE) {
			error("%s: Buffer size limit exceeded (%u > %u)",
			      __func__, (buffer->size + size_val + BUF_SIZE),
			      MAX_BUF_SIZE);
			return;
		}
		buffer->size += (size_val + BUF_SIZE);
		xrealloc_nz(buffer->head, buffer->size);
	}

	memcpy(&buffer->head[buffer->processed], &ns, sizeof(ns));
	buffer->processed += sizeof(ns);

	if (size_val) {
		memcpy(&buffer->head[buffer->processed], valp, size_val);
		buffer->processed += size_val;
	}
}


/*
 * Given a buffer containing a network byte order 16-bit integer,
 * and an arbitrary data string, return a pointer to the
 * data string in 'valp'.  Also return the sizes of 'valp' in bytes.
 * Adjust buffer counters.
 * NOTE: valp is set to point into the buffer bufp, a copy of
 *	the data is not made
 */
int unpackmem_ptr(char **valp, uint32_t * size_valp, Buf buffer)
{
	uint32_t ns;

	if (remaining_buf(buffer) < sizeof(ns))
		return SLURM_ERROR;

	memcpy(&ns, &buffer->head[buffer->processed], sizeof(ns));
	*size_valp = ntohl(ns);
	buffer->processed += sizeof(ns);

	if (*size_valp > MAX_PACK_MEM_LEN) {
		error("%s: Buffer to be unpacked is too large (%u > %u)",
		      __func__, *size_valp, MAX_PACK_MEM_LEN);
		return SLURM_ERROR;
	}
	else if (*size_valp > 0) {
		if (remaining_buf(buffer) < *size_valp)
			return SLURM_ERROR;
		*valp = &buffer->head[buffer->processed];
		buffer->processed += *size_valp;
	} else
		*valp = NULL;
	return SLURM_SUCCESS;
}


/*
 * Given a buffer containing a network byte order 16-bit integer,
 * and an arbitrary data string, copy the data string into the location
 * specified by valp.  Also return the sizes of 'valp' in bytes.
 * Adjust buffer counters.
 * NOTE: The caller is responsible for the management of valp and
 * insuring it has sufficient size
 */
int unpackmem(char *valp, uint32_t * size_valp, Buf buffer)
{
	uint32_t ns;

	if (remaining_buf(buffer) < sizeof(ns))
		return SLURM_ERROR;

	memcpy(&ns, &buffer->head[buffer->processed], sizeof(ns));
	*size_valp = ntohl(ns);
	buffer->processed += sizeof(ns);

	if (*size_valp > MAX_PACK_MEM_LEN) {
		error("%s: Buffer to be unpacked is too large (%u > %u)",
		      __func__, *size_valp, MAX_PACK_MEM_LEN);
		return SLURM_ERROR;
	}
	else if (*size_valp > 0) {
		if (remaining_buf(buffer) < *size_valp)
			return SLURM_ERROR;
		memcpy(valp, &buffer->head[buffer->processed], *size_valp);
		buffer->processed += *size_valp;
	} else
		*valp = 0;
	return SLURM_SUCCESS;
}

/*
 * Given a buffer containing a network byte order 16-bit integer,
 * and an arbitrary data string, copy the data string into the location
 * specified by valp.  Also return the sizes of 'valp' in bytes.
 * Adjust buffer counters.
 * NOTE: valp is set to point into a newly created buffer,
 *	the caller is responsible for calling xfree() on *valp
 *	if non-NULL (set to NULL on zero size buffer value)
 */
int unpackmem_xmalloc(char **valp, uint32_t * size_valp, Buf buffer)
{
	uint32_t ns;

	if (remaining_buf(buffer) < sizeof(ns))
		return SLURM_ERROR;

	memcpy(&ns, &buffer->head[buffer->processed], sizeof(ns));
	*size_valp = ntohl(ns);
	buffer->processed += sizeof(ns);

	if (*size_valp > MAX_PACK_MEM_LEN) {
		error("%s: Buffer to be unpacked is too large (%u > %u)",
		      __func__, *size_valp, MAX_PACK_MEM_LEN);
		return SLURM_ERROR;
	}
	else if (*size_valp > 0) {
		if (remaining_buf(buffer) < *size_valp)
			return SLURM_ERROR;
		*valp = xmalloc_nz(*size_valp);
		memcpy(*valp, &buffer->head[buffer->processed],
		       *size_valp);
		buffer->processed += *size_valp;
	} else
		*valp = NULL;
	return SLURM_SUCCESS;
}

/*
 * Given a buffer containing a network byte order 16-bit integer,
 * and an arbitrary data string, copy the data string into the location
 * specified by valp.  Also return the sizes of 'valp' in bytes.
 * Adjust buffer counters.
 * NOTE: valp is set to point into a newly created buffer,
 *	the caller is responsible for calling free() on *valp
 *	if non-NULL (set to NULL on zero size buffer value)
 */
int unpackmem_malloc(char **valp, uint32_t * size_valp, Buf buffer)
{
	uint32_t ns;

	if (remaining_buf(buffer) < sizeof(ns))
		return SLURM_ERROR;

	memcpy(&ns, &buffer->head[buffer->processed], sizeof(ns));
	*size_valp = ntohl(ns);
	buffer->processed += sizeof(ns);
	if (*size_valp > MAX_PACK_MEM_LEN) {
		error("%s: Buffer to be unpacked is too large (%u > %u)",
		      __func__, *size_valp, MAX_PACK_MEM_LEN);
		return SLURM_ERROR;
	}
	else if (*size_valp > 0) {
		if (remaining_buf(buffer) < *size_valp)
			return SLURM_ERROR;
		*valp = malloc(*size_valp);
		if (*valp == NULL) {
			log_oom(__FILE__, __LINE__, __CURRENT_FUNC__);
			abort();
		}
		memcpy(*valp, &buffer->head[buffer->processed],
		       *size_valp);
		buffer->processed += *size_valp;
	} else
		*valp = NULL;
	return SLURM_SUCCESS;
}

/*
 * Given a pointer to array of char * (char ** or char *[] ) and a size
 * (size_val), convert size_val to network byte order and store in the
 * buffer followed by the data at valp. Adjust buffer counters.
 */
void packstr_array(char **valp, uint32_t size_val, Buf buffer)
{
	int i;
	uint32_t ns = htonl(size_val);

	if (remaining_buf(buffer) < sizeof(ns)) {
		if ((buffer->size + BUF_SIZE) > MAX_BUF_SIZE) {
			error("%s: Buffer size limit exceeded (%u > %u)",
			      __func__, (buffer->size + BUF_SIZE),
			      MAX_BUF_SIZE);
			return;
		}
		buffer->size += BUF_SIZE;
		xrealloc_nz(buffer->head, buffer->size);
	}

	memcpy(&buffer->head[buffer->processed], &ns, sizeof(ns));
	buffer->processed += sizeof(ns);

	for (i = 0; i < size_val; i++) {
		packstr(valp[i], buffer);
	}

}

/*
 * Given 'buffer' pointing to a network byte order 16-bit integer
 * (size) and a array of strings  store the number of strings in
 * 'size_valp' and the array of strings in valp
 * NOTE: valp is set to point into a newly created buffer,
 *	the caller is responsible for calling xfree on *valp
 *	if non-NULL (set to NULL on zero size buffer value)
 */
int unpackstr_array(char ***valp, uint32_t * size_valp, Buf buffer)
{
	int i;
	uint32_t ns;
	uint32_t uint32_tmp;

	if (remaining_buf(buffer) < sizeof(ns))
		return SLURM_ERROR;

	memcpy(&ns, &buffer->head[buffer->processed], sizeof(ns));
	*size_valp = ntohl(ns);
	buffer->processed += sizeof(ns);

	if (*size_valp > MAX_PACK_ARRAY_LEN) {
		error("%s: Buffer to be unpacked is too large (%u > %u)",
		      __func__, *size_valp, MAX_PACK_ARRAY_LEN);
		return SLURM_ERROR;
	}
	else if (*size_valp > 0) {
		*valp = xmalloc_nz(sizeof(char *) * (*size_valp + 1));
		for (i = 0; i < *size_valp; i++) {
			if (unpackmem_xmalloc(&(*valp)[i], &uint32_tmp, buffer))
				return SLURM_ERROR;
		}
		(*valp)[i] = NULL;	/* NULL terminated array so that execle */
		/*    can detect end of array */
	} else
		*valp = NULL;
	return SLURM_SUCCESS;
}

/*
 * Given a pointer to memory (valp), size (size_val), and buffer,
 * store the memory contents into the buffer
 */
void packmem_array(char *valp, uint32_t size_val, Buf buffer)
{
	if (remaining_buf(buffer) < size_val) {
		if ((buffer->size + size_val + BUF_SIZE) > MAX_BUF_SIZE) {
			error("%s: Buffer size limit exceeded (%u > %u)",
			      __func__, (buffer->size + size_val + BUF_SIZE),
			      MAX_BUF_SIZE);
			return;
		}
		buffer->size += (size_val + BUF_SIZE);
		xrealloc_nz(buffer->head, buffer->size);
	}

	memcpy(&buffer->head[buffer->processed], valp, size_val);
	buffer->processed += size_val;
}

/*
 * Given a pointer to memory (valp), size (size_val), and buffer,
 * store the buffer contents into memory
 */
int unpackmem_array(char *valp, uint32_t size_valp, Buf buffer)
{
	if (remaining_buf(buffer) >= size_valp) {
		memcpy(valp, &buffer->head[buffer->processed], size_valp);
		buffer->processed += size_valp;
		return SLURM_SUCCESS;
	} else {
		*valp = 0;
		return SLURM_ERROR;
	}
}
