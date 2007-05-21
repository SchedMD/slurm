#include "parsecliques.h"
#include <ctype.h>  /* isdigit */
#include <stdlib.h> /* malloc, free, qsort, atoi */
#include <string.h> /* strlen */

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

static int QSortIntCompare(const void *a, const void *b)
{
    if (*(int*)a > *(int*)b)
        return 1;
    if (*(int*)a == *(int*)b)
        return 0;
    return -1;
}

static void MergeMembers(int nCount, int *pMembers, int *nOutCount, int **ppOutMembers)
{
    int i;
    int *pOut;
    int *pTemp;
    /* make a new array big enough to hold both the output and input arrays */
    pTemp = (int*)MALLOC((nCount + *nOutCount) * sizeof(int));
    /*printf("MALLOC: %x\n", pTemp);*/
    pOut = pTemp;
    /* add the output members to the new array */
    for (i=0; i<*nOutCount; i++)
    {
        *pTemp = (*ppOutMembers)[i];
        pTemp++;
    }
    /* add the input members to the new array */
    for (i=0; i<nCount; i++)
    {
        *pTemp = pMembers[i];
        pTemp++;
    }
    /* delete the original array and save the new one */
    if (*ppOutMembers != NULL)
    {
        /*printf("FREE:   %x\n", *ppOutMembers);*/
        FREE(*ppOutMembers);
    }
    *ppOutMembers = pOut;
    *nOutCount = *nOutCount + nCount;
}

static int GetNumber(char **pChar)
{
    char buffer[100], *pBuf = buffer;
    while (isdigit(**pChar))
    {
        *pBuf = **pChar;
        (*pChar)++;
        pBuf++;
    }
    *pBuf = '\0';
    return atoi(buffer);
}

static void AddRange(int nFirst, int nLast, int *count, int **members)
{
    int nCount, *pMembers, i;

    if (nFirst < 0 || nLast < 0 || nLast < nFirst)
        return;

    nCount = nLast - nFirst + 1;
    pMembers = (int*)MALLOC(nCount * sizeof(int));
    /*printf("MALLOC: %x\n", pMembers);*/
    for (i=0; i<nCount; i++)
        pMembers[i] = i + nFirst;
    MergeMembers(nCount, pMembers, count, members);
}

static int GetClique(char **pChar, int iproc, int *count, int **members)
{
    int nFirst, nLast;
    (*pChar)++; /* advance over '(' */
    while ( (**pChar != ')') && (**pChar != '\0') )
    {
        nFirst = GetNumber(pChar);
        switch (**pChar)
        {
        case '.':
            pChar++; /* advance over '.' */
            if (**pChar != '.')
                return FALSE;
            (*pChar)++; /* advance over '.' */
            nLast = GetNumber(pChar);
            AddRange(nFirst, nLast, count, members);
            if (**pChar == ',')
                (*pChar)++;
            break;
        case ',':
            MergeMembers(1, &nFirst, count, members);
            (*pChar)++;
            break;
        case ')':
            MergeMembers(1, &nFirst, count, members);
            break;
        default:
            return FALSE;
        }
    }
    if (**pChar == '\0')
        return FALSE;
    (*pChar)++;
    return TRUE;
}

static int InClique(int candidate, int nCount, int *pMembers)
{
    int i;
    for (i=0; i<nCount; i++)
        if (pMembers[i] == candidate)
            return TRUE;
    return FALSE;
}

static int ReplicateNextMembers(int **pNextMembers, int **pCurMembers, int n)
{
    int i, offset;
    if (n < 1)
    {
        *pNextMembers = NULL;
        return 0;
    }

    *pNextMembers = (int*)MALLOC(n * sizeof(int));
    /*printf("MALLOC: %x\n", *pNextMembers);*/
    if (*pNextMembers == NULL)
        return 1;
    offset = (*pCurMembers)[n-1] - **pCurMembers + 1;
    for (i=0; i<n; i++)
        (*pNextMembers)[i] = (*pCurMembers)[i] + offset;

    return 0;
}

int ParseCliques(char *pszCliques, int iproc, int nproc, int *count, int **members)
{
    char *pCurrent = pszCliques;
    int nCurCount, *pCurMembers, *pNextMembers, i, n;
    int index;

    /* Check arguments */
    if (iproc > nproc || nproc < 1 || iproc < 0 || count == NULL || members == NULL || pszCliques == NULL)
        return 1;

    *count = 0;
    *members = NULL;

    if (strlen(pszCliques) < 1)
        return 0;

    if (*pszCliques == '-')
    {
        /* Special clique including nobody */
        return 0;
    }

    if (*pszCliques == '*')
    {
        /* Special clique including everyone */
        *count = nproc;
        *members = (int*)MALLOC(nproc * sizeof(int));
        /*printf("MALLOC: %x\n", *members);*/
        for (i=0; i<nproc; i++)
            (*members)[i] = i;
        return 0;
    }

    if (*pszCliques == '.')
    {
        /* Special clique including only yourself */
        *count = 1;
        *members = (int*)MALLOC(sizeof(int));
        /*printf("MALLOC: %x\n", *members);*/
        **members = iproc;
        return 0;
    }

    while (*pCurrent == '(')
    {
        nCurCount = 0;
        pCurMembers = NULL;
        if (GetClique(&pCurrent, iproc, &nCurCount, &pCurMembers))
        {
            if (*pCurrent == '*')
            {
                if (nCurCount > 0)
                {
                    /* Replicate and check each clique until there are members higher than nproc */
                    while (pCurMembers[nCurCount-1] < nproc)
                    {
                        if (ReplicateNextMembers(&pNextMembers, &pCurMembers, nCurCount))
                            return 1;
                        if (InClique(iproc, nCurCount, pCurMembers))
                        {
                        /* If iproc is a member of the current clique then merge this clique
                        * to the output array and break out of the while loop.  iproc cannot
                        * be a member of any other higher cliques 
                            */
                            MergeMembers(nCurCount, pCurMembers, count, members);
                            FREE(pCurMembers);
                            /*printf("FREE:   %x\n", pCurMembers);*/
                            pCurMembers = pNextMembers;
                            nCurCount = 0;
                            break;
                        }
                        FREE(pCurMembers);
                        /*printf("FREE:   %x\n", pCurMembers);*/
                        pCurMembers = pNextMembers;
                    }
                    /* Check the last clique which may have extended beyond nproc.
                    * Remove all entries beyond nproc and then check to see if iproc
                    * is a member of this clique 
                    */
                    while (nCurCount > 0 && pCurMembers[nCurCount-1] >= nproc)
                        nCurCount--;
                    if (nCurCount > 0 && InClique(iproc, nCurCount, pCurMembers))
                        MergeMembers(nCurCount, pCurMembers, count, members);
                    FREE(pCurMembers);
                    /*printf("FREE:   %x\n", pCurMembers);*/
                }
            }
            else if (*pCurrent == 'x')
            {
                pCurrent++;
                n = GetNumber(&pCurrent);
                if (n>0)
                {
                    for (i=0; i<n; i++)
                    {
                        if (ReplicateNextMembers(&pNextMembers, &pCurMembers, nCurCount))
                            return 1;
                        if (InClique(iproc, nCurCount, pCurMembers))
                        {
                            /* Remove any entries higher than nproc */
                            while (nCurCount > 0 && pCurMembers[nCurCount-1] >= nproc)
                                nCurCount--;
                            MergeMembers(nCurCount, pCurMembers, count, members);
                            FREE(pCurMembers);
                            /*printf("FREE:   %x\n", pCurMembers);*/
                            pCurMembers = pNextMembers;
                            break;
                        }
                        FREE(pCurMembers);
                        /*printf("FREE:   %x\n", pCurMembers);*/
                        pCurMembers = pNextMembers;
                    }
                    FREE (pNextMembers);
                    /*printf("FREE:   %x\n", pNextMembers);*/
                }
            }
            else
            {
                if (InClique(iproc, nCurCount, pCurMembers))
                    MergeMembers(nCurCount, pCurMembers, count, members);
                FREE(pCurMembers);
                /*printf("FREE:   %x\n", pCurMembers);*/
            }
        }
        else
            return 1;
    }

    if (*count > 0)
    {
        /* sort the resulting array */
        qsort(*members, *count, 4, QSortIntCompare);
        /* delete the duplicates */
        index = 0;
        for (i=0; i<*count-1; i++)
        {
            if ((*members)[i] != (*members)[i+1])
            {
                (*members)[index] = (*members)[i];
                index++;
            }
        }
        (*members)[index] = (*members)[*count-1];
        *count = index+1;
    }
    return 0;
}
