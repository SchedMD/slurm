#include <stdio.h>

void handle_listen_input( int );
void handle_client_listen_input( int );
void handle_mpirun_input( int );
void handle_parent_mpd_input( int );
void handle_client_msgs_input( int );
void handle_lhs_msgs_input( int );
void handle_rhs_msgs_input( int );
void handle_con_stdin_input( int );
void handle_con_cntl_input( int );
void handle_client_stdout_input( int );
void handle_client_stderr_input( int );
void handle_tree_stdout_input( int );
void handle_tree_stderr_input( int );
void man_handle_input_fd( int );
void man_cli_alive( int );
void man_cli_findclient ( int );
void man_cli_abort_job ( int );
void man_cli_interrupt_peer_with_msg( int );
void man_cli_bnr_get( int );
void man_cli_bnr_put( int );
void man_cli_bnr_fence_in( int );
void man_cleanup( void );
void man_compute_nodes_in_print_tree( int, int, int *, int *, int * );
void man_cli_client_ready( int );
void man_cli_accepting_signals( int );
void sig_all( char * );
int  get_client_idx_from_fdtable_idx( int );

/* defines for status of client to handle signals to read its pipe */
#define NOT_ACCEPTING_SIGNALS   0
#define ACCEPTING_SIGNALS       1
#define SIGNALS_TO_BE_SENT      2

/* mananger defines for handlers are in mpd.h */
