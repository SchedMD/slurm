/* 
 * General comments on making changes to this server
 *
 * System specific code: Be VERY careful when considering removing 
 * system-specific code; it is probably there for a reason.  Note that the
 * secure server is called that because it has been designed so that it
 * can run as root; some system-specific code is relevant to root-only 
 * operations.
 *
 * Though the initial code doesn't reflect this very well, the code should
 * NOT rely on system type to choose options; rather, it should use
 * HAVE_xxxx, with a configure script creating the HAVE_xxxx defines.
 */

#include "server.h"

/* 
 * old defines for compatibility with the Nexus secure server.  These
 * can be removed when v3.0 is stopped being supported.
 */
#define SERVER_CD_NOTIFIER "\0"
#define SERVER_ENV_NOTIFIER "\1"

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <stdio.h>
#include <ctype.h>
#include <pwd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/signal.h>
#include <sys/wait.h>

/* Unix domain sockets */
#include <sys/un.h>

#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifndef HAVE_INDEX
#define index strchr
#endif

#ifndef HAVE_RINDEX
#define rindex strrchr
#endif

#if defined(HAVE_KERBEROS) && defined(HAVE_AFS)
#include <afs/kauth.h>
#include <afs/kautils.h>
#include <afs/auth.h>
#define NOPAG 0xffffffff
#else
/* 
 * I don't know where to find the Kerberos directories on a non-AFS
 * machine, but I assume it should be easy enough to find out if we
 * need to.
 */
#if defined(HAVE_KERBEROS)
#undef HAVE_KERBEROS
#endif

#endif

/* 
   If we are supporting secure sockets, we need a few items from the 
   ssl library.  See also server_ssl.c 
 */
#ifdef HAVE_SSL
#include "ssl.h"
#include "ssllib.h"
extern SSLHandle *ssl_handle;
extern int ssl_mode;
#endif

#ifdef IWAY
char ss_hostname[32];
char part;
int ss_hostname_length;
int seed;
int t;
#endif

/* Older systems have strchr only in string.h, not strings.h */
/* Prefer string because it is the ANSI version */
/* Include *both* if possible.  Even this isn't always a good idea; 
   in some cases, systems fail when both are included */
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#if defined(HAVE_STRINGS_H)
#include <strings.h>
#endif

/* Type for get/setsockopt calls */
#ifdef USE_SOCKLEN_T
typedef socklen_t p4_sockopt_len_t;
#elif defined(USE_SIZE_T_FOR_SOCKLEN_T)
typedef size_t p4_sockopt_len_t;
#else
typedef int p4_sockopt_len_t;
#endif


#ifndef HAVE_GETOPT

/* This is from the released BSD sources lib/libc/getopt.c */

/*
 * get option letter from argument vector
 */
int	opterr = 1,		/* if error message should be printed */
	optind = 1,		/* index into parent argv vector */
	optopt;			/* character checked for validity */
char	*optarg;		/* argument associated with option */

#else

extern char *optarg;
extern int optind;

#endif

#define MAXARGS 256

#ifndef LOGFILE
#define LOGFILE "/usr/adm/secure_server.log"
#endif
char logfile[1024];
FILE *logfile_fp;
int   logfile_fd;


#ifndef SERVER_APPS_FILE
#define SERVER_APPS_FILE "%s/.server_apps"
#endif
char server_apps_file[1024];

#define notice2(a,b) {sprintf(tmpbuf, a, b); notice(tmpbuf);}
#define notice3(a,b,c) {sprintf(tmpbuf, a, b,c); notice(tmpbuf);}
#define failure2(a,b) {sprintf(tmpbuf, a, b); failure(tmpbuf);}
#define failure3(a,b,c) {sprintf(tmpbuf, a, b,c); failure(tmpbuf);}

extern char *crypt();
#ifndef HAVE_STRERROR
extern char *sys_errlist[];
#define strerror(n) sys_errlist[n]
#endif
extern int errno;

char tmpbuf[1024];
char *fromhost;

char fileport[1024];
int  use_local_port = 0;

#define DEFAULT_PORT 753

int daemon_mode;
int daemon_port;
int daemon_pid;     /* pid of parent */
int stdfd_closed = 0;
int debug = 0;
int never_fork = 0;

int print_pid = 0;

char *this_username;
int this_uid;

#ifdef IWAY
char token[1024];
#endif

void doit                    ( int, int );
void execute                 ( char *, char **, int,
			       char *, char *, int, int, int,
			       struct hostent * );
void get_environment         ( char ***, int * );
int getline                  ( char *, int );
void failure                 ( char * );
void notice                  ( char * );
int net_accept               ( int );
void net_setup_listener      ( int, int, int * );
void net_setup_anon_listener ( int, int *, int *);
void net_setup_local_listener ( int, int *, char * );
void error_check             ( int, char * );
char *timestamp              ( void );
char *save_string            ( char * );
int handle_remote_conn       ( int, int );
int handle_local_conn        ( int, int );
int Process_pgm_commands     ( char *, char *, char *, int, int,
			       struct hostent *hp, int );
int check_allowed_file       ( char *, char *, char * );

static int connect_to_listener ( struct hostent *, int, int );
void reaper ( int );
int main ( int, char ** );

#define REMOTE_CONN 1
#define LOCAL_COMM 2
/*
 * Notes on the use of file descriptors (fds)
 *
 * This code uses the <stdio> routines to read/write data to the
 * connected socket.  This simplifies much of the code.  However,
 * using stdin/out/err is a problem, since in various modes we may
 * close those units (for example, in order to start the server with
 * rsh but have the rsh return when the server starts, it is necessary
 * to close stdin/out/err).  
 *
 * Thus, instead of relying on particular 
 * unit numbers for stdin/out/err, we set (change) these values.
 */

int stdin_fd	= 0;
FILE *stdin_fp	= 0; /* stdin; */
int stdout_fd	= 1;
FILE *stdout_fp	= 0; /* stdout; */
int stderr_fd	= 2;
FILE *stderr_fp	= 0; /* stderr; */

/* This should check that the pid received was the one expected... */
void reaper(int sigval)
{
    int pid;
#ifdef HAVE_UNION_WAIT
    union wait status;
#else
    int status;
#endif

#ifdef HAVE_WAIT3
    while ((pid = wait3(&status, WNOHANG, NULL)) > 0) ;
#else
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) ;
#endif
    if (pid < 0) { 
        /* dummy to make compiler happy about declaring pid */
	/* Note that since this is called in a signal handler, there isn't
	   much that we can do */
	fprintf( logfile_fp, "Error %d from wait in reaper (%s)\n", errno,
		 strerror(errno) );
    }
}

int main(int argc, char *argv[])
{
    int c;
    struct sockaddr_in name;
    p4_sockopt_len_t namelen;
    int pid;

    /* Initialize the FILE handles */
    stdin_fp	= stdin;
    stdout_fp	= stdout;
    stderr_fp	= stderr;

    daemon_pid = getpid();

    strcpy( fileport, "/tmp/servertest" );
    strcpy( server_apps_file, SERVER_APPS_FILE );

    if (getuid() == 0)
    {
	strcpy(logfile, LOGFILE);
	daemon_port = DEFAULT_PORT;
    }
    else
    {
	sprintf(logfile, "Secure_Server.Log.%d", (int)getpid());
	daemon_port = 0;
	debug = 1;
    }

    namelen = sizeof(name);
    /* AIX wants namelen to be size_t */
    if (getpeername(0, (struct sockaddr *) &name, &namelen) < 0)
	daemon_mode = 1;
    else
	daemon_mode = 0;

    /* Initialize SSL layer if available */
    Init_ssl();

    while ((c = getopt(argc, argv, "DdoPuhnp:l:s:f:a:w:")) != EOF)
    {
	switch (c)
	{
	case 'D':
	    debug++;
	    break;
	    
	case 'n':
	    /* For debugging, we never fork */
	    never_fork = 1;
	    break;

	case 'd':
	    daemon_mode++;
	    break;
	    
	case 'o':
	    /* Orphan mode; I'd use -detach, but old-fashioned getopt wants
	       single letter names */
	    daemon_mode++;
	    close(0);
	    close(1);
	    close(2);
	    stdfd_closed = 1;
	    pid = fork();
	    if (pid < 0) {
		/* We've closed stderr! */
		exit(1);
	    }
	    else if (pid > 0) exit(0);
	    /* We're the child, so we continue on */
	    daemon_pid = getpid();
	    break;
	case 'p':
	    daemon_port = atoi(optarg);
	    break;

	case 'l':
	    strcpy(logfile, optarg);
	    break;

	case 's':
	    if (Set_ssl_paths( optarg, argv[optind], argv[optind+1] )) {
		fprintf(stderr, "This server does not support SSL\n");
	    }
	    optind += 2;
	    break;

	case 'w':
	    if (chdir(optarg)) {
		fprintf( stderr, "Could not change directory to %s\n", 
			 optarg );
	    }
	    break;

	case 'P':
	    print_pid = 1;
	    break;

	case 'f':
	    strcpy(fileport, optarg);
	    use_local_port = 1;
	    break;

	case 'a':
	    strcpy( server_apps_file, optarg );
	    break;

	case '?':
	case 'u':
	case 'h':
	default:
	    fprintf(stderr, "\
Usage: %s [-d] [-D] [-p port] [-l logfile] [-o] [-s cert_file key_file key_password] [-P] [-f fileport] [-a appsfile] [-w server_dir]\n",argv[0]);
	    exit(1);
	}
    }

    Setup_ssl( );

    if ((logfile_fp = fopen(logfile, "a")) == NULL)
    {
	if (getuid() != 0)
	{
	    sendline("Cannot open logfile, disabling logging\n");
	    logfile_fp = fopen("/dev/null", "w");
	    
	}
	else
	{
	    fprintf( stderr, "Cannot open logfile %s: %s\n",
		    logfile, strerror(errno));
	    exit(1);
	}
    }
#ifndef IWAY
    else {
	if (!stdfd_closed)
	{
	    char tmp[1024];
	    sprintf(tmp, "Logging to %s\n", logfile);
	    sendline(tmp);
	}
    }
#endif

    logfile_fd = fileno( logfile_fp );

    setbuf(logfile_fp, NULL);
    /* 
     * ???
     *
     * Nexus also has stdout set to no buffering.  Was this added by
     * Nexus, or removed by p4.  Any reason why it shouldn't be in here?
     */
    
    fprintf( logfile_fp, "%s pid=%d starting at %s, logfile fd is %d\n",
	    argv[0], (int)getpid(), timestamp(), logfile_fd );
	     
    fflush( logfile_fp );

    if (stdfd_closed) {
	/* redirect stdout and stderr to logfile */
	dup2( logfile_fd, 1 );
	dup2( logfile_fd, 2 );
    }

    if (daemon_mode)
    {
	int lfd, /* lfd is listener fd */
	    fd;  /* fd is accepted connection fd */
	int local_lfd = -1;  /* Local (domain) listener fd, if enabled */

#ifdef HAVE_BSD_SIGNAL
	signal(SIGCHLD, reaper);
#else
	{
	    struct sigaction act;

	    act.sa_handler = reaper;
	    sigemptyset(&act.sa_mask);
	    sigaddset(&act.sa_mask, SIGCHLD);
	    act.sa_flags = 0;
	    sigaction(SIGCHLD, &act, NULL);
	}
#endif

	if (daemon_port == 0)
	{
	    net_setup_anon_listener(2, &daemon_port, &lfd);
	}
	else
	{
	    net_setup_listener(2, daemon_port, &lfd);
	}

	fprintf( logfile_fp, "Listening on port %d\n", daemon_port );

	if (use_local_port)
	    net_setup_local_listener( 2, &local_lfd, fileport );

#ifdef IWAY
	/* 
  	 * get my hostname, generate a token and output IWAY
         * authentication line including PID, if requested
         */
	seed = getpid();
	srand(seed);
	t = 0;
	while (t < 8) {
	    part = rand() % 128;
	    sprintf(token+t, "%c", part);
	    if (isalnum(token[t])) { t++; }
	}

	gethostname(ss_hostname, &ss_hostname_length);
	if (print_pid) {
	    char tmp[1024];

	    sprintf(tmp, "%s ss_port= %d ss_token=%s ss_pid=%d\n",
		ss_hostname, daemon_port, token, (int)getpid());
	    sendline(tmp);
	} else {
	    char tmp[1024];

	    sprintf(tmp, "%s ss_port=%d ss_token=%s\n",
		ss_hostname, daemon_port, token);
	    sendline(tmp);
	}
#else
	if (debug || (daemon_port != DEFAULT_PORT && !stdfd_closed))
	{
	    char tmp[1024];

	    sprintf(tmp, "Listening on %d\n", daemon_port);
	    sendline(tmp);
	}
#endif
	    
	if (!debug)
	{
	    /* Create an orphan process and exit.  This is for
	       root use only (debug set to 1 if getuid() != 0) 
	       NOTE: Since changes in handling of fd's, this code
	       may no longer work.
	     */
	    if (fork())
		exit(0);

	    for (fd = 0; fd < 10; fd++)
		if (fd != lfd && fd != logfile_fd)
		    close(fd);
	    
#if defined(P4SYSV) || defined(__hpux) || defined(CRAY)
	    fd = open ("/dev/console", O_RDWR);
	    if (fd < 0)
		fd = open ("/dev/tty", O_RDWR);
	    if (fd < 0)
		fd = open ("/dev/null", O_RDWR);
#    if defined(CRAY) || defined(__hpux)
	    (void) dup2(0, 1);
	    (void) dup2(0, 2);
#    else
	    (void) dup2(STDIN_FILENO, STDOUT_FILENO);
	    (void) dup2(STDIN_FILENO, STDERR_FILENO);
#    endif
#if defined(P4SYSV) && defined(SETPGRP_VOID)
	    (void) setpgrp();
#endif
#else
	    (void) open("/", 0);
	    (void) dup2(0, 1);
	    (void) dup2(0, 2);
#ifdef TIOCNOTTY
	    /* Cygwin, for example, doesn't define /dev or TIOCNOTTY */
	    fd = open("/dev/tty", O_RDWR);
	    if (fd >= 0) {
		ioctl(fd, TIOCNOTTY, 0);
		(void) close(fd);
	    }
#endif
#endif
	}

	while (1)
	{
	    /* Wait for a new connection attempt */
	    /* We'd actually like to accept commands from three types of
	       sources:
	       remote connection attempts through our advertised socket
	           (requires authentication of the remote party)
               local connection attemps through a Unix domain socket 
	           (authenticated through the file system)
               partner servers (use IP sequence counts; could use SSL
	           for better security)

	       local connection code is not fully implemented, but
	           the basic code is set up.
  	     */
	    struct timeval *timeoutp = 0;
	    fd_set         readfds, writefds, exceptfds;
	    int            nfds, max_fd;

	    FD_ZERO(&readfds);
	    FD_ZERO(&writefds);
	    FD_ZERO(&exceptfds);
	    FD_SET(lfd,&readfds);
	    max_fd = lfd;
	    if (local_lfd >= 0) {
		FD_SET(local_lfd,&readfds);
		if (local_lfd > max_fd) max_fd = local_lfd;
	    }
	    nfds = select( max_fd+1, &readfds, &writefds, &exceptfds, 
			   timeoutp );
	    if (nfds == -1) {
		switch (errno) {
		case EINTR:  /* interrupted system call */
		    break;
		case EBADF:  /* bad fd */
		    /* This will cause all selects to fail until cleaned up */
		    return -1;
		    
		case EFAULT: /* bad pointer */
		    return -1;
		    
		case EINVAL: /* time out-of-range (tv_usec < 0 or tv_sec > 10^8 
				or tv_usec > 10^6) */
		    return -1;
		    
		default: 
		    /* Unrecognized error */
		    return -1;
		}
		continue;
	    }
	    /* Invoke handler for active fd */
	    if (FD_ISSET(lfd,&readfds)) {
		fd = net_accept(lfd);
		handle_remote_conn( lfd, fd );
		close( fd );
	    }
	    if (local_lfd >= 0 && FD_ISSET(local_lfd,&readfds)) {
		fd = net_accept(local_lfd);
		handle_local_conn( lfd, fd );
		close( fd );
	    }
	    /* 
	       check on partner fd's for connections to other demons
	       ...
	     */

	    /* We can't close the new fd until we're sure that the fork has 
	       successfully started */
	    /* What we REALLY want to do is close the fd when the child exits
	     */
	    /* sleep(2); */
	    /* close(fd); */
	}
    }
    else
    {
	doit(0,0);
    }

    return 0;	
}

/* This is called (possibly in a subprocess) to process create p4-process
 * requests.
 *
 * It undertakes many jobs:
 *   Validate user if remote connection
 *   
 * See Process_pgm_commands for handling the input of commands
 */
void doit( fd, is_local )
int fd, is_local;
{
    struct sockaddr_in name;
    p4_sockopt_len_t namelen;
    struct hostent *hp;
    struct passwd *pw;
    char client_user[80], server_user[80];
    int valid;
    char *user_home;
#ifdef IWAY
    char user_token[1024];
#else
    int superuser;
#endif
#ifdef HAVE_KERBEROS
    int reason;
#endif

    this_uid = getuid();
    pw = getpwuid(this_uid);
    if (pw == NULL)
    {
	fprintf( logfile_fp, "Cannot get pw entry for user %d\n", this_uid);
	exit(1);
    }
    this_username = save_string(pw->pw_name);

    if (this_uid != 0)
	fprintf( logfile_fp, "WARNING: Not run as root\n");

    setbuf(stdout_fp, NULL);

    fprintf( logfile_fp, "Got connection at %s", timestamp());

    if (is_local) {
	hp = 0;
	fromhost = "Local socket";
    }
    else {
	namelen = sizeof(name);
	
	/* AIX wants namelen to be size_t */
	if (getpeername(fd, (struct sockaddr *) &name, &namelen) != 0)
	{
	    fprintf( logfile_fp, "getpeername failed: %s\n",
		     strerror(errno));
	    exit(1);
	}

	fromhost = inet_ntoa(name.sin_addr);
	
	hp = gethostbyaddr((char *) &name.sin_addr,
			   sizeof(name.sin_addr),
			   (int) name.sin_family);
	if (hp == NULL)
	    failure2("Cannot get remote address for %s", fromhost);

	fromhost = hp->h_name;
    }

    /* Start processing the command prototcol.  Either a 
       client user name, or %ssl to indicate a switch to secure sockets 
     */
    if (!getline(client_user, sizeof(client_user)))
	failure("No client user");
    
    if (strcmp(client_user, "%ssl") == 0)
    {
	if (Create_ssl_handle( )) {
	    failure("SSL not supported on this server");
	}
    }

    if (!getline(server_user, sizeof(server_user)))
	failure("No server user");

#ifndef IWAY
    pw = getpwnam(server_user);
    if (pw == NULL)
	failure2("No such user: %s\n", server_user);

    if (this_uid != 0 && this_uid != pw->pw_uid)
	failure2("Server is not running as root. Only %s can start processes\n",
		 this_username);

    user_home = pw->pw_dir;
    superuser = (pw->pw_uid == 0);

    if (is_local)
	valid = 0;
    else {
	fprintf( logfile_fp, "Starting ruserok at %s\n", timestamp() );
	valid = ruserok(fromhost, superuser, client_user, server_user);
	fprintf( logfile_fp, "Completed ruserok at %s (valid = %d)\n", 
		 timestamp(), valid );
    }

    if (valid != 0)
    {
	char user_pw[80];
	char *xpw;
	
	error_check( sendline("Password\n"), "Password request" );
	if (!getline(user_pw, sizeof(user_pw)))
	    failure("No server user");

#ifdef HAVE_KERBEROS
	if (ka_UserAuthenticateGeneral(KA_USERAUTH_VERSION,
				       client_user, "", "", user_pw,
				       0, 0, 0, &reason))
	{
	    failure("Invalid password");
	}
	/* 
  	 * This stupid hack goes here because ka_UserAuthenticateGeneral()
         * destroys my data structure without notifying me about it!!!
         */
	hp = gethostbyaddr((char *)&name.sin_addr,
			   sizeof(name.sin_addr),
			   (int)name.sin_family);
#else /* HAVE_KERBEROS */
	xpw = crypt(user_pw, pw->pw_passwd);
	if (strcmp(pw->pw_passwd, xpw) != 0)
	    failure("Invalid password");
#endif
    }
#else /* IWAY */
    error_check( sendline("Password\n"), "Password request" );
    if (!getline(user_token, sizeof(user_token)))
    {
	failure("No user token");
    }
    if (strcmp(user_token, token) != 0)
    {
	failure("Token does not match");
    }
#endif
    error_check( sendline("Proceed-2\n"), "Proceed in doit" );

    sprintf(tmpbuf, "authenticated client_id=%s server_id=%s\n",
	    client_user, server_user);
    notice(tmpbuf);

    /* 
       At this point, we have an authenticated user.  Now we can accept 
       commands.
     */
    Process_pgm_commands( client_user, server_user, user_home, pw->pw_uid, 
			  pw->pw_gid, hp, is_local );
}

/*
 * This routine processes the actual commands.  They are
 *    %id  - Give id and exit
 *    %exit - Force server to exit

 *    %dir\ndirectory
 *    %env\nenvironment info
 *    %run
 *    pgm
 *    pgm_args (blank separated!)
 *    port for stdout connection back at partner, if -1, use existing 
 *    connection
 *
 *    environment info is in the following form:
 *    n\n   (number of variables sent)
 *    #chars\n
 *    variablename=value\n
 *    ...
 */
int Process_pgm_commands( client_user, server_user, user_home, uid, gid, hp, 
			  is_local )
char *client_user, *server_user, *user_home;
int  uid, gid, is_local;
struct hostent *hp;
{
    char dir[1024], pgm[1024], pgm_args[1024];
    int i;
    int cd_notifier = 0;
    int env_notifier = 0;
    char **env;
    int n_env = 0;
    int complete;
    int stdout_port;
    char stdout_port_str[1024];

    /* Get the program to execute */
    if (!getline(pgm, sizeof(pgm)))
	failure("No pgm");

    /* 
     * For backwards compatibility with Nexus v3.0, we need to do these
     * checks.  These can disappear when that version is no longer in use.
     */
    if (strcmp(pgm, SERVER_CD_NOTIFIER) == 0)
    {
	cd_notifier = 1;
	notice("Got CD_NOTIFIER");
	if (!getline(dir, sizeof(dir)))
	{
	    failure("No working directory");
	}
	if (!getline(pgm, sizeof(pgm)))
	{
	    failure("No program after directory");
	}
	if (strcmp(pgm, SERVER_ENV_NOTIFIER) == 0)
	{
	    env_notifier = 1;
	    notice("Got ENV_NOTIFIER");
	    get_environment(&env, &n_env);
	    if (!getline(pgm, sizeof(pgm)))
	    {
		failure("No program after directory and environment");
	    }
	}
    }
    else if (strcmp(pgm, SERVER_ENV_NOTIFIER) == 0)
    {
        env_notifier = 1;
	notice("Got ENV_NOTIFIER");
	get_environment(&env, &n_env);
	if (!getline(pgm, sizeof(pgm)))
	{
	    failure("No program after environment");
	}
    }

    /* 
     * End of Nexus compatibility section
     */

    /* Check for key words:
       %id (give id)
       %dir (get starting directory)
       %env (get starting environment)
       %exit (exit)
       %run (run program)
         pgm
	 args
	 port or host:port; -1 means keep current port.
     */
    complete = 0;
    while (!complete)
    {
        if (strcmp(pgm, "%id" ) == 0) {
	    char tmp[1024];

	    sprintf(tmp, "Server-2: Port %d for client %s and server user %s\n",
	        daemon_port, client_user, server_user );
	    sendline(tmp);
	    notice("received %id token");
	    exit(0);
        }
        else if (strcmp(pgm, "%run" ) == 0) {
	    /* Just get the program */
	    notice("received %run token");
	    complete = 1;
        }
        else if (strcmp(pgm, "%dir") == 0) {
	    cd_notifier = 1;
	    if (!getline(dir, sizeof(dir)))
	    {
	        failure("No working directory");
	    }
	    notice("received %dir token");
        }
        else if (strcmp(pgm, "%env") == 0) {
	    env_notifier = 1;
	    get_environment(&env, &n_env);
	    notice("received %env token");
        }
        else if (strcmp( pgm, "%exit" ) == 0) {
	    /* Remove local name if present */
	    if (use_local_port) {
		unlink( fileport );
	    }
	    notice("received %exit token");
	    kill( daemon_pid, SIGINT );
	    sleep(1);
	    kill( daemon_pid, SIGQUIT );
	    exit(1);
        }
	else {
	    /* 
      	     * There is no match, so this is an older protocol that
             * directly sent the program without the %run notice.  The
             * next item in the socket will be program arguments, so
             * we are done with this loop.
             */
	    complete = 1;
	    break;
	}
        if (!getline(pgm, sizeof(pgm)))
	{
	    failure("Missing token");
	}
    }

    if (!getline(pgm_args, sizeof(pgm_args)))
	failure("No pgm args");

    notice2("dir = %s", cd_notifier ? dir : "(none)");
    notice2("pgm = %s", pgm);
    notice2("got args `%s'", pgm_args);

    if (!cd_notifier && pgm[0] != '/')
	failure2("%s is not a full pathname", pgm);

    if (this_uid == 0)
    {
#if defined(HAVE_SETEGID)
	if (setegid(gid) != 0) 
	    failure2("setegid failed: %s", strerror(errno));
#else
	failure("No way to set egid!");
#endif
#if defined(HAVE_SETEUID) 
	if (seteuid(uid) != 0)
	    failure2("seteuid failed: %s", strerror(errno));
#elif defined(HAVE_SETRESUID)
	if (setresuid(-1, uid, -1) != 0)
	    failure2("setresuid failed: %s", strerror(errno));
#else
        failure("No way to set euid!");
#endif
    }
    
#ifndef IWAY
    if (!is_local && !check_allowed_file( pgm, user_home, 
					  cd_notifier ? dir : NULL )) {
	exit( 1 );
    }
#endif /* IWAY */

    /*********/
    if (!getline(stdout_port_str, sizeof(stdout_port_str)))
	failure("No stdout");

    /* check if it is a port or a host:port */
    for (i = 0;
	 (stdout_port_str[i] != '\0') && (stdout_port_str[i] != ':');
	 i++)
	 /* go to next letter */ ;
    
    notice2("checked stdout port string %s", stdout_port_str);
    if (stdout_port_str[i] == ':')
    {
	stdout_port = atoi(&(stdout_port_str[i+1]));
	stdout_port_str[i] = '\0';
	hp = gethostbyname(stdout_port_str);
    }
    else
    {
	stdout_port = atoi(stdout_port_str);
    }

    notice2("got stdout_port %d", stdout_port);
    /*********/

    notice3("executing %s %s", pgm, pgm_args);

    execute(cd_notifier ? dir : NULL,
	    env_notifier ? env : NULL,
	    n_env, pgm, pgm_args, uid, gid, stdout_port, hp);

    return 0;
}

void execute(dir, env, n_env, pgm, pgm_args, uid, gid, stdout_port, hp)
char *dir;
char **env;
int n_env;
char *pgm, *pgm_args;
int uid, gid, stdout_port;
struct hostent *hp;
{
    int p[2];
    int rd, wr;
    int pid, n;
    char *args[MAXARGS];
    int nargs;
    char *s, *end;
    int i;
    char buf[1024];
    int new_stdout_fd;
    char tempbuf[100];
    char quote;

#ifdef DEBUG
    if (n_env > 0)
    {
	for (i = 0; i < n_env; i++)
	{
	    notice3("env[%d] is \"%s\"", i, (env)[i]);
	}
    }
#endif

    s = pgm_args;
    while (*s && isspace(*s))
	s++;

    args[0] = pgm;

    nargs = 1;
    quote = 0;
    while (*s)
    {
	args[nargs] = s;

	if (*s == '\"')         /* " */
	{
	    args[nargs] = ++s;
	    quote = !quote;
	}

	while (*s && (!isspace(*s) || quote))
	{
	    if (*s == '\"')      /* " */
	    {
		quote = !quote;
	    }
	    s++;
	}

	end = s;
	if (*(end-1) == '\"')     /* " */
	{
	    end--;
	}

	while (*s && isspace(*s))
	    s++;

	*end = 0;
	nargs++;
	if (nargs + 1>= MAXARGS)
	    failure("Too many arguments to pgm");
    }

    args[nargs] = NULL;

    if (pipe(p) != 0)
	failure2("Cannot create pipe: %s", strerror(errno));

    rd = p[0];
    wr = p[1];

    if (fcntl(wr, F_SETFD, 1) != 0)
	failure2("fcntl F_SETFD failed: %s", strerror(errno));

    if (this_uid == 0)
    {
#if HAVE_SETRESGID
	if (setresgid(gid, gid, -1) != 0)
	    failure2("cannot setresgid: %s", strerror(errno));
#else
	failure("No way to set gid!");
#endif
#if HAVE_SETRESUID
	if (setresuid(uid, uid, -1) != 0)
	    failure2("cannot setresuid: %s", strerror(errno));
#else
	if (seteuid(0) != 0)
	    failure2("cannot seteuid: %s", strerror(errno));
#ifndef HAVE_SETREUID
	if (setuid(uid) != 0)
	    failure2("cannot setuid: %s", strerror(errno));
#else	
	if (setreuid(uid, uid) != 0)
	    failure2("cannot setreuid: %s", strerror(errno));
#endif
#endif
    }
    
    pid = fork();
    if (pid < 0)
	failure2("fork failed: %s", strerror(errno));

    if (pid == 0)
    {
	close(rd);

	close(0);
	open("/dev/null", O_RDONLY);

	if (stdout_port >= 0) {
	    new_stdout_fd = connect_to_listener(hp,stdout_port, wr);
	    notice2("stdout_fd=%d", new_stdout_fd);
	}
	else
	    new_stdout_fd = stdout_fd;

	if (new_stdout_fd != 1) {
	    close(1);
	    dup(new_stdout_fd);
	}
	/* open("/dev/null", O_WRONLY); */

	if (new_stdout_fd != 2) {
	    close(2);
	    dup(new_stdout_fd);
	}
	/* open("/dev/null", O_WRONLY); */

	/*****
	strcpy(tempbuf,"writing this to stdout_fd");
	write(new_stdout_fd,tempbuf,strlen(tempbuf)+1);
	strcpy(tempbuf,"writing this to real stdout");
	write(new_stdout_fd,tempbuf,strlen(tempbuf)+1);
	*****/

	if (dir)
	{
	    if (chdir(dir))
	    {
		sprintf(tmpbuf, "chdir(%s) failed: %s\n",
		    dir, strerror(errno));
		write(wr, tmpbuf, strlen(tmpbuf));
		exit(0);
	    }
	}

	if (n_env > 0)
	{
	    /* Some of the environment (in env) can affect whether the
	       program can start *at all* .  For example, for a 
	       program built with shared libraries, we may need
	       LD_LIBRARY_PATH in *our* environment before exec'ing the
	       program.  For now, we look for LD_ in the transmitted
	       environment and putenv those values before executing 
	       execve */
	    for (i=0; i<n_env; i++) {
		if (strncmp( "LD_", env[i], 3 ) == 0) {
		    putenv( env[i] );
		    notice2("set env %s", env[i] );
		}
	    }
	    i = execve(pgm, args, env);
	}
	else
	{
	    i = execv(pgm, args);
	}
	if (i != 0)
	{
	    sprintf(tmpbuf, "Exec failed: %s\n", strerror(errno));
	    write(wr, tmpbuf, strlen(tmpbuf));
	    exit(0);
	}
    }

    close(wr);

    if ((n = read(rd, buf, sizeof(buf))) > 0)
    {
	buf[n] = 0;
	s = index(buf, '\n');
	if (s)
	    *s = 0;
	
	failure2("child failed: %s", buf);
    }
    sprintf(tempbuf, "Success: Child %d started\n", (int)pid);
    sendline(tempbuf);
    notice2("Child %d started", pid);
}

void get_environment(char ***env, int *n_env_p)
{
    extern char **environ;
    int environ_count;
    int env_count, n_env;
    char s[10];
    int i;

    for (environ_count = 0; environ[environ_count] != NULL; environ_count++)
	/* continue counting */ ;
    
    if (!getline(s, sizeof(s)))
    {
	failure("No environment count");
    }
    env_count = atoi(s);
    /* This is the max possible (see below for possible reductions) */
    n_env = env_count + environ_count;
    notice2("Got %d environment variables", env_count);
    *env = (char **)malloc(sizeof(char *) * (n_env + 1));
    if (!(*env))
    {
	failure("Unable to allocate envrionment space");
    }
    for (i = 0; i < env_count; i++)
    {
	if (!getline(s, sizeof(s)))
	{
	    failure2("No size for env[%d]", i);
	}
	(*env)[i] = (char *)malloc(sizeof(char) * (atoi(s) + 2));
	if (!(*env)[i])
	{
	    failure2("Unable to allocate env[%d]", i);
	}
	if (!getline((*env)[i], atoi(s)+2))
	{
	    failure2("No element for env[%d]", i);
	}
    }
    /* Here's a problem: we want the transmitted environment to
       override the existing environment.  This is particularly 
       critical for things like LD_LIBRARY_PATH.
       
       Since the environment is often small, we do this with a very 
       simple n*n algorithm.  A better version would sort the two
       environment lists and then merge them; that can be done in linear time 
       (using lexigraphic sorts)
    */
    n_env = env_count;
    for (i = 0; i < environ_count; i++)
    {
	int j, namelen;
	for (j=0; j<env_count; j++) {
	    namelen = strchr( (*env)[j], '=' ) - (*env)[j];
	    if (strncmp( (*env)[j], environ[i], namelen ) == 0) {
		break;   /* Found! */
	    }
	}
	if (j == env_count)
	    (*env)[n_env++] = environ[i];
    }
    (*env)[n_env] = NULL;
    *n_env_p = n_env;
}

int sendline(str)
char *str;
{
    int rc;

#ifdef HAVE_SSL
    if (ssl_mode)
    {
	rc = SSL_Write(ssl_handle, str, strlen(str));
    }
    else /* ssl_mode == 0 */
#endif
    {
	rc = fprintf(stdout_fp, str);
	/* 
  	 * Make sure str gets sent to the client and not buffered on
         * this side.
         */
	fflush(stdout_fp);
    }

    return rc;
}

int getline(str, len)
char *str;
int len;
{
    char *s;
    
#ifdef HAVE_SSL
    if (ssl_mode)
    {
	int rc;

	rc = SSL_Read(ssl_handle, str, len);
	if (rc <= 0)
	{
	    return 0;
	}
    }
    else /* ssl_mode == 0 */
#endif
    {
        if (fgets(str,  len, stdin_fp) == NULL)
	    return 0;
    }

    if ((s = index(str, '\n')) != NULL)
	*s = 0;
    if ((s = index(str, '\r')) != NULL)
	*s = 0;

    /* notice(str); */

    return 1;
}
    

void failure(s)
char *s;
{
    char tmp[1024];

    sprintf(tmp, "Failure <%s>: %s\n", fromhost, s);
    sendline(tmp);
    fprintf(logfile_fp, tmp);
    fflush(logfile_fp);
    exit(1);
}

void notice(s)
char *s;
{
    fprintf( logfile_fp, "Notice <%s>: %s\n", fromhost, s);
    fflush(logfile_fp);
}


/*
  Accept a connection on socket skt and return fd of new connection.
 */
int net_accept(int skt)
{
    struct sockaddr_in from;
    p4_sockopt_len_t fromlen;
    int skt2;
    int gotit;

    fromlen = sizeof(from);
    gotit = 0;
    while (!gotit)
    {
	/* AIX wants fromlen to be size_t */
	skt2 = accept(skt, (struct sockaddr *) &from, &fromlen);
	if (skt2 == -1)
	{
	    if (errno == EINTR)
		continue;
	    else
		error_check(skt2, "net_accept accept");
	}
	else
	    gotit = 1;
    }

    return(skt2);
}

void net_setup_listener(backlog, port, skt)
int backlog, port, *skt;
{
struct sockaddr_in sin;

    *skt = socket(AF_INET, SOCK_STREAM, 0);

    error_check(*skt,"net_setup_listener socket");

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);

    error_check(bind(*skt,(struct sockaddr *) &sin,sizeof(sin)),
		   "net_setup_listener bind");


    error_check(listen(*skt, backlog), "net_setup_listener listen");
}

void net_setup_local_listener(backlog, skt, server_path )
int backlog, *skt;
char *server_path;
{
    struct sockaddr_un sa;

    bzero( &sa, sizeof(sa) );
    sa.sun_family = AF_UNIX;
    strncpy( sa.sun_path, server_path, sizeof(sa.sun_path) - 1 );

    *skt = socket(AF_UNIX, SOCK_STREAM, 0 );

    error_check(*skt,"net_setup_local_listener socket");

    error_check(bind(*skt,(struct sockaddr *) &sa,sizeof(sa)),
		   "net_setup_local_listener bind");

    error_check(listen(*skt, backlog), "net_setup_local_listener listen");

    /* ? need to save sa, AF_UNIX? */
}

void net_setup_anon_listener(backlog, port, skt)
int backlog, *port, *skt;
{
#ifdef USE_SIZE_T_FOR_SOCKLEN_T
    size_t sinlen;
#elif defined(USE_SOCKLEN_T)
    socklen_t sinlen;
#else
    int sinlen;
#endif
    struct sockaddr_in sin;

    *skt = socket(AF_INET, SOCK_STREAM, 0);

    error_check(*skt,"net_setup_anon_listener socket");

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(0);

    sinlen = sizeof(sin);

    error_check(bind(*skt,(struct sockaddr *) &sin,sizeof(sin)),
		   "net_setup_anon_listener bind");


    error_check(listen(*skt, backlog), "net_setup_anon_listener listen");

    /* AIX wants sinlen to be size_t */
    getsockname(*skt, (struct sockaddr *) &sin, &sinlen);
    *port = ntohs(sin.sin_port);
}

void error_check(val, str)
int val;
char *str;
{
    if (val < 0)
    {
	fprintf(logfile_fp, "%s: %s\n",
		str,
		strerror(errno));
	exit(1);
    }
}

char *timestamp()
{
    /* This used to be long.  If this causes a problem, create a
       test for time_t vs long for the result from time and the
       input to localtime */
    time_t clock;
    struct tm *tmp;

    clock = time((time_t *)NULL);
    tmp = localtime(&clock);
    return asctime(tmp);
}

char *save_string(s)
char *s;
{
    char *rc = (char *) malloc(strlen(s) + 1);
    strcpy(rc, s);
    return rc;
}

#ifndef HAVE_GETOPT
/* This is from the released BSD sources lib/libc/getopt.c */
/*
 * Copyright (c) 1987 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#define	BADCH	(int)'?'
#define	EMSG	""

int
getopt(nargc, nargv, ostr)
	int nargc;
	char **nargv;
	char *ostr;
{
	static char *place = EMSG;		/* option letter processing */
	register char *oli;			/* option letter list index */
	char *p;

	if (!*place) {				/* update scanning pointer */
		if (optind >= nargc || *(place = nargv[optind]) != '-') {
			place = EMSG;
			return(EOF);
		}
		if (place[1] && *++place == '-') {	/* found "--" */
			++optind;
			place = EMSG;
			return(EOF);
		}
	}					/* option letter okay? */
	if ((optopt = (int)*place++) == (int)':' ||
	    !(oli = index(ostr, optopt))) {
		/*
		 * if the user didn't specify '-' as an option,
		 * assume it means EOF.
		 */
		if (optopt == (int)'-')
			return(EOF);
		if (!*place)
			++optind;
		if (opterr) {
			if (!(p = rindex(*nargv, '/')))
				p = *nargv;
			else
				++p;
			(void)fprintf(stderr, "%s: illegal option -- %c\n",
			    p, optopt);
		}
		return(BADCH);
	}
	if (*++oli != ':') {			/* don't need argument */
		optarg = NULL;
		if (!*place)
			++optind;
	}
	else {					/* need an argument */
		if (*place)			/* no white space */
			optarg = place;
		else if (nargc <= ++optind) {	/* no arg */
			place = EMSG;
			if (!(p = rindex(*nargv, '/')))
				p = *nargv;
			else
				++p;
			if (opterr)
				(void)fprintf(stderr,
				    "%s: option requires an argument -- %c\n",
				    p, optopt);
			return(BADCH);
		}
	 	else				/* white space */
			optarg = nargv[optind];
		place = EMSG;
		++optind;
	}
	return(optopt);				/* dump back option letter */
}

#endif

static int connect_to_listener(struct hostent *hp,int stdout_port, 
			       int pipe_out)
{
    int conn;
    int rc;
    struct sockaddr_in addr;
    char tmpbuf[1024];

    conn = socket(AF_INET, SOCK_STREAM, 0);
    if (conn < 0)
    {
	sprintf(tmpbuf, "connect_to_listener: socket failed");
	write(pipe_out, tmpbuf, strlen(tmpbuf));
	exit(1);
    }

    addr.sin_family = hp->h_addrtype;
    addr.sin_port = htons(stdout_port);
    memcpy(&addr.sin_addr, hp->h_addr, hp->h_length);

    rc = connect(conn, (struct sockaddr *) & addr, sizeof(addr));
    if (rc < 0)
    {
	notice2("connect_to_listener: errno = %d", errno);
	sprintf(tmpbuf, "connect_to_listener: connect failed");
	write(pipe_out, tmpbuf, strlen(tmpbuf));
	exit(1);
    }

    return conn;
}

/* Create a process to handle commands.  This does such things as setting
   the process group and redirecting output?

   Returns pid of created process, 0 for child, and < 0 on error.

   When debugging, this can return pid == 0 WITHOUT forking; allowing
   the server to sit in a single process.  This is useful ONLY for
   debugging.
 */
int create_process_session( lfd, fd, o_fd )
int lfd, fd, *o_fd;
{
    int pid;

    if (never_fork) return 0;

    pid = fork();

    if (pid < 0)
    {
	fprintf( logfile_fp, "Fork failed: %s\n",
		 strerror(errno));
	exit(pid);
    }
    if (pid == 0)
    {
#ifndef HAVE_SETPGRP
	int ttyfd;
#endif
	fprintf( logfile_fp, 
		 "Started subprocess for connection at %s with pid %d\n", 
		 timestamp(), (int)getpid() );
#if HAVE_SETPGRP
	/* This is correct ONLY for the SYSV setpgrp */
#if defined(P4SYSV) || defined(SETPGRP_VOID)
	(void) setpgrp();
#else
	/* We could try setsid() instead ... */
	(void) setpgrp(0,0);
#endif /* P4SYSV */
#else
	ttyfd = open("/dev/tty",O_RDWR);
	if (ttyfd >= 0)
	{
#    if !defined(CRAY)
	    ioctl(ttyfd, TIOCNOTTY, 0);
#    endif
	    close(ttyfd);
	}
#endif
	/* Make stdin/stdout refer to fd */
	if (stdfd_closed) {
	    stdin_fp  = fdopen( fd, "r" );
	    stdout_fp = fdopen( fd, "a" );
	    stderr_fp = logfile_fp;
	    if (stdin_fp == NULL || stdout_fp == NULL) {
		fprintf( logfile_fp, 
			 "Could not fdopen stdin or out\n" );
		exit(1);
	    }
	    close(lfd);
	    *o_fd = fd;
		
	}
	else {
	    close(0);
	    dup2(fd, 0);
	    close(1);
	    dup2(fd, 1);
	    close(2);
	    dup2( logfile_fd, 2);
	    close(lfd);
		
	    *o_fd = 0;
	}
    }
    return pid;
}

int handle_remote_conn( lfd, fd )
int lfd, fd;
{
    int pid, o_fd;

    pid = create_process_session( lfd, fd, &o_fd );

    if (pid == 0)
    {
	doit( o_fd, 0 );
	if (never_fork == 0) exit(0);
    }
    return 0;
}

int handle_local_conn( lfd, fd )
int lfd, fd;
{
    int pid, o_fd;

    pid = create_process_session( lfd, fd, &o_fd );
    if (pid == 0) {
	doit( o_fd, 1 );
	if (never_fork == 0) exit(0);
    }
    return 0;
}

/*
 * Check that the program is listed in the server-specified apps file
 * This file must have one executable per line, be readable only by
 * the owner, and have full pathnames (no relative paths).
 */
int check_allowed_file( pgm, user_home, dir )
char *pgm, *user_home, *dir;
{
    struct stat statbuf, statbuf_pgm, statbuf_apps_entry;
    char filename[1024];
    char fullpgm[1024];
    char progline[1024];
    FILE *fp;
    int valid;

    /* Do not need execute test on local connections (?) */
    sprintf(filename, server_apps_file, user_home);
    valid = 0;
    notice2("looking for files in %s", filename);
    
    fp = fopen(filename,"r");
	
    if (fp != (FILE *) NULL)
    {
	char *s1, *s2;
	    
#ifndef HAVE_AFS
	if (fstat( fileno(fp), &statbuf) != 0)
	    failure2("cannot stat %s", filename);
	    
	if (statbuf.st_mode & 077)
	    failure2("server appsfile %s readable by others",filename);
#else
/* 
 * We should really check the acl, but I have no idea how to do that on
 * an AFS machine.
 */
#endif

	if (dir && pgm[0] != '/')
	{
	    sprintf(fullpgm, "%s/%s", dir, pgm);
	}
	else
	{
	    sprintf(fullpgm, "%s", pgm);
	}
	    
	notice2("Trying to find program %s\n", fullpgm);
    restart_read:
	errno = 0; /* ensure that it isn't EINTR from some previous event */
	while (fgets(progline, sizeof(progline), fp) != NULL)
	{
	    s1 = progline;
	    while (*s1 && isspace(*s1))
		s1++;
	    if (*s1 == '\0' || *s1 == '#')
		continue;
		
	    s2 = s1;
	    while (*s2 && !isspace(*s2))
		s2++;
	    *s2 = 0;
	    /* notice2("Checking: %s\n", progline); */
	    if (   strcmp(fullpgm, s1) == 0
		   || strcmp(pgm, s1) == 0)
	    {
		valid = 1;
		break;
	    }
	    else
	    {
		/* 
		 * This needs directory here so that the relative path
		 * stuff works.
		 */
		if (stat(fullpgm, &statbuf_pgm) != 0)
		    continue;
		if (!(statbuf_pgm.st_mode & 0111))
		    failure2("Cannot execute %s", fullpgm);
		if (stat(s1, &statbuf_apps_entry) != 0)
		    continue;
		if (statbuf_pgm.st_ino == statbuf_apps_entry.st_ino)
		    valid = 1;
	    }
	}
	/* Fix for fgets fails because of interrupted system call */
	if ((errno == EINTR) || (errno == EAGAIN))
	    goto restart_read;
	fclose(fp);
    }

    if (!valid)
	failure3("Invalid program %s: file is not accessible or is not in server apps file %s", pgm,filename);
    
    return valid;
}

