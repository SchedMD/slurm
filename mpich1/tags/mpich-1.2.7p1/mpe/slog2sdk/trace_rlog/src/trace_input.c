/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  $Id: trace_input.c,v 1.6 2005/05/17 22:35:11 chan Exp $
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifdef USE_WINCONF_H
#include "wintrace_impl.h"
#else
#include "trace_impl.h"
#endif
#include "rlog.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_CTYPE_H
#include <ctype.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include "trace_API.h"

#define TRACEINPUT_SUCCESS 0
#define TRACEINPUT_FAIL    -1

#define INVALID_EVENT -1

typedef struct _trace_file
{
    RLOG_IOStruct *pInput;
    RLOG_STATE state;
    RLOG_ARROW arrow;
    RLOG_BOOL bArrowAvail;
    RLOG_EVENT **ppEvent;
    RLOG_BOOL **ppEventAvail;
} _trace_file;

static RLOG_BOOL PackQuadChar(char *str, int *length, char *base, int *pos, const int max);
static RLOG_BOOL PackQuadChar(char *str, int *length, char *base, int *pos, const int max)
{
    /* Am I supposed to include the terminating NULL character? */
    /* This function does not. */
    if (*pos + (int)strlen(str) > max)
	return RLOG_FALSE;
    *length = strlen(str);
    memcpy(&base[*pos], str, *length);
    *pos += *length;
    return RLOG_TRUE;
}

static RLOG_BOOL PackQuadInt(int n1, int n2, int *length, int *base, int *pos, const int max);
static RLOG_BOOL PackQuadInt(int n1, int n2, int *length, int *base, int *pos, const int max)
{
    if (*pos + 2 > max)
	return RLOG_FALSE;
    *length = 2;
    base[*pos] = n1;
    base[*pos+1] = n2;
    *pos += 2;
    return RLOG_TRUE;
}

static RLOG_BOOL PackQuadDouble(double d1, double d2, int *length, double *base, int *pos, const int max);
static RLOG_BOOL PackQuadDouble(double d1, double d2, int *length, double *base, int *pos, const int max)
{
    if (*pos + 2 > max)
	return RLOG_FALSE;
    *length = 2;
    base[*pos] = d1;
    base[*pos+1] = d2;
    *pos += 2;
    return RLOG_TRUE;
}

TRACE_EXPORT int TRACE_Open( const char filespec[], TRACE_file *fp )
{
    int i, j;
    RLOG_IOStruct *pInput;

    if (filespec == NULL || fp == NULL)
	return TRACEINPUT_FAIL;

    if (strstr(filespec, "-h"))
    {
	*fp = NULL;
	return TRACEINPUT_SUCCESS;
    }

    *fp = (_trace_file*)malloc(sizeof(_trace_file));
    if (*fp == NULL)
	return TRACEINPUT_FAIL;

    (*fp)->pInput = pInput = RLOG_CreateInputStruct(filespec);
    if (pInput == NULL)
    {
	free(*fp);
	*fp = NULL;
	return TRACEINPUT_FAIL;
    }

    (*fp)->bArrowAvail = (RLOG_GetNextArrow(pInput, &(*fp)->arrow) == 0);
    if (pInput->nNumRanks > 0)
    {
	(*fp)->ppEvent = (RLOG_EVENT**)malloc(sizeof(RLOG_EVENT*) * pInput->nNumRanks);
	(*fp)->ppEventAvail = (int**)malloc(sizeof(int*) * pInput->nNumRanks);

	for (i=0; i<pInput->nNumRanks; i++)
	{
	    if (pInput->pNumEventRecursions[i] > 0)
	    {
		(*fp)->ppEvent[i] = (RLOG_EVENT*)malloc(sizeof(RLOG_EVENT) * pInput->pNumEventRecursions[i]);
		(*fp)->ppEventAvail[i] = (int*)malloc(sizeof(int) * pInput->pNumEventRecursions[i]);
	    }
	    else
	    {
		(*fp)->ppEvent[i] = NULL;
		(*fp)->ppEventAvail[i] = NULL;
	    }
	}
    }
    else
    {
	(*fp)->ppEvent = NULL;
	(*fp)->ppEventAvail = NULL;
    }
    for (j=0; j<pInput->nNumRanks; j++)
    {
	for (i=0; i<pInput->pNumEventRecursions[j]; i++)
	{
	    (*fp)->ppEventAvail[j][i] = (RLOG_GetNextEvent(pInput, j+pInput->header.nMinRank, i, &(*fp)->ppEvent[j][i]) == 0);
	}
    }
    return TRACEINPUT_SUCCESS;
}

TRACE_EXPORT int TRACE_Close( TRACE_file *fp )
{
    int i;

    if (*fp == NULL)
	return TRACEINPUT_SUCCESS;

    if ((*fp)->pInput)
    {
	for (i=0; i<(*fp)->pInput->nNumRanks; i++)
	{
	    if ( (*fp)->ppEvent[i] )
		free( (*fp)->ppEvent[i] );
	    if ( (*fp)->ppEventAvail[i] )
		free( (*fp)->ppEventAvail[i] );
	}
	RLOG_CloseInputStruct(&(*fp)->pInput);
    }
    if ((*fp)->ppEvent)
	free( (*fp)->ppEvent );
    if ((*fp)->ppEventAvail)
	free( (*fp)->ppEventAvail );
    free( (*fp) );
    *fp = NULL;

    return TRACEINPUT_SUCCESS;
}

TRACE_EXPORT int TRACE_Peek_next_kind( const TRACE_file fp, TRACE_Rec_Kind_t *next_kind )
{
    int i,j;

    *next_kind = TRACE_EOF;
    if (fp->pInput->nCurState < fp->pInput->nNumStates)
    {
	*next_kind = TRACE_CATEGORY;
	return TRACEINPUT_SUCCESS;
    }

    for (j=0; j<fp->pInput->nNumRanks; j++)
    {
	for (i=0; i<fp->pInput->pNumEventRecursions[j]; i++)
	{
	    if (fp->ppEventAvail[j][i])
	    {
		*next_kind = TRACE_PRIMITIVE_DRAWABLE;
		return TRACEINPUT_SUCCESS;
	    }
	}
    }	
    if (fp->bArrowAvail)
    {
	*next_kind = TRACE_PRIMITIVE_DRAWABLE;
	return TRACEINPUT_SUCCESS;
    }

    return TRACEINPUT_SUCCESS;
}

TRACE_EXPORT int TRACE_Get_next_method( const TRACE_file fp,
                           char method_name[], char method_extra[], 
                           int *methodID )
{
    return TRACEINPUT_FAIL;
}

TRACE_EXPORT int TRACE_Peek_next_category( const TRACE_file fp,
                              int *n_legend, int *n_label,
                              int *n_methodIDs )
{
    if (fp->pInput->nCurState >= fp->pInput->nNumStates)
	return TRACEINPUT_FAIL;

    if (RLOG_GetNextState(fp->pInput, &fp->state) != 0)
	return TRACEINPUT_FAIL;
    *n_legend = strlen(fp->state.description)+1;
    *n_label = 0;
    *n_methodIDs = 0;

    return TRACEINPUT_SUCCESS;
}

TRACE_EXPORT int TRACE_Get_next_category( const TRACE_file fp, 
                             TRACE_Category_head_t *head,
                             int *n_legend, char legend_base[],
                             int *legend_pos, const int legend_max,
                             int *n_label, char label_base[],
                             int *label_pos, const int label_max,
                             int *n_methodIDs, int methodID_base[],
                             int *methodID_pos, const int methodID_max )
{
    char *pColorStr = fp->state.color;

    head->index = fp->state.event;
    while (isspace(*pColorStr))
	pColorStr++;
    head->red = atoi(pColorStr);
    while (!isspace(*pColorStr))
	pColorStr++;
    while (isspace(*pColorStr))
	pColorStr++;
    head->green = atoi(pColorStr);
    while (!isspace(*pColorStr))
	pColorStr++;
    while (isspace(*pColorStr))
	pColorStr++;
    head->blue = atoi(pColorStr);
    head->alpha = 255;
    if (fp->state.event == RLOG_ARROW_EVENT_ID)
	head->shape = TRACE_SHAPE_ARROW;
    else
	head->shape = TRACE_SHAPE_STATE;
    head->width = 1;

    if (!PackQuadChar(fp->state.description, n_legend, legend_base, legend_pos, legend_max))
	return TRACEINPUT_FAIL;

    *n_label = 0;
    *n_methodIDs = 0;

    return TRACEINPUT_SUCCESS;
}

TRACE_EXPORT int TRACE_Peek_next_ycoordmap( TRACE_file fp,
                               int *n_rows, int *n_columns,
                               int *max_column_name,
                               int *max_title_name,
                               int *n_methodIDs )
{
    return TRACEINPUT_FAIL;
}

TRACE_EXPORT int TRACE_Get_next_ycoordmap( TRACE_file fp,
                              char *title_name,
                              char **column_names,
                              int *coordmap_sz, int coordmap_base[],
                              int *coordmap_pos, const int coordmap_max,
                              int *n_methodIDs, int methodID_base[],
                              int *methodID_pos, const int methodID_max )
{
    return TRACEINPUT_FAIL;
}

TRACE_EXPORT int TRACE_Peek_next_primitive( const TRACE_file fp,
                               double *starttime, double *endtime,
                               int *n_tcoords, int *n_ycoords, int *n_bytes )
{
    int i, j, rank = -1, level = -1;
    double dmin = -10000000.0;
    RLOG_BOOL done = RLOG_FALSE;

    *n_tcoords = 2;
    *n_ycoords = 2;
    *n_bytes = 0;

    for (j=0; j<fp->pInput->nNumRanks && !done; j++)
    {
	for (i=0; i<fp->pInput->pNumEventRecursions[j] && !done; i++)
	{
	    if (fp->ppEventAvail[j][i])
	    {
		level = i;
		rank  = j;
		dmin = fp->ppEvent[j][i].end_time;
		done = RLOG_TRUE;
	    }
	}
    }
    if (level == -1)
    {
	if (!fp->bArrowAvail)
	    return TRACEINPUT_FAIL;
	*starttime = fp->arrow.start_time;
	*endtime = fp->arrow.end_time;
	return TRACEINPUT_SUCCESS;
    }
    for (j=0; j<fp->pInput->nNumRanks; j++)
    {
	for (i=0; i<fp->pInput->pNumEventRecursions[j]; i++)
	{
	    if (fp->ppEventAvail[j][i])
	    {
		if (fp->ppEvent[j][i].end_time < dmin)
		{
		    level = i;
		    rank = j;
		    dmin = fp->ppEvent[j][i].end_time;
		}
	    }
	}
    }
    if (fp->bArrowAvail)
    {
	if (fp->arrow.end_time < dmin)
	{
	    *starttime = fp->arrow.start_time;
	    *endtime = fp->arrow.end_time;
	    return TRACEINPUT_SUCCESS;
	}
    }
    *starttime = fp->ppEvent[rank][level].start_time;
    *endtime = fp->ppEvent[rank][level].end_time;

    return TRACEINPUT_SUCCESS;
}

TRACE_EXPORT int TRACE_Get_next_primitive( const TRACE_file fp, 
                              int *category_index, 
                              int *n_tcoords, double tcoord_base[],
                              int *tcoord_pos, const int tcoord_max, 
                              int *n_ycoords, int ycoord_base[], 
                              int *ycoord_pos, const int ycoord_max,
                              int *n_bytes, char byte_base[],
                              int *byte_pos, const int byte_max )
{
    int i, j, rank = 1, level = -1;
    double dmin = -10000000.0;
    /* RLOG_BOOL done = RLOG_FALSE; */

    *n_bytes = 0;

    for (j=0; j<fp->pInput->nNumRanks; j++)
    {
	for (i=0; i<fp->pInput->pNumEventRecursions[j]; i++)
	{
	    if (fp->ppEventAvail[j][i])
	    {
		level = i;
		rank = j;
		dmin = fp->ppEvent[j][i].end_time;
		break;
	    }
	}
    }
    if (level == -1)
    {
	if (!fp->bArrowAvail)
	    return TRACEINPUT_FAIL;
	*category_index = RLOG_ARROW_EVENT_ID;
	if (fp->arrow.leftright == RLOG_ARROW_RIGHT)
	{
	    PackQuadDouble(
		fp->arrow.start_time, 
		fp->arrow.end_time, 
		n_tcoords, tcoord_base, tcoord_pos, tcoord_max);
	}
	else
	{
	    PackQuadDouble(
		fp->arrow.end_time,
		fp->arrow.start_time, 
		n_tcoords, tcoord_base, tcoord_pos, tcoord_max);
	}
	PackQuadInt(
	    fp->arrow.src, 
	    fp->arrow.dest, 
	    n_ycoords, ycoord_base, ycoord_pos, ycoord_max);
	fp->bArrowAvail = (RLOG_GetNextArrow(fp->pInput, &fp->arrow) == 0);
	return TRACEINPUT_SUCCESS;
    }
    for (j=0; j<fp->pInput->nNumRanks; j++)
    {
	for (i=0; i<fp->pInput->pNumEventRecursions[j]; i++)
	{
	    if (fp->ppEventAvail[j][i])
	    {
		if (fp->ppEvent[j][i].end_time < dmin)
		{
		    level = i;
		    rank = j;
		    dmin = fp->ppEvent[j][i].end_time;
		}
	    }
	}
    }
    if (fp->bArrowAvail)
    {
	if (fp->arrow.end_time < dmin)
	{
	    *category_index = RLOG_ARROW_EVENT_ID;
	    if (fp->arrow.leftright == RLOG_ARROW_RIGHT)
	    {
		PackQuadDouble(
		    fp->arrow.start_time, 
		    fp->arrow.end_time, 
		    n_tcoords, tcoord_base, tcoord_pos, tcoord_max);
	    }
	    else
	    {
		PackQuadDouble(
		    fp->arrow.end_time,
		    fp->arrow.start_time, 
		    n_tcoords, tcoord_base, tcoord_pos, tcoord_max);
	    }
	    PackQuadInt(
		fp->arrow.src, 
		fp->arrow.dest, 
		n_ycoords, ycoord_base, ycoord_pos, ycoord_max);
	    fp->bArrowAvail = (RLOG_GetNextArrow(fp->pInput, &fp->arrow) == 0);
	    return TRACEINPUT_SUCCESS;
	}
    }
    *category_index = fp->ppEvent[rank][level].event;
    PackQuadDouble(
	fp->ppEvent[rank][level].start_time, 
	fp->ppEvent[rank][level].end_time, 
	n_tcoords, tcoord_base, tcoord_pos, tcoord_max);
    PackQuadInt(
	fp->ppEvent[rank][level].rank, 
	fp->ppEvent[rank][level].rank, 
	n_ycoords, ycoord_base, ycoord_pos, ycoord_max);
    fp->ppEventAvail[rank][level] = 
	(RLOG_GetNextEvent(
			    fp->pInput, 
			    rank + fp->pInput->header.nMinRank, 
			    level, 
			    &fp->ppEvent[rank][level]) == 0);

    return TRACEINPUT_SUCCESS;
}

TRACE_EXPORT int TRACE_Peek_next_composite( const TRACE_file fp,
                               double *starttime, double *endtime,
                               int *n_primitives, int *n_bytes )
{
    return TRACEINPUT_FAIL;
}

TRACE_EXPORT int TRACE_Get_next_composite( const TRACE_file fp,
                              int *category_index,
                              int *n_bytes, char byte_base[],
                              int *byte_pos, const int byte_max )
{
    return TRACEINPUT_FAIL;
}


TRACE_EXPORT int TRACE_Get_position( TRACE_file fp, TRACE_int64_t *offset )
{
    return TRACEINPUT_FAIL;
}

TRACE_EXPORT int TRACE_Set_position( TRACE_file fp, TRACE_int64_t offset )
{
    return TRACEINPUT_FAIL;
}

TRACE_EXPORT char *TRACE_Get_err_string( int ierr )
{
    return "Failure in RLOG file processing.";
}
