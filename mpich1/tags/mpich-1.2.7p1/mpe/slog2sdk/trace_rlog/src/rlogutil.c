/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  $Id: rlogutil.c,v 1.7 2005/07/05 18:38:54 ashton Exp $
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "rlog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

static int ReadFileData(char *pBuffer, int length, FILE *fin)
{
    int num_read;

    while (length)
    {
	num_read = fread(pBuffer, 1, length, fin);
	if (num_read == -1)
	{
	    printf("Error: fread failed - %s\n", strerror(errno));
	    return errno;
	}
	if (num_read == 0 && length)
	    return -1;

	/*printf("fread(%d)", num_read);fflush(stdout);*/

	length -= num_read;
	pBuffer += num_read;
    }
    return 0;
}

static int WriteFileData(const char *pBuffer, int length, FILE *fout)
{
    int num_written;

    while (length)
    {
	num_written = fwrite(pBuffer, 1, length, fout);
	if (num_written == -1)
	{
	    printf("Error: fwrite failed - %s\n", strerror(errno));
	    return errno;
	}
	if (num_written == 0 && length)
	    return -1;

	/*printf("fwrite(%d)", num_written);fflush(stdout);*/

	length -= num_written;
	pBuffer += num_written;
    }
    return 0;
}

static int rlog_err_printf(char *str, ...);
static int rlog_err_printf(char *str, ...)
{
    int n;
    va_list list;

    va_start(list, str);
    n = vprintf(str, list);
    va_end(list);

    fflush(stdout);

    return n;
}

RLOG_IOStruct *RLOG_CreateInputStruct(const char *filename)
{
    int i, j, rank_index, cur_rank, min_rank = 0;
    RLOG_IOStruct *pInput;
    int type, length;

    /* allocate an input structure */
    pInput = (RLOG_IOStruct*)malloc(sizeof(RLOG_IOStruct));
    if (pInput == NULL)
    {
	printf("malloc failed - %s\n", strerror(errno));
	return NULL;
    }
    pInput->ppCurEvent = NULL;
    pInput->ppCurGlobalEvent = NULL;
    pInput->gppCurEvent = NULL;
    pInput->gppPrevEvent = NULL;
    pInput->ppEventOffset = NULL;
    pInput->ppNumEvents = NULL;
    pInput->nNumArrows = 0;
    /* open the input rlog file */
    pInput->f = fopen(filename, "rb");
    if (pInput->f == NULL)
    {
	printf("fopen(%s) failed, error: %s\n", filename, strerror(errno));
	free(pInput);
	return NULL;
    }
    pInput->nNumRanks = 0;
    /* read the sections */
    while (fread(&type, sizeof(int), 1, pInput->f))
    {
	fread(&length, sizeof(int), 1, pInput->f);
	switch (type)
	{
	case RLOG_HEADER_SECTION:
	    /*printf("type: RLOG_HEADER_SECTION, length: %d\n", length);*/
	    if (length != sizeof(RLOG_FILE_HEADER))
	    {
		printf("error in header size %d != %d\n", length, sizeof(RLOG_FILE_HEADER));
	    }
	    if (ReadFileData((char*)&pInput->header, sizeof(RLOG_FILE_HEADER), pInput->f))
	    {
		rlog_err_printf("reading rlog header failed\n");
		return NULL;
	    }
	    
	    pInput->nNumRanks = pInput->header.nMaxRank + 1 - pInput->header.nMinRank;
	    min_rank = pInput->header.nMinRank;
	    
	    pInput->pRank = (int*)malloc(pInput->nNumRanks * sizeof(int));
	    pInput->pNumEventRecursions = (int*)malloc(pInput->nNumRanks * sizeof(int));
	    pInput->ppNumEvents = (int**)malloc(pInput->nNumRanks * sizeof(int*));
	    pInput->ppCurEvent = (int**)malloc(pInput->nNumRanks * sizeof(int*));
	    pInput->ppCurGlobalEvent = (int**)malloc(pInput->nNumRanks * sizeof(int*));
	    pInput->gppCurEvent = (RLOG_EVENT**)malloc(pInput->nNumRanks * sizeof(RLOG_EVENT*));
	    pInput->gppPrevEvent = (RLOG_EVENT**)malloc(pInput->nNumRanks * sizeof(RLOG_EVENT*));
	    pInput->ppEventOffset = (long**)malloc(pInput->nNumRanks * sizeof(long*));
	    for (i=0; i<pInput->nNumRanks; i++)
	    {
		pInput->pRank[i] = -1;
		pInput->pNumEventRecursions[i] = 0;
		pInput->ppNumEvents[i] = NULL;
		pInput->ppCurEvent[i] = NULL;
		pInput->ppCurGlobalEvent[i] = NULL;
		pInput->gppCurEvent[i] = NULL;
		pInput->gppPrevEvent[i] = NULL;
		pInput->ppEventOffset[i] = NULL;
	    }
	    break;
	case RLOG_STATE_SECTION:
	    /*printf("type: RLOG_STATE_SECTION, length: %d\n", length);*/
	    pInput->nNumStates = length / sizeof(RLOG_STATE);
	    pInput->nStateOffset = ftell(pInput->f);
	    fseek(pInput->f, length, SEEK_CUR);
	    break;
	case RLOG_ARROW_SECTION:
	    /*printf("type: RLOG_ARROW_SECTION, length: %d\n", length);*/
	    pInput->nNumArrows = length / sizeof(RLOG_ARROW);
	    pInput->nArrowOffset = ftell(pInput->f);
	    fseek(pInput->f, length, SEEK_CUR);
	    break;
	case RLOG_EVENT_SECTION:
	    /*printf("type: RLOG_EVENT_SECTION, length: %d, ", length);*/
	    fread(&cur_rank, sizeof(int), 1, pInput->f);
	    if (cur_rank - min_rank >= pInput->nNumRanks)
	    {
		printf("Error: event section out of range - %d <= %d <= %d\n", pInput->header.nMinRank, cur_rank, pInput->header.nMaxRank);
		free(pInput);
		return NULL;
	    }
	    rank_index = cur_rank - min_rank;
	    fread(&pInput->pNumEventRecursions[rank_index], sizeof(int), 1, pInput->f);
	    /*printf("levels: %d\n", pInput->nNumEventRecursions);*/
	    if (pInput->pNumEventRecursions[rank_index])
	    {
		pInput->ppCurEvent[rank_index] = (int*)malloc(pInput->pNumEventRecursions[rank_index] * sizeof(int));
		pInput->ppCurGlobalEvent[rank_index] = (int*)malloc(pInput->pNumEventRecursions[rank_index] * sizeof(int));
		pInput->gppCurEvent[rank_index] = (RLOG_EVENT*)malloc(pInput->pNumEventRecursions[rank_index] * sizeof(RLOG_EVENT));
		pInput->gppPrevEvent[rank_index] = (RLOG_EVENT*)malloc(pInput->pNumEventRecursions[rank_index] * sizeof(RLOG_EVENT));
		pInput->ppNumEvents[rank_index] = (int*)malloc(pInput->pNumEventRecursions[rank_index] * sizeof(int));
		pInput->ppEventOffset[rank_index] = (long*)malloc(pInput->pNumEventRecursions[rank_index] * sizeof(long));
	    }
	    for (i=0; i<pInput->pNumEventRecursions[rank_index]; i++)
	    {
		fread(&pInput->ppNumEvents[rank_index][i], sizeof(int), 1, pInput->f);
		/*printf(" level %2d: %d events\n", i, pInput->pNumEvents[i]);*/
	    }
	    if (pInput->pNumEventRecursions[rank_index])
	    {
		pInput->ppEventOffset[rank_index][0] = ftell(pInput->f);
		for (i=1; i<pInput->pNumEventRecursions[rank_index]; i++)
		{
		    pInput->ppEventOffset[rank_index][i] = pInput->ppEventOffset[rank_index][i-1] + (pInput->ppNumEvents[rank_index][i-1] * sizeof(RLOG_EVENT));
		}
	    }
	    length -= ((pInput->pNumEventRecursions[rank_index] + 2) * sizeof(int));
	    fseek(pInput->f, length, SEEK_CUR);
	    break;
	default:
	    /*printf("unknown section: type %d, length %d\n", type, length);*/
	    fseek(pInput->f, length, SEEK_CUR);
	    break;
	}
    }
    /* reset the iterators */
    RLOG_ResetStateIter(pInput);
    RLOG_ResetArrowIter(pInput);
    for (j=0; j<pInput->nNumRanks; j++)
    {
	for (i=0; i<pInput->pNumEventRecursions[j]; i++)
	    RLOG_ResetEventIter(pInput, j+pInput->header.nMinRank, i);
    }
    RLOG_ResetGlobalIter(pInput);

    return pInput;
}

static int compareArrows(const RLOG_ARROW *pLeft, const RLOG_ARROW *pRight)
{
    if (pLeft->end_time < pRight->end_time)
	return -1;
    if (pLeft->end_time == pRight->end_time)
	return 0;
    return 1;
}

static int ModifyArrows(FILE *f, int nNumArrows, int nMin, double *pOffsets, int n)
{
    RLOG_ARROW arrow, *pArray;
    int i, index, bModified;
    long arrow_pos;
    int error;
    double temp_time;

    fseek(f, 0, SEEK_CUR);
    arrow_pos = ftell(f);
    if (arrow_pos == -1)
	return errno;
    pArray = (RLOG_ARROW*)malloc(nNumArrows * sizeof(RLOG_ARROW));
    if (pArray)
    {
	printf("Modifying %d arrows\n", nNumArrows);
	/* read the arrows */
	fseek(f, 0, SEEK_CUR);
	error = ReadFileData((char*)pArray, nNumArrows * sizeof(RLOG_ARROW), f);
	if (error)
	{
	    free(pArray);
	    return error;
	}

	/* modify the arrows */
	for (i=0; i<nNumArrows; i++)
	{
	    arrow = pArray[i];

	    bModified = RLOG_FALSE;
	    index = (arrow.leftright == RLOG_ARROW_RIGHT) ? arrow.src - nMin : arrow.dest - nMin;
	    if (index >= 0 && index < n && pOffsets[index] != 0)
	    {
		arrow.start_time += pOffsets[index];
		bModified = RLOG_TRUE;
	    }
	    index = (arrow.leftright == RLOG_ARROW_RIGHT) ? arrow.dest - nMin : arrow.src - nMin;
	    if (index >= 0 && index < n && pOffsets[index] != 0)
	    {
		arrow.end_time += pOffsets[index];
		bModified = RLOG_TRUE;
	    }
	    if (bModified)
	    {
		if (arrow.start_time > arrow.end_time)
		{
		    temp_time = arrow.start_time;
		    arrow.start_time = arrow.end_time;
		    arrow.end_time = temp_time;
		    arrow.leftright = (arrow.leftright == RLOG_ARROW_LEFT) ? RLOG_ARROW_RIGHT : RLOG_ARROW_LEFT;
		}
		pArray[i] = arrow;
	    }
	}

	/* sort the arrows */
	qsort(pArray, (size_t)nNumArrows, sizeof(RLOG_ARROW), 
	      (int (*)(const void *,const void*))compareArrows);

	/* write the arrows back */
	fseek(f, arrow_pos, SEEK_SET);
	error = WriteFileData((char*)pArray, nNumArrows * sizeof(RLOG_ARROW), f);
	if (error)
	{
	    free(pArray);
	    return error;
	}
	fseek(f, 0, SEEK_CUR);
	free(pArray);
    }
    else
    {
	printf("Error: unable to allocate an array big enough to hold %d arrows\n", nNumArrows);
	return -1;
    }
    return 0;
}

#if 0
static int ModifyArrows(FILE *f, int nNumArrows, int nMin, double *pOffsets, int n)
{
    RLOG_ARROW arrow, *pArray;
    int i, index, bModified;
    int num_bytes;
    long arrow_pos;
    int error;
    double temp_time;

    printf("Modifying %d arrows\n", nNumArrows);
    arrow_pos = ftell(f);
    for (i=0; i<nNumArrows; i++)
    {
	num_bytes = fread(&arrow, 1, sizeof(RLOG_ARROW), f);
	if (num_bytes != sizeof(RLOG_ARROW))
	{
	    printf("reading arrow failed - num_bytes %d != %d, error %d\n", num_bytes, sizeof(RLOG_ARROW), ferror(f));
	    return -1;
	}
	bModified = RLOG_FALSE;
	index = (arrow.leftright == RLOG_ARROW_RIGHT) ? arrow.src - nMin : arrow.dest - nMin;
	if (index >= 0 && index < n && pOffsets[index] != 0)
	{
	    arrow.start_time += pOffsets[index];
	    bModified = RLOG_TRUE;
	}
	index = (arrow.leftright == RLOG_ARROW_RIGHT) ? arrow.dest - nMin : arrow.src - nMin;
	if (index >= 0 && index < n && pOffsets[index] != 0)
	{
	    arrow.end_time += pOffsets[index];
	    bModified = RLOG_TRUE;
	}
	if (bModified)
	{
	    if (arrow.start_time > arrow.end_time)
	    {
		temp_time = arrow.start_time;
		arrow.start_time = arrow.end_time;
		arrow.end_time = temp_time;
		arrow.leftright = (arrow.leftright = RLOG_ARROW_LEFT) ? RLOG_ARROW_RIGHT : RLOG_ARROW_LEFT;
	    }
	    fseek(f, -(int)sizeof(RLOG_ARROW), SEEK_CUR);
	    if (fwrite(&arrow, 1, sizeof(RLOG_ARROW), f) != sizeof(RLOG_ARROW))
	    {
		printf("writing modified arrow failed - error %d\n", ferror(f));
		return -1;
	    }
	    fseek(f, 0, SEEK_CUR);
	}
    }

    pArray = (RLOG_ARROW*)malloc(nNumArrows * sizeof(RLOG_ARROW));
    if (pArray)
    {
	/* read the arrows */
	fseek(f, arrow_pos, SEEK_SET);
	error = ReadFileData((char*)pArray, nNumArrows * sizeof(RLOG_ARROW), f);
	if (error)
	{
	    free(pArray);
	    return error;
	}
	/* sort the arrows */
	qsort(pArray, (size_t)nNumArrows, sizeof(RLOG_ARROW), 
	      (int (*)(const void *,const void*))compareArrows);
	/* write the arrows back */
	fseek(f, arrow_pos, SEEK_SET);
	error = WriteFileData((char*)pArray, nNumArrows * sizeof(RLOG_ARROW), f);
	if (error)
	{
	    free(pArray);
	    return error;
	}
	fseek(f, 0, SEEK_CUR);
	free(pArray);
    }
    else
    {
	printf("Error: unable to allocate an array big enough to hold %d arrows\n", nNumArrows);
	return -1;
    }
    return 0;
}
#endif

static int ModifyEvents(FILE *f, int nNumEvents, int nMin, double *pOffsets, int n);
static int ModifyEvents(FILE *f, int nNumEvents, int nMin, double *pOffsets, int n)
{
    RLOG_EVENT event;
    int i, index;
    int error;

    printf("Modifying %d events\n", nNumEvents);
    fseek(f, 0, SEEK_CUR);
    for (i=0; i<nNumEvents; i++)
    {
	error = ReadFileData((char*)&event, sizeof(RLOG_EVENT), f);
	if (error)
	{
	    rlog_err_printf("reading event failed.\n");
	    return -1;
	}
	index = event.rank - nMin;
	if (index >= 0 && index < n && pOffsets[index] != 0)
	{
	    event.start_time += pOffsets[index];
	    event.end_time += pOffsets[index];
	    fseek(f, -(int)sizeof(RLOG_EVENT), SEEK_CUR);
	    error = WriteFileData((const char *)&event, sizeof(RLOG_EVENT), f);
	    if (error)
	    {
		rlog_err_printf("writing modified event failed.\n");
		return -1;
	    }
	    fseek(f, 0, SEEK_CUR);
	}
    }
    return 0;
}

int RLOG_ModifyEvents(const char *filename, double *pOffsets, int n)
{
    int i, rank_index, cur_rank, min_rank = 0;
    /*int j;*/
    RLOG_IOStruct *pInput;
    int type, length;
    int error;

    /* allocate an input structure */
    pInput = (RLOG_IOStruct*)malloc(sizeof(RLOG_IOStruct));
    if (pInput == NULL)
    {
	printf("malloc failed - %s\n", strerror(errno));
	return -1;
    }
    pInput->ppCurEvent = NULL;
    pInput->ppCurGlobalEvent = NULL;
    pInput->gppCurEvent = NULL;
    pInput->gppPrevEvent = NULL;
    pInput->ppEventOffset = NULL;
    pInput->ppNumEvents = NULL;
    pInput->nNumArrows = 0;
    /* open the input rlog file */
    pInput->f = fopen(filename, "rb+");
    if (pInput->f == NULL)
    {
	printf("fopen(%s) failed, error: %s\n", filename, strerror(errno));
	free(pInput);
	return -1;
    }
    pInput->nNumRanks = 0;
    /* read the sections */
    while (fread(&type, sizeof(int), 1, pInput->f))
    {
	fread(&length, sizeof(int), 1, pInput->f);
	switch (type)
	{
	case RLOG_HEADER_SECTION:
	    /*printf("type: RLOG_HEADER_SECTION, length: %d\n", length);*/
	    if (length != sizeof(RLOG_FILE_HEADER))
	    {
		rlog_err_printf("error in header size %d != %d\n", length, sizeof(RLOG_FILE_HEADER));
		return -1;
	    }
	    if (ReadFileData((char*)&pInput->header, sizeof(RLOG_FILE_HEADER), pInput->f))
	    {
		rlog_err_printf("error reading rlog header\n");
		return -1;
	    }
	    
	    pInput->nNumRanks = pInput->header.nMaxRank + 1 - pInput->header.nMinRank;
	    min_rank = pInput->header.nMinRank;
	    
	    pInput->pRank = (int*)malloc(pInput->nNumRanks * sizeof(int));
	    pInput->pNumEventRecursions = (int*)malloc(pInput->nNumRanks * sizeof(int));
	    pInput->ppNumEvents = (int**)malloc(pInput->nNumRanks * sizeof(int*));
	    pInput->ppCurEvent = (int**)malloc(pInput->nNumRanks * sizeof(int*));
	    pInput->ppCurGlobalEvent = (int**)malloc(pInput->nNumRanks * sizeof(int*));
	    pInput->gppCurEvent = (RLOG_EVENT**)malloc(pInput->nNumRanks * sizeof(RLOG_EVENT*));
	    pInput->gppPrevEvent = (RLOG_EVENT**)malloc(pInput->nNumRanks * sizeof(RLOG_EVENT*));
	    pInput->ppEventOffset = (long**)malloc(pInput->nNumRanks * sizeof(long*));
	    for (i=0; i<pInput->nNumRanks; i++)
	    {
		pInput->pRank[i] = -1;
		pInput->pNumEventRecursions[i] = 0;
		pInput->ppNumEvents[i] = NULL;
		pInput->ppCurEvent[i] = NULL;
		pInput->ppCurGlobalEvent[i] = NULL;
		pInput->gppCurEvent[i] = NULL;
		pInput->gppPrevEvent[i] = NULL;
		pInput->ppEventOffset[i] = NULL;
	    }
	    break;
	case RLOG_STATE_SECTION:
	    /*printf("type: RLOG_STATE_SECTION, length: %d\n", length);*/
	    pInput->nNumStates = length / sizeof(RLOG_STATE);
	    pInput->nStateOffset = ftell(pInput->f);
	    fseek(pInput->f, length, SEEK_CUR);
	    break;
	case RLOG_ARROW_SECTION:
	    /*printf("type: RLOG_ARROW_SECTION, length: %d\n", length);*/
	    pInput->nNumArrows = length / sizeof(RLOG_ARROW);
	    pInput->nArrowOffset = ftell(pInput->f);
	    error = ModifyArrows(pInput->f, pInput->nNumArrows, pInput->header.nMinRank, pOffsets, n);
	    if (error)
	    {
		printf("Modifying the arrow section failed, error %d\n", error);
		RLOG_CloseInputStruct(&pInput);
		return -1;
	    }
	    /* fseek(pInput->f, length, SEEK_CUR); */
	    break;
	case RLOG_EVENT_SECTION:
	    /*printf("type: RLOG_EVENT_SECTION, length: %d, ", length);*/
	    fread(&cur_rank, sizeof(int), 1, pInput->f);
	    if (cur_rank - min_rank >= pInput->nNumRanks)
	    {
		printf("Error: event section out of range - %d <= %d <= %d\n", pInput->header.nMinRank, cur_rank, pInput->header.nMaxRank);
		RLOG_CloseInputStruct(&pInput);
		return -1;
	    }
	    rank_index = cur_rank - min_rank;
	    fread(&pInput->pNumEventRecursions[rank_index], sizeof(int), 1, pInput->f);
	    /*printf("levels: %d\n", pInput->nNumEventRecursions);*/
	    if (pInput->pNumEventRecursions[rank_index])
	    {
		pInput->ppCurEvent[rank_index] = (int*)malloc(pInput->pNumEventRecursions[rank_index] * sizeof(int));
		pInput->ppCurGlobalEvent[rank_index] = (int*)malloc(pInput->pNumEventRecursions[rank_index] * sizeof(int));
		pInput->gppCurEvent[rank_index] = (RLOG_EVENT*)malloc(pInput->pNumEventRecursions[rank_index] * sizeof(RLOG_EVENT));
		pInput->gppPrevEvent[rank_index] = (RLOG_EVENT*)malloc(pInput->pNumEventRecursions[rank_index] * sizeof(RLOG_EVENT));
		pInput->ppNumEvents[rank_index] = (int*)malloc(pInput->pNumEventRecursions[rank_index] * sizeof(int));
		pInput->ppEventOffset[rank_index] = (long*)malloc(pInput->pNumEventRecursions[rank_index] * sizeof(long));
	    }
	    for (i=0; i<pInput->pNumEventRecursions[rank_index]; i++)
	    {
		fread(&pInput->ppNumEvents[rank_index][i], sizeof(int), 1, pInput->f);
		/*printf(" level %2d: %d events\n", i, pInput->pNumEvents[i]);*/
	    }
	    if (pInput->pNumEventRecursions[rank_index])
	    {
		pInput->ppEventOffset[rank_index][0] = ftell(pInput->f);
		for (i=1; i<pInput->pNumEventRecursions[rank_index]; i++)
		{
		    pInput->ppEventOffset[rank_index][i] = pInput->ppEventOffset[rank_index][i-1] + (pInput->ppNumEvents[rank_index][i-1] * sizeof(RLOG_EVENT));
		}
	    }
	    length -= ((pInput->pNumEventRecursions[rank_index] + 2) * sizeof(int));
	    ModifyEvents(pInput->f, length / sizeof(RLOG_EVENT), pInput->header.nMinRank, pOffsets, n);
	    /* fseek(pInput->f, length, SEEK_CUR); */
	    break;
	default:
	    /*printf("unknown section: type %d, length %d\n", type, length);*/
	    fseek(pInput->f, length, SEEK_CUR);
	    break;
	}
    }
    /* reset the iterators */
    /*
    RLOG_ResetStateIter(pInput);
    RLOG_ResetArrowIter(pInput);
    for (j=0; j<pInput->nNumRanks; j++)
    {
	for (i=0; i<pInput->pNumEventRecursions[j]; i++)
	    RLOG_ResetEventIter(pInput, j+pInput->header.nMinRank, i);
    }
    RLOG_ResetGlobalIter(pInput);
    */
    RLOG_CloseInputStruct(&pInput);
    return 0;
}

int RLOG_CloseInputStruct(RLOG_IOStruct **ppInput)
{
    int i;
    if (ppInput == NULL)
	return -1;
    fclose((*ppInput)->f);
    for (i=0; i<(*ppInput)->nNumRanks; i++)
    {
	if ((*ppInput)->ppCurEvent[i])
	    free((*ppInput)->ppCurEvent[i]);
	if ((*ppInput)->ppCurGlobalEvent[i])
	    free((*ppInput)->ppCurGlobalEvent[i]);
	if ((*ppInput)->gppCurEvent[i])
	    free((*ppInput)->gppCurEvent[i]);
	if ((*ppInput)->gppPrevEvent[i])
	    free((*ppInput)->gppPrevEvent[i]);
	if ((*ppInput)->ppEventOffset[i])
	    free((*ppInput)->ppEventOffset[i]);
	if ((*ppInput)->ppNumEvents[i])
	    free((*ppInput)->ppNumEvents[i]);
    }
    if ((*ppInput)->ppCurEvent)
	free((*ppInput)->ppCurEvent);
    if ((*ppInput)->ppCurGlobalEvent)
	free((*ppInput)->ppCurGlobalEvent);
    if ((*ppInput)->gppCurEvent)
	free((*ppInput)->gppCurEvent);
    if ((*ppInput)->gppPrevEvent)
	free((*ppInput)->gppPrevEvent);
    if ((*ppInput)->ppEventOffset)
	free((*ppInput)->ppEventOffset);
    if ((*ppInput)->ppNumEvents)
	free((*ppInput)->ppNumEvents);
    free(*ppInput);
    *ppInput = NULL;
    return 0;
}

int RLOG_GetFileHeader(RLOG_IOStruct *pInput, RLOG_FILE_HEADER *pHeader)
{
    if (pInput == NULL)
	return -1;
    memcpy(pHeader, &pInput->header, sizeof(RLOG_FILE_HEADER));
    return 0;
}

int RLOG_GetNumStates(RLOG_IOStruct *pInput)
{
    if (pInput == NULL)
	return -1;
    return pInput->nNumStates;
}

int RLOG_GetState(RLOG_IOStruct *pInput, int i, RLOG_STATE *pState)
{
    if (pInput == NULL || pState == NULL || i < 0 || i >= pInput->nNumStates)
	return -1;
    fseek(pInput->f, pInput->nStateOffset + (i * sizeof(RLOG_STATE)), SEEK_SET);
    if (ReadFileData((char*)pState, sizeof(RLOG_STATE), pInput->f))
    {
	rlog_err_printf("Error reading rlog state\n");
	return -1;
    }

    pInput->nCurState = i+1;

    return 0;
}

int RLOG_ResetStateIter(RLOG_IOStruct *pInput)
{
    if (pInput == NULL)
	return -1;
    pInput->nCurState = 0;
    return 0;
}

int RLOG_GetNextState(RLOG_IOStruct *pInput, RLOG_STATE *pState)
{
    if (pInput == NULL || pState == NULL)
	return -1;
    if (pInput->nCurState >= pInput->nNumStates)
	return 1;
    fseek(pInput->f, pInput->nStateOffset + (pInput->nCurState * sizeof(RLOG_STATE)), SEEK_SET);
    if (ReadFileData((char*)pState, sizeof(RLOG_STATE), pInput->f))
    {
	rlog_err_printf("Error reading next rlog state\n");
	return -1;
    }
    pInput->nCurState++;
    return 0;
}

int RLOG_GetNumArrows(RLOG_IOStruct *pInput)
{
    if (pInput == NULL)
	return -1;
    return pInput->nNumArrows;
}

int RLOG_GetArrow(RLOG_IOStruct *pInput, int i, RLOG_ARROW *pArrow)
{
    if (pInput == NULL || pArrow == NULL || i < 0 || i >= pInput->nNumArrows)
	return -1;
    fseek(pInput->f, pInput->nArrowOffset + (i * sizeof(RLOG_ARROW)), SEEK_SET);
    if (ReadFileData((char*)pArrow, sizeof(RLOG_ARROW), pInput->f))
    {
	rlog_err_printf("Error reading rlog arrow\n");
	return -1;
    }

    pInput->nCurArrow = i+1;

    return 0;
}

int RLOG_ResetArrowIter(RLOG_IOStruct *pInput)
{
    if (pInput == NULL)
	return -1;
    pInput->nCurArrow = 0;
    return 0;
}

int RLOG_GetNextArrow(RLOG_IOStruct *pInput, RLOG_ARROW *pArrow)
{
    if (pInput == NULL)
	return -1;
    if (pInput->nCurArrow >= pInput->nNumArrows)
	return 1;
    fseek(pInput->f, pInput->nArrowOffset + (pInput->nCurArrow * sizeof(RLOG_ARROW)), SEEK_SET);
    if (ReadFileData((char*)pArrow, sizeof(RLOG_ARROW), pInput->f))
    {
	rlog_err_printf("Error reading next rlog arrow\n");
	return -1;
    }
    pInput->nCurArrow++;
    return 0;
}

int RLOG_GetNumEventRecursions(RLOG_IOStruct *pInput, int rank)
{
    if (pInput == NULL || rank < pInput->header.nMinRank || rank > pInput->header.nMaxRank)
	return -1;
    return pInput->pNumEventRecursions[rank - pInput->header.nMinRank];
}

int RLOG_GetNumEvents(RLOG_IOStruct *pInput, int rank, int recursion_level)
{
    int rank_index;
    if (pInput == NULL || recursion_level < 0 || rank < pInput->header.nMinRank || rank > pInput->header.nMaxRank)
	return -1;
    rank_index = rank - pInput->header.nMinRank;
    if (recursion_level >= pInput->pNumEventRecursions[rank_index])
	return -1;
    return pInput->ppNumEvents[rank_index][recursion_level];
}

int RLOG_GetEvent(RLOG_IOStruct *pInput, int rank, int recursion_level, int index, RLOG_EVENT *pEvent)
{
    int rank_index;
    if (pInput == NULL || pEvent == NULL || rank < pInput->header.nMinRank || rank > pInput->header.nMaxRank)
	return -1;
    rank_index = rank - pInput->header.nMinRank;
    if (recursion_level < 0 || recursion_level >= pInput->pNumEventRecursions[rank_index])
	return -1;
    if (index < 0 || index >= pInput->ppNumEvents[rank_index][recursion_level])
	return -1;

    fseek(pInput->f, pInput->ppEventOffset[rank_index][recursion_level] + (index * sizeof(RLOG_EVENT)), SEEK_SET);
    if (ReadFileData((char*)pEvent, sizeof(RLOG_EVENT), pInput->f))
    {
	rlog_err_printf("Error reading rlog event\n");
	return -1;
    }

    /* GetEvent sets the current iteration position also */
    pInput->ppCurEvent[rank_index][recursion_level] = index+1;

    return 0;
}

int RLOG_FindEventBeforeTimestamp(RLOG_IOStruct *pInput, int rank, int recursion_level, double timestamp, RLOG_EVENT *pEvent, int *pIndex)
{
    RLOG_EVENT event;
    int low, high, mid;
    int rank_index;

    if (pInput == NULL || pEvent == NULL || rank < pInput->header.nMinRank || rank > pInput->header.nMaxRank)
	return -1;
    rank_index = rank - pInput->header.nMinRank;
    if (recursion_level < 0 || recursion_level >= pInput->pNumEventRecursions[rank_index])
	return -1;

    low = 0;
    high = pInput->ppNumEvents[rank_index][recursion_level]-1;
    mid = high/2;

    for (;;)
    {
	RLOG_GetEvent(pInput, rank, recursion_level, mid, &event);
	if (event.start_time < timestamp)
	{
	    low = mid;
	}
	else
	    high = mid;
	mid = (low + high) / 2;
	if (low == mid)
	{
	    if (event.start_time < timestamp)
	    {
		RLOG_GetEvent(pInput, rank, recursion_level, low+1, &event);
		if (event.start_time < timestamp)
		    low++;
	    }
	    break;
	}
    }
    if (pIndex != NULL)
	*pIndex = low;
    return RLOG_GetEvent(pInput, rank, recursion_level, low, pEvent);
}

int RLOG_FindAnyEventBeforeTimestamp(RLOG_IOStruct *pInput, int rank, double timestamp, RLOG_EVENT *pEvent)
{
    RLOG_EVENT event, cur_event;
    int index, i, rank_index;

    if (pInput == NULL || pEvent == NULL || rank < pInput->header.nMinRank || rank > pInput->header.nMaxRank)
	return -1;
    rank_index = rank - pInput->header.nMinRank;

    if (RLOG_FindEventBeforeTimestamp(pInput, rank, 0, timestamp, &event, &index) == -1)
	return -1;
    for (i=1; i<pInput->pNumEventRecursions[rank_index]; i++)
    {
	if (RLOG_FindEventBeforeTimestamp(pInput, rank, i, timestamp, &cur_event, &index) != -1)
	{
	    if (cur_event.start_time > event.start_time)
		event = cur_event;
	}
    }
    *pEvent = event;
    return 0;
}

int RLOG_ResetEventIter(RLOG_IOStruct *pInput, int rank, int recursion_level)
{
    int rank_index;
    if (pInput == NULL || recursion_level < 0 || rank < pInput->header.nMinRank || rank > pInput->header.nMaxRank)
	return -1;
    rank_index = rank - pInput->header.nMinRank;
    if (recursion_level < pInput->pNumEventRecursions[rank_index] && 
	pInput->ppCurEvent[rank_index] != NULL)
    {
	pInput->ppCurEvent[rank_index][recursion_level] = 0;
    }
    return 0;
}

int RLOG_GetNextEvent(RLOG_IOStruct *pInput, int rank, int recursion_level, RLOG_EVENT *pEvent)
{
    int rank_index;
    if (pInput == NULL || recursion_level < 0 || pEvent == NULL || rank < pInput->header.nMinRank || rank > pInput->header.nMaxRank)
	return -1;
    rank_index = rank - pInput->header.nMinRank;
    if (recursion_level < pInput->pNumEventRecursions[rank_index] && pInput->ppCurEvent[rank_index] != NULL)
    {
	if (pInput->ppCurEvent[rank_index][recursion_level] >= pInput->ppNumEvents[rank_index][recursion_level])
	    return 1;
	fseek(pInput->f, 
	    pInput->ppEventOffset[rank_index][recursion_level] + 
	    (pInput->ppCurEvent[rank_index][recursion_level] * sizeof(RLOG_EVENT)), SEEK_SET);
	if (ReadFileData((char*)pEvent, sizeof(RLOG_EVENT), pInput->f))
	{
	    rlog_err_printf("Error reading next rlog event\n");
	    return -1;
	}
	pInput->ppCurEvent[rank_index][recursion_level]++;
	return 0;
    }
    return 1;
}

int RLOG_GetRankRange(RLOG_IOStruct *pInput, int *pMin, int *pMax)
{
    if (pInput == NULL)
	return -1;
    *pMin = pInput->header.nMinRank;
    *pMax = pInput->header.nMaxRank;
    return 0;
}

static RLOG_BOOL FindMinGlobalEvent(RLOG_IOStruct *pInput, int *rank, int *level, int *index);
static RLOG_BOOL FindMinGlobalEvent(RLOG_IOStruct *pInput, int *rank, int *level, int *index)
{
    int i,j;
    double dmin = RLOG_MAX_DOUBLE;
    RLOG_BOOL found = RLOG_FALSE;

    if (pInput == NULL)
	return RLOG_FALSE;

    for (i=0; i<pInput->nNumRanks; i++)
    {
	for (j=0; j<pInput->pNumEventRecursions[i]; j++)
	{
	    if (pInput->ppCurGlobalEvent[i][j] < pInput->ppNumEvents[i][j])
	    {
		if (pInput->gppCurEvent[i][j].start_time < dmin)
		{
		    *rank = i;
		    *level = j;
		    *index = pInput->ppCurGlobalEvent[i][j];
		    dmin = pInput->gppCurEvent[i][j].start_time;
		    found = RLOG_TRUE;
		}
	    }
	}
    }

    return found;
}

static RLOG_BOOL FindMaxGlobalEvent(RLOG_IOStruct *pInput, int *rank, int *level, int *index);
static RLOG_BOOL FindMaxGlobalEvent(RLOG_IOStruct *pInput, int *rank, int *level, int *index)
{
    int i,j;
    double dmax = RLOG_MIN_DOUBLE;
    RLOG_BOOL found = RLOG_FALSE;

    if (pInput == NULL)
	return RLOG_FALSE;

    for (i=0; i<pInput->nNumRanks; i++)
    {
	for (j=0; j<pInput->pNumEventRecursions[i]; j++)
	{
	    if (pInput->ppCurGlobalEvent[i][j] > 0)
	    {
		if (pInput->gppPrevEvent[i][j].start_time > dmax)
		{
		    *rank = i;
		    *level = j;
		    *index = pInput->ppCurGlobalEvent[i][j];
		    dmax = pInput->gppPrevEvent[i][j].start_time;
		    found = RLOG_TRUE;
		}
	    }
	}
    }

    return found;
}

int RLOG_ResetGlobalIter(RLOG_IOStruct *pInput)
{
    int i,j, n;
    RLOG_EVENT min_event = {0};
    RLOG_BOOL bMinSet = RLOG_FALSE;

    if (pInput == NULL)
	return -1;

    pInput->gnCurRank = 0;
    pInput->gnCurLevel = 0;
    pInput->gnCurEvent = 0;

    for (i=0; i<pInput->nNumRanks; i++)
    {
	/* reset all the cur_events to zero for each rank */
	for (j=0; j<pInput->pNumEventRecursions[i]; j++)
	{
	    pInput->ppCurGlobalEvent[i][j] = 0;
	    /* get the first event for each rank:level */
	    n = pInput->ppCurEvent[i][j];
	    RLOG_GetEvent(pInput, pInput->header.nMinRank + i, j, 0, &pInput->gppCurEvent[i][j]);
	    /* reset the cur_event after reading */
	    pInput->ppCurEvent[i][j] = n;
	}
	if (pInput->pNumEventRecursions[i] > 0)
	{
	    if (!bMinSet)
	    {
		min_event = pInput->gppCurEvent[pInput->header.nMinRank+i][0];
		bMinSet = RLOG_TRUE;
	    }
	    /* save the rank with the earliest event */
	    if (min_event.start_time > pInput->gppCurEvent[i][0].start_time)
	    {
		min_event = pInput->gppCurEvent[i][0];
		pInput->gnCurRank = i;
	    }
	}
    }

    /* save the global current event */
    pInput->gCurEvent = pInput->gppCurEvent[pInput->gnCurRank][pInput->gnCurLevel];

    /* get the next event to replace the current */
    n = pInput->ppCurEvent[pInput->gnCurRank][pInput->gnCurLevel];
    /* get the next event */
    RLOG_GetEvent(pInput, pInput->gnCurRank, pInput->gnCurLevel, 1, 
	&pInput->gppCurEvent[pInput->gnCurRank][pInput->gnCurLevel]);
    pInput->ppCurGlobalEvent[pInput->gnCurRank][pInput->gnCurLevel] = 1;
    /* reset the cur_event after reading */
    pInput->ppCurEvent[pInput->gnCurRank][pInput->gnCurLevel] = n;

    return 0;
}

int RLOG_GetNextGlobalEvent(RLOG_IOStruct *pInput, RLOG_EVENT *pEvent)
{
    int n;

    if (pInput == NULL || pEvent == NULL)
	return -1;

    /* put the current in the previous slot */
    pInput->gppPrevEvent[pInput->gnCurRank][pInput->gnCurLevel] = pInput->gCurEvent;

    /* find the next event and put it in the current event */
    if (!FindMinGlobalEvent(pInput, &pInput->gnCurRank, &pInput->gnCurLevel, &pInput->gnCurEvent))
    {
	/* find min failed meaning we are at the end, so replace the event we just over-wrote */
	n = pInput->ppCurEvent[pInput->gnCurRank][pInput->gnCurLevel];
	RLOG_GetEvent(pInput, pInput->gnCurRank, pInput->gnCurLevel, pInput->gnCurEvent-2, 
	    &pInput->gppCurEvent[pInput->gnCurRank][pInput->gnCurLevel]);
	pInput->ppCurEvent[pInput->gnCurRank][pInput->gnCurLevel] = n;
	return -1;
    }
    pInput->gCurEvent = pInput->gppCurEvent[pInput->gnCurRank][pInput->gnCurLevel];

    /* replace the next event with its next event */

    /* save the current position */
    n = pInput->ppCurEvent[pInput->gnCurRank][pInput->gnCurLevel];
    /* get the next event */
    RLOG_GetEvent(pInput, pInput->gnCurRank, pInput->gnCurLevel, pInput->gnCurEvent+1, 
	&pInput->gppCurEvent[pInput->gnCurRank][pInput->gnCurLevel]);
    pInput->ppCurGlobalEvent[pInput->gnCurRank][pInput->gnCurLevel] = pInput->gnCurEvent+1;
    /* reset the current position */
    pInput->ppCurEvent[pInput->gnCurRank][pInput->gnCurLevel] = n;


    /* return the new current event */
    *pEvent = pInput->gCurEvent;

    return 0;
}

int RLOG_GetPreviousGlobalEvent(RLOG_IOStruct *pInput, RLOG_EVENT *pEvent)
{
    int n;

    if (pInput == NULL || pEvent == NULL)
	return -1;

    /* put the current back in its next slot */
    pInput->gppCurEvent[pInput->gnCurRank][pInput->gnCurLevel] = pInput->gCurEvent;
    pInput->ppCurGlobalEvent[pInput->gnCurRank][pInput->gnCurLevel]--;

    /* find the previous event and put it in the current event */
    if (!FindMaxGlobalEvent(pInput, &pInput->gnCurRank, &pInput->gnCurLevel, &pInput->gnCurEvent))
    {
	/* find max failed meaning we are at the beginning, so replace the event we just over-wrote */
	n = pInput->ppCurEvent[pInput->gnCurRank][pInput->gnCurLevel];
	RLOG_GetEvent(pInput, pInput->gnCurRank, pInput->gnCurLevel, 1, 
	    &pInput->gppCurEvent[pInput->gnCurRank][pInput->gnCurLevel]);
	pInput->ppCurGlobalEvent[pInput->gnCurRank][pInput->gnCurLevel] = 1;
	pInput->ppCurEvent[pInput->gnCurRank][pInput->gnCurLevel] = n;
	return -1;
    }
    pInput->gCurEvent = pInput->gppPrevEvent[pInput->gnCurRank][pInput->gnCurLevel];


    /* replace the previous event with its previous event */

    /* save the current position */
    n = pInput->ppCurEvent[pInput->gnCurRank][pInput->gnCurLevel];
    /* get the previous event */
    RLOG_GetEvent(pInput, pInput->gnCurRank, pInput->gnCurLevel, pInput->gnCurEvent-2, 
	&pInput->gppPrevEvent[pInput->gnCurRank][pInput->gnCurLevel]);
    /* reset the current position */
    pInput->ppCurEvent[pInput->gnCurRank][pInput->gnCurLevel] = n;


    /* return the new current event */
    *pEvent = pInput->gCurEvent;

    return 0;
}

int RLOG_GetCurrentGlobalEvent(RLOG_IOStruct *pInput, RLOG_EVENT *pEvent)
{
    if (pInput == NULL || pEvent == NULL)
	return -1;
    if (pInput->gnCurRank < 0 || pInput->gnCurRank >= pInput->nNumRanks)
	return -1;
    if (pInput->gnCurLevel < 0 || pInput->gnCurLevel >= pInput->pNumEventRecursions[pInput->gnCurRank])
	return -1;
    if (pInput->gnCurEvent < 0 || pInput->gnCurEvent >= pInput->ppNumEvents[pInput->gnCurRank][pInput->gnCurLevel])
	return -1;

    *pEvent = pInput->gCurEvent;

    return 0;
}

int RLOG_PrintGlobalState(RLOG_IOStruct *pInput)
{
    int i,j;

    for (i=0; i<pInput->nNumRanks; i++)
    {
	for (j=0; j<pInput->pNumEventRecursions[i]; j++)
	{
	    printf("[%d][%d] prev: (%g - %g) ", i, j, pInput->gppPrevEvent[i][j].start_time, pInput->gppPrevEvent[i][j].end_time);
	    printf("next: (%g - %g)\n", pInput->gppCurEvent[i][j].start_time, pInput->gppCurEvent[i][j].end_time);
	}
    }
    return 0;
}

int RLOG_FindGlobalEventBeforeTimestamp(RLOG_IOStruct *pInput, double timestamp, RLOG_EVENT *pEvent)
{
    int i,j, n;

    if (pInput == NULL || pEvent == NULL)
	return -1;

    pInput->gnCurRank = 0;
    pInput->gnCurLevel = 0;
    pInput->gnCurEvent = 0;

    /* set all the current and previous events for each rank */
    for (i=0; i<pInput->nNumRanks; i++)
    {
	for (j=0; j<pInput->pNumEventRecursions[i]; j++)
	{
	    n = pInput->ppCurEvent[i][j]; /* save iterator */

	    RLOG_FindEventBeforeTimestamp(pInput, 
		pInput->header.nMinRank + i, j, 
		timestamp, 
		&pInput->gppPrevEvent[i][j], 
		&pInput->ppCurGlobalEvent[i][j]);
	    if (pInput->gppPrevEvent[i][j].start_time > timestamp)
	    {
		/* the start time can only be after the timestamp if this event is the very first event at this level */
		/*
		if (pInput->ppCurGlobalEvent[i][j] != 0);
		{
		    printf("RLOG_FindGlobalEventBeforeTimestamp: Error, start_time > timestamp, %g > %g", pInput->gppPrevEvent[i][j].start_time, timestamp);
		    return -1;
		}
		*/
		pInput->gppCurEvent[i][j] = pInput->gppPrevEvent[i][j];
	    }
	    else
	    {
		pInput->ppCurGlobalEvent[i][j]++;
		RLOG_GetEvent(pInput, pInput->header.nMinRank + i, j,
		    pInput->ppCurGlobalEvent[i][j],
		    &pInput->gppCurEvent[i][j]);
	    }

	    pInput->ppCurEvent[i][j] = n; /* restore iterator */
	}
    }

    /* find the maximum of the previous events */
    FindMaxGlobalEvent(pInput, &pInput->gnCurRank, &pInput->gnCurLevel, &pInput->gnCurEvent);

    /* save this event as the global current event */
    pInput->gCurEvent = pInput->gppPrevEvent[pInput->gnCurRank][pInput->gnCurLevel];

    /* save the current position */
    n = pInput->ppCurEvent[pInput->gnCurRank][pInput->gnCurLevel];
    /* get the previous event */
    RLOG_GetEvent(pInput, pInput->gnCurRank, pInput->gnCurLevel, pInput->gnCurEvent-1, 
	&pInput->gppPrevEvent[pInput->gnCurRank][pInput->gnCurLevel]);
    /* reset the current position */
    pInput->ppCurEvent[pInput->gnCurRank][pInput->gnCurLevel] = n;

    /* return the new current event */
    *pEvent = pInput->gCurEvent;

    return 0;
}

int RLOG_FindArrowBeforeTimestamp(RLOG_IOStruct *pInput, double timestamp, RLOG_ARROW *pArrow, int *pIndex)
{
    RLOG_ARROW arrow;
    int low, high, mid;

    if (pInput == NULL || pArrow == NULL)
	return -1;

    low = 0;
    high = pInput->nNumArrows - 1;
    mid = high/2;

    for (;;)
    {
	RLOG_GetArrow(pInput, mid, &arrow);
	if (arrow.end_time < timestamp)
	{
	    low = mid;
	}
	else
	    high = mid;
	mid = (low + high) / 2;
	if (low == mid)
	{
	    if (arrow.end_time < timestamp)
	    {
		RLOG_GetArrow(pInput, low+1, &arrow);
		if (arrow.end_time < timestamp)
		    low++;
	    }
	    break;
	}
    }
    if (pIndex != NULL)
	*pIndex = low;
    return RLOG_GetArrow(pInput, low, pArrow);
}

int RLOG_HitTest(RLOG_IOStruct *pInput, int rank, int level, double timestamp, RLOG_EVENT *pEvent)
{
    int rank_index;
    if (pInput == NULL || pEvent == NULL || level < 0)
	return -1;
    if (rank < pInput->header.nMinRank || rank >= pInput->header.nMaxRank)
	return -1;
    rank_index = rank - pInput->header.nMinRank;
    if (level >= pInput->pNumEventRecursions[rank_index])
	return -1;
    return 0;
}
