#include "p4.h"
#include "p4_sys.h"

#ifdef SCYLD_BEOWULF
#include <sys/bproc.h>
#endif

int create_remote_processes(struct p4_procgroup *pg)
{
    struct p4_procgroup_entry *pe;
    struct net_initial_handshake hs;
    int i, serv_port, serv_fd, rm_fd, rm_fds[P4_MAXPROCS], rm_num;

    net_setup_anon_listener(MAX_P4_CONN_BACKLOG, &serv_port, &serv_fd);
    if (execer_starting_remotes)
    {
	if (pg->num_entries > 1) 
	    put_execer_port(serv_port);
	for (i=1, pe = pg->entries+1; i < pg->num_entries; i++, pe++)
	{
	    rm_fd = net_accept(serv_fd);
	    hs.pid = (int) htonl(getpid());
	    net_send(rm_fd, &hs, sizeof(hs), P4_FALSE);
	    net_recv(rm_fd, &hs, sizeof(hs));
	    rm_num = (int) ntohl(hs.rm_num);
	    rm_fds[rm_num] = rm_fd;
	}
	for (i=1, pe = pg->entries+1; i < pg->num_entries; i++, pe++)
	{
	    pe = pg->entries+i;
	    net_slave_info(pe, rm_outfile_head, rm_fds[i], i);
	}
    }
    else
    {
	for (i=1, pe = pg->entries+1; i < pg->num_entries; i++, pe++)
	{
	    rm_fd = net_create_slave(serv_port,serv_fd,
				     pe->host_name,
				     pe->slave_full_pathname,
				     pe->username,
				     pe->rm_rank );
#ifdef SCYLD_BEOWULF
	    if (rm_fd < 0) 
		break;
#endif
	    net_slave_info(pe, rm_outfile_head, rm_fd, i);
	}
    }

#ifdef SCYLD_BEOWULF
    if (rm_fd == -2)    /* We are an rforked child */
	return (-2);
#endif
    close( serv_fd );
    return (0);
}

P4VOID net_slave_info(pe, outfile, rm_fd, rm_num)
struct p4_procgroup_entry *pe;
char *outfile;
int rm_fd, rm_num;
{
    struct bm_rm_msg msg;
    P4BOOL done;
    int type, status, port, remote_switch_port;
    int slave_idx, slave_pid, pidx, rm_ind;

    msg.type = p4_i_to_n(INITIAL_INFO);
    msg.numinproctab = p4_i_to_n(p4_global->num_in_proctable);
    msg.rm_num = p4_i_to_n( rm_num );
    msg.numslaves = p4_i_to_n(pe->numslaves_in_group);
    if (strlen( outfile ) >= P4_MAX_PGM_LEN) {
	p4_error( "Output filename must be less than ", P4_MAX_PGM_LEN );
    }
    strncpy(msg.outfile, outfile, P4_MAX_PGM_LEN);
    msg.debug_level = p4_i_to_n(p4_remote_debug_level);
    msg.memsize = p4_i_to_n(globmemsize);
    msg.logging_flag = p4_i_to_n(logging_flag);
    strcpy(msg.application_id, p4_global->application_id);
    strcpy(msg.version, P4_PATCHLEVEL);
    if ( strlen( pe->slave_full_pathname ) >= P4_MAX_PGM_LEN ) {
	p4_error( "Program names must be less than ", P4_MAX_PGM_LEN );
    }
    strncpy(msg.pgm, pe->slave_full_pathname, P4_MAX_PGM_LEN );
    strncpy(msg.wdir, p4_wd, P4_MAX_PGM_LEN );

    net_send(rm_fd, &msg, sizeof(msg), P4_FALSE);

    port = -1;
    pidx = -1;
    for (done = P4_FALSE; !done;)
    {
	status = net_recv(rm_fd, &msg, sizeof(msg));
	if (status == PRECV_EOF)
	{
	    p4_dprintf("OOPS! got EOF in net_slave_info\n");
	    return;
	}


	type = p4_n_to_i(msg.type);
	switch (type)
	{
	  case REMOTE_LISTENER_INFO:
	    port = p4_n_to_i(msg.port);
	    break;

	  case REMOTE_MASTER_INFO:
	  case REMOTE_SLAVE_INFO:
	    if (type == REMOTE_MASTER_INFO)
	       rm_ind = P4_TRUE;
	    else
	       rm_ind = P4_FALSE;
	    slave_idx = p4_n_to_i(msg.slave_idx);
	    slave_pid = p4_n_to_i(msg.slave_pid);
	    remote_switch_port = p4_n_to_i(msg.switch_port);
	    if (port == -1)
		p4_dprintf("OOPS! got slave_info w/o getting port first\n");
	    /* big master installing remote processes */
	    pidx = install_in_proctable(rm_num,port,slave_pid,
					pe->host_name,
					pe->host_name, slave_idx,
					msg.machine_type,remote_switch_port);
            p4_dprintfl(90, "net_slave_info: adding connection to %d (%d) \n",
		        pidx,rm_num);

            if (p4_local->conntab[pidx].type == CONN_REMOTE_SWITCH)
            {
	        p4_local->conntab[pidx].switch_port = remote_switch_port;
	        p4_local->conntab[pidx].port = rm_fd;
            }
            else if (p4_local->conntab[pidx].type == CONN_REMOTE_NON_EST)
            {
		if (type == REMOTE_MASTER_INFO)
		{
	            p4_local->conntab[pidx].type = CONN_REMOTE_EST;
	            p4_local->conntab[pidx].port = rm_fd;
		    p4_local->conntab[pidx].same_data_rep =
			same_data_representation(p4_local->my_id,pidx);
		}
            }
            else
            {
	        p4_error("net_slave_info: invalid conn type in conntab\n",
		         p4_local->conntab[pidx].type);
            }
	    break;

	  case REMOTE_SLAVE_INFO_END:
	    done = P4_TRUE;
	    break;
	}
    }
}

/* This routine is called if the net_accept fails to complete quickly */
#include <sys/time.h>
#ifndef TIMEOUT_VALUE 
#define TIMEOUT_VALUE 300
#endif
static char *curhostname = 0;
static char errbuf[512];
static int  child_pid = 0;
/* active_fd is the fd that we're waiting on when the timeout happened */
static int  active_fd = -1;
P4VOID p4_accept_timeout ( int );
P4VOID p4_accept_timeout( int sigval)
{
    /* First, we should check that the timeout has actually be reached,
       and this isn't some other alarm */
    
    if (child_pid) {
	kill( child_pid, SIGQUIT );
    }
    if (curhostname) {
	sprintf( errbuf, 
		 "Timeout in making connection to remote process on %s", 
		 curhostname );
	p4_error( errbuf, 0 );
    }
    else {
	p4_error( "Timeout in making connection to remote process", 0 );
    }
    if (active_fd >= 0)
	close( active_fd );
    exit(1);
}
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
P4VOID p4_accept_sigchild ( int );
P4VOID p4_accept_sigchild( int sigval )
{
    int status;
    /* See if this is a child that we're waiting on */
    if (!child_pid) return;

    /* If we did not find sys/wait.h , WHOHANG won't be defined.  What
       can we do? */
    if (waitpid( child_pid, &status, WNOHANG )) {
	/* waitpid returns 0 if the child hasn't exited */
    }

    if (curhostname) {
	sprintf( errbuf, 
		 "Child process exited while making connection to remote process on %s", 
		 curhostname );
	p4_error( errbuf, 0 );
    }
    else {
	p4_error( "Child process exited while making connection to remote process", 0 );
    }
    if (active_fd >= 0)
	close( active_fd );
    exit(1);
}

/*
 *	Run the slave pgm on host; returns the file descriptor of the
 *	connection to the slave.  This creates the remote slave, which
 *      in turn is responsible for creating the slaves.
 */
int net_create_slave( int serv_port, int serv_fd, char *host, char *pgm, 
		      char *username, int rm_rank )
{
    struct net_initial_handshake hs;
    char myhostname[100];
    char remote_shell[P4_MAX_PGM_LEN];
    char serv_port_c[64];
    int rc;
    char rm_rank_str[12];
#ifdef USE_OLD_SERVER
    struct net_message_t msg;
    int success, connection_fd;
#endif
    int slave_fd;
    int fcntl_flags;

    char *am_slave_c = "-p4amslave";

#   if defined(SYMMETRY) || defined(SUN) || \
    defined(DEC5000)  || defined(SGI) || \
    defined(RS6000)   || defined(HP)  || \
    defined(NEXT)     || defined(CRAY) || \
    defined(CONVEX)   || defined(KSR)  || \
    defined(FX2800)   || defined(FX2800_SWITCH)  || \
    defined(SP1)
/*     char *getpw_ss (char *); */
#   endif

    sprintf( rm_rank_str, "%d", rm_rank );

#   if defined(SP1)
    strcpy(myhostname,p4_global->proctable[0].host_name);
    p4_dprintfl(80,"net_create_slave: myhost=%s\n",myhostname);
#   else
    myhostname[0] = '\0';
    get_qualified_hostname(myhostname,sizeof(myhostname));
#   endif

    if (hand_start_remotes)
    {
	printf("waiting for process on host %s:\n%s %s %d %s\n",
	       host, pgm, myhostname, serv_port, am_slave_c);
        rc = 0;
    }
    else
    {
	/* try to connect to (secure) server */

#       if !defined(P4_DO_NOT_USE_SERVER)

        /* Do not try the secure server by default.  The attempt to contact
	   the default secure server port can cause the startup step to
	   hang, due to IP security settings that cause some connections
	   to go unacknowledged (not even refused).  Currently, the test
	   for this is on the sserver_port, which is initialized to -1
	   (rather than the old default of 753).
	   */

	/*****  secure server stuff  *******/
	p4_dprintfl(20, "trying to create remote slave on %s via server\n",host);
	rc = start_slave(host, username, pgm, serv_port, am_slave_c, getpw_ss);

	if (rc < -1)
	{
	    extern char *start_prog_error;
	    p4_dprintfl(20,"Warning from secure server: %s\n", start_prog_error);
	}
	else if (rc == 0)
	    p4_dprintfl(10, "created remote slave on %s via server\n",host);
	/*****************************************/
	
	else {
	    /* A -1 is failure, not warning */
	    extern char *start_prog_error;
	    p4_dprintfl( 20, 
			 "Failed to connect to secure server: %s\n",
			 start_prog_error );
	}
#       else

	rc = -1;

#       endif
    }

    if (rc <= -1)
    {
#ifdef USE_OLD_SERVER
	/* try to connect to (old) server */
	connection_fd = net_conn_to_listener(host, UNRESERVED_PORT, 1);

	if (connection_fd >= 0)
	{
	    p4_dprintfl(20, "creating remote slave on %s via old server\n",host);
	    msg.type = p4_i_to_n(NET_EXEC);
	    strcpy(msg.pgm, pgm);
	    strcpy(msg.host, myhostname);
	    strcpy(msg.am_slave, am_slave_c);
	    msg.port = p4_i_to_n(serv_port);
	    net_send(connection_fd, &msg, sizeof(msg), P4_FALSE);
	    net_recv(connection_fd, &msg, sizeof(msg));

	    success = p4_n_to_i(msg.success);
	    if (!success)
	    {
		p4_dprintf("create failed: %s\n", msg.message);
		return (-1);
	    }
	    close(connection_fd);
	    p4_dprintfl(10, "created remote slave on %s via old server\n",host);
	}
	else
#endif /* USE_OLD_SERVER */
	{
#ifdef SCYLD_BEOWULF
	int node_num;
	int curr_node;

	    p4_dprintfl(20, "trying to create remote slave on %s\n",host);

	    sprintf(serv_port_c,"%d",serv_port);
	    node_num=bproc_getnodebyname(host);
	    if(node_num==BPROC_NODE_NONE) p4_error("net_create_slave: host not a bproc node",node_num);

            curr_node=bproc_currnode();		
            if(curr_node==node_num)
	    {
		p4_dprintfl(20, "spawning slave via regular fork\n");
		rc=child_pid=fork();
	    } else {
	        p4_dprintfl(20, "spawning slave via bproc\n");
	        rc=child_pid=fork(); if(!child_pid) { rc=bproc_move(node_num); if(rc==-1) { p4_error("net_create_slave: bproc_move",rc); }}
            } 
	    if(!rc)
	    {
		reset_fork_p4(); /* reset some global crap */
		curhostname = 0; /* global crap */
		child_pid=0;	/* global crap */
		active_fd=-1;	/* global crap */
		close(serv_fd);
		/* this helps p4_printf routines */
		sprintf(whoami_p4, "p%d_%d", p4_get_my_id(), getpid());	
	        p4_dprintfl(20, "bproc: (pid=%d)\n",getpid());
		p4_local  = NULL;
		p4_global = NULL;
    		SIGNAL_P4(SIGALRM,SIG_DFL);
    		SIGNAL_P4(LISTENER_ATTN_SIGNAL,SIG_DFL);
		
		{
		  int argc = 4;
		  char *argv[4];
		  static char port_str[6];
		  snprintf (port_str, 6, "%d", serv_port);
		  argv[1] = myhostname;
		  argv[2] = port_str;
		  rm_start (&argc, argv);
		}
		/*ALOG_SETUP(p4_local->my_id,ALOG_TRUNCATE);*/
		return -2;
	    } else if(rc<0) {
		p4_error("net_create_slave: bproc_rfork",rc);
	    } else {
		p4_dprintfl(20, "bproc: (pid=%d) child pid is %d\n",getpid(),child_pid);
	    }

#else /* !SCYLD_BEOWULF */
#if defined(HAS_RSHCOMMAND)
	    strncpy( remote_shell, RSHCOMMAND, P4_MAX_PGM_LEN );
	    /* Allow the environment variable "P4RSHCOMMAND" to 
               override the default choice */
	    { char *p = getenv( "P4_RSHCOMMAND" ); 
	    if (p && *p) strncpy( remote_shell, p, P4_MAX_PGM_LEN );
	    }
#endif
#if defined(DELTA)
	    p4_dprintf("delta cannot create remote processes\n");
#else
#if defined(P4BSD) && !defined(HAS_RSHCOMMAND)
	    strcpy(remote_shell, "rsh");
#endif

/* RL - added || defined(RS6000) to get around afs problems.  In earlier
   versions of AIX we could not use rsh since rsh was the restricted
   shell, which has been renamed Rsh  */
#if defined(P4SYSV) && !defined(HAS_RSHCOMMAND)
#    if defined(TITAN) || defined(SGI) || defined(SUN_SOLARIS) || defined(RS6000)
	    strcpy(remote_shell, "rsh");
#    else
#        if defined(SYMMETRY_PTX)
	    strcpy(remote_shell, "resh");
#        else
	    strcpy(remote_shell, "remsh");
#        endif
#    endif
#endif
	    p4_dprintfl(20, "creating remote slave on %s via remote shell %s\n",host, remote_shell);

	    sprintf(serv_port_c, "%d", serv_port);
	    /* We should remember ALL of the children's pid's so we can 
	       forcibly stop them if necessary */
	    child_pid = rc = fork_p4();
	    if (rc == 0)
	    {
/* define SHORT_CIRCUIT_LOCALHOST */
/* This doesn't work yet.  redirection of stdin/out/error are undoubtedly
   part of the problem.  We'll leave this for the next release */
#ifdef SHORT_CIRCUIT_LOCALHOST
		/* If host is localhost or myhost, then we don't need to run 
		   remote shell (do we? what about stdin/out/err?) */
		if (strcmp( host, "localhost" ) == 0 ||
		    strcmp( myhostname, host ) == 0) { 
		    p4_dprintfl( 80, "Not using rsh to localhost\n" );
		    rc = execlp(pgm, pgm,
			    myhostname, serv_port_c, am_slave_c, 
			    "-p4yourname", host, "-p4rmrank", rm_rank_str, 
				NULL);
		}
		else {
		    rc = execlp(remote_shell, remote_shell,
			    host, 
#if !defined(RSH_HAS_NO_L)
			    "-l", username, 
#endif
			    "-n", pgm,
			    myhostname, serv_port_c, am_slave_c, 
#ifdef HAVE_BROKEN_RSH
			    "\\-p4yourname", host, "\\-p4rmrank", rm_rank_str,
#else
			    "-p4yourname", host, "-p4rmrank", rm_rank_str,
#endif
				NULL);
		}
#else
#   if defined(HAVE_BROKEN_RSH)
		/* This must be in this branch because the backslash 
		   is not stripped off if rsh is not used */
		/* On some LINUX systems, it was necessary to escape the
		   - in -p4amslave.  It is reported that current systems
		   do not require this, but it should be safe. */
		am_slave_c = "\\-p4amslave";
#   endif

/* #define RSH_NEEDS_OPTS */
#ifdef RSH_NEEDS_OPTS
		/* The following code allows the remote shell command string
		   to include additional command line options, such as
		   ssh -q */
		{
		    char *argv[64];
		    char rshell_string[P4_MAX_PGM_LEN];
		    char *next_parm;
		    int argcount = 0;

		    strcpy( rshell_string, remote_shell );
		    /* Find the first blank, set next_parm to the next char,
		       and set the blank to null.  If no next_parm, leave
		       it pointing at the null */
		    next_parm = strchr( rshell_string, ' ' );
		    if (next_parm) {
			*next_parm++ = 0;
		    }
		    argv[argcount++] = rshell_string;
		    argv[argcount++] = host;
#if !defined(RSH_HAS_NO_L)
		    argv[argcount++] = "-l";
		    argv[argcount++] = username;
#endif
		    while (next_parm && argcount < 51) {
			argv[argcount++] = next_parm;
			next_parm = strchr( next_parm, ' ' );
			if (next_parm) {
			    *next_parm++ = 0;
			}
		    }
		    argv[argcount++] = "-n";
		    argv[argcount++] = pgm;
		    argv[argcount++] = myhostname;
		    argv[argcount++] = serv_port_c;
		    argv[argcount++] = am_slave_c;
#ifdef HAVE_BROKEN_RSH
		    argv[argcount++] = "\\-p4yourname";
#else
		    argv[argcount++] = "-p4yourname";
#endif
		    argv[argcount++] = host;
#ifdef HAVE_BROKEN_RSH
		    argv[argcount++] = "\\-p4rmrank";
#else
		    argv[argcount++] = "-p4rmrank";
#endif
		    argv[argcount++] = rm_rank_str;
		    argv[argcount++] = 0;
		    rc = execvp( rshell_string, argv );
		}
		    /* ENOEXEC - unrecognized executable,
		       ENOENT  - file no found */
#else

		{ /* Code to pass environment variables for MPICH-G2 (RL) */
		    char *p;
		    p = getenv( "P4_SETS_ALL_ENVVARS" ); 
		    if ( p ) {
			/* This code prepends "setenv FOO BAR;setenv FAZZ BAZZ; ..." to
			   the program name to be rsh'd */
			/* This code needs more stringent attention to string lengths */
                        /*                      ^^^^^^                                */
			extern char **environ;
			int i, pgm_prefix_len;
#                       define MAX_PGM_PREFIX_LEN 1024
			char pgm_prefix[MAX_PGM_PREFIX_LEN], *c;
			char envvar_buf[256], setenv_buf[256], varname[256], varvalue[1024];

			p4_dprintfl( 10, "P4_SETS_ALL_ENVVARS is set\n"); 
			pgm_prefix_len = 0;
			for (i = 0; environ[i] != NULL; i++ ) { 
			    p4_dprintfl( 90, "environ[%d]: %s\n", i, environ[i] );
			    pgm_prefix_len += strlen(environ[i]);
			}
			/* prefix will need accumulated length plus room for i
			   copies of "setenv ;" where i is the number of env vars */
			pgm_prefix_len += i * strlen("setenv ;");
			p4_dprintfl( 90, "prefix needs %d characters\n", pgm_prefix_len);
			/* 256 seems to be limit of string passed to rsh through execlp */
/*
			if ( pgm_prefix_len > 256 - strlen(pgm) )
			    p4_error( "environment-setting prefix would be too long: ",
				      pgm_prefix_len);
*/
			pgm_prefix[0] = '\0';
			for (i = 0; environ[i] != NULL; i++ ) { 
			    /* separate name from value; add setenv cmd */
			    strcpy(envvar_buf, environ[i] );
			    c = strtok( envvar_buf, "=" ); /* get varname */
			    strcpy( varname, c );
			    /* here is where to exclude some env vars */
			    if ( strcmp( varname, "P4_SETS_ALL_ENVVARS" ) == 0 )
				continue;
			    if ( strcmp( varname, "FOO" ) == 0 )
				continue;
			    if ( strcmp( varname, "PWD" ) == 0 )
				continue;
			    if ( strcmp( varname, "MACHTYPE" ) == 0 )
				continue;
			    if ( strcmp( varname, "SHLVL" ) == 0 )
				continue;
			    if ( strcmp( varname, "SHELL" ) == 0 )
				continue;
			    if ( strcmp( varname, "OSTYPE" ) == 0 )
				continue;
			    if ( strcmp( varname, "HOSTTYPE" ) == 0 )
				continue;
			    if ( strcmp( varname, "TERM" ) == 0 )
				continue;
			    if ( strcmp( varname, "PATH" ) == 0 )
				continue;
			    c = strtok( NULL, "\n" ); /* get varvalue */
			    if ( c )
				strcpy( varvalue, c );
			    else
				varvalue[0] = '\0';
			    sprintf( setenv_buf, "setenv %s %s;", varname, varvalue);
			    p4_dprintfl( 90, "setenv_buf = :%s:\n", setenv_buf );
			    strcat( pgm_prefix, setenv_buf );
			}
			p4_dprintfl( 90, "prefix=:%s:\n", pgm_prefix );
			/* now prepend to pgm if not too long */
			if (strlen(pgm_prefix) + strlen(pgm) >= P4_MAX_PGM_LEN )
			    p4_error("prefix too long", 0 );
			else {
			    strcat(pgm_prefix, pgm);
			    strcpy(pgm, pgm_prefix);
			}
		    }
		    /*
		    p4_dprintf( "pgm argument to remote shell = :%s:\n", pgm );
		    p4_dprintf( "length of pgm argument to remote shell = %d\n",
				strlen(pgm) );
		    */
		}

		rc = execlp(remote_shell, remote_shell,
			    host, 
#if !defined(RSH_HAS_NO_L)
			    "-l", username, 
#endif
			    "-n", pgm,
			    myhostname, serv_port_c, am_slave_c, 
#ifdef HAVE_BROKEN_RSH
			    "\\-p4yourname", host, "\\-p4rmrank", rm_rank_str,
#else
			    "-p4yourname", host, "-p4rmrank", rm_rank_str,
#endif
			    NULL);
#endif /* RSH_NEEDS_OPTS */
#endif /* Short_circuit_localhost */
		/* host,"-n","cluster","5",pgm,myhostname,serv_port_c,0); for butterfly */
		if (rc < 0) {
		    /* Trap common user errors and generate a more 
		       helpful error message */
		    char *pmsg = "net_create_slave: execlp";
		    char fullmsg[512];
		    switch (errno) {
			/* noent - component of file doesn't exist */
		    case ENOENT: pmsg = "Path to program is invalid"; 
			break;
			/* notdir - component of file isn't a directory */
		    case ENOTDIR: 
			pmsg = "A directory in the program path is not a valid directory"; 
			break;
			/* acces - Search permission denied, file not 
			   executable */
		    case EACCES: 
			pmsg = "Program is not an executable or is not accessible"; break;
			/* interrupt received! */
		    case EINTR: pmsg = "Interrupt received while starting program"; break;
		    default:
			;
		    }
		    strcpy( fullmsg, pmsg );
		    strcat( fullmsg, " while starting " );
		    strncat( fullmsg, pgm, 511 );
		    strncat( fullmsg, " with ", 511 );
		    strncat( fullmsg, remote_shell, 511 );
		    strncat( fullmsg, " on ", 511 );
		    strncat( fullmsg, myhostname, 511 );
		    p4_error(fullmsg, rc);
		}
	    }
	    p4_dprintfl(10, "created remote slave on %s via remote shell\n",host);
	    p4_dprintfl(90, "remote slave is running program %s as user %s\n",
			pgm, username );
#endif
#endif /* SCYLD_BEOWULF */
	}
    }
    /* WDG - There is a chance that we'll hang here forever.  Thus, we set a 
       timeout that causes the whole job to fail if we don't get a timely
       response from the created process.  See MPIBUGS #989; I got a traceback 
       showing this exact problem. 
     */
    curhostname = host;
    active_fd = serv_fd;
    SIGNAL_P4(SIGALRM,p4_accept_timeout);
    {
#ifndef CRAY
    struct itimerval timelimit;
    struct timeval tval;
    struct timeval tzero;
    tval.tv_sec		  = TIMEOUT_VALUE;
    tval.tv_usec	  = 0;
    tzero.tv_sec	  = 0;
    tzero.tv_usec	  = 0;
    timelimit.it_interval = tzero;       /* Only one alarm */
    timelimit.it_value	  = tval;
    setitimer( ITIMER_REAL, &timelimit, 0 );
#else
    alarm( TIMEOUT_VALUE );
#endif
    /* 
       If the user's program never starts and the forked child process
       fails, then this step will hang (eventually failing due to the
       timeout).  The p4 code in general needs to manage SIGCHLD, 
       but this gives a simple way to warn of problems.
     */
#ifndef SIGCHLD
#define SIGCHLD SIGCLD
#endif
    SIGNAL_P4(SIGCHLD,p4_accept_sigchild)
    slave_fd		  = net_accept(serv_fd);
    /* 
       Thanks to Laurie Costello (lmc@cray.com) for this fix.  This 
       helps systems with broken rsh/rexec that don't properly use
       fd_set in their code; this closes FDs that would otherwise
       be held open when fork/exec is done to start remote jobs. 
     */
    /* Some systems (like NeXT) don't define a name for the bit, 
       though they accept it */
#ifndef FD_CLOEXEC
#define FD_CLOEXEC 0x1
#endif
    fcntl_flags = fcntl(slave_fd, F_GETFD);
    if (fcntl_flags == -1) {
        p4_dprintfl(10, "fcntl F_GETFD failed for fd %d\n", slave_fd);
    } else {
        fcntl_flags |= FD_CLOEXEC;
        if (fcntl(slave_fd, F_SETFD, fcntl_flags) < 0) {
            p4_dprintfl(10, "fcntl for close on exec failed for fd %d\n", 
			slave_fd);
        }
    }

    /* Go back to default handling of alarms */
    curhostname		  = 0;
    child_pid		  = 0;
#ifndef CRAY
    timelimit.it_value	  = tzero;   /* Turn off timer */
    setitimer( ITIMER_REAL, &timelimit, 0 );
#else
    alarm( 0 );
#endif
    active_fd             = -1;
    SIGNAL_P4(SIGALRM,SIG_DFL);
    /* We should be more careful about cigchld */
    SIGNAL_P4(SIGCHLD,SIG_DFL);
    }

    hs.pid = (int) htonl(getpid());
    hs.rm_num = 0;   /* To make Insight etc happy */
    net_send(slave_fd, &hs, sizeof(hs), P4_FALSE);
    net_recv(slave_fd, &hs, sizeof(hs));

    return(slave_fd);
}


