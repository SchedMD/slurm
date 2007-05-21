[/**\ --MPE_Log--
*  * mpe_log_merge.c - routines for performing a parallel merge of
*  *                   all the data logged by each process
*  *
*  * MPE_Log currently represents some code written by Dr. William
*  * Gropp, stolen from Chameleon's 'blog' logging package and
*  * modified by Ed Karrels, as well as some fresh code written
*  * by Ed Karrels.
*  *
*  * All work funded by Argonne National Laboratory
\**/

#ifndef MPE_HAS_PROCID
extern int MPE_Log_procid;
#endif

static MPE_Log_BLOCK *readBlock;
   /* this is used for flexibility with the b->reload stuff. */
   /* the only functions accessing it are MPE_Log_ParallelMerge and */
   /* MPE_Log_ReloadFromData */

#include <math.h>
#ifdef HUGE_VAL
#define TIME_INF HUGE_VAL
#else
#define TIME_INF 1.0e300
#endif
/* merge buffer size should be bigger */

/* Here is a structure to hold the original header and the source procid */
typedef struct {
  MPE_Log_HEADER header;
  int            procid;
} MPE_Log_MERGE_HEADER;


/* 
   Generate the header 
 */
static void MPE_Log_GenerateHeader( fp )
FILE *fp;
{
  int nevents, neventTypes, numProcs, totalnevents,
      totalneventTypes;
  double startTime, endTime, minimumTime, maximumTime;
  MPI_Comm_size(MPI_COMM_WORLD,&numProcs);

  MPE_Log_GetStatistics( &nevents, &neventTypes, &startTime, &endTime );
  MPI_Reduce( &nevents, &totalnevents, 1, MPI_INT, MPI_SUM, 0,
	      MPI_COMM_WORLD );
  MPI_Reduce( &neventTypes, &totalneventTypes, 1, MPI_INT, MPI_SUM, 0,
	      MPI_COMM_WORLD );
    /* get total number of events */
  MPI_Reduce( &startTime, &minimumTime, 1, MPI_DOUBLE, MPI_MIN, 0,
	      MPI_COMM_WORLD );
    /* get min. start time */
  MPI_Reduce( &endTime, &maximumTime, 1, MPI_DOUBLE, MPI_MAX, 0,
	      MPI_COMM_WORLD );
    /* get max. end time */

  if (MPE_Log_procid == 0) {
      char title[101];
#define FULL_TITLE
#ifdef FULL_TITLE
      struct passwd *pw;
      int    ln;
      time_t tloc;
      pw = getpwuid( getuid() );
      if (pw) {
	  sprintf( title, "Created by %s at ", pw->pw_name );
      }
      time( &tloc );
      strcat( title, ctime( &tloc ) );
      /* Must remove trailing newline */
      ln = strlen( title );
      if (title[ln-1] == '\n') title[ln-1] = 0;
#else
      strcpy( title, "Me" );
#endif

    fprintf( fp, "-1 0 0 0 0 0 %s\n", title );
    fprintf( fp, "-2 0 0 %d 0 0\n", totalnevents );
    fprintf( fp, "-3 0 0 %d 0 0\n", numProcs );
    fprintf( fp, "-4 0 0 1 0 0\n" );
    fprintf( fp, "-5 0 0 %d 0 0\n", totalneventTypes );

    /* There have been some problems with bogus minimum and maximum times */
    /* These need to be integers (for upshot) and made smaller */
    /* ALL processors should perform a uniform shift to make the mintime
       0 */
    fprintf( fp, "-6 0 0 0 0 %.0f\n", minimumTime*1000000 );
    fprintf( fp, "-7 0 0 0 0 %.0f\n", maximumTime*1000000 );
    fprintf( fp, "-8 0 0 1 0 0\n" );  /* timer cycles */
    fprintf( fp, "-11 %d 0 0 0 0\n", MPE_Log_procid);  /* 0 rollovers */
  }
}


/* Output from the buffer */    
static void MPE_Log_Output( inBuffer, outBuffer, mesgtag, srcs, fp, parent )
     MPE_Log_MBuf *inBuffer, *outBuffer;
     int      mesgtag, *srcs;
     FILE     *fp;
     int      parent;
{
  int recLen;
  MPE_Log_HEADER *recHdr;
  int recordBuf[MPE_Log_BUF_SIZE]; /* temporary storage for one record */

  recHdr  = (MPE_Log_HEADER *)(inBuffer->p);

#if DEBUG
  fprintf( debug_file, "output record to outBuffer %d %d\n",
	   outBuffer->p - outBuffer->buf, outBuffer->plast - outBuffer->buf );
#endif

  recLen = recHdr->len;
  if (recLen <= 0) {
    fprintf( stderr, "[%d] Error in log file; length of entry = %d\n",
	    MPE_Log_procid, recLen );
#if DEBUG
    fprintf( debug_file, "[%d] Error in log file; length of entry = %d\n",
	    MPE_Log_procid, recLen );
    fflush( debug_file );
#endif
    (*srcs)--;
    return;
  }
  if (parent >= 0) {		 /* if child process, */
    if (recLen + outBuffer->p >= outBuffer->plast) { /* if outBuffer is full */
#if DEBUG>1
      fprintf( debug_file, "Sending buffer to parent:\n" );
      PrintMbuf( debug_file, outBuffer );
      fflush( stderr );
#endif
      MPI_Send( outBuffer->buf, (outBuffer->p - outBuffer->buf) * 
	        sizeof( int ), MPI_BYTE, parent, mesgtag, MPI_COMM_WORLD );
        /* send outBuffer to parent */
        /* send as raw BYTEs, don't want MPI changing the data */
      outBuffer->p = outBuffer->buf; /* mark outBuffer as empty */
#if DEBUG
      fprintf( debug_file, "[%d] sent data to parent\n", MPE_Log_procid );
      fflush( debug_file );
#endif
    }
    memcpy( outBuffer->p, inBuffer->p, recLen*sizeof(int) );
      /* copy data from inBuffer to outBuffer */
    outBuffer->p += recLen;
  }
  else {			 /* if process 0 */
				 /* Repack the buffer */
    if (recHdr->event != MPE_Log_EVENT_SYNC) {
      memcpy( recordBuf, inBuffer->p, sizeof(MPE_Log_HEADER) );
        /* copy header to temp area */
      ((MPE_Log_HEADER *)recordBuf)->len--;
        /* cut out the procid that was inserted */
      memcpy( recordBuf + MPE_Log_HEADERSIZE, inBuffer->p +
	     MPE_Log_HEADERSIZE + 1,
	     (recHdr->len - MPE_Log_HEADERSIZE) * sizeof(int) );
        /* copy the log entry data to the temp area */

      MPE_Log_FormatRecord( fp, inBuffer->p[MPE_Log_HEADERSIZE], recordBuf );
        /* print the log entry to the logfile */
    }
  }
  inBuffer->p    += recLen;	 /* note the data used from the inBuffer */
  if (inBuffer->p >= inBuffer->plast) {	/* if the inBuffer is empty, */
    if (!(*(inBuffer->reload))( inBuffer, srcs )) {
        /* if there's no more data there,  */
      inBuffer->t = TIME_INF;	 /* mark that input source as empty */
      return;
    }
  } else {
    MOVEDBL( &inBuffer->t, &( ((MPE_Log_HEADER *)(inBuffer->p))->time ) );
      /* get next time */
  }
}


#define NUMINTS 4


static void MPE_Log_FormatRecord (fp, procid, rec)
FILE *fp;
int procid, *rec;
{
  MPE_Log_HEADER *hdr;
  MPE_Log_VFIELD *fld;
  int left;	       /* # of ints left in this record to be read */
  int i[NUMINTS], iused;     /* storage ints, and # used */
  char *str;	       /* string data */
  int intsToCopy, j;   /* nubmer of ints to copy, counter */
  double temp_time;

  hdr = (MPE_Log_HEADER *)rec;
  fprintf( fp, "%d %d ", hdr->event, procid );
  MOVEDBL( &temp_time, &hdr->time );
#if DEBUG>1 || DEBUG_FORMATRECORD
  fprintf( debug_file, "printing event %d from %d with size %d\n", hdr->event,
	  procid, hdr->len );
      fflush( debug_file );
#endif
  left = hdr->len;
  fld = (MPE_Log_VFIELD *)(hdr+1);
  left -= (sizeof(MPE_Log_HEADER) / sizeof(int));
  if (left > 0) {
    for (iused=0; iused<NUMINTS; iused++) {
      i[iused] = 0;
    }
    iused = 0;
    str    = "";
      /* clear everything */

    while (left) {
      /* There may be a string or integer data.  It there is integer data,
	 take the first element only */
      if (fld->dtype == MPE_Log_INT) {
#if DEBUG>1 || DEBUG_FORMATRECORD
	fprintf( debug_file, "%d ints left, this field is %d ints long\n",
		left, fld->len - MPE_Log_VFIELDSIZE(0) );
	fflush( debug_file );
#endif
	/* # of ints present */
	intsToCopy = (int)fld->len - MPE_Log_VFIELDSIZE(0); 

	  /* if intsToCopy>space left, just copy what we can */
	for (j=0; intsToCopy > 0 && iused<NUMINTS; j++,iused++,intsToCopy--)
	    /* copy the integers */
	  i[iused]=fld->other[j];

      } else if (fld->dtype == MPE_Log_CHAR) {
	str = (char *)(fld->other);
      }
      left -= fld->len;
#if DEBUG>1 || DEBUG_FORMATRECORD
      fprintf( debug_file, "%d ints left, %d taken away, %d iused\n", left,
	      fld->len, iused );
      fflush( debug_file );
#endif
      fld = (MPE_Log_VFIELD *)((int *)fld + fld->len);
    }

    if (hdr->event == LOG_STATE_DEF) 
      fprintf( fp, "%d %d 0 0 %s\n", i[0], i[1], str );
      /* State events are special - they have two data values  */
    else if (hdr->event == LOG_MESG_SEND || hdr->event == LOG_MESG_RECV)
      fprintf( fp, "0 %d 0 %.0f %d %d\n", i[0],
	       temp_time*1000000, i[1], i[2] );
      /* Sends and receives need 3:  otherParty, tag, size*/
    else
	fprintf( fp, "0 %d 0 %.0f %s\n", 
		i[0], temp_time*1000000, str );
  }  /* if (left>0) */
  else
    fprintf( fp, "0 0 0 %.0f\n", temp_time*1000000);
}


static int MPE_Log_ReloadFromData( destBuffer, srcs )
MPE_Log_MBuf *destBuffer;
int      *srcs;
{
  MPE_Log_HEADER *readHdr;
  int             readBufSize, intsUsed, *readPtr, *writePtr;
  /* Destination; repacked as (header)(procid)(fields) */

  /* Note: this function copies all the data from one MPE_Log_BLOCK to */
  /* one MPE_Log_MBuf.  MBufs must be bigger because each entry has an */
  /* extra int */

  if (readBlock) {
    /* We have to insert the procid as an int after the header and before
       any vfields */
    writePtr    = destBuffer->buf;
    readBufSize = readBlock->size;
    readPtr     = (int *)(readBlock + 1);
    intsUsed    = 0;
/*
   fprintf( stderr, "[%d] reading buffer of size %d, srcs = %d\n",
            MPE_Log_procid, readBufSize, *srcs );
*/
    while (intsUsed < readBufSize) {
      readHdr  = (MPE_Log_HEADER *)readPtr;
/*
fprintf( stderr, "[%d] reloading %d\n", MPE_Log_procid, h->event );
*/
      if (readHdr->event <= MAX_HEADER_EVT && readHdr->event >= MIN_HEADER_EVT)
	/* Reserved header events have all times set to zero */
	MPE_Log_ZEROTIME(readHdr);
      memcpy( writePtr, readPtr, sizeof(MPE_Log_HEADER) );
				 /* copy header */
      ((MPE_Log_HEADER *)writePtr)->len = readHdr->len + 1;
				 /* increase record length by 1 */
      writePtr[MPE_Log_HEADERSIZE] = MPE_Log_procid;
				 /* insert procid */
      memcpy( writePtr + MPE_Log_HEADERSIZE + 1, readPtr + MPE_Log_HEADERSIZE, 
	     (readHdr->len - MPE_Log_HEADERSIZE) * sizeof(int) );
				/* copy all the fields */
      /* Increment the lengths (writePtr includes the procid) */
      intsUsed += readHdr->len;
#if DEBUG_RELOAD
      fprintf( debug_file, "reload from: " ); 
      PrintRecord( debug_file, readPtr );
      fflush( debug_file );
#endif
      readPtr  += readHdr->len;
#if DEBUG_RELOAD
      fprintf( debug_file, "reload to: " );
      PrintMbufRecord( debug_file, writePtr );
      fflush( debug_file );
#endif
      writePtr += readHdr->len + 1;
    }
    destBuffer->p = destBuffer->buf; /* reset the pointer in the new buffer */
    destBuffer->plast = writePtr; /* mark the end of the buffer */
    readHdr = (MPE_Log_HEADER *)(destBuffer->p);
    MOVEDBL( &destBuffer->t, &readHdr->time ); /* make a copy of the time */
    readBlock = readBlock->next; /* go to the next block */
#if DEBUG
    PrintMbuf( debug_file, destBuffer );
      fflush( debug_file );
#endif
    return 1;
  }
  else {	/* no more local data */
    destBuffer->t = TIME_INF;
    destBuffer->p = destBuffer->plast = destBuffer->buf;
    (*srcs)--;
#if DEBUG
    fprintf( debug_file, "[%d] done reloading from data\n", MPE_Log_procid );
      fflush( debug_file );
#endif
    return 0;
  }
}


/* 
   There are two routines to reload the buffer.  One gets data from
   another processor (FromChild); the other from the internal buffer 
 */

static int MPE_Log_ReloadFromChild( destBuffer, msgtype, srcs )
int msgtype, *srcs;
MPE_Log_MBuf *destBuffer;
{
  int ln;
  MPE_Log_HEADER *h;
  MPI_Status mesgStatus;

#if DEBUG
  fprintf( debug_file, "[%d] reloading from %s child\n",
           MPE_Log_procid, msgtype==100?"left":"right");
      fflush( debug_file );
#endif

  MPI_Recv( destBuffer->buf, MPE_Log_MBUF_SIZE * sizeof( int ), MPI_BYTE,
	    MPI_ANY_SOURCE, msgtype, MPI_COMM_WORLD, &mesgStatus);
  MPI_Get_count( &mesgStatus, MPI_BYTE, &ln );
#if DEBUG
  fprintf( debug_file, "[%d] Received %d bytes from %s child\n",
	   MPE_Log_procid, ln, msgtype==100?"left":"right" );
      fflush( debug_file );
#endif
  if (ln == 0) {
#if DEBUG
    fprintf( debug_file, "[%d] End of data from %s child\n", MPE_Log_procid, msgtype==100?"left":"right" );
      fflush( debug_file );
#endif
    destBuffer->t  = TIME_INF;
    *srcs = *srcs - 1;
    return 0;
  }
  else {
    destBuffer->p     = destBuffer->buf;
    destBuffer->plast = destBuffer->p + (ln / sizeof(int));
#if DEBUG
    PrintMbuf( debug_file, destBuffer );
    fprintf( debug_file, "[%d] %d ints should be available\n",
             MPE_Log_procid, destBuffer->plast-destBuffer->p);
      fflush( debug_file );
#endif
    h        = (MPE_Log_HEADER *)(destBuffer->p);
    MOVEDBL( &destBuffer->t, &h->time );
  }
  return 1;
}


static int MPE_Log_ReloadFromChildL( b, srcs )
MPE_Log_MBuf *b;
int      *srcs;
{
  return MPE_Log_ReloadFromChild( b, 100, srcs );
}


static int MPE_Log_ReloadFromChildR( b, srcs )
MPE_Log_MBuf *b;
int      *srcs;
{
  return MPE_Log_ReloadFromChild( b, 101, srcs );
}






/* move all the negative event records above the positive ones */

static MPE_Log_BLOCK *MPE_Log_Sort( readBlock )
MPE_Log_BLOCK *readBlock;
{
  MPE_Log_BLOCK *newLogHeadBlk, *newLogBlk, *readBlk;
  MPE_Log_HEADER *readRecHdr, *newRecHdr;
  int i;			/* # of ints read from the input block */
  int n;			/* # of ints in this block */

  newLogHeadBlk=newLogBlk=0;
  readBlk = readBlock;		/* start from first block */
  MPE_Log_TRAVERSE_LOG((readRecHdr->event <= MAX_HEADER_EVT) &&
		       (readRecHdr->event >= MIN_HEADER_EVT));
  readBlk = readBlock;		/* start from first block */
  MPE_Log_TRAVERSE_LOG(readRecHdr->event > MAX_HEADER_EVT ||
		       readRecHdr->event < MIN_HEADER_EVT);
  MPE_Log_FreeLogMem (readBlock); /* free memory */
  return newLogHeadBlk;
}


static void MPE_Log_SetTreeNodes( procid, np, lchild, rchild, parent, am_left )
int procid, np, *lchild, *rchild, *parent, *am_left;
{
  *parent = (procid) ? ((procid - 1) >> 1) : -1;
  *lchild = (procid << 1) + 1;
  if (*lchild >= np) {
    *lchild = -1;
    *rchild = -1;
  } else {
    *rchild = (procid << 1) + 2;
    if (*rchild >= np) *rchild = -1;
  }
  *am_left = procid % 2;
}


static int MPE_Log_ParallelMerge( filename )
     char *filename;
{
  int      srcs, lchild, rchild, parent, np, mtype, am_left;
  MPE_Log_MBuf *ba, *bb, *bc, *bout;
  FILE *fp;
/*   void MPE_Log_FlushOutput(); */


  MPI_Comm_size(MPI_COMM_WORLD,&np);

  MPE_Log_SetTreeNodes( MPE_Log_procid, np, &lchild, &rchild, &parent, &am_left );

  if (MPE_Log_procid==0) {
    fp = fopen (filename,"w");
    if (!fp) {
      fprintf(stderr,"Could not open logfile: %s.\n", filename);
      return MPE_Log_FILE_PROB;
    }
  }


#if DEBUG
  fprintf( debug_file, "[%d] Generating header\n", MPE_Log_procid );
      fflush( debug_file );
#endif
  MPE_Log_GenerateHeader( fp );


  /* On to the business of generating the logfile */
  readBlock = MPE_Log_firstBlock;

#if DEBUG
  PrintBlockChain( debug_file, MPE_Log_firstBlock );
      fflush( debug_file );
#endif

#if DEBUG
  fprintf( debug_file, "[%d] Sorting logfile\n", MPE_Log_procid );
      fflush( debug_file );
#endif
  readBlock = MPE_Log_firstBlock = MPE_Log_Sort( MPE_Log_firstBlock );
  /* filter negative events to the top */
#if DEBUG
  fprintf( debug_file, "[%d] Finished sorting logfile\n", MPE_Log_procid );
      fflush( debug_file );
#endif

  ba      = (MPE_Log_MBuf *)MALLOC( sizeof(MPE_Log_MBuf));
  bb      = (MPE_Log_MBuf *)MALLOC( sizeof(MPE_Log_MBuf));
  bc      = (MPE_Log_MBuf *)MALLOC( sizeof(MPE_Log_MBuf));
  bout    = (MPE_Log_MBuf *)MALLOC( sizeof(MPE_Log_MBuf));
  bout->p     = bout->buf;
  bout->plast = bout->buf + MPE_Log_MBUF_SIZE;

  srcs = 1;
  ba->reload = MPE_Log_ReloadFromData;
  (*ba->reload)( ba, &srcs );

  mtype = (am_left) ? 100 : 101;

  if (lchild >= 0) {
    srcs++;
    bb->reload = MPE_Log_ReloadFromChildL;
    (*bb->reload)( bb, &srcs );
  }
  else
    bb->t = TIME_INF;

  if (rchild >= 0) {
    srcs++;
    bc->reload = MPE_Log_ReloadFromChildR;
    (*bc->reload)( bc, &srcs );
  }
  else
    bc->t = TIME_INF;
/*
fprintf( stderr, "[%d] ba->t = %lf, bb->t = %lf, bc->t = %lf\n",
         MPE_Log_procid, ba->t, bb->t, bc->t );
*/

  while (srcs > 0) {

#if DEBUG>2
    fprintf( debug_file, "[%d] comparing %10lf %10lf %10lf\n",
	     MPE_Log_procid, (ba->t<10e200)?ba->t:-1,
	     (bb->t<10e200)?bb->t:-1, (bc->t<10e200)?bc->t:-1 );
#endif
    if (ba->t <= bb->t) {
      if (ba->t <= bc->t) MPE_Log_Output( ba, bout, mtype, &srcs, fp, parent );
      else                MPE_Log_Output( bc, bout, mtype, &srcs, fp, parent );
    }
    else {
      if (bb->t <= bc->t) MPE_Log_Output( bb, bout, mtype, &srcs, fp, parent );
      else                MPE_Log_Output( bc, bout, mtype, &srcs, fp, parent );
    }
  }

  if (parent >= 0) {								 /* if this is a child */
    if ((int)(bout->p - bout->buf) > 0) {					 /* if buffer has data in it */
      MPI_Send( bout->buf, (bout->p - bout->buf) * sizeof( int ), MPI_BYTE, 
	        parent, mtype, MPI_COMM_WORLD );
        /* send as raw BYTEs, don't want MPI changing the data */
#if DEBUG
      fprintf( debug_file, "[%d] send data to parent\n", MPE_Log_procid );
      fflush( debug_file );
#endif
    }
    MPI_Send( bout->buf, 0 , MPI_BYTE, parent, mtype, MPI_COMM_WORLD );		 /* tell the parent that's the last of it */
#if DEBUG
      fprintf( debug_file, "[%d] tell parent I'm all out\n", MPE_Log_procid );
      fflush( debug_file );
#endif
  } else {
    fclose (fp);								 /* if process 0, just close output file */
  }

#if DEBUG
  fprintf( debug_file, "About to free bout\n" ); fflush(debug_file);
#endif
  FREE(bout);
#if DEBUG
  fprintf( debug_file, "About to free ba\n" ); fflush(debug_file);
#endif
  FREE(ba);
#if DEBUG
  fprintf( debug_file, "About to free bb\n" ); fflush(debug_file);
#endif
  FREE(bb);
#if DEBUG
  fprintf( debug_file, "About to free bc\n" ); fflush(debug_file);
#endif
  FREE(bc);
#if DEBUG
  fprintf( debug_file, "About to free LogMem\n" ); fflush(debug_file);
#endif
  MPE_Log_FreeLogMem( MPE_Log_firstBlock );
  /* Make sure that everyone has finished before exiting */
#if DEBUG
  fprintf( debug_file, "About to do barrier\n" ); fflush(debug_file);
#endif
  MPI_Barrier( MPI_COMM_WORLD );
#if DEBUG
  fprintf( debug_file, "Done with barrier\n" ); fflush(debug_file);
#endif
return 0;
}


static void MPE_Log_GetStatistics( nevents, ne_types, startTime, endTime )
int           *nevents, *ne_types;
double *startTime, *endTime;
{
  MPE_Log_BLOCK    *bl;
  int               xx_i, n, *bp;
  MPE_Log_HEADER   *ap;
  int               ne, net;
  double            ttest;
  
  ne  = 0;
  net = 0;

  MOVEDBL( startTime, &((MPE_Log_HEADER *)(MPE_Log_firstBlock + 1))->time );
  *endTime = *startTime;
  
  bl = MPE_Log_firstBlock;
  while (bl) {
    n      = bl->size;
    bp     = (int *)(bl + 1);
    xx_i   = 0;
    while (xx_i < n) {
      ap    = (MPE_Log_HEADER *)bp;
      if (ap->event > MAX_HEADER_EVT || ap->event < MIN_HEADER_EVT) {
	ne++;
	MOVEDBL( &ttest, &ap->time );
	if (ttest < *startTime) {
	  *startTime = ttest;
	}
	if (ttest > *endTime) {
	  *endTime   = ttest;
	}
      }
      xx_i += ap->len;
      bp   += ap->len;
    }
    bl = bl->next;
  }
  *nevents  = ne;
  *ne_types = net;
}



#if DEBUG
PrintMbufRecord( outf, recHdr )
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
	  *((int*)(recHdr+1)), recHdr->len, recHdr->event, temp_time );
  recIntsRead = MPE_Log_HEADERSIZE + 1;
  fldPtr = (MPE_Log_VFIELD *)((int *)recHdr + MPE_Log_HEADERSIZE + 1) ;
  
  while (recIntsRead < recHdr->len) {
    fprintf( outf, "%d ", fldPtr->len );
    recIntsRead += fldPtr->len;
    fldPtr = (MPE_Log_VFIELD *) ((int *)fldPtr + fldPtr->len );
  } /* per field in a record */
  putc( '\n', outf );
}



PrintMbuf( outf, thisBlock )
FILE *outf;
MPE_Log_MBuf *thisBlock;
{
  int blkIntsRead=0, recIntsRead, fldIntsRead, i;
  MPE_Log_HEADER *recHdr;
  MPE_Log_VFIELD *fldPtr;
  double temp_time;

  recHdr = (MPE_Log_HEADER *)(thisBlock->buf);
  blkIntsRead = 0;

  fprintf( outf, "\n[%d] start mbuf, %d read, %d full\n\n", 
	   MPE_Log_procid, thisBlock->p-thisBlock->buf, thisBlock->plast-
	   thisBlock->buf );
  while ((int*)recHdr < thisBlock->p) {
    fprintf( outf, "(%d of %d) ", (int*)recHdr-thisBlock->buf,
	     thisBlock->p - thisBlock->buf );
    PrintMbufRecord( outf, recHdr );
    recHdr = (MPE_Log_HEADER *) ((int *)recHdr + recHdr->len);
  } /* per record in a block */

  fprintf( outf, "\n[%d] end of mbuf\n\n", MPE_Log_procid );
}

#endif /* if DEBUG */
