#include <netinet/in.h>
#include <src/common/slurm_protocol_api.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int main ( int argc , char * argv[] )
{
	slurm_fd listen_socket ;
	slurm_fd worker_socket ;
	slurm_addr peer_address ;
	slurm_msg_t msg;
	slurm_msg_t resp;
	int16_t port = 0;
	update_node_msg_t *in_msg, out_msg;
	List ret_list = NULL;
		
	/* init address sturctures */
	if (argc > 1)
		port = atoi( argv[1] ) ;

	if ((argc < 2) || (port < 1)) {
		printf("Usage: %s <port_number>\n", argv[0] );
		exit( 1 );
	}
	listen_socket = slurm_init_msg_engine_port (port);
	printf ( "listen socket %i\n", listen_socket ) ;

	worker_socket = slurm_accept_msg_conn (  listen_socket , & peer_address ) ;
	printf ( "worker socket %i\n", worker_socket ) ;

	while (1) {
		if ((ret_list = slurm_receive_msg(worker_socket, &resp, 0)) 
		    == NULL) {
			printf ("Error reading slurm_receive_msg %m\n");
			break;
		} else {
			if(list_count(ret_list)>0) {
				error("We didn't do things correctly "
				      "got %d responses didn't expect any",
				      list_count(ret_list));
			}
			list_destroy(ret_list);
		}

		if (msg.msg_type == REQUEST_SHUTDOWN_IMMEDIATE) {
			printf ("processing shutdown request\n");
			break;
		}
		if (msg.msg_type == REQUEST_UPDATE_NODE) {
			in_msg = (update_node_msg_t *) msg.data;
			if (msg.data_size > 0)
				printf ("Message received=%s\n",in_msg->node_names);
		}

		resp.address = msg.address;		
		resp.msg_type = REQUEST_UPDATE_NODE;
		out_msg.node_state = 0x1234;
		out_msg.node_names = "Message received";
		resp.data = &out_msg;
		printf("Sending message=%s\n", out_msg.node_names);
		slurm_send_node_msg( worker_socket , &resp ) ;

	}

	slurm_shutdown_msg_engine ( worker_socket ) ;
	return 0 ;
}
