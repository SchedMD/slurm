#include "p4.h"
#include "p4_sys.h"

/****
#include <stdio.h>
#include <ctype.h>
#include <pwd.h>
#include <errno.h>
#include <signal.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
****/

#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#ifdef _AIX
#include <sys/wait.h>
#endif


extern char *inet_ntoa ( struct in_addr );  /* from <arpa/inet.h> */

#ifdef FOO
/* These are in p4_sys.h, done better */
#ifdef P4BSD
#include <strings.h>
#endif

#ifdef P4SYSV
#include <string.h>
#endif
#endif

#if defined(SYMMETRY)
#define NEED_GETOPT
#endif

#ifdef NEED_GETOPT

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

#endif

#define MAXARGS 256

#ifndef LOGFILE
#define LOGFILE "/usr/adm/serv_p4.log"
#endif

#define notice2(a,b) {sprintf(tmpbuf, a, b); notice(tmpbuf);}
#define notice3(a,b,c) {sprintf(tmpbuf, a, b,c); notice(tmpbuf);}
#define failure2(a,b) {sprintf(tmpbuf, a, b); failure(tmpbuf);}

/* extern char *crypt ( const char *, const char * ); */
extern char *crypt();
#ifndef HAVE_STRERROR
extern char *sys_errlist[];
#define strerror(n) sys_errlist[n]
#endif
extern int errno;

char tmpbuf[1024];
char *fromhost;

char logfile[1024];
FILE *logfile_fp;
int   logfile_fd;

#define DEFAULT_PORT 753

int daemon_mode;
int daemon_port;
int daemon_pid;     /* pid of parent */
int stdfd_closed = 0;
int debug = 0;

char *this_username;
int this_uid;

void doit                    (int);
void execute                 ( char *, char *, int, int, struct hostent * );
int getline                  ( char *, int );
void failure                 ( char * );
void notice                  ( char * );
int net_accept               ( int );
void net_setup_listener      ( int, int, int * );
void net_setup_anon_listener ( int, int *, int *);
void error_check             ( int, char * );
char *timestamp              ( void );

char *save_string            ( char * );
static int connect_to_listener ( struct hostent *, int );
void reaper ( int );
int main ( int, char ** );

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
FILE *stdin_fp  = 0; /*	= stdin; */
int stdout_fd	= 1;
FILE *stdout_fp = 0; /*	= stdout; */
int stderr_fd	= 2;
FILE *stderr_fp = 0; /*	= stderr; */

void reaper(sigval)
int sigval;
{
    int i;
    /* For systems where signal does not work (signals one-shot),
       we need to reinstall the handler.  This isn't reliable, since there
       is a race condition, but it is a small exposure 
       
       Later versions will use sigaction where available.
     */
    signal(SIGCHLD, reaper);
    wait(&i);
}

int main(argc, argv)
int argc;
char **argv;
{
    int c;
    struct sockaddr_in name;
    int namelen;
    int pid;

    /* Initialize the FILE handles */
    stdin_fp	= stdin;
    stdout_fp	= stdout;
    stderr_fp	= stderr;

    daemon_pid = getpid();

    if (getuid() == 0)
    {
	strcpy(logfile, LOGFILE);
	daemon_port = DEFAULT_PORT;
    }
    else
    {
	sprintf(logfile, "P4Server.Log.%d", (int)getpid());
	daemon_port = 0;
	debug = 1;
    }

    namelen = sizeof(name);
    if (getpeername(0, (struct sockaddr *) &name, &namelen) < 0)
	daemon_mode = 1;
    else
	daemon_mode = 0;
    
    while ((c = getopt(argc, argv, "Ddop:l:")) != EOF)
    {
	switch (c)
	{
	case 'D':
	    debug++;
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

	case '?':
	default:
	    fprintf(stderr, "\
Usage: %s [-d] [-D] [-p port] [-l logfile] [-o]\n",argv[0]);
	    exit(1);
	}
    }

    if ((logfile_fp = fopen(logfile, "a")) == NULL)
    {
	if (getuid() != 0)
	{
	    fprintf( stdout_fp, "Cannot open logfile, disabling logging\n");
	    logfile_fp = fopen("/dev/null", "w");
	    
	}
	else
	{
	    fprintf( stderr, "Cannot open logfile %s: %s\n",
		    logfile, strerror(errno));
	    exit(1);
	}
    }
    else {
	if (!stdfd_closed)
	    fprintf( stdout_fp, "Logging to %s\n", logfile);
    }
    logfile_fd = fileno( logfile_fp );

    /* Note that this may not set no buffering, depending on the flavor of
       Unix */
    setbuf(logfile_fp, NULL);
    
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
	    fd,  /* fd is accepted connection fd */
            pid; /* pid of child when forking a process to handle connection */

	signal(SIGCHLD, reaper);

	if (daemon_port == 0)
	{
	    net_setup_anon_listener(2, &daemon_port, &lfd);
	}
	else
	{
	    net_setup_listener(2, daemon_port, &lfd);
	}

	fprintf( logfile_fp, "Listening on port %d\n", daemon_port );

	if ((debug || daemon_port != DEFAULT_PORT) && !stdfd_closed)
	    fprintf( stdout_fp, "Listening on %d\n", daemon_port);
	    
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
	    
#if defined(P4SYSV) || !defined(TIOCNOTTY)
	    fd = open ("/dev/console", O_RDWR);
	    if (fd < 0)
		fd = open ("/dev/tty", O_RDWR);
	    if (fd < 0)
		fd = open ("/dev/null", O_RDWR);
#    if defined(CRAY)
	    (void) dup2(0, 1);
	    (void) dup2(0, 2);
#    else
	    (void) dup2(STDIN_FILENO, STDOUT_FILENO);
	    (void) dup2(STDIN_FILENO, STDERR_FILENO);
#    endif
            /* If your system says "too few arguments to function setpgrp"
               then you do NOT have the SYSV version of this function (that
               version takes no arguments) */
	    (void) setpgrp();
#else
	    /* Does anyone remember why we open / and forget the fd? */
	    (void) open("/", 0);
	    (void) dup2(0, 1);
	    (void) dup2(0, 2);
	    fd = open("/dev/tty", O_RDWR);
	    if (fd >= 0) {
		ioctl(fd, TIOCNOTTY, 0);
		(void) close(fd);
	    }
#endif
	}

	while (1)
	{
	    /* Wait for a new connection attempt */
	    fd = net_accept(lfd);

	    pid = fork();

	    if (pid < 0)
	    {
		fprintf( logfile_fp, "Fork failed: %s\n",
			 strerror(errno));
		exit(pid);
	    }
	    if (pid == 0)
	    {
		fprintf( logfile_fp, 
		 "Started subprocess for connection at %s with pid %d\n", 
			 timestamp(), (int)getpid() );
#if defined(HP)
		(void) setpgrp();
#else
		{
		    int ttyfd = open("/dev/tty",O_RDWR);
		    if (ttyfd >= 0)
		    {
#    if !defined(CRAY) && defined(TIOCNOTTY)
			ioctl(ttyfd, TIOCNOTTY, 0);
#    endif
			close(ttyfd);
		    }
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
		    
		    doit(fd);
		}
		else {
		    close(0);
		    dup2(fd, 0);
		    close(1);
		    dup2(fd, 1);
		    close(2);
 		    dup2( logfile_fd, 2);
		    close(lfd);
		    
		    doit(0);
		}
		exit(0);
	    }
	    /* We can't close the new fd until we're sure that the fork has 
	       successfully started */
	    /* What we REALLY want to do is close the fd when the child exits
	     */
	    /* sleep(2); */
	    close(fd);
	}
    }
    else
    {
	doit(0);
    }
	
}

/* This is called (possibly in a subprocess) to process create p4-process
 * requests.
 */
void doit(fd)
int fd;
{
    struct sockaddr_in name;
    int namelen;
    struct hostent *hp;

    struct passwd *pw;
    char client_user[80], server_user[80];
    char pgm[1024], pgm_args[1024];
    char *user_home;
    int superuser;
    int valid;
    FILE *fp;
    int stdout_port;
    char stdout_port_str[16];

    char filename[1024], progline[1024];
    struct stat statbuf, statbuf_pgm, statbuf_apps_entry;

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

#ifdef HAVE_SETBUF_WITH_NULL
    /* SYS V says that a NULL second argument causes no-buffering; other 
       systems say that a NULL tells setbuf to allocate a buffer.  However,
       this may require that we introduce more fflush commands */
    setbuf(stdout_fp, NULL);
#endif

    fprintf( logfile_fp, "Got connection at %s", timestamp());

    namelen = sizeof(name);

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

    fromhost = (char *) (hp->h_name);

    if (!getline(client_user, sizeof(client_user)))
	failure("No client user");

    if (!getline(server_user, sizeof(server_user)))
	failure("No server user");

    pw = getpwnam(server_user);
    if (pw == NULL)
	failure2("No such user: %s\n", server_user);

    if (this_uid != 0 && this_uid != pw->pw_uid)
	failure2("Server is not running as root. Only %s can start processes\n",
		 this_username);

    user_home = pw->pw_dir;
    superuser = (pw->pw_uid == 0);

    fprintf( logfile_fp, "Starting ruserok at %s\n", timestamp() );
    valid = ruserok(fromhost, superuser, client_user, server_user);
    fprintf( logfile_fp, "Completed ruserok at %s\n", timestamp() );

    if (valid != 0)
    {
	char user_pw[80];
	char *xpw;

	fprintf( logfile_fp, "Ruserok failed, asking for password at %s\n", 
		 timestamp() );
	
	if (fprintf( stdout_fp, "Password\n") <= 0) {
	    failure("Write to client failed as password" );
	}
	fflush( stdout_fp );

	if (!getline(user_pw, sizeof(user_pw)))
	    failure("No server user (for authorization)");

	xpw = crypt(user_pw, pw->pw_passwd);
	if (strcmp(pw->pw_passwd, xpw) != 0)
	    failure("Invalid password");

	if (fprintf( stdout_fp, "Proceed\n") <= 0) {
	    failure("Write to client failed at Proceed" );
	}
    }
    else {
	if (fprintf( stdout_fp, "Proceed\n") <= 0) {
	    failure( "Write to client failed at Proceed" );
	}
    }
    /* Make sure that the proceed message is delivered */
    fflush( stdout_fp );

    sprintf(tmpbuf, "authenticated client_id=%s server_id=%s\n",
	    client_user, server_user);
    notice(tmpbuf);

    /* 
       At this point, we have an authenticated user.  We could accept
       additional commands beyond just "start program".  For example,
       we could accept "exit", which would allow simpler management of
       the servers.  (Note that we'd have to kill the parent, since we're
       probably just a forked child).
     */

    /* Get the program to execute */
    if (!getline(pgm, sizeof(pgm)))
	failure("No pgm");

    /* Check for key words:
       %id (give id)
       %run (run program)
       %exit (exit)
     */
    if (strcmp( pgm, "%id" ) == 0) {
	fprintf( stdout_fp, "Port %d for client %s and server user %s\n", 
		daemon_port, client_user, server_user );
	exit(0);
    }
    else if (strcmp( pgm, "%run" ) == 0) {
	/* Just get the program */
	if (!getline(pgm, sizeof(pgm)))
	    failure("No pgm");
    }
    else if (strcmp( pgm, "%exit" ) == 0) {
	kill( daemon_pid, SIGINT );
	sleep(1);
	kill( daemon_pid, SIGQUIT );
	exit(1);
    }

    if (!getline(pgm_args, sizeof(pgm_args)))
	failure("No pgm args");

    notice2("got args %s", pgm_args);

    if (pgm[0] != '/')
	failure2("%s is not a full pathname", pgm);

    if (this_uid == 0)
    {
#if defined(HP) && !defined(SUN_SOLARIS)
	if (setresuid(-1, pw->pw_uid, -1) != 0)
	    failure2("setresuid failed: %s", strerror(errno));
#else
#ifndef _AIX
	if (seteuid(pw->pw_uid) != 0)
	    failure2("seteuid failed: %s", strerror(errno));
#endif
#endif
    }
    
#define P4_APP_TEST
#ifdef P4_APP_TEST
    sprintf(filename, "%s/.p4apps", user_home);
    valid = 0;
    
#ifdef _AIX
    {
	int reader_pipe[2];
	int reader_pid;
	
	if (pipe(reader_pipe) < 0)
	    failure2("reader pipe failed: %s", strerror(errno));

	if ((reader_pid = fork()) < 0)
	    failure2("reader fork failed: %s", strerror(errno));

	notice2("got reader pid %d", reader_pid);

	if (reader_pid == 0)
	{
	    int fd;
	    
	    close(reader_pipe[0]);
#if defined(SUN_SOLARIS)
	    if (setuid(pw->pw_uid) != 0)
	      failure2("cannot setuid: %s", strerror(errno));
	    if (seteuid(pw->pw_uid) != 0)
	      failure2("cannot seteuid: %s", strerror(errno));
#else	
	    /* We can only call setreuid if we are root; otherwise we
	       get EPERM */
	    if (this_uid == 0 && setreuid(pw->pw_uid, pw->pw_uid) != 0)
		exit(1);
#endif

	    if ((fd = open(filename, O_RDONLY)) >= 0)
	    {
		char buf[1024];
		int n;

		while ((n = read(fd, buf, sizeof(buf))) > 0) {
		    if (n != write(reader_pipe[1], buf, n))
			notice("Short write");
		}

		if (n < 0)
		{
		    close(reader_pipe[1]);
		    exit(1);
		}
		   
		close(fd);
	    }
	    else
	    {
		close(reader_pipe[1]);
		exit(1);
	    }

	    close(reader_pipe[1]);
	    exit(0);
	}
	else
	{
	    close(reader_pipe[1]);
	    fp = fdopen(reader_pipe[0], "r");
	    if (!fp) {
		fprintf( logfile_fp, 
			 "Could not get FILE for reader_pipe fd\n" );
	    }
	}
    }

#else
    fp = fopen(filename,"r");
#endif /* _AIX */

    if (fp != (FILE *) NULL)
    {
	char *s1, *s2;
	
	if (fstat( fileno(fp), &statbuf) != 0)
	    failure2("cannot stat %s", filename);
	
	if (statbuf.st_mode & 077)
	    failure(".p4apps readable by others");
	
	/* fprintf( logfile_fp, "Trying to find program %s\n", pgm ); */
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
	    /* fprintf( logfile_fp, "Checking %s\n", s1 ); */
	    if (strcmp(pgm, s1) == 0)
	    {
		valid = 1;
		break;
	    }
	    else
	    {
		if (stat(pgm, &statbuf_pgm) != 0)
		    continue;
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

#ifdef _AIX
	{
	    int status;
	    pid_t pid;

	    pid = wait(&status);

	    notice2("got wait return %d", status);

	    if (WEXITSTATUS(status) == 1)
		failure("reader child failed");
	}
#endif
	
    }

    if (!valid)
	failure2("Invalid program %s", pgm);
    
    if (stat(pgm, &statbuf) != 0)
	failure2("Cannot stat %s", pgm);

    if (!(statbuf.st_mode & 0111))
	failure2("Cannot execute %s", pgm);
#endif

    /*********/
    if (!getline(stdout_port_str, sizeof(stdout_port_str)))
	failure("No stdout");
    else
	stdout_port = atoi(stdout_port_str);

    notice2("got stdout_port %d", stdout_port);
    /*********/

    notice3("executing %s %s", pgm, pgm_args);

    execute(pgm, pgm_args, pw->pw_uid, stdout_port, hp);
	    
}

void execute(pgm, pgm_args, uid, stdout_port, hp)
char *pgm, *pgm_args;
int uid, stdout_port;
struct hostent *hp;
{
    int p[2];
    int rd, wr;
    int pid, n;
    char *args[MAXARGS];
    int nargs;
    char *s, *end;
    char buf[1024];
    int stdout_fd;

    s = pgm_args;
    while (*s && isspace(*s))
	s++;

    args[0] = pgm;

    nargs = 1;
    while (*s)
    {
	args[nargs] = s;

	while (*s && !isspace(*s))
	    s++;

	end = s;

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
#if defined(HP) && !defined(SUN_SOLARIS)
	if (setresuid(uid, uid, -1) != 0)
	    failure2("cannot setresuid: %s", strerror(errno));
#else
	if (seteuid(0) != 0)
	    failure2("cannot seteuid: %s", strerror(errno));
#if defined(SUN_SOLARIS)
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

	stdout_fd = connect_to_listener(hp,stdout_port);
	notice2("stdout_fd=%d", stdout_fd);
	close(1);
	dup(stdout_fd);
	/* open("/dev/null", O_WRONLY); */

	close(2);
	dup(stdout_fd);
	/* open("/dev/null", O_WRONLY); */

	/*****
	{char tempbuf[100];
	strcpy(tempbuf,"writing this to stdout_fd");
	write(stdout_fd,tempbuf,strlen(tempbuf)+1);
	strcpy(tempbuf,"writing this to real stdout");
	write(stdout_fd,tempbuf,strlen(tempbuf)+1);
        }
	*****/

	if (execv(pgm, args) != 0)
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
    fprintf( stdout_fp, "Success: Child %d started\n", pid);
    notice2("Child %d started", pid);
}

int getline(str, len)
char *str;
int len;
{
    char *s;
    
    if (fgets(str,  len, stdin_fp) == NULL)
	return 0;

    if ((s = index(str, '\n')) != NULL)
	*s = 0;
    if ((s = index(str, '\r')) != NULL)
	*s = 0;
    return 1;
}
    

void failure(s)
char *s;
{
    fprintf( stdout_fp, "Failure <%s>: %s\n", fromhost, s);
    fprintf( logfile_fp, "Failure <%s>: %s\n", fromhost, s);
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
int net_accept(skt)
int skt;
{
struct sockaddr_in from;
int fromlen;
int skt2;
int gotit;

    fromlen = sizeof(from);
    gotit = 0;
    while (!gotit)
    {
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
int sinlen;
struct sockaddr_in sin;

    *skt = socket(AF_INET, SOCK_STREAM, 0);

    error_check(*skt,"net_setup_anon_listener socket");

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);

    sinlen = sizeof(sin);

    error_check(bind(*skt,(struct sockaddr *) &sin,sizeof(sin)),
		   "net_setup_listener bind");


    error_check(listen(*skt, backlog), "net_setup_listener listen");
}

void net_setup_anon_listener(backlog, port, skt)
int backlog, *port, *skt;
{
int sinlen;
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

#include <time.h>
char *timestamp()
{
    long clock;
    struct tm *tmp;

    clock = time(0L);
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

#ifdef NEED_GETOPT
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

static int connect_to_listener(hp,stdout_port)
struct hostent *hp;
int stdout_port;
{
    int conn;
    int rc;
    struct sockaddr_in addr;

    conn = socket(AF_INET, SOCK_STREAM, 0);
    if (conn < 0)
    {
	failure("connect_to_listener: socket failed");
    }

    addr.sin_family = hp->h_addrtype;
    addr.sin_port = htons(stdout_port);
    bcopy(hp->h_addr, &addr.sin_addr, hp->h_length);

    rc = connect(conn, (struct sockaddr *) & addr, sizeof(addr));
    if (rc < 0)
    {
	failure("connect_to_listener: connect failed");
    }

    return conn;
}

