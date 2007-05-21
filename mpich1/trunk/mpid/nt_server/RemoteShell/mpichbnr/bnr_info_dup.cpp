#include "bnr_internal.h"

MPICH_BNR_API int BNR_Info_dup(BNR_Info info, BNR_Info *newinfo)
{
    BNR_Info curr_old, curr_new;

    if ((info <= (BNR_Info) 0) || (info->cookie != BNR_INFO_COOKIE)) {
		return BNR_FAIL;
    }

    *newinfo = new BNR_Info_struct; //(BNR_Info) MALLOC(sizeof(struct MPIR_Info));
    if (!*newinfo) {
		return BNR_FAIL;
    }
    curr_new = *newinfo;
    curr_new->cookie = BNR_INFO_COOKIE;
    curr_new->key = 0;
    curr_new->value = 0;
    curr_new->next = 0;

    curr_old = info->next;
    while (curr_old) {
	curr_new->next = new BNR_Info_struct; //(BNR_Info) MALLOC(sizeof(struct MPIR_Info));
	if (!curr_new->next) {
		return BNR_FAIL;
	}
	curr_new = curr_new->next;
	curr_new->cookie = 0;  /* cookie not set on purpose */
	curr_new->key = strdup(curr_old->key);
	curr_new->value = strdup(curr_old->value);
	curr_new->next = 0;
	
	curr_old = curr_old->next;
    }

    return BNR_SUCCESS;
}
