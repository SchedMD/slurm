#include "p4.h"
#include "p4_sys.h"

extern int errno;
#ifndef HAVE_STRERROR
extern char *sys_errlist[];
#define strerror(n) sys_errlist[n]
#endif

VOID reaper();

main(argc,argv)
int argc;
char **argv;
{
    int listen_fd, connection_fd;
    int port;
    int done;
    struct sockaddr_in from;
    int fromlen;
    
    p4_initenv(&argc,argv);
    sprintf(whoami_p4, "server"); /* alter p4 environment */

    net_setup_listener(5, UNRESERVED_PORT, &listen_fd);

#ifdef P4BSD
    signal(SIGCHLD, reaper);
#endif
#ifdef P4SYSV
    signal(SIGCLD, reaper);
#endif

    done = FALSE;
    while (!done)
    {
	fromlen = sizeof(from);
	connection_fd = accept(listen_fd, (struct sockaddr *)&from, &fromlen);
	if (connection_fd == -1)
	{
	    if (errno == EINTR)
		continue;
	    else
	    {
		perror("server accept");
		exit(1);
	    }
	}
	p4_dprintfl(20,"accepted on %d\n", connection_fd);
	done = process_connection(connection_fd);
	shutdown(connection_fd, 2);
	close(connection_fd);
    }

    shutdown(listen_fd, 2);
    close(listen_fd);
}

process_connection(fd)
int fd;
{
struct net_message_t msg;
int type;
char *pgm;
char *host;
char *am_slave;
int port;

    if (net_recv(fd, &msg, sizeof(msg)) == NET_RECV_EOF)
	return(FALSE);

    type = p4_n_to_i(msg.type);

    switch (type)
    {
	case NET_EXEC:
	    pgm = msg.pgm;
	    host = msg.host;
	    am_slave = msg.am_slave;
	    port = p4_n_to_i(msg.port);
	    p4_dprintfl(20,"server got exec msg: pgm=%s host=%s port=%d am_slave=%s\n",
		   pgm, host, port, am_slave);
	    exec_pgm(host, pgm, port, am_slave);
	    msg.type = p4_i_to_n(NET_RESPONSE);
	    msg.success = p4_i_to_n(1);
	    net_send(fd, &msg, sizeof(msg), FALSE);
	    return(FALSE);

	case NET_DONE:
	    p4_dprintfl(20,"server got done message\n");
	    return(TRUE);

	default:
	    p4_dprintfl(20,"server got unknown message type %d\n", type);
	    return(FALSE);
    }
}
	
VOID exec_pgm(host, pgm, port, am_slave)
char *host, *pgm, *am_slave;
int port;
{
int pid;
int rc;
char shortpgm[100];
char sport[10];
char *s;

    if ((s = rindex(pgm, '/')) != NULL)
	strcpy(shortpgm, s + 1);
    else
	strcpy(shortpgm, pgm);

    sprintf(sport, "%d", port);

    p4_dprintfl(20,"exec_pgm: pgm=%s short=%s sport=%s\n",
		    pgm, shortpgm, sport);

    fflush(stdout);
    pid = fork();  /* Not fork_p4 here as don't want interrupts on error */
    if (pid < 0)
	p4_error("exec_pgm fork",pid);
    if (pid == 0)
    {
	/* The child */

	/****
	  RB This line is typically used, but some versions
	  of exec seem to have a bug requiring the full pgm
	  name to be in both arg positions.

	rc = execl(pgm, shortpgm, host, sport, am_slave, NULL);
	****/

	rc = execl(pgm, pgm, host, sport, am_slave, NULL);

	if (rc < 0)
	    p4_error("exec_pgm execl",rc);
	exit(1);
    }
}

VOID reaper()
{
int status;

int pid;
    
    p4_dprintfl(20,"server: entering reaper\n");
    pid = wait(&status);
    p4_dprintfl(20,"server: pid %d died with status %d\n", pid, status);
}

int slave() /* dummy proc */
{
    return(0);
}
