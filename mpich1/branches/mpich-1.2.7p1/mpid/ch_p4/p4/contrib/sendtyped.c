#include "p4.h"

#define FINAL_BARRIER 5

main( argc, argv )
int argc;
char **argv;
{
  p4_initenv( &argc, argv );
  if (!p4_get_my_id()) p4_create_procgroup();
  slave();
  p4_dprintf("Waiting for end.\n");
  p4_global_barrier(FINAL_BARRIER); /* broadcasts may be in progress */
  p4_dprintf("All done.\n");
  p4_wait_for_end();
}


slave()
{
  int myid, np;

  int i, j, k;
  float f;
  double d;
  unsigned char c;

  myid = p4_get_my_id();
  np = p4_num_total_ids();

  if (!myid) {
    i=42, f=42.42, d=42.4242;
    printf( "Process 0 says ints, floats, and doubles are %d, %d, and %d bytes long, respectively.\n", sizeof(int), sizeof(float), sizeof(double) );
    printf( "Process 0 sending %d, %f, and %lf.\n", i, f, d );
    printf( "[0] d = %lf, size = %d\n", d, sizeof(double) );
    for (j=0; j<sizeof(double); j++) {
      c = *( ( (unsigned char *) &d ) + j );
      k = c;
      printf( "%d ", k );
    }
    printf( "\n" );

    p4_sendx( 1, 1, &i, sizeof(int), P4INT );
    p4_sendx( 2, 1, &f, sizeof(float), P4FLT );
    p4_sendx( 3, 1, &d, sizeof(double), P4DBL );

  } else if (myid==1) {

    int from, tag, intlen, floatlen, doublelen;
    char *msg;

    printf( "Process 1 says ints, floats, and doubles are %d, %d, and %d bytes long, respectively.\n", sizeof(int), sizeof(float), sizeof(double) );

    msg = p4_msg_alloc(2000);
    from = 0, tag = 1;
    p4_recv( &tag, &from, &msg, &intlen );
    i = *((int *)msg);

    from = 0, tag = 2;
    p4_recv( &tag, &from, &msg, &floatlen );
    f = *((float *)msg);

    from = 0, tag = 3;
    p4_recv( &tag, &from, &msg, &doublelen );
    d = *((double *)msg);

    printf( "[1] d = %lf, size = %d\n", d, sizeof(double) );
    for (j=0; j<sizeof(double); j++) {
      c = *( ( (unsigned char *) &d ) + j );
      k = c;
      printf( "%d ", k );
    }
    printf( "\n" );

    printf( "Process 1 received %d, %f, and %lf, lengths %d %d %d.\n",
	    i, f, d, intlen, floatlen, doublelen );
    p4_msg_free( msg );
  }
}
