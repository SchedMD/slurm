#include "clog2slog.h"

/*
    Functions given below change the byte ordering of data in the various
    structs to make sure that always data is written out in accordance to 
    the MPI standard. Only datatypes of int and doubles may be changed and
    also in the case of doubles we are only concerned with the byte ordering
    assuming that all machined follow the IEEE storage convention
*/
void adjust_CLOG_HEADER ( h )
CLOG_HEADER *h;
{
  CLOGByteSwapDouble (&(h->timestamp), 1);
  CLOGByteSwapInt (&(h->rectype), 3); /* We do not adjust the 'pad' field */
}

void adjust_CLOG_MSG ( msg )
CLOG_MSG *msg;
{
  CLOGByteSwapInt (&(msg->etype), 6);
}

void adjust_CLOG_COLL ( coll )
CLOG_COLL *coll;
{
  CLOGByteSwapInt (&(coll->etype), 5); /* We do not adjust the 'pad' field */
}

void adjust_CLOG_COMM ( comm )
CLOG_COMM *comm;
{
  CLOGByteSwapInt (&(comm->etype), 4); 
}

void adjust_CLOG_STATE ( state )
CLOG_STATE *state;
{
  CLOGByteSwapInt (&(state->stateid), 3);
  /* 'color' and 'description' fields are not adjusted */
}

void adjust_CLOG_EVENT ( event )
CLOG_EVENT *event;
{
  CLOGByteSwapInt (&(event->etype), 1);
  /* 'pad' and 'description' are not adjusted */
}

void adjust_CLOG_SRC ( src )
CLOG_SRC *src;
{
  CLOGByteSwapInt (&(src->srcloc), 2);
  /* 'filename' is not adjusted */
}

void adjust_CLOG_RAW ( raw )
CLOG_RAW *raw;
{
  CLOGByteSwapInt (&(raw->etype), 3);
  /* 'pad' and 'string' are not adjusted */
}

/*
  CLOGByteSwapInt - Swap bytes in an integer
*/
void CLOGByteSwapInt(int *buff,int n)
{
  int  i,j,tmp =0;
  int  *tptr = &tmp;          /* Need to access tmp indirectly to get */
                                /* arround the bug in DEC-ALPHA compilers*/
  char *ptr1,*ptr2 = (char *) &tmp;

  for ( j=0; j<n; j++ ) {
    ptr1 = (char *) (buff + j);
    for (i=0; i<sizeof(int); i++) {
      ptr2[i] = ptr1[sizeof(int)-1-i];
    }
    buff[j] = *tptr;
  }
}
/* --------------------------------------------------------- */
/*
  CLOGByteSwapDouble - Swap bytes in a double
*/
void CLOGByteSwapDouble(double *buff,int n)
{
  int    i,j;
  double tmp,*buff1 = (double *) buff;
  double *tptr = &tmp;          /* take care pf bug in DEC-ALPHA g++ */
  char   *ptr1,*ptr2 = (char *) &tmp;

  for ( j=0; j<n; j++ ) {
    ptr1 = (char *) (buff1 + j);
    for (i=0; i<sizeof(double); i++) {
      ptr2[i] = ptr1[sizeof(double)-1-i];
    }
    buff1[j] = *tptr;
  }
}
