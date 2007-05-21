
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

char *xx_malloc (int, int);
char *MD_shmalloc (int);
char *print_conn_type (int);
char *xx_shmalloc (unsigned);
int MD_clock (void);
P4VOID get_qualified_hostname (char *, int);
P4VOID MD_set_reference_time (void);
P4VOID MD_initenv (void);
P4VOID MD_initmem (int);
P4VOID MD_malloc_hint (int,int);
P4VOID MD_shfree (char *);
/* P4VOID MD_start_cube_slaves ( ); */
int bm_start (int *, char **)	;
/* P4VOID compute_conntab ( ); */
#ifdef THREAD_LISTENER
int request_connection (int);
#else
P4VOID request_connection (int);
#endif
int create_bm_processes (struct p4_procgroup *);
P4VOID net_slave_info (struct p4_procgroup_entry *, char *, int, int);
int create_remote_processes (struct p4_procgroup *);
P4VOID create_rm_processes (int, int)	;
P4VOID dump_conntab ( int );
P4VOID dump_global (int)	;
P4VOID dump_listener (int)	;
P4VOID dump_local (int)	;
P4VOID dump_procgroup (struct p4_procgroup *, int)	;
P4VOID dump_tmsg (struct p4_msg *);
int p4_has_timedout ( int );
int establish_connection (int);
int p4_establish_all_conns (void);
/* P4VOID exec_pgm ( ); */
P4VOID put_execer_port (int);
int get_execer_port (char *);
void p4_clean_execer_port( void );
int fork_p4 (void);
P4VOID free_p4_msg (struct p4_msg *);
P4VOID free_quel (struct p4_queued_msg *);
P4VOID get_inet_addr (struct in_addr *);
P4VOID get_inet_addr_str (char *);
void p4_print_sock_params( int skt );
void p4_socket_stat( FILE *fp );
#if !defined(CRAY)
P4VOID dump_sockaddr ( char *, struct sockaddr_in *);
P4VOID dump_sockinfo (char *, int);
#endif
void mpiexec_reopen_stdin(void);
int p4_make_socket_nonblocking( int );
P4VOID get_pipe (int *, int *)	;
int getswport (char *);
P4VOID handle_connection_interrupt (int);
P4BOOL shmem_msgs_available (void);
P4BOOL socket_msgs_available (void);
int p4_sockets_ready( int, int );
P4VOID p4_socket_control( char * );
P4BOOL MD_tcmp_msgs_available (int *, int *);
P4BOOL MD_i860_msgs_available (void);
P4BOOL MD_CM5_msgs_available (void);
P4BOOL MD_NCUBE_msgs_available (void);
P4BOOL MD_euih_msgs_available (void);
int MD_i860_send (struct p4_msg *);
int MD_CM5_send (struct p4_msg *);
int MD_NCUBE_send (struct p4_msg *);
int MD_eui_send (struct p4_msg *);
int MD_euih_send (struct p4_msg *);
struct p4_msg *MD_i860_recv (void);
struct p4_msg *MD_CM5_recv (void);
struct p4_msg *MD_NCUBE_recv (void);
struct p4_msg *MD_eui_recv (void);
struct p4_msg *MD_euih_recv (void);
P4BOOL in_same_cluster (int,int);
P4VOID p4_cluster_shmem_sync (P4VOID **);
/* P4VOID **cluster_shmem; */
P4VOID init_avail_buffs (void);
P4VOID free_avail_buffs (void);
P4VOID initialize_msg_queue (struct p4_msg_queue *);
int install_in_proctable (int,int,int,char *,char *,int,char *,int);
#ifdef LAZY_GETHOSTBYNAME
void p4_procgroup_setsockaddr( struct proc_info * );
#endif
/* P4VOID kill_server ( ); */
P4VOID listener (void);
P4VOID thread_listener (void);
struct listener_data *alloc_listener_info (int);
struct local_data *alloc_local_bm (void);
struct local_data *alloc_local_listener (void);
struct local_data *alloc_local_rm (void);
struct local_data *alloc_local_slave (void);
int myhost (void);
int net_accept (int)	;
/* int net_conn_to_addr_listener ( )	; */
int net_conn_to_listener (char *, int, int )	;
/* int net_conn_to_named_listener ( )	; */
int net_create_slave (int, int, char *, char *, char *, int );
int net_recv ( int, void *, int)	;
int net_recv_timeout ( int, void *, int, int)	;
int net_send ( int, void *, int, int )	;
int net_send_w ( int, void *, int, int )	;
int net_send2 ( int, void *, int, void *, int, int )	;
P4VOID net_setup_anon_listener (int, int *, int *)	;
P4VOID net_setup_listener (int, int, int *)	;
/* P4VOID net_setup_named_listener ( )	; */
P4VOID net_set_sockbuf_size ( int, int );
int num_in_mon_queue (p4_monitor_t *,int);
P4VOID alloc_global (void);
struct p4_msg *alloc_p4_msg (int);
struct p4_msg *get_tmsg (int, int, int, char *, int, int, int, int);
struct p4_msg *recv_message (int *, int *);
struct p4_msg *search_p4_queue (int, int, P4BOOL);
struct p4_msg *shmem_recv (void);
struct p4_msg *socket_recv (int);
struct p4_msg *socket_recv_on_fd (int);
struct p4_queued_msg *alloc_quel (void);
void p4_yield( void );
int p4_wait_for_socket_msg( int );
P4VOID free_avail_quels (void);
P4VOID process_args (int *, char **)	;
/* P4BOOL process_connect_request (int)	; */
/* int process_connection ( ); */
/* P4BOOL process_slave_message (int)	; */
struct p4_procgroup *alloc_procgroup (void);
struct p4_procgroup *read_procgroup (void)	;
P4VOID procgroup_to_proctable (struct p4_procgroup *);
P4VOID queue_p4_message (struct p4_msg *, struct p4_msg_queue *);
/* P4VOID reaper ( ); */
P4VOID receive_proc_table (int)	;
/* int rm_newline ( ); */
int rm_start (int *, char **);
P4VOID send_ack (int,int)	;
P4VOID sync_with_remotes (void);
P4VOID send_proc_table (void);
P4VOID setup_conntab (void);
int shmem_send (struct p4_msg *);
P4VOID shutdown_p4_socks (void);
#if defined(GP_1000) || defined(TC_2000)
int simple_lock (int *);
int simple_unlock (int *);
P4VOID waitspin (int);
#endif
P4BOOL sock_msg_avail_on_fd (int);
int socket_send (int,int,int,char *,int,int,int);
int socket_close_conn (int);
void p4_look_for_close( int );
int start_slave (char *, char *, char *, int, char *, 
		 char *(*)(char *, char *));
int subtree_broadcast_p4 (int, int, void *,int,int);
P4VOID trap_sig_errs (void);
P4VOID wait_for_ack (int)	;
int xdr_recv (int, struct p4_msg *);
int xdr_send (int, int, int, char *, int, int, int);
int data_representation (char *);
P4BOOL same_data_representation (int, int);
P4VOID xx_init_shmalloc (char *, unsigned);
P4VOID xx_shfree (char *);
P4VOID zap_p4_processes (void);
P4VOID zap_remote_p4_processes (void);
struct p4_msg *MD_tcmp_recv (void);
int MD_tcmp_send (int, int, int, char *, int, int, int);
void p4_timein_hostbyname( int *, int * );
struct hostent *gethostbyname_p4 ( char *);
int gethostname_p4( char *, size_t );
char *getpw_ss (char *, char * );

P4VOID p4_dprint_last ( FILE * );

#ifdef CHECK_SIGNALS
P4VOID p4_CheckSighandler ( );
#else
#define p4_CheckSighandler( a )
#endif

#ifdef SYSV_IPC
int init_sysv_semset (int);
P4VOID MD_lock_init (MD_lock_t *);
P4VOID MD_lock (MD_lock_t *);
P4VOID MD_unlock (MD_lock_t *);
P4VOID remove_sysv_ipc (void);
#endif

#if defined(NEEDS_STDLIB_PROTOTYPES)
/* Some gcc installations have out-of-date include files and need these
   definitions to handle the "missing" prototypes.  This is NOT
   autodetected, but is provided and can be selected by using a switch
   on the options line.

   These are from stdlib.h, stdio.h, and unistd.h
 */
extern int fprintf(FILE*,const char*,...);
extern int printf(const char*,...);
extern int fflush(FILE *);
extern int fclose(FILE *);
extern int fscanf(FILE *,char *,...);
extern int sscanf(char *, char *, ... );
extern int pclose (FILE *);

/* String.h */
extern void bzero( void *, size_t );
extern void bcopy( const void *, void *, size_t);
extern int bcmp( const void *, const void *, size_t);


extern int gettimeofday( struct timeval *, struct timezone *);
extern void perror(const char *);
extern int  gethostname( char *, int );
extern int  getdtablesize(void);

/* time.h */
extern time_t   time (time_t *);

/* signal.h */
extern int sigblock (int);
extern int sigsetmask (int);

/* sys/socket.h */
extern int socket( int, int, int );
extern int bind( int, const struct sockaddr *, int);
extern int listen( int, int );
extern int accept( int, struct sockaddr *, int *);
extern int connect( int, const struct sockaddr *, int);
extern int socketpair( int, int, int, int[2] );
extern int setsockopt( int, int, int, const void *, int );
extern int getsockopt(int, int, int, void *, int *);
extern int getsockname(int, struct sockaddr *, int *);
extern int getpeername(int, struct sockaddr *, int *);
extern int recv(int, void *, int, int);
extern int shutdown(int, int);

/* netinet/in.h or arpa/inet.h */
extern char *inet_ntoa(struct in_addr);

/* sys/select.h */
int select (int, fd_set *, fd_set *, fd_set *, struct timeval *);

/* sys/time.h */
extern int setitimer (int, struct itimerval *, struct itimerval *);

/* unistd.h */
extern char *getlogin(void);

#ifdef FOO
/* Unistd.h */
extern int  atoi(const char *);
extern int  unlink(const char *);
extern int  close(int);
extern int  read(int, void *, size_t);
extern int  write(int, const void *, size_t);


extern int  getpid(void);
extern int  getppid(void);
extern int  getuid(void);
extern char *getlogin(void);

extern int setitimer( int, const struct itimerval *, struct itimerval *);
extern int sleep(unsigned int);

extern int execlp( const char *, const char *, ... );
extern int fork (void);

#endif
#endif
