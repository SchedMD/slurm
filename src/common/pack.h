/* 
 * $Id$
 */
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

#include <assert.h>

void 	_pack32(uint32_t val, void **bufp, int *lenp);
void	_unpack32(uint32_t *valp, void **bufp, int *lenp);

void	_pack16(uint16_t val, void **bufp, int *lenp);
void	_unpack16(uint16_t *valp, void **bufp, int *lenp);

void	_pack8(uint8_t val, void **bufp, int *lenp);
void	_unpack8(uint8_t *valp, void **bufp, int *lenp);

void	_pack32array(uint32_t *valp, uint16_t size_val, void **bufp, int *lenp);
void	_unpack32array( uint32_t **valp, uint16_t* size_val, void **bufp, int *lenp);

void	_packstrarray(char **valp, uint16_t size_val, void **bufp, int *lenp);
void	_unpackstrarray(char ***valp, uint16_t* size_val, void **bufp, int *lenp);

void	_packmem_array(char *valp, uint16_t size_val, void **bufp, int *lenp);
void	_unpackmem_array(char *valp, uint16_t size_valp, void **bufp, int *lenp);

void	_packmem(char *valp, uint16_t size_val, void **bufp, int *lenp);
void	_unpackmem(char *valp, uint16_t *size_valp, void **bufp, int *lenp);
void	_unpackmem_ptr(char **valp, uint16_t *size_valp, void **bufp, int *lenp);
void	_unpackmem_xmalloc(char **valp, uint16_t *size_valp, void **bufp, int *lenp);
void	_unpackmem_malloc(char **valp, uint16_t *size_valp, void **bufp, int *lenp);

#define pack32(val,bufp,lenp) do {			\
	assert(sizeof(val) == sizeof(uint32_t)); 	\
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= sizeof(val));			\
	_pack32(val,bufp,lenp);				\
} while (0)

#define pack16(val,bufp,lenp) do {			\
	assert(sizeof(val) == sizeof(uint16_t)); 	\
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= sizeof(val));			\
	_pack16(val,bufp,lenp);				\
} while (0)

#define pack8(val,bufp,lenp) do {			\
	assert(sizeof(val) == sizeof(uint8_t)); 	\
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= sizeof(val));			\
	_pack8(val,bufp,lenp);				\
} while (0)

#define unpack32(valp,bufp,lenp) do {			\
	assert((valp) != NULL); 			\
	assert(sizeof(*valp) == sizeof(uint32_t)); 	\
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= sizeof(*(valp)));		\
	_unpack32(valp,bufp,lenp);			\
} while (0)

#define unpack16(valp,bufp,lenp) do {			\
	assert((valp) != NULL); 			\
	assert(sizeof(*valp) == sizeof(uint16_t)); 	\
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= sizeof(*(valp)));		\
	_unpack16(valp,bufp,lenp);			\
} while (0)

#define unpack8(valp,bufp,lenp) do {			\
	assert((valp) != NULL); 			\
	assert(sizeof(*valp) == sizeof(uint8_t)); 	\
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= sizeof(*(valp)));		\
	_unpack8(valp,bufp,lenp);			\
} while (0)

#define packmem(valp,size_val,bufp,lenp) do {		\
	assert(size_val == 0 || valp != NULL);		\
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= (sizeof(size_val)+size_val));	\
	_packmem(valp,(uint16_t)size_val,bufp,lenp);	\
} while (0)

#define unpackmem(valp,size_valp,bufp,lenp) do {	\
	assert(valp != NULL);		                \
	assert(sizeof(*size_valp) == sizeof(uint16_t)); \
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= sizeof(uint16_t));		\
	_unpackmem(valp,(uint16_t *)size_valp,bufp,lenp);\
} while (0)

#define packstr(str,bufp,lenp) do {			\
	uint32_t _size;					\
	_size = (uint32_t)(str ? strlen(str)+1 : 0);	\
        assert(_size == 0 || str != NULL);		\
	assert(_size <= 0xffffffff);			\
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= (sizeof(_size)+_size));	\
	_packmem(str,(uint16_t)_size,bufp,lenp);	\
} while (0)				

#define packstring_array(array,_size,bufp,lenp) do {	\
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= (sizeof(_size)+_size));	\
	_packstrarray(array,(uint16_t)_size,bufp,lenp);	\
} while (0)				

#define unpackstring_array(valp,size_valp,bufp,lenp) do {\
	assert(valp != NULL);				\
	assert(sizeof(*size_valp) == sizeof(uint16_t)); \
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= sizeof(uint16_t));		\
	_unpackstrarray(valp,(uint16_t *)size_valp,bufp,lenp);\
} while (0)

#define pack32_array(array,_size,bufp,lenp)             \
        packint_array(array,_size,bufp,lenp)

#define packint_array(array,_size,bufp,lenp) do {	\
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= (sizeof(_size)+_size));	\
	_pack32array(array,(uint16_t)_size,bufp,lenp);	\
} while (0)				

#define unpack32_array(array,_size,bufp,lenp)           \
        unpackint_array(array,_size,bufp,lenp)

#define unpackint_array(valp,size_valp,bufp,lenp) do {	\
	assert(valp != NULL);				\
	assert(sizeof(*size_valp) == sizeof(uint16_t)); \
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= sizeof(uint16_t));		\
	_unpack32array(valp,(uint16_t *)size_valp,bufp,lenp);\
} while (0)


#define unpackstr_ptr		                        \
        unpackmem_ptr

#define unpackmem_ptr(valp,size_valp,bufp,lenp) do {	\
	assert(valp != NULL);				\
	assert(sizeof(*size_valp) == sizeof(uint16_t)); \
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= sizeof(uint16_t));		\
	_unpackmem_ptr(valp,(uint16_t *)size_valp,bufp,lenp);\
} while (0)

#define unpackstr_malloc	                        \
        unpackmem_malloc

#define unpackmem_malloc(valp,size_valp,bufp,lenp) do { \
	assert(valp != NULL);				\
	assert(sizeof(*size_valp) == sizeof(uint16_t)); \
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= sizeof(uint16_t));		\
	_unpackmem_malloc(valp,(uint16_t *)size_valp,bufp,lenp);\
} while (0)

#define unpackstr_xmalloc	                        \
        unpackmem_xmalloc

#define unpackmem_xmalloc(valp,size_valp,bufp,lenp) do {\
	assert(valp != NULL);				\
	assert(sizeof(*size_valp) == sizeof(uint16_t)); \
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= sizeof(uint16_t));		\
	_unpackmem_xmalloc(valp,(uint16_t *)size_valp,bufp,lenp);\
} while (0)

#define packmem_array(valp,size,bufp,lenp) do {	\
	assert(size == 0 || valp != NULL);		\
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= (size));			\
	_packmem_array(valp,(uint16_t)size,bufp,lenp);	\
} while (0)

#define unpackmem_array(valp,size,bufp,lenp) do {	\
	assert(valp != NULL);				\
	assert(sizeof(size) == sizeof(uint16_t)); 	\
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= sizeof(uint16_t));		\
	_unpackmem_array(valp,size,bufp,lenp);	\
} while (0)


#endif /* _PACK_INCLUDED */
