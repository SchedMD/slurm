/****************************************************************************\
 *  pack.h - definitions for lowest level un/pack functions. all functions
 *	utilize a Buf structure. Call init_buf, un/pack, and free_buf
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

#include <assert.h>
#include <inttypes.h>
#include <time.h>
#include <stdbool.h>
#include <string.h>

#include "src/common/bitstring.h"

#define BUF_MAGIC 0x42554545
#define BUF_SIZE (16 * 1024)
#define MAX_BUF_SIZE ((uint32_t) 0xffff0000)	/* avoid going over 32-bits */
#define REASONABLE_BUF_SIZE ((uint32_t) 0xbfff4000) /* three-quarters of max */
#define FLOAT_MULT 1000000

/* If we unpack a buffer that contains bad data, we want to avoid a memory
 * allocation error due to array or buffer sizes that are unreasonably large */
#define MAX_PACK_ARRAY_LEN	(128 * 1024)
#define MAX_PACK_MEM_LEN	(1024 * 1024 * 1024)

struct slurm_buf {
	uint32_t magic;
	char *head;
	uint32_t size;
	uint32_t processed;
	bool mmaped;
};

typedef struct slurm_buf * Buf;

#define get_buf_data(__buf)		(__buf->head)
#define get_buf_offset(__buf)		(__buf->processed)
#define set_buf_offset(__buf,__val)	(__buf->processed = __val)
#define remaining_buf(__buf)		(__buf->size - __buf->processed)
#define size_buf(__buf)			(__buf->size)

Buf	create_buf (char *data, uint32_t size);
Buf	create_mmap_buf(char *file);
void	free_buf(Buf my_buf);
Buf	init_buf(uint32_t size);
void    grow_buf (Buf my_buf, uint32_t size);
void	*xfer_buf_data(Buf my_buf);

void	pack_time(time_t val, Buf buffer);
int	unpack_time(time_t *valp, Buf buffer);

void 	packfloat(float val, Buf buffer);
int	unpackfloat(float *valp, Buf buffer);

void 	packdouble(double val, Buf buffer);
int	unpackdouble(double *valp, Buf buffer);

void 	packlongdouble(long double val, Buf buffer);
int	unpacklongdouble(long double *valp, Buf buffer);

void 	pack64(uint64_t val, Buf buffer);
int	unpack64(uint64_t *valp, Buf buffer);

void 	pack32(uint32_t val, Buf buffer);
int	unpack32(uint32_t *valp, Buf buffer);

void	pack16(uint16_t val, Buf buffer);
int	unpack16(uint16_t *valp, Buf buffer);

void	pack8(uint8_t val, Buf buffer);
int	unpack8(uint8_t *valp, Buf buffer);

void	packbool(bool val, Buf buffer);
int	unpackbool(bool *valp, Buf buffer);

void    pack16_array(uint16_t *valp, uint32_t size_val, Buf buffer);
int     unpack16_array(uint16_t **valp, uint32_t* size_val, Buf buffer);

void	pack32_array(uint32_t *valp, uint32_t size_val, Buf buffer);
int	unpack32_array(uint32_t **valp, uint32_t* size_val, Buf buffer);

void	pack64_array(uint64_t *valp, uint32_t size_val, Buf buffer);
int	unpack64_array(uint64_t **valp, uint32_t* size_val, Buf buffer);

void	pack64_array_as_32(uint64_t * valp, uint32_t size_val, Buf buffer);
int	unpack64_array_from_32(uint64_t ** valp, uint32_t * size_val, Buf buffer);

void	packdouble_array(double *valp, uint32_t size_val, Buf buffer);
int	unpackdouble_array(double **valp, uint32_t* size_val, Buf buffer);

void	packlongdouble_array(long double *valp, uint32_t size_val, Buf buffer);
int	unpacklongdouble_array(long double **valp, uint32_t* size_val,
			       Buf buffer);

void	packmem(char *valp, uint32_t size_val, Buf buffer);
int	unpackmem(char *valp, uint32_t *size_valp, Buf buffer);
int	unpackmem_ptr(char **valp, uint32_t *size_valp, Buf buffer);
int	unpackmem_xmalloc(char **valp, uint32_t *size_valp, Buf buffer);
int	unpackmem_malloc(char **valp, uint32_t *size_valp, Buf buffer);

int	unpackstr_xmalloc_escaped(char **valp, uint32_t *size_valp, Buf buffer);
int	unpackstr_xmalloc_chooser(char **valp, uint32_t *size_valp, Buf buffer);

void	packstr_array(char **valp, uint32_t size_val, Buf buffer);
int	unpackstr_array(char ***valp, uint32_t* size_val, Buf buffer);

void	packmem_array(char *valp, uint32_t size_val, Buf buffer);
int	unpackmem_array(char *valp, uint32_t size_valp, Buf buffer);

#define safe_unpack_time(valp,buf) do {			\
	assert(sizeof(*valp) == sizeof(time_t));	\
	assert(buf->magic == BUF_MAGIC);		\
        if (unpack_time(valp,buf))			\
		goto unpack_error;			\
} while (0)

#define safe_unpackfloat(valp,buf) do {		\
	assert(sizeof(*valp) == sizeof(float));        \
	assert(buf->magic == BUF_MAGIC);		\
        if (unpackfloat(valp,buf))			\
		goto unpack_error;			\
} while (0)

#define safe_unpackdouble(valp,buf) do {		\
	assert(sizeof(*valp) == sizeof(double));        \
	assert(buf->magic == BUF_MAGIC);		\
        if (unpackdouble(valp,buf))			\
		goto unpack_error;			\
} while (0)

#define safe_unpacklongdouble(valp,buf) do {		\
	assert(sizeof(*valp) == sizeof(long double));	\
	assert(buf->magic == BUF_MAGIC);		\
        if (unpacklongdouble(valp,buf))			\
		goto unpack_error;			\
} while (0)

#define safe_unpack64(valp,buf) do {			\
	assert(sizeof(*valp) == sizeof(uint64_t));      \
	assert(buf->magic == BUF_MAGIC);		\
        if (unpack64(valp,buf))				\
		goto unpack_error;			\
} while (0)

#define safe_unpack32(valp,buf) do {			\
	assert(sizeof(*valp) == sizeof(uint32_t));      \
	assert(buf->magic == BUF_MAGIC);		\
        if (unpack32(valp,buf))				\
		goto unpack_error;			\
} while (0)

#define safe_unpack16(valp,buf) do {			\
	assert(sizeof(*valp) == sizeof(uint16_t)); 	\
	assert(buf->magic == BUF_MAGIC);		\
        if (unpack16(valp,buf))				\
		goto unpack_error;			\
} while (0)

#define safe_unpack8(valp,buf) do {			\
	assert(sizeof(*valp) == sizeof(uint8_t)); 	\
	assert(buf->magic == BUF_MAGIC);		\
        if (unpack8(valp,buf))				\
		goto unpack_error;			\
} while (0)

#define safe_unpackbool(valp,buf) do {			\
	assert(sizeof(*valp) == sizeof(bool)); 	\
	assert(buf->magic == BUF_MAGIC);		\
        if (unpackbool(valp,buf))				\
		goto unpack_error;			\
} while (0)

#define safe_unpack16_array(valp,size_valp,buf) do {    \
        assert(sizeof(*size_valp) == sizeof(uint32_t)); \
        assert(buf->magic == BUF_MAGIC);                \
        if (unpack16_array(valp,size_valp,buf))         \
                goto unpack_error;                      \
} while (0)

#define safe_unpack32_array(valp,size_valp,buf) do {	\
	assert(sizeof(*size_valp) == sizeof(uint32_t)); \
	assert(buf->magic == BUF_MAGIC);		\
	if (unpack32_array(valp,size_valp,buf))		\
		goto unpack_error;			\
} while (0)

#define safe_unpack64_array(valp,size_valp,buf) do {	\
	assert(sizeof(*size_valp) == sizeof(uint32_t)); \
	assert(buf->magic == BUF_MAGIC);		\
	if (unpack64_array(valp,size_valp,buf))		\
		goto unpack_error;			\
} while (0)

#define safe_unpack64_array_from_32(valp,size_valp,buf) do {	\
	assert(sizeof(*size_valp) == sizeof(uint32_t)); \
	assert(buf->magic == BUF_MAGIC);		\
	if (unpack64_array_from_32(valp,size_valp,buf))	\
		goto unpack_error;			\
} while (0)

#define safe_unpackdouble_array(valp,size_valp,buf) do {	\
	assert(sizeof(*size_valp) == sizeof(uint32_t)); \
	assert(buf->magic == BUF_MAGIC);		\
	if (unpackdouble_array(valp,size_valp,buf))	\
		goto unpack_error;			\
} while (0)

#define safe_unpacklongdouble_array(valp,size_valp,buf) do {	\
	assert(sizeof(*size_valp) == sizeof(uint32_t)); \
	assert(buf->magic == BUF_MAGIC);		\
	if (unpacklongdouble_array(valp,size_valp,buf))	\
		goto unpack_error;			\
} while (0)

#define safe_unpackmem(valp,size_valp,buf) do {		\
	assert(sizeof(*size_valp) == sizeof(uint32_t)); \
	assert(buf->magic == BUF_MAGIC);		\
	if (unpackmem(valp,size_valp,buf))		\
		goto unpack_error;			\
} while (0)

#define safe_unpackmem_ptr(valp,size_valp,buf) do {	\
	assert(sizeof(*size_valp) == sizeof(uint32_t)); \
	assert(buf->magic == BUF_MAGIC);		\
	if (unpackmem_ptr(valp,size_valp,buf))		\
		goto unpack_error;			\
} while (0)

#define safe_unpackmem_xmalloc(valp,size_valp,buf) do {	\
	assert(sizeof(*size_valp) == sizeof(uint32_t)); \
	assert(buf->magic == BUF_MAGIC);		\
	if (unpackmem_xmalloc(valp,size_valp,buf))	\
		goto unpack_error;			\
} while (0)

#define safe_unpackmem_malloc(valp,size_valp,buf) do {	\
	assert(sizeof(*size_valp) == sizeof(uint32_t)); \
	assert(buf->magic == BUF_MAGIC);		\
	if (unpackmem_malloc(valp,size_valp,buf))	\
		goto unpack_error;			\
} while (0)

#define packstr(str,buf) do {				\
	uint32_t _size = 0;				\
	if((char *)str != NULL)				\
		_size = (uint32_t)strlen(str)+1;	\
        assert(_size == 0 || str != NULL);             	\
	assert(_size <= 0xffffffff);			\
	assert(buf->magic == BUF_MAGIC);		\
	packmem(str,(uint32_t)_size,buf);		\
} while (0)

#define packnull(buf) do { \
	assert(buf != NULL); \
	assert(buf->magic == BUF_MAGIC); \
	packmem(NULL, 0, buf); \
} while (0)

#define pack_bit_str_hex(bitmap,buf) do {		\
	assert(buf->magic == BUF_MAGIC);		\
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
	assert(*bitmap == NULL);					\
	assert(buf->magic == BUF_MAGIC);				\
	safe_unpack32(&_size, buf);					\
	if (_size != NO_VAL) {						\
		safe_unpackstr_xmalloc(&tmp_str, &_tmp_uint32, buf);	\
		*bitmap = bit_alloc(_size);				\
		bit_unfmt_hexmask(*bitmap, tmp_str);			\
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

#define safe_unpackstr_xmalloc(valp, size_valp, buf) do {	\
	assert(sizeof(*size_valp) == sizeof(uint32_t));        	\
	assert(buf->magic == BUF_MAGIC);		        \
	if (unpackstr_xmalloc_chooser(valp, size_valp, buf))    \
		goto unpack_error;		       		\
} while (0)

#define safe_unpackstr_array(valp,size_valp,buf) do {	\
	assert(sizeof(*size_valp) == sizeof(uint32_t)); \
	assert(buf->magic == BUF_MAGIC);		\
	if (unpackstr_array(valp,size_valp,buf))	\
		goto unpack_error;			\
} while (0)

#define safe_unpackmem_array(valp,size,buf) do {	\
	assert(valp != NULL);				\
	assert(sizeof(size) == sizeof(uint32_t)); 	\
	assert(buf->magic == BUF_MAGIC);		\
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
