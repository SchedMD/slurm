/****************************************************************************\
 *  pack.h - definitions for lowest level un/pack functions. all functions
 *	utilize a buf_t structure. Call init_buf, un/pack, and free_buf
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>, Morris Jette <jette1@llnl.gov>, et. al.
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

#ifndef _PACK_INCLUDED
#define _PACK_INCLUDED

#include <inttypes.h>
#include <time.h>
#include <stdbool.h>
#include <string.h>

#include "src/common/bitstring.h"
#include "src/common/xassert.h"

/*
 *  Maximum message size. Messages larger than this value (in bytes)
 *  will not be received.
 */
#define MAX_MSG_SIZE (1024 * 1024 * 1024)

#define BUF_MAGIC 0x42554545
#define BUF_SIZE (16 * 1024)
#define MAX_BUF_SIZE ((uint32_t) 0xffff0000)	/* avoid going over 32-bits */
#define REASONABLE_BUF_SIZE ((uint32_t) 0xbfff4000) /* three-quarters of max */
#define FLOAT_MULT 1000000

/* If we unpack a buffer that contains bad data, we want to avoid a memory
 * allocation error due to array or buffer sizes that are unreasonably large */
#define MAX_PACK_MEM_LEN	(1024 * 1024 * 1024)

typedef struct {
	uint32_t magic;
	char *head;
	uint32_t size;
	uint32_t processed;
	bool mmaped;
} buf_t;

#define get_buf_data(__buf)		(__buf->head)
#define get_buf_offset(__buf)		(__buf->processed)
#define set_buf_offset(__buf,__val)	(__buf->processed = __val)
#define remaining_buf(__buf)		(__buf->size - __buf->processed)
#define size_buf(__buf)			(__buf->size)

extern buf_t *create_buf(char *data, uint32_t size);
extern buf_t *create_mmap_buf(const char *file);
extern void free_buf(buf_t *my_buf);
extern buf_t *init_buf(uint32_t size);
extern void grow_buf(buf_t *my_buf, uint32_t size);
extern void *xfer_buf_data(buf_t *my_buf);

extern void pack_time(time_t val, buf_t *buffer);
extern int unpack_time(time_t *valp, buf_t *buffer);

extern void packfloat(float val, buf_t *buffer);
extern int unpackfloat(float *valp, buf_t *buffer);

extern void packdouble(double val, buf_t *buffer);
extern int unpackdouble(double *valp, buf_t *buffer);

extern void packlongdouble(long double val, buf_t *buffer);
extern int unpacklongdouble(long double *valp, buf_t *buffer);

extern void pack64(uint64_t val, buf_t *buffer);
extern int unpack64(uint64_t *valp, buf_t *buffer);

extern void pack32(uint32_t val, buf_t *buffer);
extern int unpack32(uint32_t *valp, buf_t *buffer);

extern void pack16(uint16_t val, buf_t *buffer);
extern int unpack16(uint16_t *valp, buf_t *buffer);

extern void pack8(uint8_t val, buf_t *buffer);
extern int unpack8(uint8_t *valp, buf_t *buffer);

extern void packbool(bool val, buf_t *buffer);
extern int unpackbool(bool *valp, buf_t *buffer);

extern void pack16_array(uint16_t *valp, uint32_t size_val, buf_t *buffer);
extern int unpack16_array(uint16_t **valp, uint32_t *size_val, buf_t *buffer);

extern void pack32_array(uint32_t *valp, uint32_t size_val, buf_t *buffer);
extern int unpack32_array(uint32_t **valp, uint32_t *size_val, buf_t *buffer);

extern void pack64_array(uint64_t *valp, uint32_t size_val, buf_t *buffer);
extern int unpack64_array(uint64_t **valp, uint32_t *size_val, buf_t *buffer);

extern void packdouble_array(double *valp, uint32_t size_val, buf_t *buffer);
extern int unpackdouble_array(double **valp, uint32_t *size_val, buf_t *buffer);

extern void packlongdouble_array(long double *valp, uint32_t size_val,
				 buf_t *buffer);
extern int unpacklongdouble_array(long double **valp, uint32_t *size_val,
				  buf_t *buffer);

extern void packmem(void *valp, uint32_t size_val, buf_t *buffer);
extern int unpackmem_ptr(char **valp, uint32_t *size_valp, buf_t *buffer);
extern int unpackmem_xmalloc(char **valp, uint32_t *size_valp, buf_t *buffer);
extern int unpackmem_malloc(char **valp, uint32_t *size_valp, buf_t *buffer);

extern int unpackstr_xmalloc_escaped(char **valp, uint32_t *size_valp,
				     buf_t *buffer);
extern int unpackstr_xmalloc_chooser(char **valp, uint32_t *size_valp,
				     buf_t *buffer);

extern void packstr_array(char **valp, uint32_t size_val, buf_t *buffer);
extern int unpackstr_array(char ***valp, uint32_t* size_val, buf_t *buffer);

extern void packmem_array(char *valp, uint32_t size_val, buf_t *buffer);
extern int unpackmem_array(char *valp, uint32_t size_valp, buf_t *buffer);

#define safe_unpack_time(valp,buf) do {			\
	xassert(sizeof(*valp) == sizeof(time_t));	\
	xassert(buf->magic == BUF_MAGIC);		\
        if (unpack_time(valp,buf))			\
		goto unpack_error;			\
} while (0)

#define safe_unpackfloat(valp,buf) do {		\
	xassert(sizeof(*valp) == sizeof(float));        \
	xassert(buf->magic == BUF_MAGIC);		\
        if (unpackfloat(valp,buf))			\
		goto unpack_error;			\
} while (0)

#define safe_unpackdouble(valp,buf) do {		\
	xassert(sizeof(*valp) == sizeof(double));	\
	xassert(buf->magic == BUF_MAGIC);		\
        if (unpackdouble(valp,buf))			\
		goto unpack_error;			\
} while (0)

#define safe_unpacklongdouble(valp,buf) do {		\
	xassert(sizeof(*valp) == sizeof(long double));	\
	xassert(buf->magic == BUF_MAGIC);		\
        if (unpacklongdouble(valp,buf))			\
		goto unpack_error;			\
} while (0)

#define safe_unpack64(valp,buf) do {			\
	xassert(sizeof(*valp) == sizeof(uint64_t));	\
	xassert(buf->magic == BUF_MAGIC);		\
        if (unpack64(valp,buf))				\
		goto unpack_error;			\
} while (0)

#define safe_unpack32(valp,buf) do {			\
	xassert(sizeof(*valp) == sizeof(uint32_t));	\
	xassert(buf->magic == BUF_MAGIC);		\
        if (unpack32(valp,buf))				\
		goto unpack_error;			\
} while (0)

#define safe_unpack16(valp,buf) do {			\
	xassert(sizeof(*valp) == sizeof(uint16_t)); 	\
	xassert(buf->magic == BUF_MAGIC);		\
        if (unpack16(valp,buf))				\
		goto unpack_error;			\
} while (0)

#define safe_unpack8(valp,buf) do {			\
	xassert(sizeof(*valp) == sizeof(uint8_t)); 	\
	xassert(buf->magic == BUF_MAGIC);		\
        if (unpack8(valp,buf))				\
		goto unpack_error;			\
} while (0)

#define safe_unpackbool(valp,buf) do {			\
	xassert(sizeof(*valp) == sizeof(bool)); 	\
	xassert(buf->magic == BUF_MAGIC);		\
        if (unpackbool(valp,buf))			\
		goto unpack_error;			\
} while (0)

#define safe_unpack16_array(valp,size_valp,buf) do {    \
        xassert(sizeof(*size_valp) == sizeof(uint32_t));\
        xassert(buf->magic == BUF_MAGIC);		\
        if (unpack16_array(valp,size_valp,buf))         \
                goto unpack_error;                      \
} while (0)

#define safe_unpack32_array(valp,size_valp,buf) do {	\
	xassert(sizeof(*size_valp) == sizeof(uint32_t));\
	xassert(buf->magic == BUF_MAGIC);		\
	if (unpack32_array(valp,size_valp,buf))		\
		goto unpack_error;			\
} while (0)

#define safe_unpack64_array(valp,size_valp,buf) do {	\
	xassert(sizeof(*size_valp) == sizeof(uint32_t));\
	xassert(buf->magic == BUF_MAGIC);		\
	if (unpack64_array(valp,size_valp,buf))		\
		goto unpack_error;			\
} while (0)

#define safe_unpackdouble_array(valp,size_valp,buf) do {	\
	xassert(sizeof(*size_valp) == sizeof(uint32_t));\
	xassert(buf->magic == BUF_MAGIC);		\
	if (unpackdouble_array(valp,size_valp,buf))	\
		goto unpack_error;			\
} while (0)

#define safe_unpacklongdouble_array(valp,size_valp,buf) do {	\
	xassert(sizeof(*size_valp) == sizeof(uint32_t));\
	xassert(buf->magic == BUF_MAGIC);		\
	if (unpacklongdouble_array(valp,size_valp,buf))	\
		goto unpack_error;			\
} while (0)

#define safe_unpackmem_ptr(valp,size_valp,buf) do {	\
	xassert(sizeof(*size_valp) == sizeof(uint32_t));\
	xassert(buf->magic == BUF_MAGIC);		\
	if (unpackmem_ptr(valp,size_valp,buf))		\
		goto unpack_error;			\
} while (0)

#define safe_unpackmem_xmalloc(valp,size_valp,buf) do {	\
	xassert(sizeof(*size_valp) == sizeof(uint32_t));\
	xassert(buf->magic == BUF_MAGIC);		\
	if (unpackmem_xmalloc(valp,size_valp,buf))	\
		goto unpack_error;			\
} while (0)

#define safe_unpackmem_malloc(valp,size_valp,buf) do {	\
	xassert(sizeof(*size_valp) == sizeof(uint32_t));\
	xassert(buf->magic == BUF_MAGIC);		\
	if (unpackmem_malloc(valp,size_valp,buf))	\
		goto unpack_error;			\
} while (0)

#define packstr(str,buf) do {				\
	uint32_t _size = 0;				\
	if((char *)str != NULL)				\
		_size = (uint32_t)strlen(str)+1;	\
        xassert(_size == 0 || str != NULL);		\
	xassert(_size <= 0xffffffff);			\
	xassert(buf->magic == BUF_MAGIC);		\
	packmem(str,(uint32_t)_size,buf);		\
} while (0)

#define packnull(buf) do { \
	xassert(buf != NULL); \
	xassert(buf->magic == BUF_MAGIC); \
	packmem(NULL, 0, buf); \
} while (0)

#define pack_bit_str_hex(bitmap,buf) do {		\
	xassert(buf->magic == BUF_MAGIC);		\
	if (bitmap) {					\
		char *_tmp_str;				\
		uint32_t _size;				\
		_tmp_str = bit_fmt_hexmask(bitmap);	\
		_size = bit_size(bitmap);               \
		pack32(_size, buf);              	\
		_size = strlen(_tmp_str)+1;		\
		packmem(_tmp_str,_size,buf);	        \
		xfree(_tmp_str);			\
	} else						\
		pack32(NO_VAL, buf);                 	\
} while (0)

#define unpack_bit_str_hex(bitmap,buf) do {				\
	char *tmp_str = NULL;						\
	uint32_t _size, _tmp_uint32;					\
	xassert(*bitmap == NULL);					\
	xassert(buf->magic == BUF_MAGIC);				\
	safe_unpack32(&_size, buf);					\
	if (_size != NO_VAL) {						\
		safe_unpackstr_xmalloc(&tmp_str, &_tmp_uint32, buf);	\
		if (_size) {						\
			*bitmap = bit_alloc(_size);			\
			if (bit_unfmt_hexmask(*bitmap, tmp_str)) {	\
				FREE_NULL_BITMAP(*bitmap);		\
				xfree(tmp_str);				\
				goto unpack_error;			\
			}						\
		} else							\
			*bitmap = NULL;					\
		xfree(tmp_str);						\
	} else								\
		*bitmap = NULL;						\
} while (0)

/* note: this would be faster if collapsed into a single function
 * rather than a combination of unpack_bit_str_hex and bitstr2inx */
#define unpack_bit_str_hex_as_inx(inx, buf) do {	\
	bitstr_t *b = NULL;				\
	unpack_bit_str_hex(&b, buf);			\
	*inx = bitstr2inx(b);				\
	FREE_NULL_BITMAP(b);				\
} while (0)

#define unpackstr_malloc	                        \
        unpackmem_malloc

#define unpackstr_xmalloc	                        \
        unpackmem_xmalloc

#define safe_unpackstr_malloc	                        \
        safe_unpackmem_malloc

#define safe_unpackstr(valp, buf) do {				\
	uint32_t size_valp;					\
	xassert(buf->magic == BUF_MAGIC);		        \
	if (unpackstr_xmalloc_chooser(valp, &size_valp, buf))   \
		goto unpack_error;		       		\
} while (0)

#define safe_unpackstr_xmalloc(valp, size_valp, buf) do {	\
	xassert(sizeof(*size_valp) == sizeof(uint32_t));	\
	xassert(buf->magic == BUF_MAGIC);		        \
	if (unpackstr_xmalloc_chooser(valp, size_valp, buf))    \
		goto unpack_error;		       		\
} while (0)

#define safe_unpackstr_array(valp,size_valp,buf) do {	\
	xassert(sizeof(*size_valp) == sizeof(uint32_t)); \
	xassert(buf->magic == BUF_MAGIC);		\
	if (unpackstr_array(valp,size_valp,buf))	\
		goto unpack_error;			\
} while (0)

#define safe_unpackmem_array(valp,size,buf) do {	\
	xassert(valp != NULL);				\
	xassert(sizeof(size) == sizeof(uint32_t)); 	\
	xassert(buf->magic == BUF_MAGIC);		\
	if (unpackmem_array(valp,size,buf))		\
		goto unpack_error;			\
} while (0)

#define safe_xcalloc(p, cnt, sz) do {			\
	size_t _cnt = cnt;				\
	size_t _sz = sz;				\
	if (!_cnt || !_sz)				\
		p = NULL;				\
	else if (!(p = try_xcalloc(_cnt, _sz)))		\
		goto unpack_error;			\
} while (0)

#define safe_xmalloc(p, sz) do {			\
	size_t _sz = sz;				\
	if (!_sz)					\
		p = NULL;				\
	else if (!(p = try_xmalloc(_sz)))		\
		goto unpack_error;			\
} while (0)

#define FREE_NULL_BUFFER(_X)		\
	do {				\
		if (_X) free_buf (_X);	\
		_X	= NULL; 	\
	} while (0)

#endif /* _PACK_INCLUDED */
