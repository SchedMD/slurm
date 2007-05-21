#include "mpeconf.h"

#include <fcntl.h>
#ifdef HAVE_WINDOWS_H
#include <io.h>
#endif
#if defined( STDC_HEADERS ) || defined( HAVE_STDLIB_H )
#include <stdlib.h>
#endif
#if defined( HAVE_UNISTD_H )
#include <unistd.h>
#endif

#if defined( HAVE_SLOG1 )
#include "clog2slog.h"
#endif
#include "clog_merge.h"
#include "clogimpl.h"
#include "mpi.h"
#if defined(NEEDS_STDLIB_PROTOTYPES) && !defined ( malloc )
#include "protofix.h"
#endif

#if defined( HAVE_STRING_H ) || defined( STDC_HEADERS )
#include <string.h>
#endif

/*********************** global variables for merge functions ***************/

static int    me, nprocs, parent, lchild, rchild;	/* tree info */
static double *mybuf, *lbuf, *rbuf, *outbuf; /* buffers for log records */
static double *myptr, *lptr, *rptr, *outptr; /* pointers into each buffer */
static double *outend;			     /* end of output buffer */
static int    inputs;			    /* number of inputs to the merge */
static double timediffs[CMERGE_MAXPROCS];  /* array of time shifts, averaged */
/*static double newdiffs[CMERGE_MAXPROCS]; */ /* array of time shifts, once  */
static int    logfd;			   /* the log file */
static int    log_type;

/*@
    CLOG_mergelogs - merge individual logfiles into one via messages

first argument says whether to do time-shifiting or not
second arg is filename

On process 0 in MPI_COMM_WORLD, collect logs from other processes and merge
them with own log.  Timestamps are assumed to be already adjusted on both
incoming logs and the master''s.  On the other processes, fill in length and
process id''s and send them, a block at a time, to the master.  The master 
writes out the merged log.

@*/
void CLOG_mergelogs( shift, execfilename, logtype )
int shift;
char *execfilename;
int logtype;
{
    MPI_Status logstatus;
    double maxtime = CLOG_MAXTIME;  /* place to hold maxtime, dummy record */
    char logfilename[256],
         *slog_file;
    int i;
    int total_events;               /* (abhi) total event count for process 
				       with rank 0.*/
    
    /* set up tree */
    PMPI_Comm_size(MPI_COMM_WORLD, &nprocs);
    PMPI_Comm_rank(MPI_COMM_WORLD, &me);

    CLOG_treesetup(me, nprocs, &parent, &lchild, &rchild);
    /* printf("merging on %d at time %f\n", me, MPI_Wtime()); */

    PMPI_Reduce(&CLOG_event_count,&total_events,1,
                MPI_INT,MPI_SUM,0,MPI_COMM_WORLD);

    if (parent == -1) {		/* open output logfile at root */
        strncpy(logfilename, execfilename, 256);
        strcat(logfilename, ".clog"); 
        log_type = logtype;     /* (abhi) need to know log_type globally */ 

        FREE( slog_buffer );    /* (abhi) free memory for slog logging */

#if defined( HAVE_SLOG1 )
        /* (abhi) checking to see if SLOG needs initalization */
        if (log_type == SLOG_LOG) {
            /* (abhi) getting total number of events.*/
            C2S1_init_clog2slog(logfilename, &slog_file);
            C2S1_init_essential_values(total_events, nprocs-1);
            C2S1_init_all_mpi_state_defs( );
            C2S1_init_SLOG(C2S_NUM_FRAMES, C2S_FRAME_BYTE_SIZE, slog_file);
        }
        else
#endif
            if ( (logfd = OPEN(logfilename, O_CREAT|O_WRONLY|O_TRUNC, 0664))
                  == -1) {
                printf("could not open file %s for logging\n",logfilename);
                MPI_Abort( MPI_COMM_WORLD, 1 );
            }
    }

    for (i = 0; i < nprocs; i++)
	timediffs[i] = 0;
    
    /* calculate time shifts if required */
    if (shift == CMERGE_SHIFT) {
	CLOG_csync(0, timediffs);
	/* CLOG_printdiffs(timediffs); */
    }
    CLOG_LOGTIMESHIFT( timediffs[me] );

    /*(abhi)*/
    if(CLOG_tempFD > 0) {
      CLOG_nodebuffer2disk();
      lseek(CLOG_tempFD, (long)0, 0);
      CLOG_reinit_buff();
    }

    /* set up buffers, send if a leaf */
    CLOG_currbuff = CLOG_first;	/* reinitialize pointer to first local buff */
    mybuf  = (double *) CLOG_first->data;
    myptr  = mybuf;
    CLOG_procbuf(mybuf);	/* do postprocessing (procids, lengths) */
    inputs = 1;			/* always have at least own buffer */

    outptr = outbuf = CLOG_out_buffer; /*(double *) MALLOC ( CLOG_BLOCK_SIZE );*/
    outend = (void *) ((char *) outptr + CLOG_BLOCK_SIZE);

    if (lchild != -1) {
	inputs++;
	lptr = lbuf = CLOG_left_buffer; /*(double *) MALLOC ( CLOG_BLOCK_SIZE );*/
	PMPI_Recv(lbuf, (CLOG_BLOCK_SIZE / sizeof (double)), MPI_DOUBLE, 
		 lchild, CMERGE_LOGBUFTYPE, MPI_COMM_WORLD, &logstatus);
    }
    else {
	lptr = &maxtime;
	FREE (CLOG_left_buffer);
    }
    if (rchild != -1) {
	inputs++;
	rptr = rbuf = CLOG_right_buffer; /*(double *) MALLOC ( CLOG_BLOCK_SIZE );*/
	PMPI_Recv(rbuf, (CLOG_BLOCK_SIZE / sizeof (double)), MPI_DOUBLE, 
		 rchild, CMERGE_LOGBUFTYPE, MPI_COMM_WORLD, &logstatus);
    }
    else {
	rptr = &maxtime;
	FREE (CLOG_right_buffer);
    }

    /* Do the merge.  Abstractly, we do this one record at a time, letting
       the CLOG_cput routine buffer input from children and output
       to the parent.  We decrement the number of inputs when we hit the end
       of log record in a buffer, and we are done when the number of inputs
       reaches 0.  The pointers mybuf, lbuf, and rbuf are pointing to the
       timestamp of the next record in each buffer, and outbuf is pointing
       at the place to put the next output record.
       */
    while (inputs > 0) {
	if (*lptr <= *rptr)
	    if (*lptr <= *myptr)
		CLOG_cput(&lptr);
            else
		CLOG_cput(&myptr);
	else
	    if (*rptr <= *myptr)
		CLOG_cput(&rptr);
            else
		CLOG_cput(&myptr);
    }
    CLOG_mergend();  /* add trailer (CLOG_ENDLOG) and flush output buffer */
    
    /* now we have a clog file;  if it should be an alog file, convert it and
       delete the clog file. */
    if (logtype == ALOG_LOG) {
	if (parent == -1) {
	    clog2alog( execfilename );
	}
	unlink(logfilename);
    }
}

/*@
    CLOG_treesetup - locally determine parent and children in binary tree

Input parameters

+  self - calling process''s id
-  np   - total number of processes in tree

Output parameters

+  parent - parent in binary tree (or -1 if root)
.  lchild - left child in binary tree (or -1 if none)
-  rchild - right child in binary tree (or -1 if none)

@*/
void CLOG_treesetup( self, numprocs, myparent, mylchild, myrchild)
int self, numprocs, *myparent, *mylchild, *myrchild;
{
    if (self == 0)
	*myparent = -1;
    else
	*myparent = (self - 1) / 2;

    if ((*mylchild = (2 * self) + 1) > numprocs - 1)
	*mylchild = -1;

    if ((*myrchild = (2 * self) + 2) > numprocs - 1)
	*myrchild = -1;
}


/*@
    CLOG_procbuf - postprocess a buffer of log records before merging

This function fills in fields in log records that were left out during
actual logging to save memory accesses.  Typical fields are the process
id and the lengths of records that are known by predefined type.  This is
also where we will adjust timestamps.

Input parameter

.  address of the buffer to be processed

@*/
void CLOG_procbuf( buf )
double *buf;
{
    CLOG_HEADER *h;

    h = (CLOG_HEADER *) buf;
    while (h->rectype != CLOG_ENDBLOCK && h->rectype != CLOG_ENDLOG) {
	h->procid = me;
	h->timestamp = h->timestamp + timediffs[me];
	h->length = CLOG_reclen(h->rectype);  /* in doubles */
	buf = (double *) h + h->length; 
	h = (CLOG_HEADER *) buf;
    }	
    /* fix trailer record, either block or log */
    h->procid = me;
    h->length = CLOG_reclen(h->rectype);  /* in doubles */
    h->timestamp = h->timestamp + timediffs[me];
}

/*@
    CLOG_mergend - finish log processing
@*/
void CLOG_mergend()
{
    CLOG_BLOCK *buffer_parser;
    /* put on end-of-log record */
    ((CLOG_HEADER *) outptr)->timestamp = CLOG_MAXTIME;
    ((CLOG_HEADER *) outptr)->rectype   = CLOG_ENDLOG;
    ((CLOG_HEADER *) outptr)->procid    = me;
    ((CLOG_HEADER *) outptr)->length    = CLOG_reclen(CLOG_ENDLOG);

    if (parent != -1) {
        PMPI_Send(outbuf, (CLOG_BLOCK_SIZE / sizeof (double)), MPI_DOUBLE,
                  parent, CMERGE_LOGBUFTYPE, MPI_COMM_WORLD);
        /* printf("%d sent to %d\n", me, parent); */
    }
    else {
#if defined( HAVE_SLOG1 )
        if ( log_type == SLOG_LOG) {
            if (C2S1_make_SLOG(outbuf) == C2S_ERROR)
                PMPI_Abort(MPI_COMM_WORLD, -1);
            C2S1_free_resources();           /*(abhi)*/
        }
        else {
#endif
            CLOG_output(outbuf); /* final output of last block at root */
            close(logfd);
#if defined( HAVE_SLOG1 )
        }
#endif
    }
    FREE (outbuf);		/* free output buffer */
    CLOG_currbuff = buffer_parser = CLOG_first;
    while(CLOG_currbuff) {
        CLOG_currbuff = CLOG_currbuff->next;
        FREE (buffer_parser);
		buffer_parser = CLOG_currbuff;
    }
    if(rchild != -1)
        FREE (CLOG_right_buffer);
    if(lchild != -1)
        FREE (CLOG_left_buffer);
    close(CLOG_tempFD);
    unlink(CLOG_tmpfilename);
}

/*@
    CLOG_Output - output a block of the log.
    The byte ordering, if needed, will be performed in this
    function using the conversion routines got from Petsc.
@*/
void CLOG_output( buf )
double *buf;
{
  int rc;   
#ifndef WORDS_BIGENDIAN
  double *p = buf;
  int         rtype;
  CLOG_HEADER *h;
  rtype = CLOG_UNDEF;

  while (rtype != CLOG_ENDBLOCK && rtype != CLOG_ENDLOG) {
    h	 = (CLOG_HEADER *) p;
    rtype = h->rectype;
    adjust_CLOG_HEADER (h); /* adjust the header record if needed */
    p	 = (double *) (h->rest);	/* skip to end of header */
    switch (rtype) {
    case CLOG_MSGEVENT:
      adjust_CLOG_MSG ((CLOG_MSG *)p);
      p = (double *) (((CLOG_MSG *) p)->end);
      break;
    case CLOG_COLLEVENT:
      adjust_CLOG_COLL ((CLOG_COLL *)p);
      p = (double *) (((CLOG_COLL *) p)->end);
      break;
    case CLOG_RAWEVENT:
      adjust_CLOG_RAW ((CLOG_RAW *)p);
      p = (double *) (((CLOG_RAW *) p)->end);
      break;
    case CLOG_SRCLOC:
      adjust_CLOG_SRC ((CLOG_SRC *)p);
      p = (double *) (((CLOG_SRC *) p)->end);
      break;
    case CLOG_COMMEVENT:
      adjust_CLOG_COMM ((CLOG_COMM *)p);
      p = (double *) (((CLOG_COMM *) p)->end);
      break;
    case CLOG_STATEDEF:
      adjust_CLOG_STATE ((CLOG_STATE *)p);
      p = (double *) (((CLOG_STATE *) p)->end);
      break;
    case CLOG_EVENTDEF:
      adjust_CLOG_EVENT ((CLOG_EVENT *)p);
      p = (double *) (((CLOG_EVENT *) p)->end);
      break;
    case CLOG_ENDBLOCK:
      break;
    case CLOG_ENDLOG:
      break;
    default:
      printf("unrecognized record type\n");
    }
  }
#endif

    /* CLOG_dumpblock(buf);	 temporary print for debugging*/
    rc = write( logfd, buf, CLOG_BLOCK_SIZE );	/* write block to file */
    if ( rc != CLOG_BLOCK_SIZE ) {
	fprintf( stderr, "write failed for clog logging, rc = %d\n", rc );
	MPI_Abort( MPI_COMM_WORLD, 1 );
    }
}

/*@
    CLOG_cput - move a log record from one of the input buffers to the output

This function moves records from one of the three buffers being merged into
the output buffer.  When the output buffer is filled, it is sent to the
parent.  A separate output routine handles output on the root.  If the input
buffer is emptied (endblock record is read) and corresponds to a child, a new
buffer is received from the child.  When an endlog record is read on the input
buffer, the number of sources is decremented and the time is set to positive
infinity so that the empty input source will never have the lowest time.

At entry we assume that *p is pointing to a log record that is not an
end-of-block record, and that outbuf is pointing to a buffer that has
room in it for the record.  We ensure that these conditions are met on
exit as well, by sending (or writing, if we are the root) and receiving
blocks as necessary.

Input parameters

.  pointer to the record to be moved into the output buffer

@*/
void CLOG_cput( ptr )
double **ptr;
{
    double *p;
    CLOG_HEADER *h;
    MPI_Status logstatus;

    p = *ptr;
    h = (CLOG_HEADER *) p;

    if (h->rectype == CLOG_ENDLOG) {
        h->timestamp = CLOG_MAXTIME;
        inputs--;
        return;
    }
    
    /*printf("putting record with timestamp= %f\n",*p); */
    memcpy(outptr, p, h->length * sizeof(double)); 
    outptr += h->length; /* skip to next output record */

    if (((char *) outptr + CLOG_MAX_REC_LEN) >= (char *) outend ) {
        /* put on end-of-block-record */
        ((CLOG_HEADER *) outptr)->timestamp = h->timestamp; /* use prev rec. */
        ((CLOG_HEADER *) outptr)->rectype   = CLOG_ENDBLOCK;
        ((CLOG_HEADER *) outptr)->procid    = me;
        ((CLOG_HEADER *) outptr)->length    = CLOG_reclen(CLOG_ENDBLOCK);

        if (parent != -1) {
            PMPI_Send(outbuf, (CLOG_BLOCK_SIZE / sizeof (double)), MPI_DOUBLE,
                      parent, CMERGE_LOGBUFTYPE, MPI_COMM_WORLD);
            /* printf("%d sent to %d\n", me, parent); */
        }
        else {
#if defined( HAVE_SLOG1 )
            if (log_type == SLOG_LOG) {
                /*(abhi) final output to SLOG */
                if (C2S1_make_SLOG(outbuf) == C2S_ERROR)
                    PMPI_Abort(MPI_COMM_WORLD, -1);
            }
            else
#endif
                CLOG_output(outbuf); /* final output of block */
        }
        outptr = outbuf;
    }

    p += h->length;		/* skip to next input record */
    *ptr = p;

    h = (CLOG_HEADER *) p;
    if (h->rectype == CLOG_ENDBLOCK) {
        if (ptr == &myptr) {
            CLOG_num_blocks--;
            if ((!CLOG_currbuff->next) || (!CLOG_num_blocks)) { 
                /* if no next buffer, endlog follows */
                if (CLOG_tempFD > 0)
                    CLOG_reinit_buff();
                if (CLOG_num_blocks == 0) {
                    p += CLOG_reclen(CLOG_ENDBLOCK);
                    /*
                    printf( "[%d] length of endblock = %d\n", me,
                            CLOG_reclen(CLOG_ENDBLOCK) );
                    */
                }
                else {
                    CLOG_currbuff = CLOG_first;
                    p = CLOG_currbuff->data;
                    CLOG_procbuf(p); /* do postprocessing(procids, lengths) */
                }
                *ptr = p;
            }
            else {
                /*p = (double *) CLOG_currbuff;*/
                CLOG_currbuff = CLOG_currbuff->next;
                /*FREE( p );*/    /* free local block just processed */
                *ptr = (double *) CLOG_currbuff->data;
                CLOG_procbuf(*ptr);	/* process next local block */
            }
        }
        if (ptr == &lptr) {
            PMPI_Recv(lbuf, (CLOG_BLOCK_SIZE / sizeof (double)), MPI_DOUBLE, 
                      lchild, CMERGE_LOGBUFTYPE, MPI_COMM_WORLD, &logstatus);
            *ptr = lbuf;
        }
        if (ptr == &rptr) {
            PMPI_Recv(rbuf, (CLOG_BLOCK_SIZE / sizeof (double)), MPI_DOUBLE, 
                      rchild, CMERGE_LOGBUFTYPE, MPI_COMM_WORLD, &logstatus);
            *ptr = rbuf;
        }
    }
}

/*@
    CLOG_reinit_buff - reads CLOG_BLOCKS from temporary logfile into memory.
@*/
void CLOG_reinit_buff( ) 
{
    
    int return_code;
    CLOG_BLOCK *buffer_parser;
    buffer_parser = CLOG_first;
    CLOG_num_blocks = 0;
    return_code = read(CLOG_tempFD, buffer_parser, sizeof (CLOG_BLOCK));

    while((buffer_parser->next != NULL) && (return_code)) {
      if(return_code == -1) {
	fprintf(stderr, "Unable to read from temporary log file on process"
		" %d\n", me);
	fflush(stderr);
	PMPI_Abort(MPI_COMM_WORLD, 1);
      }
      else  
	CLOG_num_blocks++;
      buffer_parser = buffer_parser->next;
      return_code = read(CLOG_tempFD, buffer_parser, sizeof (CLOG_BLOCK));
    }
    if(return_code == -1) {
      fprintf(stderr, "Unable to read from temporary log file on process"
	      " %d\n", me);
      fflush(stderr);
      PMPI_Abort(MPI_COMM_WORLD, 1);
    }
    else if((buffer_parser->next == NULL) && (return_code))
      CLOG_num_blocks++;
    CLOG_currbuff = CLOG_first;
}
	
      
      
    

/*@
    CLOG_csync - synchronize clocks for adjusting times in merge

This version is sequential and non-scalable.  The root process serially
synchronizes with each slave, using the first algorithm in Gropp, "Scalable
clock synchronization on distributed processors without a common clock".
The array is calculated on the root but broadcast and returned on all
processes.

Inout Parameters:

+ root      - process to serve as master
- timediffs - array of doubles to be filled in

@*/

void CLOG_csync( root, diffs )
int root;
double diffs[];
{
    int myrank, numprocs, numtests = 3, i, j;
    double time_1, time_2, time_i, bestgap, bestshift = 0.0;
    int dummy;
    MPI_Status status;

    PMPI_Comm_rank(MPI_COMM_WORLD, &myrank);
    PMPI_Comm_size(MPI_COMM_WORLD, &numprocs);
    PMPI_Barrier(MPI_COMM_WORLD);
    PMPI_Barrier(MPI_COMM_WORLD); /* approximate common starting point */

    if (root == myrank) {	/* I am the master, but not nec. 0 */
	for (i = 0; i < numprocs; i++) {
	    if (i != myrank) {
		bestgap = 1000000.0; /* infinity, fastest turnaround so far */
		for (j = 0; j < numtests; j++) {
		    PMPI_Send(&dummy, 0, MPI_INT, i, MASTER_READY, MPI_COMM_WORLD);
		    PMPI_Recv(&dummy, 0, MPI_INT, i, SLAVE_READY,  MPI_COMM_WORLD,
			     &status); 
		    time_1 = CLOG_timestamp();
		    PMPI_Send(&dummy,  0, MPI_INT, i, TIME_QUERY,  MPI_COMM_WORLD);
		    PMPI_Recv(&time_i, 1, MPI_DOUBLE, i, TIME_ANSWER,
			     MPI_COMM_WORLD, &status);
		    time_2 = CLOG_timestamp();
		    if ((time_2 - time_1) < bestgap) {
			bestgap   = time_2 - time_1;
			bestshift =  0.5 * (time_2 + time_1) - time_i;
		    }
		}
		timediffs[i] = bestshift;
	    }
	    else 		/* i = root */
		timediffs[i] = 0;
	}
    }
    else {			/* not the root */
	for (j = 0; j < numtests; j++) {
	    PMPI_Recv(&dummy, 0, MPI_INT, root, MASTER_READY, MPI_COMM_WORLD, &status); 
	    PMPI_Send(&dummy, 0, MPI_INT, root, SLAVE_READY, MPI_COMM_WORLD);
	    PMPI_Recv(&dummy, 0, MPI_INT, root, TIME_QUERY, MPI_COMM_WORLD, &status);
	    time_i = CLOG_timestamp();
	    PMPI_Send(&time_i, 1, MPI_DOUBLE, root, TIME_ANSWER, MPI_COMM_WORLD);
	}
    }
    PMPI_Bcast(timediffs, CMERGE_MAXPROCS, MPI_DOUBLE, root, MPI_COMM_WORLD);
}

void CLOG_printdiffs( diffs )
double diffs[];
{
    int i, numprocs, self;

    PMPI_Comm_size(MPI_COMM_WORLD, &numprocs);
    PMPI_Comm_rank(MPI_COMM_WORLD, &self);
    printf("[%d] time shift array:  ", self);
    for (i = 0; i < numprocs; i++)
	printf("%f ", diffs[i]);
    printf("\n");
}
