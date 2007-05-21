#if !defined(GLOBUS2_DEBUG_H)
#define GLOBUS2_DEBUG_H

#include "chconfig.h"


#define DEBUG_MODULE_ALL		0xffff
#define DEBUG_MODULE_MP			0x0002
#define DEBUG_MODULE_TCP		0x0004
#define DEBUG_MODULE_SEND		0x0008
#define DEBUG_MODULE_RECV		0x0010
#define DEBUG_MODULE_COMM		0x0020
#define DEBUG_MODULE_TYPES		0x0040
#define DEBUG_MODULE_INIT		0x0080

#define DEBUG_INFO_ALL			0xffff
#define DEBUG_INFO_FUNC			0x0001
#define DEBUG_INFO_ARGS			0x0002
#define DEBUG_INFO_RC			0x0004
#define DEBUG_INFO_FAILURE		0x0008
#define DEBUG_INFO_WARNING		0x0010
#define DEBUG_INFO_MISC			0x0020

#define DEBUG_QUOTE(A) DEBUG_QUOTE2(A)
#define DEBUG_QUOTE2(A) #A

#if DEBUG_ENABLED

extern int mpich_globus2_debug_rank;
extern int mpich_globus2_debug_level;

#define DEBUG_CHECK(M,I) \
(((DEBUG_MODULES_ENABLED) & (M)) && ((DEBUG_INFO_ENABLED) & (I)))

#define DEBUG_PRINTF(M,I,A)			\
{						\
    if (DEBUG_CHECK((M),(I)))			\
    {						\
	DEBUG_PRINTF_NOCHECK(A);		\
    }						\
}

#define DEBUG_PRINTF_NOCHECK(A)						\
{									\
    globus_libc_printf("dbg(%d)%*s%s",					\
		       mpich_globus2_debug_rank,			\
		       mpich_globus2_debug_level, "",			\
		       DEBUG_QUOTE(DEBUG_FN_NAME) "(): ");		\
    globus_libc_printf A;						\
    fflush(stdout);							\
}

#define DEBUG_FN_ENTRY(M)				\
{							\
    mpich_globus2_debug_level += 2;			\
    DEBUG_PRINTF((M), DEBUG_INFO_FUNC, ("entering\n"));	\
    mpich_globus2_debug_level += 2;			\
}

#define DEBUG_FN_EXIT(M)				\
{							\
    mpich_globus2_debug_level -= 2;			\
    DEBUG_PRINTF((M), DEBUG_INFO_FUNC, ("exiting\n"));	\
    mpich_globus2_debug_level -= 2;			\
}

#else /* !DEBUG_ENABLED */

#define DEBUG_CHECK(M,I) (0)
#define DEBUG_PRINTF(M,I,A)
#define DEBUG_FN_ENTRY(M)
#define DEBUG_FN_EXIT(M)

#endif /* DEBUG_ENABLED */

void mpich_globus2_debug_init();

#endif /* defined(GLOBUS2_DEBUG_H) */
