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
#endif  /* HAVE_CONFIG_H   */

#include <assert.h>

void 	_pack32(uint32_t val, void **bufp, int *lenp);
void	_unpack32(uint32_t *valp, void **bufp, int *lenp);

void	_pack16(uint16_t val, void **bufp, int *lenp);
void	_unpack16(uint16_t *valp, void **bufp, int *lenp);

void	_packstr(char *valp, uint32_t size_val, void **bufp, int *lenp);
void	_unpackstr_ptr(char **valp, uint32_t *size_valp, void **bufp, int *lenp);
void	_unpackstr_xmalloc(char **valp, uint32_t *size_valp, void **bufp, int *lenp);

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

#define packstr(valp,size_val,bufp,lenp) do {		\
	assert(size_val == 0 || valp != NULL);		\
	assert(sizeof(size_val) == sizeof(uint32_t));	\
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= (sizeof(size_val)+size_val));	\
	_packstr(valp,size_val,bufp,lenp);		\
} while (0)

#define unpackstr_ptr(valp,size_valp,bufp,lenp) do {	\
	assert(valp != NULL);				\
	assert(sizeof(size_valp) == sizeof(uint32_t *));\
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= sizeof(uint32_t));		\
	_unpackstr_ptr(valp,size_valp,bufp,lenp);	\
} while (0)

#define unpackstr_xmalloc(valp,size_valp,bufp,lenp) do {\
	assert(valp != NULL);				\
	assert(sizeof(size_valp) == sizeof(uint32_t *));\
	assert((bufp) != NULL && *(bufp) != NULL);	\
        assert((lenp) != NULL);				\
        assert(*(lenp) >= sizeof(uint32_t));		\
	_unpackstr_xmalloc(valp,size_valp,bufp,lenp);	\
} while (0)

#endif /* _PACK_INCLUDED */
