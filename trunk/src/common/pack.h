/****************************************************************************\
 *  pack.h - definitions for lowest level un/pack functions. all functions 
 *	utilize a Buf structure. Call init_buf, un/pack, and free_buf
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>, Moe Jette <jette1@llnl.gov>, et. al.
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
\****************************************************************************/

#ifndef _PACK_INCLUDED
#define _PACK_INCLUDED

#if HAVE_CONFIG_H
#include <config.h>
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#else  /* !HAVE_INTTYPES_H */
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#endif  /* HAVE_INTTYPES_H */
#else   /* !HAVE_CONFIG_H */
#include <stdint.h>
#endif  /* HAVE_CONFIG_H */

#include <time.h>

#include <assert.h>

#define BUF_MAGIC 0x42554545

struct slurm_buf {
	uint32_t magic;
	char *head;
	uint32_t size;
	uint32_t processed;
};

typedef struct slurm_buf * Buf;

#define get_buf_data(__buf)		(__buf->head)
#define get_buf_offset(__buf)		(__buf->processed)
#define set_buf_offset(__buf,__val)	(__buf->processed = __val)
#define remaining_buf(__buf)		(__buf->size - __buf->processed)
#define size_buf(__buf)			(__buf->size)

Buf	create_buf (char *data, int size);
void	free_buf(Buf my_buf);
Buf	init_buf(int size);
void	*xfer_buf_data(Buf my_buf);

void 	_pack32(uint32_t val, Buf buffer);
void	_unpack32(uint32_t *valp, Buf buffer);

void	_pack16(uint16_t val, Buf buffer);
void	_unpack16(uint16_t *valp, Buf buffer);

void	_pack8(uint8_t val, Buf buffer);
void	_unpack8(uint8_t *valp, Buf buffer);

void	_pack32array(uint32_t *valp, uint16_t size_val, Buf buffer);
void	_unpack32array( uint32_t **valp, uint16_t* size_val, Buf buffer);

void	_packstrarray(char **valp, uint16_t size_val, Buf buffer);
void	_unpackstrarray(char ***valp, uint16_t* size_val, Buf buffer);

void	_packmem_array(char *valp, uint32_t size_val, Buf buffer);
void	_unpackmem_array(char *valp, uint32_t size_valp, Buf buffer);

void	_packmem(char *valp, uint16_t size_val, Buf buffer);
void	_unpackmem(char *valp, uint16_t *size_valp, Buf buffer);
void	_unpackmem_ptr(char **valp, uint16_t *size_valp, Buf buffer);
void	_unpackmem_xmalloc(char **valp, uint16_t *size_valp, Buf buffer);
void	_unpackmem_malloc(char **valp, uint16_t *size_valp, Buf buffer);

void 	pack_time(time_t val, Buf buffer);
void    unpack_time(time_t *valp, Buf buffer);

#define pack32(val,buf) do {				\
	assert(sizeof(val) == sizeof(uint32_t)); 	\
	assert(buf->magic == BUF_MAGIC);		\
	_pack32(val,buf);				\
} while (0)

#define unpack32(valp,buf) do {				\
	assert((valp) != NULL); 			\
	assert(sizeof(*valp) == sizeof(uint32_t)); 	\
	assert(buf->magic == BUF_MAGIC);		\
	_unpack32(valp,buf);				\
} while (0)

#define pack16(val,buf) do {				\
	assert(sizeof(val) == sizeof(uint16_t)); 	\
	assert(buf->magic == BUF_MAGIC);		\
	_pack16(val,buf);				\
} while (0)

#define unpack16(valp,buf) do {				\
	assert((valp) != NULL); 			\
	assert(sizeof(*valp) == sizeof(uint16_t)); 	\
	assert(buf->magic == BUF_MAGIC);		\
	_unpack16(valp,buf);				\
} while (0)

#define pack8(val,buf) do {				\
	assert(sizeof(val) == sizeof(uint8_t)); 	\
	assert(buf->magic == BUF_MAGIC);		\
	_pack8(val,buf);				\
} while (0)

#define unpack8(valp,buf) do {				\
	assert((valp) != NULL); 			\
	assert(sizeof(*valp) == sizeof(uint8_t)); 	\
	assert(buf->magic == BUF_MAGIC);		\
	_unpack8(valp,buf);				\
} while (0)

#define packmem(valp,size_val,buf) do {			\
	assert(sizeof(size_val) == sizeof(uint16_t)); 	\
	assert(size_val == 0 || valp != NULL);		\
	assert(buf->magic == BUF_MAGIC);		\
	_packmem(valp,size_val,buf);			\
} while (0)

#define unpackmem(valp,size_valp,buf) do {		\
	assert(valp != NULL);		                \
	assert(sizeof(*size_valp) == sizeof(uint16_t)); \
	assert(buf->magic == BUF_MAGIC);		\
	_unpackmem(valp,size_valp,buf);			\
} while (0)

#define packstr(str,buf) do {				\
	uint32_t _size;					\
	_size = (uint32_t)(str ? strlen(str)+1 : 0);	\
        assert(_size == 0 || str != NULL);		\
	assert(_size <= 0xffff);			\
	assert(buf->magic == BUF_MAGIC);		\
	_packmem(str,(uint16_t)_size,buf);		\
} while (0)				

#define packstring_array(array,size_val,buf) do {	\
	assert(size_val == 0 || array != NULL);		\
	assert(buf->magic == BUF_MAGIC);		\
	_packstrarray(array,size_val,buf);		\
} while (0)				

#define unpackstring_array(valp,size_valp,buf) do {	\
	assert(valp != NULL);				\
	assert(size_valp != NULL);			\
	assert(sizeof(*size_valp) == sizeof(uint16_t)); \
	assert(buf->magic == BUF_MAGIC);		\
	_unpackstrarray(valp,size_valp,buf);		\
} while (0)

#define pack32_array(array,size_val,buf)		\
        packint_array(array,size_val,buf)

#define packint_array(array,size_val,buf) do {		\
	assert(size_val == 0 || array != NULL);		\
	assert(buf->magic == BUF_MAGIC);		\
	_pack32array(array,size_val,buf);		\
} while (0)				

#define unpack32_array(array,_size,buf)			\
        unpackint_array(array,_size,buf)

#define unpackint_array(valp,size_valp,buf) do {	\
	assert(valp != NULL);				\
	assert(sizeof(*size_valp) == sizeof(uint16_t)); \
	assert(buf->magic == BUF_MAGIC);		\
	_unpack32array(valp,size_valp,buf);		\
} while (0)


#define unpackstr_ptr		                        \
        unpackmem_ptr

#define unpackmem_ptr(valp,size_valp,buf) do {		\
	assert(valp != NULL);				\
	assert(sizeof(*size_valp) == sizeof(uint16_t)); \
	assert(buf->magic == BUF_MAGIC);		\
	_unpackmem_ptr(valp,size_valp,buf);		\
} while (0)

#define unpackstr_malloc	                        \
        unpackmem_malloc

#define unpackmem_malloc(valp,size_valp,buf) do {	\
	assert(valp != NULL);				\
	assert(sizeof(*size_valp) == sizeof(uint16_t)); \
	assert(buf->magic == BUF_MAGIC);		\
	_unpackmem_malloc(valp,size_valp,buf);		\
} while (0)

#define unpackstr_xmalloc	                        \
        unpackmem_xmalloc

#define unpackmem_xmalloc(valp,size_valp,buf) do {	\
	assert(valp != NULL);				\
	assert(sizeof(*size_valp) == sizeof(uint16_t)); \
	assert(buf->magic == BUF_MAGIC);		\
	_unpackmem_xmalloc(valp,size_valp,buf);		\
} while (0)

#define packmem_array(valp,size,buf) do {		\
	assert(size == 0 || valp != NULL);		\
	assert(sizeof(size) == sizeof(uint32_t));	\
	assert(buf->magic == BUF_MAGIC);		\
	_packmem_array(valp,size,buf);			\
} while (0)

#define unpackmem_array(valp,size,buf) do {		\
	assert(valp != NULL);				\
	assert(sizeof(size) == sizeof(uint32_t)); 	\
	assert(buf->magic == BUF_MAGIC);		\
	_unpackmem_array(valp,size,buf);		\
} while (0)


#endif /* _PACK_INCLUDED */
