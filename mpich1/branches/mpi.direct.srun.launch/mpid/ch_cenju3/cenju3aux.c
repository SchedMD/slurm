/*  This file contains routines which were rmoved from cenju3priv.c */

/*  This routine is recursively called to increment the stack pointer */

int MPID_CENJU3_write (from, lid, to, bytes)
char *from, *to;
int  lid, bytes;
{
    int err;
    double dummy[1000];

/*  printf("start of MPID_CENJU3_write: get_stack =%d, to = %d\n",
           MPID_CENJU3_Get_Stack(), to);
    fflush (stdout); */

    if (MPID_CENJU3_Get_Stack() > to) {
       err = MPID_CENJU3_write (from, lid, to, bytes);

/* note: the following check should be senseless;
         It is used to ensure that dummy is put on the stack */

       if ((char *) dummy < to) {
          fflush (stdout);
          fprintf(stderr, "Inconsitent in MPID_CENJU3_write\n");
       }
    }
    else err = CJrmwrite (from, lid, to, bytes);

    return (err);
}

/******* Are this routines really required ???? **********************/

void *MPID_CENJU3_malloc (len)
int len;
{
       void *pointer;

#ifdef MPID_DEBUG_ALL
       if (MPID_DebugFlag) {
          fprintf( MPID_DEBUG_FILE, 
	          "[%d] Starting Malloc len = %d\n", MPID_MyWorldRank, len);
          fflush( MPID_DEBUG_FILE );
       }
#endif /* MPID_DEBUG_ALL */

/*     CJrpcmask (); */
       pointer = (void *) malloc((size_t) len);
/*     CJrpcunmask (); */

#ifdef MPID_DEBUG_ALL
       if (MPID_DebugFlag) {
          fprintf( MPID_DEBUG_FILE, 
	          "[%d] pointer returned by malloc = %d\n", MPID_MyWorldRank,
                   pointer);
          fflush( MPID_DEBUG_FILE );
       }
#endif /* MPID_DEBUG_ALL */

       return (pointer);
}

void MPID_CENJU3_free (ptr)
void *ptr;
{
#ifdef MPID_DEBUG_ALL
       if (MPID_DebugFlag) {
          fprintf( MPID_DEBUG_FILE, 
	          "[%d] Starting Free; pointer = %d\n", MPID_MyWorldRank, ptr);
          fflush( MPID_DEBUG_FILE );
       }
#endif /* MPID_DEBUG_ALL */

/*     CJrpcmask (); */
       free((void *) ptr);
/*     CJrpcunmask (); */
}
