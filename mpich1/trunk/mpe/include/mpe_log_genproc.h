/**\ --MPE_Log--
*  * mpe_log_genproc.h - typedefs, structures, #defines, macros, and
*  *                     your usual assortement of header file type stuff
*  *
*  * MPE_Log currently represents some code written by Dr. William
*  * Gropp, stolen from Chameleon's 'blog' logging package and
*  * modified by Ed Karrels, as well as some fresh code written
*  * by Ed Karrels.
*  *
*  * All work funded by Argonne National Laboratory
\**/

#define MPE_Log_BUF_SIZE   500
#define MPE_Log_EVENT_SYNC -100
#define MAX_HEADER_EVT     -1
#define MIN_HEADER_EVT     -100

#define LOG_STATE_DEF -13
#define LOG_MESG_SEND -101
#define LOG_MESG_RECV -102

typedef struct _MPE_Log_BLOCK {
  struct _MPE_Log_BLOCK *next;
  int size;
} MPE_Log_BLOCK;


/* Here is the definition of a MPE_Log record: */
/*
Note that this is variable-sized, and that "size" is in ints.
This really isn't sufficient.  We really need to identify the type of
data.  Thus, I propose:
 */
typedef struct {
  short len;
  short dtype;
  int   other[1];
} MPE_Log_VFIELD;

typedef struct {
  short len;
  short event;
  double time;
} MPE_Log_HEADER;
 
/*
   This assumes that sizeof(short) % sizeof(int) == 0.
   The use of shorts keeps the size of the individual records down.
   Using pairs is helpful, since we want ints to be on int boundaries.
	
There are further assumptions for "alog" records:

if (size == 4), record has no "other" part.
if (size > 4)   record is:
    size, event, time, data1, string
*/

/*
  To make these easier to use, the following macros are provided:
*/
#define MPE_Log_INT 0
#define MPE_Log_CHAR 1
#define MPE_Log_DOUBLE 2

/* 
 * This is needed incase we're compiling with the header that changes all
 * MPI_ to PMPI_, including MPI_Wtime.  This should be ok, except the IBM
 * MPI has PMPI_Wtime always returning zero (!).
 */
#ifdef MPI_Wtime
#undef MPI_Wtime
#endif

/* These give the sizes in ints */
#define MPE_Log_HEADERSIZE    (sizeof(MPE_Log_HEADER)/sizeof(int))
#define MPE_Log_VFIELDSIZE(n) ((sizeof(MPE_Log_VFIELD)/sizeof(int))+(n-1))

#define MPE_Log_ADDHEADER(b,ev) { \
  double temp_time; \
  b           = (MPE_Log_HEADER *)((int *)(MPE_Log_thisBlock+1) + MPE_Log_i); \
  b->len      = (sizeof(MPE_Log_HEADER)/sizeof(int)); \
  b->event    = ev; \
  temp_time = MPI_Wtime(); \
  MOVEDBL( &b->time, &temp_time ); \
  MPE_Log_i   += b->len; }

#define MPE_Log_ADDINTS(b,v,n,i) \
{    v           = (MPE_Log_VFIELD *)((int *)(MPE_Log_thisBlock+1) + \
				      MPE_Log_i);\
    v->len      = (sizeof(MPE_Log_VFIELD) / sizeof(int)) + (n-1);\
    b->len      += v->len;\
    v->dtype    = MPE_Log_INT;\
    memcpy(v->other,i,n*sizeof(int) ); \
    MPE_Log_i   += v->len;  }

#define MPE_Log_ADDSTRING(b,v,str) \
{   int ln, ln4;\
    ln          = strlen(str) + 1;\
    ln4         = (ln + sizeof(int) - 1) / sizeof(int);\
    v           = (MPE_Log_VFIELD *)((int *)(MPE_Log_thisBlock+1) + MPE_Log_i);\
    v->len      = (sizeof(MPE_Log_VFIELD) / sizeof(int)) + ln4 - 1;\
    b->len      += v->len;\
    v->dtype    = MPE_Log_CHAR;\
    memcpy( v->other, str, ln );\
    MPE_Log_i   += v->len;}

#define MPE_Log_ZEROTIME(b) { double x=0; MOVEDBL( &b->time, &x); }

/* macro definitions */

#define MPE_Log_ADDRECORD \
{ if (!newLogBlk ||		/* if this is the first block, */ \
      (newLogBlk->size+readRecHdr->len > MPE_Log_size)) { \
			        /* or if this block is full, */ \
    if (newLogHeadBlk) {	/* tack the new one on the end */ \
      newLogBlk->next = MPE_Log_GetBuf(); \
      newLogBlk = newLogBlk->next; \
    } else {			/* if this is first block in the chain */ \
      newLogHeadBlk = newLogBlk = MPE_Log_GetBuf(); \
				/* set the head pointer */ \
    } \
    newRecHdr=(MPE_Log_HEADER *)(newLogBlk+1); \
				/* go to after the block header */ \
  } \
  memcpy (newRecHdr, readRecHdr, readRecHdr->len*sizeof(int)); \
				/* copy record */ \
  newLogBlk->size += readRecHdr->len; /* update block length */ \
  newRecHdr = (MPE_Log_HEADER*)((int*)newRecHdr+readRecHdr->len); \
				/* set position for next record write */ \
}

#define MPE_Log_TRAVERSE_LOG(condition) { \
while (readBlk) {		/* loop through the linked list of blocks */ \
     n = readBlk->size;		/* get # of ints in this block */ \
     readRecHdr = (MPE_Log_HEADER *)(readBlk + 1); \
				/* goto first record in this block */ \
     i = 0; \
     while (i < n) {		/* loop through until all ints used up */ \
       if (condition) {		/* if this is the correct pass, */ \
	 MPE_Log_ADDRECORD;	/* copy this record */ \
       } \
       i += readRecHdr->len;	/* update used int count */ \
       readRecHdr = (MPE_Log_HEADER*)((int*)readRecHdr + \
				    readRecHdr->len); /* goto next record */ \
     } \
     readBlk = (MPE_Log_BLOCK *)(readBlk->next); /* goto next block */ \
  } \
}

#define MPE_Log_MBUF_SIZE MPE_Log_BUF_SIZE*2

typedef struct _MPE_Log_MBuf {
  int    *p, *plast;		 /* Pointers to current and last+1 entries */
  int    buf[MPE_Log_MBUF_SIZE]; /* Holds blog buffer plus some */
  double t;			 /* Time of current entry */
  int    (*reload) ( struct _MPE_Log_MBuf *, int * );
    /* routine and context used to reload buf */
  void   *reload_ctx;
} MPE_Log_MBuf;

static void MPE_Log_GenerateHeader ( FILE *fp );
static void MPE_Log_Output ( MPE_Log_MBuf *inBuffer, MPE_Log_MBuf
				    *outBuffer, int mesgtag, int *srcs,
				    FILE *fp, int parent );
static void MPE_Log_FormatRecord ( FILE *fp, int procid, int *rec );
static int MPE_Log_ReloadFromData ( MPE_Log_MBuf *destBuffer, int *srcs );
static int MPE_Log_ReloadFromChild ( MPE_Log_MBuf *destBuffer,
						 int msgtype, int *srcs );
static int MPE_Log_ReloadFromChildL ( MPE_Log_MBuf *b, int *srcs );
static int MPE_Log_ReloadFromChildR ( MPE_Log_MBuf *b, int *srcs ));
static MPE_Log_BLOCK *MPE_Log_Sort ( MPE_Log_BLOCK *readBlock );
static void MPE_Log_SetTreeNodes ( int procid, int np, int *lchild,
					  int *rchild, int *parent,
					  int *am_left );
static MPE_Log_ParallelMerge ( char *filename );
static void MPE_Log_GetStatistics ( int *nevents, int *ne_types,
					   double *startTime,
					   double *endTime );

MPE_Log_BLOCK *MPE_Log_GetBuf ( void );
MPE_Log_BLOCK *MPE_Log_Flush ( void );
int MPE_Log_FreeLogMem  (MPE_Log_BLOCK * );
int MPE_Log_init_clock ( void );
void MPE_Log_def ( int, char * );
/* void MPE_Log_FlushOutput (); */

