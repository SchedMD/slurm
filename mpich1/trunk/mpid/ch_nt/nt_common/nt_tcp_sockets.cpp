#include "nt_tcp_sockets.h"
#include "nt_log.h"

// Function name	: NT_Tcp_create_bind_socket
// Description	    : 
// Return type		: int 
// Argument         : SOCKET *sock
// Argument         : WSAEVENT *event
// Argument         : int port
// Argument         : unsigned long addr
int NT_Tcp_create_bind_socket(SOCKET *sock, WSAEVENT *event, int port /*=0*/, unsigned long addr /*=INADDR_ANY*/)
{
#ifdef USE_LINGER_SOCKOPT
	struct linger linger;
#endif
	int optval, len;

	// create the event
	*event = WSACreateEvent();
	if (*event == WSA_INVALID_EVENT)
		return WSAGetLastError();

	// create the socket
	*sock = WSASocket(PF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (*sock == INVALID_SOCKET)
		return WSAGetLastError();

	SOCKADDR_IN sockAddr;
	memset(&sockAddr,0,sizeof(sockAddr));
	
	sockAddr.sin_family = AF_INET;
	sockAddr.sin_addr.s_addr = addr;
	sockAddr.sin_port = htons((unsigned short)port);
	
	if (bind(*sock, (SOCKADDR*)&sockAddr, sizeof(sockAddr)) == SOCKET_ERROR)
		return WSAGetLastError();

#ifdef USE_LINGER_SOCKOPT
	/* Set the linger on close option */
	linger.l_onoff = 1 ;
	linger.l_linger = 60;
	setsockopt(*sock, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));
#endif

	len = sizeof(int);
	if (!getsockopt(*sock, SOL_SOCKET, SO_RCVBUF, (char*)&optval, &len))
	{
	    optval = 32*1024;
	    setsockopt(*sock, SOL_SOCKET, SO_RCVBUF, (char*)&optval, sizeof(int));
	}
	len = sizeof(int);
	if (!getsockopt(*sock, SOL_SOCKET, SO_SNDBUF, (char*)&optval, &len))
	{
	    optval = 32*1024;
	    setsockopt(*sock, SOL_SOCKET, SO_SNDBUF, (char*)&optval, sizeof(int));
	}
	
	return 0;
}

// Function name	: NT_Tcp_connect
// Description	    : 
// Return type		: int 
// Argument         : SOCKET sock
// Argument         : char *host
// Argument         : int port
int NT_Tcp_connect(SOCKET sock, char *host, int port)
{
	SOCKADDR_IN sockAddr;
	memset(&sockAddr,0,sizeof(sockAddr));

	sockAddr.sin_family = AF_INET;
	sockAddr.sin_addr.s_addr = inet_addr(host);

	if (sockAddr.sin_addr.s_addr == INADDR_NONE)
	{
		LPHOSTENT lphost;
		lphost = gethostbyname(host);
		if (lphost != NULL)
			sockAddr.sin_addr.s_addr = ((LPIN_ADDR)lphost->h_addr)->s_addr;
		else
		{
			return WSAEINVAL;
		}
	}

	sockAddr.sin_port = htons((u_short)port);

	DWORD error;
	int reps = 0;
	while (connect(sock, (SOCKADDR*)&sockAddr, sizeof(sockAddr)) == SOCKET_ERROR)
	{
		error = WSAGetLastError();
		if( (error == WSAECONNREFUSED || error == WSAETIMEDOUT || error == WSAENETUNREACH || error == WSAEADDRINUSE)
			&& (reps < 15) )
		{
			double d = (double)rand() / (double)RAND_MAX;
			Sleep(200 + (int)(d*200));
			reps++;
			switch (error)
			{
			case WSAECONNREFUSED:
			    LogMsg("WSAECONNREFUSED error, re-attempting connect");
			    break;
			case WSAETIMEDOUT:
			    LogMsg("WSAETIMEDOUT error, re-attempting connect");
			    break;
			case WSAENETUNREACH:
			    LogMsg("WSAENETUNREACH error, re-attempting connect");
			    break;
			case WSAEADDRINUSE:
			    LogMsg("WSAEADDRINUSE error, re-attempting connect");
			    break;
			default:
			    LogMsg("%d error, re-attempting connect");
			    break;
			}
		}
		else
		{
			MakeErrMsg(error, "Unable to connect to %s on port %d", host, port);
			break;
		}
	}
	return 0;
}

// Function name	: NT_Tcp_closesocket
// Description	    : 
// Return type		: int 
// Argument         : SOCKET sock
// Argument         : WSAEVENT event
int NT_Tcp_closesocket(SOCKET sock, WSAEVENT event)
{
	shutdown(sock, SD_BOTH);
	closesocket(sock);
	if (event)
		WSACloseEvent(event);
	return 0;
}

// Function name	: NT_Tcp_get_sock_info
// Description	    : 
// Return type		: int 
// Argument         : SOCKET sock
// Argument         : char *name
// Argument         : int *port
int NT_Tcp_get_sock_info(SOCKET sock, char *name, int *port)
{
	sockaddr_in addr;
	int name_len = sizeof(addr);
	getsockname(sock, (sockaddr*)&addr, &name_len);
	*port = ntohs(addr.sin_port);
	gethostname(name, 100);
	return 0;
}

// Function name	: NT_Tcp_get_ip_string
// Description	    : 
// Return type		: int 
// Argument         : char *host
// Argument         : char *ipstr
int NT_Tcp_get_ip_string(char *host, char *ipstr)
{
    unsigned int a, b, c, d;
    struct hostent *pH;
    
    pH = gethostbyname(host);
    if (pH == NULL)
	return FALSE;

    a = (unsigned char)(pH->h_addr_list[0][0]);
    b = (unsigned char)(pH->h_addr_list[0][1]);
    c = (unsigned char)(pH->h_addr_list[0][2]);
    d = (unsigned char)(pH->h_addr_list[0][3]);

    sprintf(ipstr, "%u.%u.%u.%u", a, b, c, d);

    return TRUE;
}
