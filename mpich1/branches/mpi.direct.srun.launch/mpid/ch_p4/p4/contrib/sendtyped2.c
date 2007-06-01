#include "p4.h"

#define FINAL_BARRIER 5

#define PassVars( inf, outf, TYPE, TYPE_P4NAME, FORMAT, msg ) { \
  TYPE x; \
  int from, tag, len, count; \
 \
  count = 0; \
  if (inf) { \
    while (fread( &x, sizeof( TYPE ), 1, inf )) { \
      p4_sendx( 4, 1, &x, sizeof( TYPE ), TYPE_P4NAME ); \
      fprintf( outf, FORMAT, x ); \
      count++; \
      /* fprintf( stderr, "Sent %d.\n", count ); */ \
    } \
    p4_send( 4, 1, &x, 0 ); \
      /* send 0-length terminator message */ \
    fprintf( stderr, "0 sent %d somethings.\n", count ); \
    rewind( inf ); \
 \
  } else { \
    while (from = 0, \
	   tag = 4, \
	   p4_recv( &tag, &from, &msg, &len ), \
	   len) { \
      fprintf( outf, FORMAT, *((TYPE *)msg) ); \
      count++; \
    } \
    fprintf( stderr, "Received %d somethings.\n", count ); \
  } \
}

main( argc, argv )
int argc;
char **argv;
{
  p4_initenv( &argc, argv );
  if (!p4_get_my_id()) p4_create_procgroup();
  slave( argc, argv );
  p4_dprintf("Waiting for end.\n");
  p4_global_barrier(FINAL_BARRIER); /* broadcasts may be in progress */
  p4_dprintf("All done.\n");
  p4_wait_for_end();
}


slave( argc, argv )
int argc;
char **argv;
{
  int myid, np;
  int from, tag, len;
  char *msg;
  int i, j, k;
  FILE *inf, *outf;

  myid = p4_get_my_id();
  np = p4_num_total_ids();

  msg = p4_msg_alloc(2000);
    
  if (!myid) {

    if (argc!=4) {
      printf( "three arguments required: input file, output file 1, \
output file 2\n\n" );
      p4_send( 1, 1, "", 1 );
      return 1;
    } else {
      p4_send( 1, 1, argv[3], strlen( argv[3] )+1 );
    }

    tag = 2; from = 1;
    p4_recv( &tag, &from, &msg, &len );
    if (!*((FILE **)msg)) {
      printf( "Could not open %s (%d).  Exiting.\n\n", argv[3],
	      (int)*((FILE **)msg) );
      return 1;
    }

    inf = fopen( argv[1], "r" );
    outf = fopen( argv[2], "w" );
    i = inf && outf;
    p4_send( 3, 1, &i, sizeof( int ) );
    if (!i) {
      printf( "Could not open %s.  Exiting.\n\n", inf?argv[2]:argv[1] );
      return 1;
    }

    fprintf( stderr, "Ready to send.\n" );

    PassVars( inf, outf, int,    P4INT, "%d\n",     msg );
    PassVars( inf, outf, double, P4DBL, "%.15lg\n", msg );
    PassVars( inf, outf, float,  P4FLT, "%.8g\n",   msg );
    PassVars( inf, outf, long,   P4LNG, "%ld\n",    msg );
    
    fclose( inf );
  } else if (myid==1) {

    tag = 1; from = 0;
    p4_recv( &tag, &from, &msg, &len );
    if (len=1 && *msg==0) return 1;
    outf = fopen( (char *)msg, "w" );

    p4_send( 2, 0, &outf, sizeof( FILE * ) );
      /* tell 0 whether outf2 was opened */

    if (!outf) return 1;

    tag=3,from=0;
    p4_recv( &tag, &from, &msg, &len );
    if (!*((int *)msg)) {
      return 1;
    }
      /* check with 0 whether inf or outf1 were opened */

    fprintf( stderr, "Ready to receive.\n" );

    PassVars( (FILE *)0, outf, int,    P4INT, "%d\n",     msg );
    PassVars( (FILE *)0, outf, double, P4DBL, "%.15lg\n", msg );
    PassVars( (FILE *)0, outf, float,  P4FLT, "%.8g\n",   msg );
    PassVars( (FILE *)0, outf, long,   P4LNG, "%ld\n",    msg );
  }

  fclose( outf );
  p4_msg_free( msg );
}
