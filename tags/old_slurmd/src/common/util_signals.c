#include <signal.h>
#include <errno.h>
#include <src/common/log.h>
#include <src/common/slurm_errno.h>
#include <src/common/util_signals.h> 
int posix_signal_pipe_ignore ()
{
	return posix_signal_ignore ( SIGPIPE ) ;
}

int posix_signal_ignore ( int signal )
{
	struct sigaction newaction ;
        struct sigaction oldaction ;
	newaction . sa_handler = SIG_IGN ;
	if ( sigaction( signal , &newaction, &oldaction) )/* ignore tty input */
	{
		error ("posix_signal_ignore: sigaction %m errno %d", errno);
		return SLURM_ERROR ;
	}
	return SLURM_SUCCESS ;
}

int unblock_all_signals_pthread ( )
{
	sigset_t set;
	if (sigfillset (&set))
	{
		error ("unblock_all_signals_pthread: sigfillset %m errno %d", errno);
		return SLURM_ERROR ;
	}
	if (pthread_sigmask (SIG_UNBLOCK, &set, NULL))
	{
		error ("unblock_all_signals_pthread: pthread_sigmask %m errno %d", errno);
		return SLURM_ERROR ;
	}
	return SLURM_SUCCESS ;
}

int block_all_signals_pthread ( )
{
	sigset_t set;
	if (sigfillset (&set))
	{
		error ("block_all_signals_pthread: sigfillset %m errno %d", errno);
		return SLURM_ERROR ;
	}
	if (pthread_sigmask (SIG_BLOCK, &set, NULL))
	{
		error ("block_all_signals_pthread: pthread_sigmask %m errno %d", errno);
		return SLURM_ERROR ;
	}
	return SLURM_SUCCESS ;
}

int unblock_all_signals ( )
{
	sigset_t set;
	if (sigfillset (&set))
	{
		error ("unblock_all_signals: sigfillset %m errno %d", errno);
		return SLURM_ERROR ;
	}
	if (sigprocmask (SIG_UNBLOCK, &set, NULL))
	{
		error ("unblock_all_signals: sigprocmask %m errno %d", errno);
		return SLURM_ERROR ;
	}
	return SLURM_SUCCESS ;
}

int block_all_signals ( )
{
	sigset_t set;
	if (sigfillset (&set))
	{
		error ("block_all_signals: sigfillset %m errno %d", errno);
		return SLURM_ERROR ;
	}
	if (sigprocmask (SIG_BLOCK, &set, NULL))
	{
		error ("block_all_signals: sigprocmask %m errno %d", errno);
		return SLURM_ERROR ;
	}
	return SLURM_SUCCESS ;
}

