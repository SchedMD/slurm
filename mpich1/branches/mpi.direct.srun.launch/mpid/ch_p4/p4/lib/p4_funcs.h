/* to import correctly for c++ users */
#ifdef __cplusplus
extern "C" {
#endif

double p4_usclock (void);
P4VOID init_usclock (void);
char *p4_shmalloc (int);
P4VOID p4_set_avail_buff (int,int);
P4BOOL p4_am_i_cluster_master (void);
int p4_askfor (p4_askfor_monitor_t *, int, int (*)(P4VOID *), 
			 P4VOID *, P4VOID (*)(void) );
int p4_askfor_init (p4_askfor_monitor_t *);
P4VOID p4_barrier (p4_barrier_monitor_t *, int);
int p4_barrier_init (p4_barrier_monitor_t *);
int p4_broadcastx (int, void *, int, int);
int p4_clock ( void );
int p4_create ( int (*)(void) );
int p4_create_procgroup (void);
int p4_startup (struct p4_procgroup *);
#if defined(DELTA)  ||  defined(NCUBE)
P4VOID p4_dprintf (int,int,int,int,int,int,int,int,int,int);
#if defined(P4_DPRINTFL)
P4VOID p4_dprintfl (int,int,int,int,int,int,int,int,int,int,int);
#endif
#else
#if defined(USE_STDARG) && !defined(USE_OLDSTYLE_STDARG)
P4VOID p4_dprintf (char *, ...);
#if defined(P4_DPRINTFL)
P4VOID p4_dprintfl (int, char *, ... );
#endif
#else
P4VOID p4_dprintf ();
#if defined(P4_DPRINTFL)
P4VOID p4_dprintfl ();
#endif
#endif /* USE_STDARG */
#endif /* DELTA ect */
P4VOID p4_error (char *, int);
P4VOID p4_set_hard_errors (int);
P4VOID p4_global_barrier (int);
P4VOID p4_get_cluster_masters (int *, int *);
P4VOID p4_get_cluster_ids (int *, int *);
int p4_get_my_cluster_id (void);
int p4_get_my_id (void);
int p4_get_my_id_from_proc (void);
int p4_getsub_init (p4_getsub_monitor_t *);
P4VOID p4_getsubs (p4_getsub_monitor_t *, int *, int, int, int);
int p4_global_op (int, void *, int, int, 
			    P4VOID (*)(char *, char *,int), int);
int p4_initenv (int *, char **);
P4VOID p4_post_init ( void );
P4VOID p4_int_absmax_op (char *, char *, int);
P4VOID p4_int_absmin_op (char *, char *, int);
P4VOID p4_int_max_op (char *, char *, int);
P4VOID p4_int_min_op (char *, char *, int);
P4VOID p4_int_mult_op (char *, char *, int);
P4VOID p4_int_sum_op (char *, char *, int);
P4VOID p4_dbl_absmax_op (char *, char *, int);
P4VOID p4_dbl_absmin_op (char *, char *, int);
P4VOID p4_dbl_max_op (char *, char *, int);
P4VOID p4_dbl_min_op (char *, char *, int);
P4VOID p4_dbl_mult_op (char *, char *, int);
P4VOID p4_dbl_sum_op (char *, char *, int);
P4VOID p4_flt_sum_op (char *, char *, int);
P4VOID p4_flt_absmax_op (char *, char *, int);
P4VOID p4_flt_absmin_op (char *, char *, int);
P4VOID p4_flt_max_op (char *, char *, int);
P4VOID p4_flt_min_op (char *, char *, int);
P4VOID p4_flt_mult_op (char *, char *, int);
P4VOID p4_flt_sum_op (char *, char *, int);
P4VOID p4_mcontinue (p4_monitor_t *, int);
P4VOID p4_mdelay (p4_monitor_t *, int);
P4VOID p4_menter (p4_monitor_t *);
P4BOOL p4_messages_available (int *, int *);
P4BOOL p4_any_messages_available (void);
P4VOID p4_mexit (p4_monitor_t *);
int p4_moninit (p4_monitor_t *, int);
P4VOID p4_msg_free (char *);
char *p4_msg_alloc (int);
int p4_num_cluster_ids (void);
int p4_num_total_ids (void);
int p4_num_total_slaves (void);
P4VOID p4_probend (p4_askfor_monitor_t *, int);
P4VOID p4_progend (p4_askfor_monitor_t *);
int p4_recv (int *, int *, char **, int *);
int p4_waitformsg( void );
int p4_get_dbg_level (void);
P4VOID p4_set_dbg_level (int);
P4VOID p4_shfree (P4VOID *);
int p4_soft_errors (int);
P4VOID p4_update (p4_askfor_monitor_t *, int (*)(void *), P4VOID * );
char *p4_version (void);
char *p4_machine_type (void);
int p4_wait_for_end (void);
int p4_proc_info ( int, char **, char ** );
P4VOID p4_print_avail_buffs (void);
struct p4_procgroup *p4_alloc_procgroup (void);
/* The p4 send routines are actually macros that use this routine */
int send_message (int, int, int, char *, int, int, P4BOOL, P4BOOL);

#ifdef __cplusplus
};
#endif

