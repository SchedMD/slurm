/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include "mpichinfo.h"
#include <stdlib.h>
#include <string.h>

int MPICH_Info_create(MPICH_Info *info)
{
    *info	        = (MPICH_Info) MALLOC(sizeof(struct MPICH_Info_struct));
    (*info)->cookie = MPICH_INFO_COOKIE;
    (*info)->key    = 0;
    (*info)->value  = 0;
    (*info)->next   = 0;
    /* this is the first structure in this linked list. it is 
       always kept empty. new (key,value) pairs are added after it. */
    
    return MPICH_SUCCESS;
}

int MPICH_Info_set(MPICH_Info info, char *key, char *value)
{
    MPICH_Info prev, curr;
    
    if ((info <= (MPICH_Info) 0) || (info->cookie != MPICH_INFO_COOKIE)) {
        return MPICH_FAIL;
    }
    
    if (!key) {
        return MPICH_FAIL;
    }
    
    if (!value) {
        return MPICH_FAIL;
    }
    
    if (strlen(key) > MPICH_MAX_INFO_KEY) {
        return MPICH_FAIL;
    }
    
    if (strlen(value) > MPICH_MAX_INFO_VAL) {
        return MPICH_FAIL;
    }
    
    if (!strlen(key)) {
        return MPICH_FAIL;
    }
    
    if (!strlen(value)) {
        return MPICH_FAIL;
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
        prev->next = (MPICH_Info) MALLOC(sizeof(struct MPICH_Info_struct));
        curr = prev->next;
        curr->cookie = 0;  /* cookie not set on purpose */
        curr->key = strdup(key);
        curr->value = strdup(value);
        curr->next = 0;
    }
    
    return MPICH_SUCCESS;
}

int MPICH_Info_get_valuelen(MPICH_Info info, char *key, int *valuelen, int *flag)
{
    MPICH_Info curr;
    
    if ((info <= (MPICH_Info) 0) || (info->cookie != MPICH_INFO_COOKIE)) {
        return MPICH_FAIL;
    }
    
    if (!key) {
        return MPICH_FAIL;
    }
    
    if (strlen(key) > MPICH_MAX_INFO_KEY) {
        return MPICH_FAIL;
    }
    
    if (!strlen(key)) {
        return MPICH_FAIL;
    }
    
    
    curr = info->next;
    *flag = 0;
    
    while (curr) {
        if (!strcmp(curr->key, key)) {
            *valuelen = strlen(curr->value);
            *flag = 1;
            break;
        }
        curr = curr->next;
    }
    
    return MPICH_SUCCESS;
}

int MPICH_Info_get_nthkey(MPICH_Info info, int n, char *key)
{
    MPICH_Info curr;
    int nkeys, i;
    
    if ((info <= (MPICH_Info) 0) || (info->cookie != MPICH_INFO_COOKIE)) {
        return MPICH_FAIL;
    }
    
    if (!key) {
        return MPICH_FAIL;
    }
    
    curr = info->next;
    nkeys = 0;
    while (curr) {
        curr = curr->next;
        nkeys++;
    }
    
    if ((n < 0) || (n >= nkeys)) {
        return MPICH_FAIL;
    }
    
    curr = info->next;
    i = 0;
    while (i < n) {
        curr = curr->next;
        i++;
    }
    strcpy(key, curr->key);
    
    return MPICH_SUCCESS;
}

int MPICH_Info_get_nkeys(MPICH_Info info, int *nkeys)
{
    MPICH_Info curr;
    
    if ((info <= (MPICH_Info) 0) || (info->cookie != MPICH_INFO_COOKIE)) {
        return MPICH_FAIL;
    }
    
    curr = info->next;
    *nkeys = 0;
    
    while (curr) {
        curr = curr->next;
        (*nkeys)++;
    }
    
    return MPICH_SUCCESS;
}

int MPICH_Info_get(MPICH_Info info, char *key, int valuelen, char *value, int *flag)
{
    MPICH_Info curr;
    
    if ((info <= (MPICH_Info) 0) || (info->cookie != MPICH_INFO_COOKIE)) {
        return MPICH_FAIL;
    }
    
    if (!key) {
        return MPICH_FAIL;
    }
    
    if (strlen(key) > MPICH_MAX_INFO_KEY) {
        return MPICH_FAIL;
    }
    
    if (!strlen(key)) {
        return MPICH_FAIL;
    }
    
    if (valuelen <= 0) {
        return MPICH_FAIL;
    }
    
    if (!value) {
        return MPICH_FAIL;
    }
    
    curr = info->next;
    *flag = 0;
    
    while (curr) {
        if (!strcmp(curr->key, key)) {
            strncpy(value, curr->value, valuelen);
            value[valuelen] = '\0';
            *flag = 1;
            break;
        }
        curr = curr->next;
    }
    
    return MPICH_SUCCESS;
}

int MPICH_Info_free(MPICH_Info *info)
{
    MPICH_Info curr, next;
    
    if ((*info <= (MPICH_Info) 0) || ((*info)->cookie != MPICH_INFO_COOKIE)) {
        return MPICH_FAIL;
    }
    
    curr = (*info)->next;
    FREE(*info);
    *info = MPICH_INFO_NULL;
    
    while (curr) {
        next = curr->next;
        free(curr->key);
        free(curr->value);
        FREE(curr);
        curr = next;
    }
    
    return MPICH_SUCCESS;
}

int MPICH_Info_dup(MPICH_Info info, MPICH_Info *newinfo)
{
    MPICH_Info curr_old, curr_new;
    
    if ((info <= (MPICH_Info) 0) || (info->cookie != MPICH_INFO_COOKIE)) {
        return MPICH_FAIL;
    }
    
    *newinfo = (MPICH_Info) MALLOC(sizeof(struct MPICH_Info_struct));
    if (!*newinfo) {
        return MPICH_FAIL;
    }
    curr_new = *newinfo;
    curr_new->cookie = MPICH_INFO_COOKIE;
    curr_new->key = 0;
    curr_new->value = 0;
    curr_new->next = 0;
    
    curr_old = info->next;
    while (curr_old) {
        curr_new->next = (MPICH_Info) MALLOC(sizeof(struct MPICH_Info_struct));
        if (!curr_new->next) {
            return MPICH_FAIL;
        }
        curr_new = curr_new->next;
        curr_new->cookie = 0;  /* cookie not set on purpose */
        curr_new->key = strdup(curr_old->key);
        curr_new->value = strdup(curr_old->value);
        curr_new->next = 0;
        
        curr_old = curr_old->next;
    }
    
    return MPICH_SUCCESS;
}

int MPICH_Info_delete(MPICH_Info info, char *key)
{
    MPICH_Info prev, curr;
    int done;
    
    if ((info <= (MPICH_Info) 0) || (info->cookie != MPICH_INFO_COOKIE)) {
        return MPICH_FAIL;
    }
    
    if (!key) {
        return MPICH_FAIL;
    }
    
    if (strlen(key) > MPICH_MAX_INFO_KEY) {
        return MPICH_FAIL;
    }
    
    if (!strlen(key)) {
        return MPICH_FAIL;
    }
    
    prev = info;
    curr = info->next;
    done = 0;
    
    while (curr) {
        if (!strcmp(curr->key, key)) {
            free(curr->key);
            free(curr->value);
            prev->next = curr->next;
            FREE(curr);
            done = 1;
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    
    if (!done) {
        return MPICH_FAIL;
    }
    
    return MPICH_SUCCESS;
}

