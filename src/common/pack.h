/* 
 * $Id$
 */
#ifndef _PACK_INCLUDED
#define _PACK_INCLUDED

void 	_pack32(uint32_t val, void **bufp, int *lenp);
void	_unpack32(uint32_t *valp, void **bufp, int *lenp);

void	_pack16(uint16_t val, void **bufp, int *lenp);
void	_unpack16(uint16_t *valp, void **bufp, int *lenp);

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

#endif /* _PACK_INCLUDED */
