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
\****************************************************************************/

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "slurm/slurm_errno.h"
#include "slurm/slurm.h"

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/xmalloc.h"
#include "src/common/xassert.h"
#include "src/slurmdbd/read_config.h"

/*
 * Define slurm-specific aliases for use by plugins, see slurm_xlator.h
 * for details.
 */
strong_alias(create_buf,	slurm_create_buf);
strong_alias(create_mmap_buf,	slurm_create_mmap_buf);
strong_alias(free_buf,		slurm_free_buf);
strong_alias(grow_buf,		slurm_grow_buf);
strong_alias(init_buf,		slurm_init_buf);
strong_alias(xfer_buf_data,	slurm_xfer_buf_data);
strong_alias(pack_time,		slurm_pack_time);
strong_alias(unpack_time,	slurm_unpack_time);
strong_alias(packfloat, 	slurm_packfloat);
strong_alias(unpackfloat,	slurm_unpackfloat);
strong_alias(packdouble,	slurm_packdouble);
strong_alias(unpackdouble,	slurm_unpackdouble);
strong_alias(packlongdouble,	slurm_packlongdouble);
strong_alias(unpacklongdouble,	slurm_unpacklongdouble);
strong_alias(pack64,		slurm_pack64);
strong_alias(unpack64,		slurm_unpack64);
strong_alias(pack32,		slurm_pack32);
strong_alias(unpack32,		slurm_unpack32);
strong_alias(pack16,		slurm_pack16);
strong_alias(unpack16,		slurm_unpack16);
strong_alias(pack8,		slurm_pack8);
strong_alias(unpack8,		slurm_unpack8);
strong_alias(packbool,		slurm_packbool);
strong_alias(unpackbool,	slurm_unpackbool);
strong_alias(pack16_array,      slurm_pack16_array);
strong_alias(unpack16_array,    slurm_unpack16_array);
strong_alias(pack32_array,	slurm_pack32_array);
strong_alias(unpack32_array,	slurm_unpack32_array);
strong_alias(packmem,		slurm_packmem);
strong_alias(unpackmem_ptr,	slurm_unpackmem_ptr);
strong_alias(unpackmem_xmalloc,	slurm_unpackmem_xmalloc);
strong_alias(unpackstr_xmalloc, slurm_unpackstr_xmalloc);
strong_alias(unpackstr_xmalloc_escaped, slurm_unpackstr_xmalloc_escaped);
strong_alias(unpackstr_xmalloc_chooser, slurm_unpackstr_xmalloc_chooser);
strong_alias(packstr_array,	slurm_packstr_array);
strong_alias(unpackstr_array,	slurm_unpackstr_array);
strong_alias(packmem_array,	slurm_packmem_array);
strong_alias(unpackmem_array,	slurm_unpackmem_array);

/* Basic buffer management routines */
/* create_buf - create a buffer with the supplied contents, contents must
 * be xalloc'ed */
buf_t *create_buf(char *data, uint32_t size)
{
	buf_t *my_buf;

	if (size > MAX_BUF_SIZE) {
		error("%s: Buffer size limit exceeded (%u > %u)",
		      __func__, size, MAX_BUF_SIZE);
		return NULL;
	}

	my_buf = xmalloc_nz(sizeof(*my_buf));
	my_buf->magic = BUF_MAGIC;
	my_buf->size = size;
	my_buf->processed = 0;
	my_buf->head = data;
	my_buf->mmaped = false;

	return my_buf;
}

/*
 * create_mmap_buf - create an mmap()'d read-only buffer from
 * the supplied file.
 */
buf_t *create_mmap_buf(const char *file)
{
	buf_t *my_buf;
	int fd;
	struct stat f_stat;
	void *data;

	if ((fd = open(file, O_RDONLY | O_CLOEXEC)) < 0) {
		debug("%s: Failed to open file `%s`, %m", __func__, file);
		return NULL;
	}

	if (fstat(fd, &f_stat)) {
		debug("%s: Failed to fstat file `%s`, %m", __func__, file);
		close(fd);
		return NULL;
	}

	data = mmap(NULL, f_stat.st_size, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);
	if (data == MAP_FAILED) {
		debug("%s: Failed to mmap file `%s`, %m", __func__, file);
		return NULL;
	}

	my_buf = xmalloc_nz(sizeof(*my_buf));
	my_buf->magic = BUF_MAGIC;
	my_buf->size = f_stat.st_size;
	my_buf->processed = 0;
	my_buf->head = data;
	my_buf->mmaped = true;

	debug3("%s: loaded file `%s` as buf_t", __func__, file);

	return my_buf;
}


/* free_buf - release memory associated with a given buffer */
void free_buf(buf_t *my_buf)
{
	if (!my_buf)
		return;
	xassert(my_buf->magic == BUF_MAGIC);
	if (my_buf->mmaped)
		munmap(my_buf->head, my_buf->size);
	else
		xfree(my_buf->head);

	xfree(my_buf);
}

/* Grow a buffer by the specified amount */
void grow_buf(buf_t *buffer, uint32_t size)
{
	if (buffer->mmaped)
		fatal_abort("attempt to grow mmap()'d buffer not supported");
	if ((buffer->size + size) > MAX_BUF_SIZE) {
		error("%s: Buffer size limit exceeded (%u > %u)",
		      __func__, (buffer->size + size), MAX_BUF_SIZE);
		return;
	}

	buffer->size += size;
	xrealloc_nz(buffer->head, buffer->size);
}

/* init_buf - create an empty buffer of the given size */
buf_t *init_buf(uint32_t size)
{
	buf_t *my_buf;

	if (size > MAX_BUF_SIZE) {
		error("%s: Buffer size limit exceeded (%u > %u)",
		      __func__, size, MAX_BUF_SIZE);
		return NULL;
	}
	if (size <= 0)
		size = BUF_SIZE;
	my_buf = xmalloc_nz(sizeof(*my_buf));
	my_buf->magic = BUF_MAGIC;
	my_buf->size = size;
	my_buf->processed = 0;
	my_buf->head = xmalloc(size);
	my_buf->mmaped = false;
	return my_buf;
}

/* xfer_buf_data - return a pointer to the buffer's data and release the
 * buffer's structure */
void *xfer_buf_data(buf_t *my_buf)
{
	void *data_ptr;

	xassert(my_buf->magic == BUF_MAGIC);

	if (my_buf->mmaped)
		fatal_abort("attempt to xfer mmap()'d buffer not supported");

	data_ptr = (void *) my_buf->head;
	xfree(my_buf);
	return data_ptr;
}

/*
 * Given a time_t in host byte order, promote it to int64_t, convert to
 * network byte order, store in buffer and adjust buffer acc'd'ngly
 */
void pack_time(time_t val, buf_t *buffer)
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

int unpack_time(time_t *valp, buf_t *buffer)
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
 * NOTE: There is an IEEE standard format for double.
 */
void packdouble(double val, buf_t *buffer)
{
	uint64_t nl;
	union {
		double d;
		uint64_t u;
	} uval;

	/*
	 * The 0.5 is here to round off.  We have found on systems going out
	 * more than 15 decimals will mess things up, but rounding corrects it.
	 */
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
 * Given a buffer containing a network byte order 32-bit integer,
 * typecast as float, and  divide by FLOAT_MULT
 * store a host float at 'valp', and adjust buffer counters.
 * NOTE: There is an IEEE standard format for float.
 */
int unpackfloat(float *valp, buf_t *buffer)
{
	uint32_t nl;
	union {
		float f;
		uint32_t u;
	} uval;

	if (unpack32(&nl, buffer) != SLURM_SUCCESS)
		return SLURM_ERROR;

	uval.u = nl;
	*valp = uval.f / FLOAT_MULT;

	return SLURM_SUCCESS;
}

/*
 * Given a float, multiple by FLOAT_MULT and then
 * typecast to a uint32_t in host byte order, convert to network byte order
 * store in buffer, and adjust buffer counters.
 * NOTE: There is an IEEE standard format for float.
 */
void packfloat(float val, buf_t *buffer)
{
	union {
		float f;
		uint32_t u;
	} uval;

	/*
	 * The FLOAT_MULT is here to round off.  We have found on systems going
	 * out more than 15 decimals will mess things up, but rounding corrects
	 * it.
	 */
	uval.f = (val * FLOAT_MULT);
	pack32(uval.u, buffer);
}

/*
 * Given a buffer containing a network byte order 64-bit integer,
 * typecast as double, and  divide by FLOAT_MULT
 * store a host double at 'valp', and adjust buffer counters.
 * NOTE: There is an IEEE standard format for double.
 */
int unpackdouble(double *valp, buf_t *buffer)
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
 * long double has no standard format, so pass the data as a string
 */
void packlongdouble(long double val, buf_t *buffer)
{
	char val_str[256];

	snprintf(val_str, sizeof(val_str), "%Lf", val);
	packstr(val_str, buffer);
}

/*
 * long double has no standard format, so pass the data as a string
 */
int unpacklongdouble(long double *valp, buf_t *buffer)
{
	long double nl;
	char *val_str = NULL;
	uint32_t size_val_str = 0;
	int rc;

	rc = unpackmem_ptr(&val_str, &size_val_str, buffer);
	if (rc != SLURM_SUCCESS)
		return rc;

	if (sscanf(val_str, "%Lf", &nl) != 1)
		return SLURM_ERROR;

	/*
	 * Workaround for a flawed glibc version which printed "nan" instead of
	 * "0.000000" for long doubles. Restoring these as NaN will corrupt
	 * the association state, so ensure they're 0.
	 *
	 * https://bugzilla.redhat.com/show_bug.cgi?id=1925204
	 */
	if (isnan(nl))
		nl = 0L;

	*valp = nl;
	return SLURM_SUCCESS;
}

/*
 * Given a 64-bit integer in host byte order, convert to network byte order
 * store in buffer, and adjust buffer counters.
 */
void pack64(uint64_t val, buf_t *buffer)
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
int unpack64(uint64_t *valp, buf_t *buffer)
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
void pack32(uint32_t val, buf_t *buffer)
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
int unpack32(uint32_t *valp, buf_t *buffer)
{
	uint32_t nl;
	if (remaining_buf(buffer) < sizeof(nl))
		return SLURM_ERROR;

	memcpy(&nl, &buffer->head[buffer->processed], sizeof(nl));
	*valp = ntohl(nl);
	buffer->processed += sizeof(nl);
	return SLURM_SUCCESS;
}

/*
 * Given a *uint16_t, it will pack an array of size_val
 */
void pack16_array(uint16_t *valp, uint32_t size_val, buf_t *buffer)
{
	uint32_t i = 0;

	xassert(valp || !size_val);

	pack32(size_val, buffer);

	for (i = 0; i < size_val; i++) {
		pack16(*(valp + i), buffer);
	}
}

/*
 * Given a int ptr, it will unpack an array of size_val
 */
int unpack16_array(uint16_t **valp, uint32_t *size_val, buf_t *buffer)
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

/*
 * Given a *uint32_t, it will pack an array of size_val
 */
void pack32_array(uint32_t *valp, uint32_t size_val, buf_t *buffer)
{
	uint32_t i = 0;

	xassert(valp || !size_val);

	pack32(size_val, buffer);

	for (i = 0; i < size_val; i++) {
		pack32(*(valp + i), buffer);
	}
}

/*
 * Given a int ptr, it will unpack an array of size_val
 */
int unpack32_array(uint32_t **valp, uint32_t *size_val, buf_t *buffer)
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

/*
 * Given a *uint64_t, it will pack an array of size_val
 */
void pack64_array(uint64_t *valp, uint32_t size_val, buf_t *buffer)
{
	uint32_t i = 0;

	xassert(valp || !size_val);

	pack32(size_val, buffer);

	for (i = 0; i < size_val; i++) {
		pack64(*(valp + i), buffer);
	}
}

/* Given a int ptr, it will unpack an array of size_val
 */
int unpack64_array(uint64_t **valp, uint32_t *size_val, buf_t *buffer)
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

void packdouble_array(double *valp, uint32_t size_val, buf_t *buffer)
{
	uint32_t i = 0;

	xassert(valp || !size_val);

	pack32(size_val, buffer);

	for (i = 0; i < size_val; i++) {
		packdouble(*(valp + i), buffer);
	}
}

int unpackdouble_array(double **valp, uint32_t* size_val, buf_t *buffer)
{
	uint32_t i = 0;

	if (unpack32(size_val, buffer))
		return SLURM_ERROR;

	*valp = xmalloc_nz((*size_val) * sizeof(double));
	for (i = 0; i < *size_val; i++) {
		if (unpackdouble((*valp) + i, buffer))
			return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

void packlongdouble_array(long double *valp, uint32_t size_val, buf_t *buffer)
{
	uint32_t i = 0;

	xassert(valp || !size_val);

	pack32(size_val, buffer);

	for (i = 0; i < size_val; i++) {
		packlongdouble(*(valp + i), buffer);
	}
}

int unpacklongdouble_array(long double **valp, uint32_t *size_val,
			   buf_t *buffer)
{
	uint32_t i = 0;

	if (unpack32(size_val, buffer))
		return SLURM_ERROR;

	*valp = xmalloc_nz((*size_val) * sizeof(long double));
	for (i = 0; i < *size_val; i++) {
		if (unpacklongdouble((*valp) + i, buffer))
			return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}



/*
 * Given a 16-bit integer in host byte order, convert to network byte order,
 * store in buffer and adjust buffer counters.
 */
void pack16(uint16_t val, buf_t *buffer)
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
int unpack16(uint16_t *valp, buf_t *buffer)
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
void pack8(uint8_t val, buf_t *buffer)
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
int unpack8(uint8_t *valp, buf_t *buffer)
{
	if (remaining_buf(buffer) < sizeof(uint8_t))
		return SLURM_ERROR;

	memcpy(valp, &buffer->head[buffer->processed], sizeof(uint8_t));
	buffer->processed += sizeof(uint8_t);
	return SLURM_SUCCESS;
}

/*
 * Given a boolean in host byte order, convert to network byte order
 * store in buffer, and adjust buffer counters.
 */
void packbool(bool val, buf_t *buffer)
{
	uint8_t tmp8 = val;
	pack8(tmp8, buffer);
}

/*
 * Given a buffer containing a network byte order 8-bit integer,
 * store a host integer at 'valp', and adjust buffer counters.
 */
int unpackbool(bool *valp, buf_t *buffer)
{
	uint8_t tmp8 = 0;

	if (unpack8(&tmp8, buffer) != SLURM_SUCCESS)
		return SLURM_ERROR;

	if (tmp8)
		*valp = tmp8;
	else
		*valp = 0;

	return SLURM_SUCCESS;
}

/*
 * Append the contents of the source buffer into the target buffer while
 * validating buffer size constraints.
 */
extern void packbuf(buf_t *source, buf_t *buffer)
{
	uint32_t size_val = get_buf_offset(source);

	if (!size_val)
		return;

	if (remaining_buf(buffer) < size_val) {
		if ((buffer->size + size_val) > MAX_BUF_SIZE) {
			error("%s: Buffer size limit exceeded (%u > %u)",
			      __func__, (buffer->size + size_val),
			      MAX_BUF_SIZE);
			return;
		}
		buffer->size += size_val;
		xrealloc_nz(buffer->head, buffer->size);
	}

	memcpy(&buffer->head[buffer->processed], get_buf_data(source), size_val);
	buffer->processed += size_val;
}

/*
 * Given a pointer to memory (valp) and a size (size_val), convert
 * size_val to network byte order and store at buffer followed by
 * the data at valp. Adjust buffer counters.
 */
extern void packmem(void *valp, uint32_t size_val, buf_t *buffer)
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
int unpackmem_ptr(char **valp, uint32_t *size_valp, buf_t *buffer)
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
 * NOTE: valp is set to point into a newly created buffer,
 *	the caller is responsible for calling xfree() on *valp
 *	if non-NULL (set to NULL on zero size buffer value)
 */
int unpackmem_xmalloc(char **valp, uint32_t *size_valp, buf_t *buffer)
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
 *	the caller is responsible for calling xfree() on *valp
 *	if non-NULL (set to NULL on zero size buffer value)
 */
int unpackstr_xmalloc(char **valp, uint32_t *size_valp, buf_t *buffer)
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
	} else if (*size_valp > 0) {
		if (remaining_buf(buffer) < *size_valp)
			return SLURM_ERROR;
		if (buffer->head[buffer->processed + *size_valp - 1] != '\0')
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
 * and an arbitrary char string, copy the data string into the location
 * specified by valp and escape ' and \ to be database safe.
 * Also return the sizes of 'valp' in bytes.
 * Adjust buffer counters.
 * NOTE: valp is set to point into a newly created buffer,
 *	the caller is responsible for calling xfree() on *valp
 *	if non-NULL (set to NULL on zero size buffer value)
 * NOTE: size_valp may not match how much data was processed from buffer, but
 *       will match the length of the returned 'valp'.
 * WARNING: These escapes are sufficient to protect MariaDB/MySQL, but
 *          may not be sufficient if databases are added in the future.
 */
int unpackstr_xmalloc_escaped(char **valp, uint32_t *size_valp, buf_t *buffer)
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
	} else if (*size_valp > 0) {
		uint32_t cnt = *size_valp;

		if (remaining_buf(buffer) < cnt)
			return SLURM_ERROR;

		/* make a buffer 2 times the size just to be safe */
		*valp = xmalloc_nz((cnt * 2) + 1);
		if (*valp) {
			char *copy = NULL, *str, tmp;
			uint32_t i;
			copy = *valp;
			str = &buffer->head[buffer->processed];

			for (i = 0; i < cnt && *str; i++) {
				tmp = *str++;
				if ((tmp == '\\') || (tmp == '\'')) {
					*copy++ = '\\';
					(*size_valp)++;
				}

				*copy++ = tmp;
			}

			/* Since we used xmalloc_nz, terminate the string. */
			*copy++ = '\0';
		}

		/* add the original value since that is what we processed */
		buffer->processed += cnt;
	} else
		*valp = NULL;
	return SLURM_SUCCESS;
}

int unpackstr_xmalloc_chooser(char **valp, uint32_t *size_valp, buf_t *buf)
{
	if (slurmdbd_conf)
		return unpackstr_xmalloc_escaped(valp, size_valp, buf);
	else
		return unpackstr_xmalloc(valp, size_valp, buf);
}


/*
 * Given a pointer to array of char * (char ** or char *[] ) and a size
 * (size_val), convert size_val to network byte order and store in the
 * buffer followed by the data at valp. Adjust buffer counters.
 */
void packstr_array(char **valp, uint32_t size_val, buf_t *buffer)
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
 * Unpack a NULL-terminated array of strings from buffer.
 * These are stored as a 32-bit (network-byte order) number of elements,
 * followed by the individual strings (packed with packstr()).
 * OUT: valp - xmalloc()'d array or NULL. Free with xfree_array().
 * OUT: size_valp - number of elements, not including the NULL-termination.
 * IN/OUT: buffer
 */
int unpackstr_array(char ***valp, uint32_t *size_valp, buf_t *buffer)
{
	int i;
	uint32_t ns;
	uint32_t uint32_tmp;

	if (remaining_buf(buffer) < sizeof(ns))
		return SLURM_ERROR;

	memcpy(&ns, &buffer->head[buffer->processed], sizeof(ns));
	*size_valp = ntohl(ns);
	buffer->processed += sizeof(ns);

	if (*size_valp > 0) {
		*valp = xcalloc(*size_valp + 1, sizeof(char *));
		for (i = 0; i < *size_valp; i++) {
			if (unpackstr_xmalloc(&(*valp)[i], &uint32_tmp, buffer)) {
				*size_valp = 0;
				xfree_array(*valp);
				return SLURM_ERROR;
			}
		}
	} else
		*valp = NULL;
	return SLURM_SUCCESS;
}

/*
 * Given a pointer to memory (valp), size (size_val), and buffer,
 * store the memory contents into the buffer
 */
void packmem_array(char *valp, uint32_t size_val, buf_t *buffer)
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
int unpackmem_array(char *valp, uint32_t size_valp, buf_t *buffer)
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
