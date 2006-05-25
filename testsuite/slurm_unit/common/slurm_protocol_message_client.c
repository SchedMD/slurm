#include <netinet/in.h>
#include <src/common/slurm_protocol_api.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int main ( int argc , char * argv[] )
{
	slurm_fd worker_socket ;
	slurm_addr worker_address ;
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
	slurm_set_addr_uint ( & worker_address , port , SLURM_INADDR_ANY ) ;
	worker_socket = slurm_open_msg_conn ( & worker_address ) ;

	msg.address = worker_address;		
	msg.msg_type = REQUEST_UPDATE_NODE;
	out_msg.node_state = 0x1234;
	out_msg.node_names = "Test message";
	msg.data = &out_msg;
	slurm_send_node_msg( worker_socket , &msg ) ;

	printf("Sending message=%s\n", out_msg.node_names);
	if ((ret_list = slurm_receive_msg(worker_socket, &resp, 0)) == NULL) {
		printf("Error reading slurm_receive_msg %m\n");
		exit(1);
	} else {
		if(list_count(ret_list)>0) {
			error("We didn't do things correctly "
			      "got %d responses didn't expect any",
			      list_count(ret_list));
		}
		list_destroy(ret_list);
	}
	if (resp.msg_type != REQUEST_UPDATE_NODE) {
		printf("Got wrong message type: %u\n", resp.msg_type);
		exit(1);
	}
	in_msg = (update_node_msg_t *) resp.data;
	printf("Message received=%s\n", in_msg->node_names);

	msg.address = worker_address;		
	msg.msg_type = REQUEST_SHUTDOWN_IMMEDIATE;
	printf("Sending server shutdown request\n");
	slurm_send_node_msg( worker_socket , &msg ) ;

	slurm_shutdown_msg_conn ( worker_socket ) ;

	return 0 ;
}
