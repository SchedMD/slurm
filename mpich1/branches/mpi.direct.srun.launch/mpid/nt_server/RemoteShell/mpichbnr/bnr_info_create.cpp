#include "bnr_internal.h"

MPICH_BNR_API int BNR_Info_create(BNR_Info *info)
{
    *info	    = new BNR_Info_struct;//(BNR_Info) MALLOC(sizeof(struct MPIR_Info));
    (*info)->cookie = BNR_INFO_COOKIE;
    (*info)->key    = 0;
    (*info)->value  = 0;
    (*info)->next   = 0;
    /* this is the first structure in this linked list. it is 
       always kept empty. new (key,value) pairs are added after it. */

    return BNR_SUCCESS;
}
