/**\ --MPE_Log--
*  * mpe_log_genproc.c - general routines used in MPE_Log internally
*  *
*  * MPE_Log currently represents some code written by Dr. William
*  * Gropp, stolen from Chameleon's 'blog' logging package and
*  * modified by Ed Karrels, as well as some fresh code written
*  * by Ed Karrels.
*  *
*  * All work funded by Argonne National Laboratory
\**/

/* static MPE_Log_BLOCK *bblock = 0, *btail = 0; */

static MPE_Log_BLOCK *MPE_Log_thisBlock = 0, *MPE_Log_firstBlock = 0;
static int    MPE_Log_size = MPE_Log_BUF_SIZE;
static int    MPE_Log_i    = MPE_Log_BUF_SIZE+1;
static int    MPE_Log_clockIsRunning = 0;
static int    MPE_Log_isLockedOut = 0;
static int    MPE_Log_AdjustedTimes = 0;
#define MPE_HAS_PROCID
static int    MPE_Log_procid;
static double MPE_Log_tinit;	/* starting time */

#if DEBUG
static FILE *debug_file;
#endif

/* need to put doubles at non-aligned locations */
#define MOVEDBL( dest, src ) {memcpy( dest, src, sizeof( double ) );}

MPE_Log_BLOCK *MPE_Log_GetBuf (void)
  /* get another block of memory (or the first block) */
{
  MPE_Log_BLOCK *newBlock;

  newBlock = (MPE_Log_BLOCK *)MALLOC(MPE_Log_size * sizeof(int) + 
				sizeof(MPE_Log_BLOCK) );
  newBlock->next = 0;
  newBlock->size = 0;
#if DEBUG
fprintf( debug_file, "[%d] new block at %p\n", MPE_Log_procid, newBlock );
fflush( debug_file );
#endif
  return newBlock;
}


MPE_Log_BLOCK *MPE_Log_Flush (void)
{
  if (MPE_Log_thisBlock) {
    MPE_Log_thisBlock->next = MPE_Log_GetBuf();
    MPE_Log_thisBlock->size = MPE_Log_i;
    MPE_Log_thisBlock = MPE_Log_thisBlock->next;
  } else {
    MPE_Log_firstBlock = MPE_Log_thisBlock = MPE_Log_GetBuf();
  }
  MPE_Log_i = 0;
  return MPE_Log_thisBlock;
}


int MPE_Log_FreeLogMem (headBlk)
MPE_Log_BLOCK *headBlk;
{
  MPE_Log_BLOCK *tmpBlk;
  while (headBlk) {
    tmpBlk = (MPE_Log_BLOCK *)(headBlk->next);
    FREE (headBlk);
    headBlk = tmpBlk;
  }
return 0;
}


#if DEBUG
MPE_Log_PrintTimes()
   /* for debugging purposes */
{
  int               i, procid, *bp, n;
  double            t;
  MPE_Log_BLOCK    *bl;
  MPE_Log_HEADER   *ap;

  MPI_Comm_rank( MPI_COMM_WORLD, &procid );
  bl       = MPE_Log_firstBlock;
  while (bl) {
    n      = bl->size;
    bp     = (int *)(bl + 1);
    i   = 0;
    while (i < n) {
      ap    = (MPE_Log_HEADER *)bp;
      MOVEDBL( &t, &ap->time );
      fprintf (stderr, "[%d] event: %d time: %20.10lf\n", procid, ap->event, t );
      i += ap->len;
      bp   += ap->len;
    }
    bl = bl->next;
  }
}
#endif

/* This routine is called by the MPE_Log initialization routine to
   set the 0-point for the clocks */		    
int MPE_Log_init_clock (void)
{
  if (!MPE_Log_clockIsRunning) {
    MPE_Log_tinit = MPI_Wtime();
      /* _tinit is a varible static to this module, defined in this file */
    MPE_Log_clockIsRunning = 1;
  }
return 0;
}



/* add string containing an event def to the logfile */
void MPE_Log_def(event,str)
int  event;
char *str;
{
  MPE_Log_HEADER *b;
  MPE_Log_VFIELD *v;
  int ln, ln4;
  
  ln = strlen(str) + 1;
  ln4 = (ln + sizeof(int) - 1) / sizeof(int);
  if (MPE_Log_i + 2 * MPE_Log_HEADERSIZE + 2*MPE_Log_VFIELDSIZE(1) + 
      MPE_Log_VFIELDSIZE(ln4) > MPE_Log_size)
    if (!MPE_Log_Flush()) return;
  b = (MPE_Log_HEADER *)((int *)(MPE_Log_thisBlock+1) + MPE_Log_i);
  MPE_Log_ADDHEADER(b,-9);
  MPE_Log_ZEROTIME(b);
  MPE_Log_ADDINTS(b,v,1,&event);
  MPE_Log_ADDSTRING(b,v,str);
  MPE_Log_ADDHEADER(b,-10);
  MPE_Log_ZEROTIME(b);
  MPE_Log_ADDINTS(b,v,1,&event);

  MPID_trvalid( "Log_def" );
}



#if DEBUG
void PrintSomeInts( outf, ptr, n )
FILE *outf;
int *ptr, n;
{
  int i;
  for (i=0; i<n; i++) fprintf( outf, "%d ", ptr[i] );
}



void PrintBlockLinks( outf )
FILE *outf;
{
  MPE_Log_BLOCK *thisBlock;
  thisBlock = MPE_Log_firstBlock;

  if (!thisBlock) {
    fprintf( outf, "no blocks\n" );
    return;
  }
  while (thisBlock) {
    fprintf( outf, "block at %p, next one at %p\n", thisBlock,
	    thisBlock->next );
    thisBlock = thisBlock->next;
  }
}

void PrintRecord( outf, recHdr )
FILE *outf;
MPE_Log_HEADER *recHdr;
{
  
  int recIntsRead, fldIntsRead, i;
  MPE_Log_VFIELD *fldPtr;
  double temp_time;

/*
  fprintf( outf, "Raw record: " );
  PrintSomeInts( outf, (int *)recHdr, recHdr->len );
  putc( '\n', outf );
*/
  MOVEDBL( &temp_time, &recHdr->time );
  fprintf( outf, "Header: pid %d ln %d evt %d %10.5lf Field lengths: ",
	  MPE_Log_procid, recHdr->len, recHdr->event, temp_time );
  recIntsRead = MPE_Log_HEADERSIZE;
  fldPtr = (MPE_Log_VFIELD *)(recHdr + 1);
  
  while (recIntsRead < recHdr->len) {
    fprintf( outf, "%d ", fldPtr->len );
    recIntsRead += fldPtr->len;
    fldPtr = (MPE_Log_VFIELD *)((int*)fldPtr + fldPtr->len);
  } /* per field in a record */
  putc( '\n', outf );
}


void PrintBlockChain( outf, firstBlock )
FILE *outf;
MPE_Log_BLOCK *firstBlock;
{
  int blkIntsRead=0;
  MPE_Log_BLOCK  *thisBlock;
  MPE_Log_HEADER *recHdr;

  thisBlock = firstBlock;

  fprintf( outf, "\n[%d] start block chain \n\n", MPE_Log_procid );

  while (thisBlock) {
    fprintf( outf, "Parsing block at %p\n", thisBlock );
    recHdr = (MPE_Log_HEADER *)(thisBlock + 1);
    blkIntsRead = 0;

    while (blkIntsRead < thisBlock->size) {
      PrintRecord( outf, recHdr );
      blkIntsRead += recHdr->len;
/*
      fprintf( outf, "blkIntsRead=%d, recHdr->len=%d, thisBlock->size=%d\n", 
	      blkIntsRead, recHdr->len, thisBlock->size );
*/
      recHdr = (MPE_Log_HEADER *)((int*)recHdr + recHdr->len);
    } /* per record in a block */
    thisBlock = thisBlock -> next;
  }
  fprintf( outf, "\n[%d] end of block chain\n\n", MPE_Log_procid );
}

#endif /* if DEBUG */
