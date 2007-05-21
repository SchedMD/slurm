#include <stdio.h>
#if defined(USE_STDARG)
#include <stdarg.h>
#else
#include <varargs.h>
#endif
#include "p4.h"
#include "p4_sys.h"

/* Choose vprintf if available and we haven't decided to force _doprnt */
#if defined(HAVE_VPRINTF) && !defined(USE__DOPRNT) && !defined(VPRINTF)
#define VPRINTF
#endif

#if defined(p4_dprintfl)
#undef p4_dprintfl
#endif

int p4_get_dbg_level( void )
{
    return(p4_debug_level);
}

P4VOID p4_set_dbg_level(int level)
{
    p4_debug_level = level;
}

/* LINUX now (?) support stdarg */
#if defined(DELTA)  ||  defined(NCUBE) 

P4VOID p4_dprintf(fmt, a, b, c, d, e, f, g, h, i)
{
    printf("%s: ",whoami_p4);
    printf(fmt,a,b,c,d,e,f,g,h,i);
    fflush(stdout);
}

#else
#if defined(USE_STDARG) && !defined(USE_OLDSTYLE_STDARG)
P4VOID p4_dprintf( char *fmt, ... )
{
    va_list ap;

    va_start( ap, fmt );
    printf("%s: ", whoami_p4);
    if (p4_global)
	printf("(%f) ", p4_usclock());
    else
	printf("(-) " );
#ifdef VPRINTF
    vprintf(fmt, ap);
#else
    _doprnt(fmt, ap, stdout);
#endif
    va_end(ap);
    fflush(stdout);
}
#else
/* Assumes old-style stdard */
#if !defined(USE_OLDSTYLE_STDARG)
#error 'unknown standard arg method'
#endif
P4VOID p4_dprintf(fmt, va_alist)
char *fmt;
va_dcl
{
    va_list ap;

    printf("%s: ", whoami_p4);
    if (p4_global)
	printf("(%f) ", p4_usclock());
    else
	printf("(-) " );
    va_start(ap);
#ifdef VPRINTF
    vprintf(fmt, ap);
#else
    _doprnt(fmt, ap, stdout);
#endif
    va_end(ap);
    fflush(stdout);
}
#endif
#endif

/*
 * The following is used to keep the last P4_LAST_DEBUG lines in memory,
 * even if not printed, so that they can be dumped by calling the
 * appropriate routine.
 * NOT YET READY
 */
#ifdef USE_HOLD_LAST_DEBUG
#ifndef P4_LAST_DEBUG
#define P4_LAST_DEBUG 128
#endif
#ifndef P4_MAX_DEBUG_LINE
#define P4_MAX_DEBUG_LINE 128
#endif
static char p4_debug_msgs[P4_LAST_DEBUG+1][P4_MAX_DEBUG_LINE];
static int  p4_msg_top = 0,     /* Index of first FREE line */
            p4_msg_bottom = 0;  /* 0 if start at bottom; 1 if start at top */

void p4_dprint_last( fp )
FILE *fp;
{
    int i;
    static int in_call = 0;   /* Use this to protect against recursive calls */
    
    if (in_call) return;
    in_call = 1;
    if (p4_msg_bottom == 1) {
	p4_msg_bottom = p4_msg_top;
    }
    i = p4_msg_bottom;
    do {
	fputs( p4_debug_msgs[i], fp );
	i++;
	if (i >= P4_LAST_DEBUG) i = 0;
    } while (i != p4_msg_top);
    in_call = 0;
}

#else
void p4_dprint_last( fp )
FILE *fp;
{
    /* Do nothing when data not available */
}
#endif

#if defined(DELTA)  ||  defined(NCUBE) 
/* LINUX now (?) supports stdarg */

P4VOID p4_dprintfl(level, fmt, a, b, c, d, e, f, g, h, i)
{
    if (level > p4_debug_level)
        return;
    printf("%s: ",whoami_p4);
    printf(fmt,a,b,c,d,e,f,g,h,i);
    fflush(stdout);
}

#else
#if defined(USE_STDARG) && !defined(USE_OLDSTYLE_STDARG)
P4VOID p4_dprintfl(int level, char *fmt, ...)
{
    va_list ap;

    va_start( ap, fmt );

#ifdef USE_HOLD_LAST_DEBUG
    {
	char *p4_debug_buf;
	int  len;
	p4_debug_buf = p4_debug_msgs[p4_msg_top++];
	if (p4_msg_top >= P4_LAST_DEBUG) { 
	    p4_msg_top = 0;
	    p4_msg_bottom = 1;  /* Indicates run from msg_top to LAST_DEBUG */
	}
	sprintf( p4_debug_buf,
#ifdef USE_PTHREADS
    "%d: %s: %u: ", level, whoami_p4, pthread_self());
#else
    "%d: %s: ", level, whoami_p4);
#endif
        len = strlen( p4_debug_buf );
	if (p4_global)
	    sprintf( p4_debug_buf + len, "(%f) ", p4_usclock());
	else
	    sprintf( p4_debug_buf + len, "(-) " );
	len = len + strlen( p4_debug_buf + len );
#ifdef VPRINTF
	vsprintf(p4_debug_buf + len, fmt, ap);
#else
???
#endif
    }
#endif 

    if (level > p4_debug_level)
	return;
#ifdef USE_PTHREADS
    printf("%d: %s: %u: ", level, whoami_p4, pthread_self());
#else
    printf("%d: %s: ", level, whoami_p4);
#endif
    if (p4_global)
	printf("(%f) ", p4_usclock());
    else
	printf("(-) " );
#ifdef VPRINTF
    vprintf(fmt, ap);
#else
    _doprnt(fmt, ap, stdout);
#endif
    va_end(ap);
    fflush(stdout);
}
#else
#if !defined(USE_OLDSTYLE_STDARG)
#error 'unknown standard arg method'
#endif
P4VOID p4_dprintfl(level, fmt, va_alist)
int level;
char *fmt;
va_dcl
{
    va_list ap;

    if (level > p4_debug_level)
	return;
#ifdef USE_PTHREADS
    printf("%d: %s: %u: ", level, whoami_p4, pthread_self());
#else
    printf("%d: %s: ", level, whoami_p4);
#endif
    if (p4_global)
	printf("(%f) ", p4_usclock());
    else
	printf("(-) " );
    va_start(ap);
#ifdef VPRINTF
    vprintf(fmt, ap);
#else
    _doprnt(fmt, ap, stdout);
#endif
    va_end(ap);
    fflush(stdout);
}
#endif
#endif 

P4VOID dump_global(int level)
{
    int i;
    struct p4_global_data *g = p4_global;
    struct proc_info *p;

    if (level > p4_debug_level)
	return;

    p4_dprintf("Dumping global data for process %d at %x\n", getpid(), g);

    for (i = 0, p = g->proctable; i < g->num_in_proctable; i++, p++)
    {
	p4_dprintf(" proctable entry %d: unix_id = %d host = %s\n",
		   i, p->unix_id, p->host_name);
	p4_dprintf("   port=%d group_id=%d switch_port=%d\n",
		   p->port, p->group_id, p->switch_port);
    }

    p4_dprintf("    listener_pid     = %d\n", g->listener_pid);
    p4_dprintf("    listener_port    = %d\n", g->listener_port);
    p4_dprintf("    local_slave_count= %d\n", g->local_slave_count);
    p4_dprintf("    my_host_name     = %s\n", g->my_host_name);
    p4_dprintf("    num_in_proctable = %d\n", g->num_in_proctable);
}

P4VOID dump_local(level)
int level;
{
    struct local_data *l = p4_local;
    int i;

    if (level > p4_debug_level)
	return;

    p4_dprintf("Dumping local data for process %d at %x\n", getpid(), l);

    for (i = 0; i < p4_global->num_in_proctable; i++)
	p4_dprintf("     %d: conntab[%d]  type:%s    port %d\n", getpid(), i,
		   print_conn_type(p4_local->conntab[i].type),
		   p4_local->conntab[i].port);

    p4_dprintf("    listener_fd = %d\n", l->listener_fd);
    p4_dprintf("    my_id       = %d\n", l->my_id);
    p4_dprintf("    am_bm       = %d\n", l->am_bm);
}

char *print_conn_type(int conn_type)
{
    static char val[20];

    switch (conn_type)
    {
      case CONN_ME:
	return ("CONN_ME");
      case CONN_REMOTE_SWITCH:
	return ("CONN_REMOTE_SWITCH");
      case CONN_REMOTE_NON_EST:
	return ("CONN_REMOTE_NON_EST");
      case CONN_REMOTE_EST:
	return ("CONN_REMOTE_EST");
      case CONN_SHMEM:
	return ("CONN_SHMEM");
      case CONN_CUBE:
	return ("CONN_CUBE");
      case CONN_REMOTE_DYING:
	return ("CONN_REMOTE_DYING");
      case CONN_REMOTE_CLOSED:
	return ("CONN_REMOTE_CLOSED");
      case CONN_REMOTE_OPENING:
	return ("CONN_REMOTE_OPENING");
      default:
	sprintf(val, "invalid: %d  ", conn_type);
	return (val);
    }
}


P4VOID dump_listener(int level)
{
    struct listener_data *l = listener_info;

    if (level > p4_debug_level)
	return;

    p4_dprintf("Dumping listener data for process %d at %x\n", getpid(), l);
    p4_dprintf("    listening_fd = %d\n", l->listening_fd);
}

P4VOID dump_procgroup(struct p4_procgroup *procgroup, int level)
{
    struct p4_procgroup_entry *pe;
    int i;

    if (level > p4_debug_level)
	return;

    p4_dprintf("Procgroup:\n");
    for (pe = procgroup->entries, i = 0;
	 i < procgroup->num_entries;
	 pe++, i++)
	p4_dprintf("    entry %d: %s %d %d %s %s \n",
		   i,
		   pe->host_name,
		   pe->numslaves_in_group,
		   pe->rm_rank, 
		   pe->slave_full_pathname,
		   pe->username);
}

P4VOID dump_tmsg(struct p4_msg *tmsg)
{
    p4_dprintf("type=%d, to=%d, from=%d, len=%d, ack_req=%x, msg=%s\n",
	       tmsg->type, tmsg->to, tmsg->from, tmsg->len, tmsg->ack_req,
	       &(tmsg->msg));
}

P4VOID dump_conntab(int level)
{
    int i;

    if (level > p4_debug_level)
	return;

    for (i = 0; i < p4_global->num_in_proctable; i++)
    {
	p4_dprintf("   %d: conntab[%d] type=%s port=%d switch_port=%d\n",
		   getpid(), i,
		   print_conn_type(p4_local->conntab[i].type),
		   p4_local->conntab[i].port,
		   p4_local->conntab[i].switch_port);
    }
}
