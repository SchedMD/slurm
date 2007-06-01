#include "p4.h"
#include "p4_sys.h"

/* Type for accept calls */
#ifdef USE_SOCKLEN_T
typedef socklen_t p4_sockopt_len_t;
#elif defined(USE_SIZE_T_FOR_SOCKLEN_T)
typedef size_t p4_sockopt_len_t;
#else
typedef int p4_sockopt_len_t;
#endif


/* #if defined(SYMMETRY) || defined(SUN)  || \
    defined(DEC5000)  || defined(SGI)  || \
    defined(RS6000)   || defined(HP)   || \
    defined(NEXT)     || defined(CRAY) || \
    defined(CONVEX)   || defined(KSR)  || \
    defined(FX2800)   || defined(FX2800_SWITCH)  || \
    defined(SP1)      || defined(LINUX) */

#if !defined(P4_DO_NOT_USE_SERVER)

/**********************************
 #include <sys/types.h>
 #include <sys/socket.h>
 #include <netinet/in.h>
 #include <netdb.h>
 #include <pwd.h>
 #include <stdio.h>
**********************************/

/* #define DEBUG */

char *start_prog_error = 0;

/* extern int errno; */

/* 
   The strerror function may be defined in string.h .  If you get warnings 
   about lines that reference strerror and HAVE_STRERROR is defined,
   make sure that <string.h> is being included (and contains the 
   prototype for strerror).  Note that some systems, such as Solaris,
   define strerror in string.h ONLY when the compiler asserts __STDC__ .
 */
#ifndef HAVE_STRERROR
#define strerror(n) sys_errlist[n]
/* Some systems define this as const char * instead of char * .  To
   handle that, we only define it if it is needed */
#ifdef NEEDS_SYS_ERRLIST
extern char *sys_errlist[];
#endif
#endif

static int connect_to_server (char *);
static void send_string (int,char *);
static void recv_string (int,char *,int);

int start_slave(char *host, char *username, char *prog, int port, 
		char *am_slave, char *(*pw_hook)(char *,char*))
{
    int n, conn;
    struct passwd *pw;
    char port_string[250];
    char pgm_args_string[250];
    char *pw_string;
    static char buf[250];
    char *local_username;
    char myhost_local[MAXHOSTNAMELEN];
    int new_port, new_fd, stdout_fd;
    char msg[500];
    struct sockaddr_in temp;
    int rc;
    p4_sockopt_len_t templen;
    int pid;
    struct timeval tv;
    fd_set rcv_fds;

    /* If no sserver port has been selected, just return failure */
    if (sserver_port < 0) {
        start_prog_error = "No secure server port set";
        return -1;
    }
    myhost_local[0] = '\0';
    get_qualified_hostname(myhost_local,sizeof(myhost_local));
    /* gethostname(myhost_local, sizeof(myhost_local)); */

    conn = connect_to_server(host);
    if (conn < 0) {
        start_prog_error = "Could not connect";
	return -1;
    }

#ifdef DEBUG
    printf("Connected\n");
#endif

    pw = getpwuid(geteuid());
    if (pw == NULL)
    {
	extern char *getlogin (void);

	local_username = getlogin();
	if (local_username == NULL)
	{
	    start_prog_error = "Cannot get pw entry";
	    return -3;
	}
    }
    else
    {
	local_username = pw->pw_name;
    }

    p4_dprintfl(50, "Sending user names local=%s remote=%s to server\n", 
		local_username, username );

    send_string(conn, local_username);
    send_string(conn, username);
    /* The original code specified a one second timeout.  This was too
       short in some cases.  Two seconds was adequate, but this really
       needs to be looked at some more.  To make this more robust,
       we now use 5 seconds.*/
    tv.tv_sec  = 5;
    tv.tv_usec = 0;
    FD_ZERO(&rcv_fds);
    FD_SET(conn, &rcv_fds);
    n = select(conn+1,&rcv_fds,0,0,&tv);
    if (n == 1)
    {
	recv_string(conn, buf, sizeof(buf));
    }
    else
    {
	if (n < 0) {
	    p4_dprintfl( 90, "Errno from select in server handshake is %d\n", errno );
	}
	else {
	    p4_dprintfl( 90, "Timeout talking to server (%d seconds)\n", 
			 tv.tv_sec );
	}
	start_prog_error = "Handshake with server failed";
	return -3;
    }
#ifdef DEBUG
    printf("Got reply1 '%s'\n", buf);
#endif

    /* Proceed-2 indicates the new server, Proceed is the old server */
    if (strncmp(buf, "Password", 8) == 0)
    {
	if (pw_hook == NULL)
	    pw_string = "";
	else
	    pw_string = (*pw_hook) (host, username);
	send_string(conn, pw_string);
	recv_string(conn, buf, sizeof(buf));
#ifdef DEBUG
	printf("Got reply '%s'\n", buf);
#endif
	if (strncmp(buf, "Proceed", 7) != 0 && 
	    strncmp(buf, "Proceed-2", 9 ) != 0)
	{
	    start_prog_error = buf;
	    return -4;
	}
    }
    else if (strncmp(buf, "Proceed", 7) != 0 && 
	     strncmp(buf, "Proceed-2", 9 ) != 0)
    {
	start_prog_error = buf;
	return -4;
    }

    /* We can send the environment here if we are using the newer server */
    /* The environment values are (count)\n#chars\nvariable=value\n.... */
#define P4_SEND_ENVIRONMENT
#ifdef P4_SEND_ENVIRONMENT
    if (strncmp( buf, "Proceed-2", 9 ) == 0) {
	extern char **environ;
	char digits[10];
	int  environ_count;

	for (environ_count = 0; environ[environ_count] != NULL; 
	     environ_count++);
	sprintf( digits, "%d", environ_count );
	send_string( conn, "%env" );
	send_string( conn, digits );
	for (environ_count = 0; environ[environ_count] != NULL; 
	     environ_count++) {
	    sprintf( digits, "%d", (int)strlen( environ[environ_count] ) );
	    send_string( conn, digits );
	    send_string( conn, environ[environ_count] );
	}
    }
#endif    
    
    /* Send the program and then the args */
    send_string(conn, prog);

    sprintf(pgm_args_string, "%s %d %s", myhost_local, port, am_slave);
    send_string(conn, pgm_args_string);

    if ((pid = fork_p4()) == 0)
    {
	/* This branch of the if is the process that listens to the
	   remote process and receives data to send to stdout/stderr */
	net_setup_anon_listener(MAX_P4_CONN_BACKLOG, &new_port, &new_fd);
	fflush(stdout);
	sprintf(port_string, "%d", new_port);
	send_string(conn, port_string);
	fflush(stdout);
	/* stdout_fd = net_accept(new_fd); */
	templen = sizeof(temp);
	SYSCALL_P4(stdout_fd, accept(new_fd, (struct sockaddr *) &temp, &templen));
	close(new_fd);

	n = 1;
	while (n > 0)
	{
	    SYSCALL_P4(n, read(stdout_fd, msg, 499));
	    if (n > 0)
	    {
		/* This can lose output if the write is blocked.
		   it should probably defer or at least indicate
		   data lost.  Note that if it waits for the write
		   to complete, it might cause the writes on the
		   other end to stall.  
		 */
		SYSCALL_P4(rc, write(1,msg,n));
		/* This fflush is unnecessary; write will bypass anyway (?) */
		fflush(stdout);
	    }
	}
	exit(0);
    }

    recv_string(conn, buf, sizeof(buf));
#ifdef DEBUG
    printf("Got reply2 '%s'\n", buf);
#endif
    if (strncmp(buf, "Success", 7) != 0)
    {
	/* kill i/o handling process and decrement num forked */
	kill(pid,SIGKILL);
	p4_global->n_forked_pids--;
	start_prog_error = buf;
	return -4;
    }

    start_prog_error = buf;
    close(conn);

/***** Peter Krauss uses these lines
    if (kill(pid, 0) == 0)
        kill(pid, SIGKILL);
    p4_dprintfl(00, "waiting for termination of anon_listener %d\n", pid);
#if defined(DEC5000) || defined(HP) || defined(SUN)
    waitpid(pid, (int *) 0, 0);
#else
    wait((int *) 0);
#endif
*****/

    return 0;
}

static int connect_to_server(host)
char *host;
{
    int conn;
    int rc;
    struct hostent *hostent;
    struct sockaddr_in addr;

#ifdef SGI_TEST
    extern P4VOID net_set_sockbuf_size(int size, int skt);	/* 7/12/95, bri@sgi.com */
#endif

    SYSCALL_P4(conn, socket(AF_INET, SOCK_STREAM, 0));
    if (conn < 0)
    {
	start_prog_error = strerror(errno);
	return -1;
    }

#ifdef SGI_TEST
    net_set_sockbuf_size(-1,conn);	/* 7/12/95, bri@sgi.com */
#endif

    hostent = gethostbyname_p4(host);

    addr.sin_family = hostent->h_addrtype;
    addr.sin_port = htons(sserver_port);
    bcopy(hostent->h_addr, &addr.sin_addr, hostent->h_length);

    SYSCALL_P4(rc, connect(conn, (struct sockaddr *) &addr, sizeof(addr)));
    if (rc < 0)
    {
	start_prog_error = strerror(errno);
	return -1;
    }

    return conn;
}

static void send_string(sock, str)
int sock;
char *str;
{
    int rc, len = strlen(str);
    char nl = 10;

    /* This code relies on the writes succeeding completely.
       This will probably happen if the total length sent is no greater
       than the socket buffer size.  More robust code would loop
       until total length sent
     */
    SYSCALL_P4(rc, write(sock, str, len));
    if (rc < 0)
    {
	perror("write");
	p4_error("send_string write 1 ", -1);
    }
    SYSCALL_P4(rc, write(sock, &nl, 1));
    if (rc < 0)
    {
	perror("write");
	p4_error("send_string write 2 ", -1);
    }

}

/* Receive from fd sock into buf which is of length len */
static void recv_string(sock, buf, len)
int sock, len;
char *buf;
{
    char *bptr;
    int n;

    bptr = buf;
    while (1)
    {
	SYSCALL_P4(n, read(sock, bptr, 1));
	if (n < 0)
	{
	    perror("read");
	    p4_error("recv_string read ", -1);
	    exit(1);
	}
	if (*bptr == '\n')
	    break;
	bptr++;
	if (bptr - buf >= len)
	    break;
    }
    *bptr = 0;
}

/*****  RMB: no longer needed due to autoconf
#if defined(LINUX) && !defined(NO_ECHO)
#define NO_ECHO
#endif
*****/

#if defined(P4BSD) && !defined(NO_ECHO)

#ifdef FREEBSD
#include <sys/ioctl_compat.h>
#else
#include <sys/ioctl.h>
#endif

static struct sgttyb orig_tty;

static int echo_off (void)
{
    struct sgttyb tty_new;

    if (ioctl(0, TIOCGETP, &orig_tty) < 0)
    {
	fprintf(stderr, "iotcl TIOCGETP failed: %s\n", strerror(errno));
	return -1;
    }

    tty_new = orig_tty;
    tty_new.sg_flags &= ~(ECHO);

    if (ioctl(0, TIOCSETP, &tty_new) < 0)
    {
	fprintf(stderr, "iotcl TIOCSETP failed: %s\n", strerror(errno));
	return -1;
    }
    return 0;
}

static int echo_on (void)
{
    if (ioctl(0, TIOCSETP, &orig_tty) < 0)
    {
	fprintf(stderr, "iotcl TIOCSETP failed: %s\n", strerror(errno));
	return -1;
    }
    return 0;
}

#elif defined(HAVE_TERMIO_H)
#include <termio.h>

struct termio tty_orig;

static int echo_off (void)
{
    struct termio tty_new;

    if (ioctl(0, TCGETA, &tty_orig) < 0)
    {
	fprintf(stderr, "tcgetattr failed: %s\n", strerror(errno));
	return -1;
    }

    tty_new = tty_orig;

    tty_new.c_lflag &= ~(ECHO);

    if (ioctl(0, TCSETA, &tty_new) < 0)
    {
	fprintf(stderr, "tcsetattr failed: %s\n", strerror(errno));
	return -1;
    }
    return (0);
}

static int echo_on (void)
{
    if (ioctl(0, TCSETA, &tty_orig) < 0)
    {
	fprintf(stderr, "tcsetattr failed: %s\n", strerror(errno));
	return -1;
    }
    return (0);
}
#elif defined(HAVE_TERMIOS_H)
#include <termios.h>

struct termios tty_orig;

static int echo_off (void)
{
    struct termios tty_new;

    if (ioctl(0, TIOCGETA, &tty_orig) < 0)
    {
	fprintf(stderr, "tcgetattr failed: %s\n", strerror(errno));
	return -1;
    }

    tty_new = tty_orig;

    tty_new.c_lflag &= ~(ECHO);

    if (ioctl(0, TIOCSETA, &tty_new) < 0)
    {
	fprintf(stderr, "tcsetattr failed: %s\n", strerror(errno));
	return -1;
    }
    return (0);
}

static int echo_on (void)
{
    if (ioctl(0, TIOCSETA, &tty_orig) < 0)
    {
	fprintf(stderr, "tcsetattr failed: %s\n", strerror(errno));
	return -1;
    }
    return (0);
}

#else
/* 
 *  No common way to turn echo on/off.  Just ignore.
 */
static int echo_off (void)
{
    return 0;
}

static int echo_on (void)
{
    return 0;
}
#endif


char *getpw_ss(host, name)
char *host, *name;
{
    static char buf[1024];
    char *s;

    echo_off();
    printf("Password for %s@%s: ", name, host);
    fflush(stdout);
    fgets(buf, sizeof(buf), stdin);
    echo_on();
    printf("\n");

    for (s = buf; *s; s++)
	if (*s == '\n')
	{
	    *s = 0;
	    break;
	}

    return buf;
}

#endif
/* #ifdef SYMMETRY */
