#include "bnr_internal.h"

MPICH_BNR_API int BNR_Info_set(BNR_Info info, char *key, char *value)
{
    BNR_Info prev, curr;

    if ((info <= (BNR_Info) 0) || (info->cookie != BNR_INFO_COOKIE)) {
		return BNR_FAIL;
    }

    if (!key) {
		return BNR_FAIL;
    }

    if (!value) {
		return BNR_FAIL;
    }

    if (strlen(key) > BNR_MAX_INFO_KEY) {
		return BNR_FAIL;
    }

    if (strlen(value) > BNR_MAX_INFO_VAL) {
		return BNR_FAIL;
    }

    if (!strlen(key)) {
		return BNR_FAIL;
    }

    if (!strlen(value)) {
		return BNR_FAIL;
    }

    prev = info;
    curr = info->next;

    while (curr) {
	if (!strcmp(curr->key, key)) {
	    free(curr->value);
	    curr->value = strdup(value);
	    break;
	}
	prev = curr;
	curr = curr->next;
    }

    if (!curr) {
	prev->next = new BNR_Info_struct; //(BNR_Info) MALLOC(sizeof(struct MPIR_Info));
	curr = prev->next;
	curr->cookie = 0;  /* cookie not set on purpose */
	curr->key = strdup(key);
	curr->value = strdup(value);
	curr->next = 0;
    }

    return BNR_SUCCESS;
}
