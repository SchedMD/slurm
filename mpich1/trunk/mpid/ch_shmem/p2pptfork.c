/*
 * This is a p2p create procs using pthreads WITH THE ASSUMPTION THAT 
 * ALL STATIC STORAGE IS PRIVATE.  In other words, these aren't really threads,
 * but since they don't have individual pids, they aren't processes either.
 */
#include <pthread.h>
#include <stdio.h>

#ifndef NO_TEST
int MPID_MyWorldRank = 0;
int MPID_MyWorldSize = 2;
#define MPID_MAX_PROCS 16
void p2p_cleanup(void);
void p2p_error(string,value)
char * string;
int value;
{
    printf("%s %d\n",string, value);
    /* printf("p2p_error is not fully cleaning up at present\n"); */
    p2p_cleanup();
}
#endif

/*
 * --- create processes ---
 * We create the processes by calling a routine that starts main over again.
 * This is ok for MPI, since we don't say what is running (or when!) before
 * MPI_Init
 */
typedef struct { int argc; char **argv; } MPID_startarg;

void *MPID_startup( aptr )
void *aptr;
{
  static int rval;
  MPID_startarg *a = (MPID_startarg*)aptr;
  rval = main( a->argc, a->argv );
  return (void *)&rval;
}

/* 
 * It is someone else's responsibility NOT to call this again.
 * This RE-CALLS the user's MAIN program.
 */
pthread_t thread[MPID_MAX_PROCS];
void p2p_create_procs( numprocs, argc, argv )
int  numprocs;
int  argc;
char **argv;
{
  MPID_startarg args;
  int i, rc;

  args.argc = argc;
  args.argv = argv;
  for (i=0; i<numprocs; i++) {
    /* pthread_attr_default */
    rc = pthread_create( &thread[i], (pthread_attr_t *) 0, 
                         MPID_startup, &args );
    if (rc != 0) {
      p2p_error( "p2p_init: thread-fork failed\n", (-1));
    }
  }
}
/* 
 * --- cleanup --
 * We can't wait for the threads to return from MPID_startup, because this
 * means returning from main (which may be loooong after MPI_Finalize).
 * Instead, we make the threads call pthread_exit and the master checks the
 * return codes for problems.
 */
void p2p_cleanup()
{
  int status, i;

  if (MPID_MyWorldRank != 0) {
    status = 0;
    pthread_exit( &status );
  }
  else {
    for (i=0; i<MPID_MyWorldSize-1; i++) {
      pthread_join( thread[i], (void **)&status );
      /* Status is the return code from pthread_exit */
    }
  }
  
}

/* Other useful thread routines 
pthread_yield( );

pthread_self( ) -- needed only to set priorities

*/
#ifndef NO_TEST
/* NEC SX4
   shared_begin and end indicate data that is shared when the compiler is
   directed to make all static data private with -hpthrspec
 */
#pragma _pthread shared_begin
int MPID_IsReady = 0;
int MPID_Globid = 0;
pthread_mutex_t MPID_mutex;
int *shared_memory = 0;
#pragma _pthread shared_end
int counter = 0;
int main( argc, argv )
int argc;
char **argv;
{
  int numprocs = 4;
  if (MPID_IsReady == 0) {
      MPID_IsReady = 1;
      counter = 4;
      pthread_mutex_init( &MPID_mutex, (pthread_mutexattr_t *)0 );
      /* Start the threads but hold mutex so counter can be set */
      p2p_create_procs( numprocs, argc, argv );
  }
  else {
    /* I'm a new thread, so get my rank */
    /*     MPID_MyWorldRank = 1; */
    pthread_mutex_lock( &MPID_mutex );
    MPID_MyWorldRank = ++MPID_Globid;
    pthread_mutex_unlock( &MPID_mutex );
  }
  printf( "I'm thread %d\n", MPID_MyWorldRank );
  printf( "[%d] counter = %d\n", MPID_MyWorldRank, counter );

  counter = counter + 1;
  printf( "[%d] +counter = %d\n", MPID_MyWorldRank, counter );
  if (MPID_MyWorldRank == 0) {
    extern void * malloc();
    shared_memory = (int *)malloc( 100 );
    /* sleep(1); */
    shared_memory[4] = 406;
  }
  else {
    int cnt = 0;
    while (!shared_memory) ;
    while (shared_memory[4] != 406) cnt++;
    printf( "[%d] cnt = %d\n", MPID_MyWorldRank, cnt );
    fflush( stdout );
  }
  
  p2p_cleanup();
  return 0;
}
#endif
