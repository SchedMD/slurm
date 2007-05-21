#include "p4.h"
#include "p4_sys.h"

typedef long P4_Aint;

#if !defined(NEXT) && !defined(HAVE_STDLIB_H)
/* Note Sun 4.1.3 defines exit as int exit(int), even though DOCUMENTED as
   void exit(int).  Try to use the definition in stdlib.h instead */
extern P4VOID exit (int);
#endif

/*
 * Some systems provide prototypes for the definitions SIG_IGN and SIG_DFL
 * only if some additional defs (like -D_ANSI_SOURCE under FreeBSD) are
 * supplied.  If you really need a completely clean compile, consider
 * adding these defs to the user cflags.
 */

static int interrupt_caught = 0; /* True if an interrupt was caught */

int p4_hard_errors = 1;

#if defined(ENCORE) || defined(SYMMETRY) || defined(TITAN) || \
    defined(SGI)    || defined(GP_1000)  || defined(TC_2000) || defined(SUN_SOLARIS)
#define P4_HANDLER_TYPE int
#else
#define P4_HANDLER_TYPE P4VOID
#endif
#if defined(__STDC__) 
#define HANDLER_ARG int
#else
#define HANDLER_ARG
#endif

#if defined(HAVE_FOUR_ARG_SIGS)
#define HANDLER_ARGS int,int,int,int
#else
#define HANDLER_ARGS int
#endif

static P4_HANDLER_TYPE (*prev_sigint_handler) (HANDLER_ARGS) = NULL;
static P4_HANDLER_TYPE (*prev_sigsegv_handler) (HANDLER_ARGS) = NULL;
static P4_HANDLER_TYPE (*prev_sigbus_handler) (HANDLER_ARGS) = NULL;
static P4_HANDLER_TYPE (*prev_sigfpe_handler) (HANDLER_ARGS) = NULL;
static P4_HANDLER_TYPE (*prev_sigquit_handler) (HANDLER_ARGS) = NULL;
static P4_HANDLER_TYPE (*prev_sigabrt_handler) (HANDLER_ARGS) = NULL;
static P4_HANDLER_TYPE (*prev_sighup_handler) (HANDLER_ARGS) = NULL;
static P4_HANDLER_TYPE (*prev_sigill_handler) (HANDLER_ARGS) = NULL;
static P4_HANDLER_TYPE (*prev_sigpipe_handler) (HANDLER_ARGS) = NULL;
static P4_HANDLER_TYPE (*prev_sigterm_handler) (HANDLER_ARGS) = NULL;
static P4_HANDLER_TYPE (*prev_sigio_handler) (HANDLER_ARGS) = NULL;
static P4_HANDLER_TYPE (*prev_err_handler) (HANDLER_ARGS) = NULL;
static int err_sig;
#if defined(HAVE_FOUR_ARG_SIGS)
static int                err_code;
static struct sigcontext *err_scp;
static char              *err_addr;
#endif

int p4_soft_errors( int onoff )
{
    int old;

    if (!p4_local)
	p4_error("p4_soft_errors: p4_local must be allocated first", 0);

    old = p4_local->soft_errors;
    p4_local->soft_errors = onoff;
    return old;
}

P4VOID p4_error( char *string, int value )
{
    static int in_p4_error = 0;
#ifdef USE_PRINT_LAST_ON_ERROR
    char ch_debug_string[128];
#endif

    if (in_p4_error) {
	/* Recursive call.  We may have caught a signal.  If not, we'll 
	   exit to avoid any possibility of an infinite loop of p4_error 
	   calls caused by one of the routines that this routine calls 
	   also signaling an error (which would be a bug, but this is intended
	   as a firewall to work around such a bug.  Another option would 
	   be to keep a counter and return if the counter got too high; 
	   this would allow some routines to call p4_error and continue, 
	   as long as they didn't loop and keep calling p4_error */
      
	if (interrupt_caught) {
	    switch (value) {
	    case SIGILL:
	    case SIGBUS:
	    case SIGSEGV:
		exit(128 + value); /* emergency stop */
	    }
	    /* Otherwise, let us continue */
	    return;
	}
	exit(1);
    }
    in_p4_error = 1;

    /* This is a good place to implement a trace back */
    /* MPIR_Print_backtrace( "cpi", 1, "Call stack\n" ); */

    /* If the following line generates a warning about prototypes,
       see the comment at the head of the file */
    SIGNAL_P4(SIGINT,SIG_IGN);
    fflush(stdout);
    if (value != SIGINT) {
	/* Don't generate this message when there is a SIGINT */
#ifdef USE_PTHREADS
	printf("%s: %u:  p4_error: %s: %d\n",whoami_p4,pthread_self(),
	       string,value);
#else
	printf("%s:  p4_error: %s: %d\n",whoami_p4,string,value);
#endif
    }
    if (value < 0)
        perror("    p4_error: latest msg from perror");
    fflush(stdout);

#ifdef USE_PRINT_LAST_ON_ERROR
    sprintf(ch_debug_string, "%s: channel device received p4_error: %s: %d\n",
	    whoami_p4, string, value );
    MPID_Ch_send_last_p4error( ch_debug_string );

    p4_dprint_last( stderr );
#endif

#if 0
    /* Enable this when debugging the listener logic */
    SIGNAL_P4(LISTENER_ATTN_SIGNAL, SIG_IGN);
    p4_dprintf("p4_error: ******** pausing before any zap **********\n");
    fflush(stdout);
    fflush(stderr);
    pause();
#endif

    /* Send interrupt to all known processes */
	zap_p4_processes(); 

    /* Send kill-clients message to all known listeners */

#ifdef P4_WITH_MPD
#ifdef FOOGLE
    /* Not right for mpd */
    if (p4_get_my_id() != -99)   /* if I am not the listener */
	zap_remote_p4_processes();
#endif
#else
    if (p4_local->my_id != -99)   /* if I am not the listener */
    {
        p4_dprintfl(99, "about to zap remote processes, value=%d\n", value);
	zap_remote_p4_processes();
    }
#endif

    /* shutdown(sock,2), close(sock) all sockets */
#   ifdef CAN_DO_SOCKET_MSGS
    shutdown_p4_socks();
#   endif

#   ifdef SYSV_IPC
    remove_sysv_ipc();
#   endif

#   if defined(SGI)  &&  defined(VENDOR_IPC)
    unlink(p4_sgi_shared_arena_filename);
#   endif

#ifdef P4_WITH_MPD
    { BNR_Group mygroup;
      int rc;
      rc = BNR_Get_group( &mygroup );
      BNR_Kill( mygroup );
    }
#endif
    p4_clean_execer_port();

    /* Allow SIGINT along with the other signals (code originally
       had "if (interrupt_caught && value != SIGINT)" ) */
    if (interrupt_caught)
    {
	switch (value)
	{
	  case SIGINT:
	    prev_err_handler = prev_sigint_handler;
	    break;
	  case SIGSEGV:
	    prev_err_handler = prev_sigsegv_handler;
	    break;
	  case SIGBUS:
	    prev_err_handler = prev_sigbus_handler;
	    break;
	  case SIGFPE:
	    prev_err_handler = prev_sigfpe_handler;
	    break;
	  case SIGQUIT:
	    prev_err_handler = prev_sigquit_handler;
	    break;
#ifdef SIGABRT
	  case SIGABRT:
	    prev_err_handler = prev_sigabrt_handler;
	    break;
#endif
#ifdef SIGHUP
	  case SIGHUP:
	    prev_err_handler = prev_sighup_handler;
	    break;
#endif
#ifdef SIGILL
	  case SIGILL:
	    prev_err_handler = prev_sigill_handler;
	    break;
#endif
#ifdef SIGPIPE
	  case SIGPIPE:
	    prev_err_handler = prev_sigpipe_handler;
	    break;
#endif
#ifdef SIGTERM
	  case SIGTERM:
	    prev_err_handler = prev_sigterm_handler;
	    break;
#endif
	  case SIGIO:
	    prev_err_handler = prev_sigio_handler;
	    break;
	  default:
	    printf("p4_error: unidentified err handler (signal %d)\n", value );
	    prev_err_handler = NULL;
	    break;
	}
	if (prev_err_handler == (P4_HANDLER_TYPE (*) (HANDLER_ARGS)) NULL)
	{
	    /* return to default handling of the interrupt by the OS */
	    SIGNAL_P4(value,SIG_DFL); 
#           if defined(NEXT)  ||  defined(KSR)
            kill(getpid(),value);
#           endif
	    /* This is really a fatal error, so ensure that we don't get 
	       any farther */
	    exit( 1 );
	    return;
	}
	else
	{
#if defined(HAVE_FOUR_ARG_SIGS)
	    (*prev_err_handler) (err_sig, err_code, err_scp, err_addr);
#else
	    (*prev_err_handler) (err_sig);
#endif
	}
    }
    else
    {

#       if defined(SP1_EUI)
	mpc_stopall(value);
#       endif
	exit(1);
    }
}

/* static P4_HANDLER_TYPE sig_err_handler(sig, code, scp, addr) */

#if defined(HAVE_FOUR_ARG_SIGS)
static P4VOID sig_err_handler(sig, code, scp, addr)
int sig, code;
struct sigcontext *scp;
char *addr;
#else
static P4VOID sig_err_handler(int sig)
#endif
{
    interrupt_caught = 1;
    err_sig = sig;
#if defined(HAVE_FOUR_ARG_SIGS)
    err_code = code;
    err_scp = scp;
    err_addr = addr;
#endif
    p4_dprintfl(90,"sig_err_handler: sig = %d\n", sig);
    if (sig == SIGSEGV)
	p4_error("interrupt SIGSEGV", sig);
    else if (sig == SIGBUS)
	p4_error("interrupt SIGBUS", sig);
    else if (sig == SIGFPE)
	p4_error("interrupt SIGFPE", sig);
    else if (sig == SIGINT) {
#if defined(USE_PRINT_LAST_ON_SIGINT) && ! defined(USE_PRINT_LAST_ON_ERROR)
	p4_dprint_last( stderr );
#endif
	p4_error("interrupt SIGINT", sig);
    }
    else
	p4_error("interrupt SIGx", sig);
    /* return( (P4_HANDLER_TYPE) NULL); */

    interrupt_caught = 0;
}


/*
  Trap signals so that we can propagate error conditions and tidy up 
  shared system resources in a manner not possible just by killing procs
*/
P4VOID trap_sig_errs( void )
{
    P4_HANDLER_TYPE (*rc) (HANDLER_ARGS);

    SIGNAL_WITH_OLD_P4(SIGINT, sig_err_handler,
                           rc= (P4_HANDLER_TYPE (*) (HANDLER_ARGS)));
    if (rc == (P4_HANDLER_TYPE (*) (HANDLER_ARGS)) -1)
	p4_error("trap_sig_errs: SIGNAL_P4 failed", SIGINT);
    if (((P4_Aint) rc > 1)  &&  ((P4_Aint) rc != (P4_Aint) sig_err_handler))
	prev_sigint_handler = rc;

/* we can not handle sigsegv on symmetry and balance because they use 
 * it for shmem stuff 
*/
#ifdef CAN_HANDLE_SIGSEGV
    SIGNAL_WITH_OLD_P4(SIGSEGV, sig_err_handler, 
                           rc= (P4_HANDLER_TYPE (*) (HANDLER_ARGS)));
    if ((P4_Aint) rc == -1)
	p4_error("trap_sig_errs: SIGNAL_P4 failed", SIGSEGV);
    if (((P4_Aint) rc > 1)  &&  ((P4_Aint) rc != (P4_Aint) sig_err_handler))
	prev_sigsegv_handler = rc;
#endif

    SIGNAL_WITH_OLD_P4(SIGBUS, sig_err_handler,
                 rc= (P4_HANDLER_TYPE (*) (HANDLER_ARGS)));
    if ((P4_Aint) rc == -1)
	p4_error("trap_sig_errs: SIGNAL_P4 failed", SIGBUS);
    if (((P4_Aint) rc > 1)  &&  ((P4_Aint) rc != (P4_Aint) sig_err_handler))
	prev_sigbus_handler = rc;

    /* SIGFPE is a special case.  On some systems (HPUX at higher
       optimization levels), speculative execution may generate
       SIGFPE (e.g.,move a divide through the test for divide by zero).
       If SIGFPE is SIG_IGN, then restore the signal handler */
    SIGNAL_WITH_OLD_P4(SIGFPE, sig_err_handler,
                           rc= (P4_HANDLER_TYPE (*) (HANDLER_ARGS)));
    if ((P4_Aint) rc == -1)
	p4_error("trap_sig_errs: SIGNAL_P4 failed", SIGFPE);
    /* Test for ignore FPE */
    if ((P4_Aint) rc == (P4_Aint)SIG_IGN) {
	SIGNAL_P4(SIGFPE,SIG_IGN);
	prev_sigfpe_handler = (P4_HANDLER_TYPE(*) (HANDLER_ARGS))(SIG_IGN); /* Just in case */
    }
    else {
	if (((P4_Aint) rc > 1)  && ((P4_Aint) rc != (P4_Aint) sig_err_handler))
	    prev_sigfpe_handler = rc;
    }
    /* Install handers for the other signals */
#ifdef SIGQUIT
    SIGNAL_WITH_OLD_P4(SIGQUIT, sig_err_handler, 
                           rc= (P4_HANDLER_TYPE (*) (HANDLER_ARGS)));
    if ((P4_Aint) rc == -1)
	p4_error("trap_sig_errs: SIGNAL_P4 failed", SIGQUIT);
    if (((P4_Aint) rc > 1)  &&  ((P4_Aint) rc != (P4_Aint) sig_err_handler))
	prev_sigquit_handler = rc;
#endif
#ifdef SIGABRT
    SIGNAL_WITH_OLD_P4(SIGABRT, sig_err_handler, 
                           rc= (P4_HANDLER_TYPE (*) (HANDLER_ARGS)));
    if ((P4_Aint) rc == -1)
	p4_error("trap_sig_errs: SIGNAL_P4 failed", SIGABRT);
    if (((P4_Aint) rc > 1)  &&  ((P4_Aint) rc != (P4_Aint) sig_err_handler))
	prev_sigabrt_handler = rc;
#endif
#ifdef SIGHUP
    SIGNAL_WITH_OLD_P4(SIGHUP, sig_err_handler, 
                           rc= (P4_HANDLER_TYPE (*) (HANDLER_ARGS)));
    if ((P4_Aint) rc == -1)
	p4_error("trap_sig_errs: SIGNAL_P4 failed", SIGHUP);
    if (((P4_Aint) rc > 1)  &&  ((P4_Aint) rc != (P4_Aint) sig_err_handler))
	prev_sighup_handler = rc;
#endif
#ifdef SIGILL
    SIGNAL_WITH_OLD_P4(SIGILL, sig_err_handler, 
                           rc= (P4_HANDLER_TYPE (*) (HANDLER_ARGS)));
    if ((P4_Aint) rc == -1)
	p4_error("trap_sig_errs: SIGNAL_P4 failed", SIGILL);
    if (((P4_Aint) rc > 1)  &&  ((P4_Aint) rc != (P4_Aint) sig_err_handler))
	prev_sigill_handler = rc;
#endif
#ifdef SIGPIPE
    SIGNAL_WITH_OLD_P4(SIGPIPE, sig_err_handler, 
                           rc= (P4_HANDLER_TYPE (*) (HANDLER_ARGS)));
    if ((P4_Aint) rc == -1)
	p4_error("trap_sig_errs: SIGNAL_P4 failed", SIGPIPE);
    if (((P4_Aint) rc > 1)  &&  ((P4_Aint) rc != (P4_Aint) sig_err_handler))
	prev_sigpipe_handler = rc;
#endif
#ifdef SIGTERM
    SIGNAL_WITH_OLD_P4(SIGTERM, sig_err_handler, 
                           rc= (P4_HANDLER_TYPE (*) (HANDLER_ARGS)));
    if ((P4_Aint) rc == -1)
	p4_error("trap_sig_errs: SIGNAL_P4 failed", SIGTERM);
    if (((P4_Aint) rc > 1)  &&  ((P4_Aint) rc != (P4_Aint) sig_err_handler))
	prev_sigterm_handler = rc;
#endif
#ifdef SIGIO
    SIGNAL_WITH_OLD_P4(SIGIO, sig_err_handler, 
                           rc= (P4_HANDLER_TYPE (*) (HANDLER_ARGS)));
    if ((P4_Aint) rc == -1)
	p4_error("trap_sig_errs: SIGNAL_P4 failed", SIGIO);
    if (((P4_Aint) rc > 1)  &&  ((P4_Aint) rc != (P4_Aint) sig_err_handler))
	prev_sigio_handler = rc;
#endif
}

P4VOID p4_set_hard_errors( int flag )
{
    p4_hard_errors = flag;
}
