#include <signal.h>

#include <src/common/util_signals.h> 
void posix_signal_pipe_ignore ()
{
	posix_signal_ignore ( SIGPIPE ) ;
}

void posix_signal_ignore ( int signal )
{
	struct sigaction newaction ;
        struct sigaction oldaction ;
	newaction . sa_handler = SIG_IGN ;
	sigaction( signal , &newaction, &oldaction); /* ignore tty input */
}
