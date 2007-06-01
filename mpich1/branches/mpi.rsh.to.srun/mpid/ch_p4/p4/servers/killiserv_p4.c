#include "p4.h"
#include "p4_sys.h"

int debug_level;
char whoami[100];
char local_domain[100];

/* Forward decls */
VOID kill_server ( char * );

main(argc,argv)
int argc;
char **argv;
{
    char host[100];
    int i;
    
    *local_domain = 0;
    debug_level = 0;
    sprintf(whoami, "kill_server_%d", getpid());
    
    if (argc == 1)
    {
	get_qualified_hostname(host);
	kill_server(host);
    }
    else
    {
	for (i = 1; i < argc; i++)
	{
	    kill_server(argv[i]);
	}
    }
}

VOID kill_server(host)
char *host;
{
    struct net_message_t msg;
    int fd;
    
    fd = net_conn_to_listener(host, UNRESERVED_PORT,1);
    if (fd == -1)
    {
	printf("couldn't connect to server on %s\n", host);
	return;
    }
    printf("killing server on %s\n", host);
    
    msg.type = p4_i_to_n(NET_DONE);
    
    net_send(fd, &msg, sizeof(msg), FALSE);
    close(fd);
}

int slave() /* dummy proc */
{
    return(0);
}

