/**\ --MPE_Log--
*  * mpe_log_adjusttime.c - routines for adjusting timer values before
*  *                        merging all the data
*  *
*  * MPE_Log currently represents some code written by Dr. William
*  * Gropp, stolen from Chameleon's 'blog' logging package and
*  * modified by Ed Karrels, as well as some fresh code written
*  * by Ed Karrels.
*  *
*  * All work funded by Argonne National Laboratory
\**/

/* Forward refs for this file */
void MPE_Log_adjtime2 (void);

/* Convert the times to an offset from the starting time */
void MPE_Log_adjtime1()
{
  int               xx_i, *bp, n;
  double            temp_time, dif;
  MPE_Log_BLOCK     *bl;
  MPE_Log_HEADER    *ap;

  bl         = MPE_Log_firstBlock;
  while (bl) {
    n      = bl->size;
    bp     = (int *)(bl + 1);
    xx_i   = 0;
    while (xx_i < n) {
      ap    = (MPE_Log_HEADER *)bp;
      MOVEDBL( &temp_time, &ap->time );	     /* temp_time = ap->time */
      if (temp_time != 0) {
	dif   = temp_time - MPE_Log_tinit;
	MOVEDBL( &ap->time, &dif );   /* ap->time.s2 = dif; */
#if DEBUG>2
fprintf( debug_file, "adjust1 time %10.5lf to %10.5lf\n", temp_time, dif );
#endif
      }
      xx_i += ap->len;
      bp   += ap->len;
    }
    bl = bl->next;
  }
}



/*
   Find skew and a basic offset based on the global sync events in the
   log.  Returns 1 if values found, 0 otherwise.
 */
int MPE_Log_FindSkew( Skew, Goff )
double  *Skew, *Goff;
{
  int               xx_i, *bp, n;
  double            sync_start, sync_end, temp_time;
  double            proc0_times[2];
  int               found_first = 0, nsync = 0;
  MPE_Log_BLOCK        *bl;
  MPE_Log_HEADER       *ap;

  /* printf( "[%d] starting adj 2\n", MPE_Log_procid ); */
  bl         = MPE_Log_firstBlock;
  while (bl) {
    n      = bl->size;
    bp     = (int *)(bl + 1);
    xx_i   = 0;
    while (xx_i < n) {
      ap   = (MPE_Log_HEADER *)bp;
      if (ap->event == MPE_Log_EVENT_SYNC) {
	MOVEDBL( &temp_time, &ap->time );
#if DEBUG
fprintf( debug_file, "[%d] sync event at %10.5lf\n", MPE_Log_procid,
	 temp_time );
#endif
	if (found_first) 
	  sync_end   = temp_time;
	else {
	  sync_start  = temp_time;
	  found_first = 1;
	}
	nsync ++;
      }
      xx_i += ap->len;
      bp   += ap->len;
    }
    bl = bl->next;
  }
  
  /* Trade the information (for more complete adjustments, everyone needs
     the skews and global offsets) */
  proc0_times[0] = sync_start;
  proc0_times[1] = sync_end;

  MPI_Bcast( proc0_times, 2, MPI_DOUBLE, 0, MPI_COMM_WORLD );
  
  /* Don't do anything if there aren't enough values */
  if (nsync < 2) {
    return 0;
  }
  
  /* printf( "[%d] scaling times \n", MPI_Log_procid ); */
  *Skew = (proc0_times[1] - proc0_times[0]) / (sync_end - sync_start);
  *Goff = proc0_times[0] - sync_start;
  return 1;
}



void MPE_Log_ApplyTimeCorrection( sk, goff )
double  sk, goff;
{
  int               xx_i, *bp, n;
  double            temp_time, adjustedTime;
  MPE_Log_BLOCK     *bl;
  MPE_Log_HEADER    *ap;
  
  bl = MPE_Log_firstBlock;
  while (bl) {
    n      = bl->size;
    bp     = (int *)(bl + 1);
    xx_i   = 0;
    while (xx_i < n) {
      ap    = (MPE_Log_HEADER *)bp;
      MOVEDBL( &temp_time, &ap->time );
      if (temp_time) {
	adjustedTime = temp_time * sk + goff;
	if (adjustedTime < 0.0) adjustedTime = 0.0;
	  /* just in case we go negative, don't want to scare anyone */
	MOVEDBL( &ap->time, &adjustedTime );
#if DEBUG>2
fprintf( debug_file, "adjust2 time %10.5lf to %10.5lf\n",
	temp_time, adjustedTime );
#endif
      }
      xx_i += ap->len;
      bp   += ap->len;
    }
    bl = bl->next;
  }
}





/*
   Adjust times for skew and offset variations to match process 0
*/
void MPE_Log_adjtime2()
{
  double  sk, goff;

  /* Set the defaults, just in case */
  sk   = 1.0;
  goff = 0.0;
  if (!MPE_Log_FindSkew( &sk, &goff )) return;
  MPE_Log_ApplyTimeCorrection( sk, goff );
}


int MPE_Log_adjusttimes()
{
  if (MPE_Log_AdjustedTimes) return 0;

  MPE_Log_adjtime1();
  MPE_Log_adjtime2();
  MPE_Log_AdjustedTimes = 1;

  return 0;
}

/* This shifts ALL times by the minimum time value on ALL processors */
int MPE_Log_adjust_time_origin()
{
    double t1;
    /* Compute the MPE_Log_tinit time */
    t1 = MPE_Log_tinit;

    MPI_Allreduce( &t1, &MPE_Log_tinit, 1, MPI_DOUBLE, MPI_MIN, 
		   MPI_COMM_WORLD );
    MPE_Log_adjtime1();
    return 0;
}
