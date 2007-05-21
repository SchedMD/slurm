/* This file provides routines to check for the use of signals by software */

#include <stdio.h>
#include <signal.h>
#include "test.h"
#include "mpi.h"

/* In order to quiet noisy C compilers, we provide ANSI-style prototypes
   where possible */
int SYiCheckSig ( FILE *, int, char * );
int SYCheckSignals ( FILE * );

#ifdef HAVE_SIGACTION
int SYiCheckSig( fp, sig, signame )
FILE *fp;
int  sig;
char *signame;
{
static int firstmsg = 1;
struct sigaction libsig;

sigaction( sig, NULL, &libsig);
if (libsig.sa_handler != SIG_IGN && libsig.sa_handler != SIG_DFL) {
    if (firstmsg) {
	firstmsg = 0;
	fprintf( fp, "Some signals have been changed.  This is not an error\n\
but rather is a warning that user programs should not redefine the signals\n\
listed here\n" );
	}
    fprintf( fp, "Signal %s has been changed\n", signame );
    return 1;
    }
return 0;
}
#else
int SYiCheckSig( fp, sig, signame )
FILE *fp;
int  sig;
char *signame;
{
void (*oldsig)();
static int firstmsg = 1;

oldsig = signal(sig,SIG_IGN);
if (oldsig != SIG_IGN && oldsig != SIG_DFL) {
    if (firstmsg) {
	firstmsg = 0;
	fprintf( fp, "Some signals have been changed.  This is not an error\n\
but rather is a warning that user programs should not redefine the signals\n\
listed here\n" );
	}
    fprintf( fp, "Signal %s has been changed\n", signame );
    return 1;
    }
signal(sig,oldsig);
return 0;
}
#endif

int SYCheckSignals( fp )
FILE *fp;
{
int  ndiff = 0;

#ifdef SIGHUP
ndiff += SYiCheckSig( fp, SIGHUP, "SIGHUP" );
#endif

#ifdef SIGINT
ndiff += SYiCheckSig( fp, SIGINT, "SIGINT" );
#endif

#ifdef SIGQUIT
ndiff += SYiCheckSig( fp, SIGQUIT, "SIGQUIT" );
#endif

#ifdef SIGILL
ndiff += SYiCheckSig( fp, SIGILL, "SIGILL" );
#endif

#ifdef SIGTRAP
ndiff += SYiCheckSig( fp, SIGTRAP, "SIGTRAP" );
#endif

#ifdef SIGIOT
ndiff += SYiCheckSig( fp, SIGIOT, "SIGIOT" );
#endif

#ifdef SIGABRT
ndiff += SYiCheckSig( fp, SIGABRT, "SIGABRT" );
#endif

#ifdef SIGEMT
ndiff += SYiCheckSig( fp, SIGEMT, "SIGEMT" );
#endif

#ifdef SIGFPE
ndiff += SYiCheckSig( fp, SIGFPE, "SIGFPE" );
#endif

#ifdef SIGBUS
ndiff += SYiCheckSig( fp, SIGBUS, "SIGBUS" );
#endif

#ifdef SIGSEGV
ndiff += SYiCheckSig( fp, SIGSEGV, "SIGSEGV" );
#endif

#ifdef SIGSYS
ndiff += SYiCheckSig( fp, SIGSYS, "SIGSYS" );
#endif

#ifdef SIGPIPE
ndiff += SYiCheckSig( fp, SIGPIPE, "SIGPIPE" );
#endif

#ifdef SIGALRM
ndiff += SYiCheckSig( fp, SIGALRM, "SIGALRM" );
#endif

#ifdef SIGTERM
ndiff += SYiCheckSig( fp, SIGTERM, "SIGTERM" );
#endif

#ifdef SIGURG
ndiff += SYiCheckSig( fp, SIGURG, "SIGURG" );
#endif

#ifdef SIGTSTP
ndiff += SYiCheckSig( fp, SIGTSTP, "SIGTSTP" );
#endif

#ifdef SIGCONT
ndiff += SYiCheckSig( fp, SIGCONT, "SIGCONT" );
#endif

#ifdef SIGCHLD
ndiff += SYiCheckSig( fp, SIGCHLD, "SIGCHLD" );
#endif

#ifdef SIGTTIN
ndiff += SYiCheckSig( fp, SIGTTIN, "SIGTTIN" );
#endif

#ifdef SIGTTOU
ndiff += SYiCheckSig( fp, SIGTTOU, "SIGTTOU" );
#endif

#ifdef SIGIO
ndiff += SYiCheckSig( fp, SIGIO, "SIGIO" );
#endif

#ifdef SIGPOLL
ndiff += SYiCheckSig( fp, SIGPOLL, "SIGPOLL" );
#endif

#ifdef SIGXCPU
ndiff += SYiCheckSig( fp, SIGXCPU, "SIGXCPU" );
#endif

#ifdef SIGXFSZ
ndiff += SYiCheckSig( fp, SIGXFSZ, "SIGXFSZ" );
#endif

#ifdef SIGVTALRM
ndiff += SYiCheckSig( fp, SIGVTALRM, "SIGVTALRM" );
#endif

#ifdef SIGPROF
ndiff += SYiCheckSig( fp, SIGPROF, "SIGPROF" );
#endif

#ifdef SIGWINCH
ndiff += SYiCheckSig( fp, SIGWINCH, "SIGWINCH" );
#endif

#ifdef SIGLOST
ndiff += SYiCheckSig( fp, SIGLOST, "SIGLOST" );
#endif

#ifdef SIGUSR1
ndiff += SYiCheckSig( fp, SIGUSR1, "SIGUSR1" );
#endif

#ifdef SIGUSR2
ndiff += SYiCheckSig( fp, SIGUSR2, "SIGUSR2" );
#endif

return ndiff;
}


int main( int argc, char **argv )
{
    int err;
    MPI_Init( &argc, &argv );
    err = SYCheckSignals( stdout );
    Test_Waitforall( );
    MPI_Finalize();
    return err;
}
