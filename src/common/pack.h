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

void	_packmem(char *valp, uint16_t size_val, void **bufp, int *lenp);
void	_unpackmem_ptr(char **valp, uint16_t *size_valp, void **bufp, int *lenp);
void	_unpackmem_xmalloc(char **valp, uint16_t *size_valp, void **bufp, int *lenp);

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

#define packmem(valp,size_val,bufp,lenp) do {		\
	assert(size_val == 0 || valp != NULL);		\
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= (sizeof(size_val)+size_val));	\
	_packmem(valp,(uint16_t)size_val,bufp,lenp);	\
} while (0)

#define packstr(str,bufp,lenp) do {			\
	uint16_t _size;					\
	_size = (uint16_t)(str ? strlen(str)+1 : 0);	\
        assert(_size == 0 || str != NULL);		\
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= (sizeof(_size)+_size));	\
	_packmem(str,(uint16_t)_size,bufp,lenp);	\
} while (0)				

#define unpackmem_ptr(valp,size_valp,bufp,lenp) do {	\
	assert(valp != NULL);				\
	assert(sizeof(size_valp) == sizeof(uint16_t *));\
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= sizeof(uint16_t));		\
	_unpackmem_ptr(valp,(uint16_t *)size_valp,bufp,lenp);\
} while (0)

#define unpackstr_ptr		unpackmem_ptr

#define unpackmem_xmalloc(valp,size_valp,bufp,lenp) do {\
	assert(valp != NULL);				\
	assert(sizeof(size_valp) == sizeof(uint16_t *));\
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= sizeof(uint16_t));		\
	_unpackmem_xmalloc(valp,(uint16_t *)size_valp,bufp,lenp);\
} while (0)

#define unpackstr_xmalloc	unpackmem_xmalloc

#endif /* _PACK_INCLUDED */
