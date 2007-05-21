/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  $Id: rlog.h,v 1.6 2005/07/05 18:38:54 ashton Exp $
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifndef RLOG_H
#define RLOG_H

#include <stdio.h>

/*
#include "mpi.h"
#define RLOG_timestamp PMPI_Wtime
*/

#if defined(__cplusplus)
extern "C" {
#endif

/* definitions */
#ifndef RLOG_BOOL
#define RLOG_BOOL int
#endif
#ifndef RLOG_TRUE
#define RLOG_TRUE 1
#endif
#ifndef RLOG_FALSE
#define RLOG_FALSE 0
#endif

#define RLOG_BUFFSIZE           (8*1024*1024)
#define RLOG_MAX_RECORD_SIZE     1024
#define RLOG_COLOR_LENGTH       (3 * sizeof(double))
#define RLOG_DESCRIPTION_LENGTH (5 * sizeof(double))
#define RLOG_MAX_DOUBLE         (1e100)
#define RLOG_MIN_DOUBLE         (-1e100)

#define RLOG_SENDER   1
#define RLOG_RECEIVER 0

#define RLOG_FIRST_EVENT_ID 1500
#define RLOG_ARROW_EVENT_ID 1499

/* structures */

typedef enum RLOG_RECORD_TYPE
{
    RLOG_INVALID_TYPE = 0,
    RLOG_ENDLOG_TYPE,
    RLOG_EVENT_TYPE,
    RLOG_ARROW_TYPE,
    RLOG_IARROW_TYPE,
    RLOG_STATE_TYPE,
    RLOG_COMM_TYPE
} RLOG_RECORD_TYPE;

typedef struct RLOG_HEADER 
{
    RLOG_RECORD_TYPE type;
    int length;
} RLOG_HEADER;

typedef struct RLOG_STATE
{
    int event;
    int pad;
    char color[RLOG_COLOR_LENGTH];
    char description[RLOG_DESCRIPTION_LENGTH];
} RLOG_STATE;

typedef struct RLOG_EVENT
{
    int rank;
    int event;
    int pad;
    int recursion;
    double start_time;
    double end_time;
} RLOG_EVENT;

#define RLOG_ARROW_LEFT  1
#define RLOG_ARROW_RIGHT 0

typedef struct RLOG_ARROW
{
    int src;
    int dest;
    int tag;
    int length;
    int leftright;
    int pad;
    double start_time;
    double end_time;
} RLOG_ARROW;

typedef struct RLOG_IARROW
{
    int sendrecv;
    int rank;
    int remote;
    int tag;
    int length;
    int pad;
    double timestamp;
} RLOG_IARROW;

typedef struct RLOG_COMM
{
    int newcomm;
    int rank;
} RLOG_COMM;

#define RLOG_HEADER_SECTION         0
#define RLOG_STATE_SECTION          1
#define RLOG_ARROW_SECTION          2
#define RLOG_EVENT_SECTION          3

typedef struct RLOG_FILE_HEADER
{
    int nMinRank, nMaxRank;
} RLOG_FILE_HEADER;

typedef struct IRLOG_IOStruct
{
    FILE *f;
    RLOG_HEADER header;
    union RLOG_DATA
    {
	RLOG_STATE state;
	RLOG_EVENT event;
	RLOG_IARROW iarrow;
	RLOG_COMM comm;
    } record;
    char *pCurHeader;
    char *pNextHeader;
    char *pEnd;
    char buffer[RLOG_BUFFSIZE];
} IRLOG_IOStruct;

typedef struct RLOG_IOStruct
{
    FILE *f;
    RLOG_FILE_HEADER header;
    int nNumStates, nCurState;
    long nStateOffset;
    int nNumArrows, nCurArrow;
    long nArrowOffset;
    int nNumRanks;
    int *pRank;
    int *pNumEventRecursions;
    int **ppNumEvents, **ppCurEvent;
    int **ppCurGlobalEvent;
    RLOG_EVENT **gppCurEvent, **gppPrevEvent, gCurEvent;
    int gnCurRank, gnCurLevel, gnCurEvent;
    long **ppEventOffset;
} RLOG_IOStruct;

typedef struct RLOG_Struct
{
    int bLogging;
    int nCurEventId;
    char pszFileName[256];
    int nRank;
    int nSize;
    int nRecursion;
    double dFirstTimestamp;
    RLOG_HEADER DiskHeader;
    RLOG_EVENT DiskEvent;

    IRLOG_IOStruct *pOutput;
} RLOG_Struct;

/* function prototypes */

/* logging functions */
RLOG_Struct* RLOG_InitLog(int rank, int size);
int RLOG_FinishLog(RLOG_Struct* pRLOG);
/*void RLOG_LogEvent(RLOG_Struct *pRLOG, int event, double starttime, double endtime, int recursion);*/
void RLOG_LogSend(RLOG_Struct* pRLOG, int dest, int tag, int size);
void RLOG_LogRecv(RLOG_Struct* pRLOG, int src, int tag, int size);
void RLOG_LogCommID(RLOG_Struct* pRLOG, int id);
void RLOG_DescribeState(RLOG_Struct* pRLOG, int state, char *name, char *color);
void RLOG_EnableLogging(RLOG_Struct* pRLOG);
void RLOG_DisableLogging(RLOG_Struct* pRLOG);
int RLOG_GetNextEventID(RLOG_Struct* pRLOG);
void RLOG_SaveFirstTimestamp(RLOG_Struct* pRLOG);

/* irlog utility functions */
IRLOG_IOStruct *IRLOG_CreateInputStruct(const char *filename);
IRLOG_IOStruct *IRLOG_CreateOutputStruct(const char *filename);
int IRLOG_GetNextRecord(IRLOG_IOStruct *pInput);
int IRLOG_WriteRecord(RLOG_HEADER *pRecord, IRLOG_IOStruct *pOutput);
int IRLOG_CloseInputStruct(IRLOG_IOStruct **ppInput);
int IRLOG_CloseOutputStruct(IRLOG_IOStruct **ppOutput);

/* rlog utility functions */
RLOG_IOStruct *RLOG_CreateInputStruct(const char *filename);
int RLOG_CloseInputStruct(RLOG_IOStruct **ppInput);
int RLOG_GetFileHeader(RLOG_IOStruct *pInput, RLOG_FILE_HEADER *pHeader);
int RLOG_GetNumStates(RLOG_IOStruct *pInput);
int RLOG_GetState(RLOG_IOStruct *pInput, int i, RLOG_STATE *pState);
int RLOG_ResetStateIter(RLOG_IOStruct *pInput);
int RLOG_GetNextState(RLOG_IOStruct *pInput, RLOG_STATE *pState);
int RLOG_GetNumArrows(RLOG_IOStruct *pInput);
int RLOG_GetArrow(RLOG_IOStruct *pInput, int i, RLOG_ARROW *pArrow);
int RLOG_ResetArrowIter(RLOG_IOStruct *pInput);
int RLOG_GetNextArrow(RLOG_IOStruct *pInput, RLOG_ARROW *pArrow);
int RLOG_GetRankRange(RLOG_IOStruct *pInput, int *pMin, int *pMax);
int RLOG_GetNumEventRecursions(RLOG_IOStruct *pInput, int rank);
int RLOG_GetNumEvents(RLOG_IOStruct *pInput, int rank, int recursion_level);
int RLOG_GetEvent(RLOG_IOStruct *pInput, int rank, int recursion_level, int index, RLOG_EVENT *pEvent);
int RLOG_ResetEventIter(RLOG_IOStruct *pInput, int rank, int recursion_level);
int RLOG_GetNextEvent(RLOG_IOStruct *pInput, int rank, int recursion_level, RLOG_EVENT *pEvent);
int RLOG_FindEventBeforeTimestamp(RLOG_IOStruct *pInput, int rank, int recursion_level, double timestamp, RLOG_EVENT *pEvent, int *pIndex);
int RLOG_FindAnyEventBeforeTimestamp(RLOG_IOStruct *pInput, int rank, double timestamp, RLOG_EVENT *pEvent);
int RLOG_ResetGlobalIter(RLOG_IOStruct *pInput);
int RLOG_GetNextGlobalEvent(RLOG_IOStruct *pInput, RLOG_EVENT *pEvent);
int RLOG_GetPreviousGlobalEvent(RLOG_IOStruct *pInput, RLOG_EVENT *pEvent);
int RLOG_GetCurrentGlobalEvent(RLOG_IOStruct *pInput, RLOG_EVENT *pEvent);
int RLOG_FindGlobalEventBeforeTimestamp(RLOG_IOStruct *pInput, double timestamp, RLOG_EVENT *pEvent);
int RLOG_FindArrowBeforeTimestamp(RLOG_IOStruct *pInput, double timestamp, RLOG_ARROW *pArrow, int *pIndex);
int RLOG_HitTest(RLOG_IOStruct *pInput, int rank, int level, double timestamp, RLOG_EVENT *pEvent);
int RLOG_ModifyEvents(const char *log_filename, double *pOffsets, int num_offsets);

/* debugging functions */
int RLOG_PrintGlobalState(RLOG_IOStruct *pInput);

/* macros */

#define RLOG_HEADER_CAST() ((RLOG_HEADER*)pRLOG->pOutput->pCurHeader)
#define RLOG_EVENT_CAST()  ((RLOG_EVENT*)((char*)pRLOG->pOutput->pCurHeader + sizeof(RLOG_HEADER)))

#define RLOG_LogEvent(pRLOG, event_param, starttime, endtime, recursion_param) \
if (pRLOG->bLogging) \
{ \
    if (pRLOG->pOutput->pCurHeader + sizeof(RLOG_HEADER) + sizeof(RLOG_EVENT) > pRLOG->pOutput->pEnd) \
    { \
	WriteCurrentDataAndLogEvent(pRLOG, event_param, starttime, endtime, recursion_param); \
    } \
    else \
    { \
	RLOG_HEADER_CAST()->type = RLOG_EVENT_TYPE; \
	RLOG_HEADER_CAST()->length = sizeof(RLOG_HEADER) + sizeof(RLOG_EVENT); \
	RLOG_EVENT_CAST()->rank = pRLOG->nRank; \
	RLOG_EVENT_CAST()->end_time = endtime - pRLOG->dFirstTimestamp; \
	RLOG_EVENT_CAST()->start_time = starttime - pRLOG->dFirstTimestamp; \
	RLOG_EVENT_CAST()->event = event_param; \
	RLOG_EVENT_CAST()->recursion = recursion_param; \
	/* advance the current position pointer */ \
	pRLOG->pOutput->pCurHeader += sizeof(RLOG_HEADER) + sizeof(RLOG_EVENT); \
    } \
}

void WriteCurrentDataAndLogEvent(RLOG_Struct *pRLOG, int event, double starttime, double endtime, int recursion);

#if defined(__cplusplus)
}
#endif

#endif
