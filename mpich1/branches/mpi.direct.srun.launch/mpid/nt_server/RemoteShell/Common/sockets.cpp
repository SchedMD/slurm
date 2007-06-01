#include "sockets.h"
#include <stdio.h>

// Function name	: NT_Tcp_create_bind_socket
// Description	    : 
// Return type		: int 
// Argument         : SOCKET *sock
// Argument         : WSAEVENT *event
// Argument         : int port
// Argument         : unsigned long addr
int NT_create_bind_socket(SOCKET *sock, WSAEVENT *event, int port /*=0*/, unsigned long addr /*=INADDR_ANY*/)
{
#ifdef USE_LINGER_SOCKOPT
	struct linger linger;
#endif
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
	return 0;
}

// Function name	: NT_Tcp_connect
// Description	    : 
// Return type		: int 
// Argument         : SOCKET sock
// Argument         : char *host
// Argument         : int port
int NT_connect(SOCKET sock, char *host, int port)
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
		if( (error == WSAECONNREFUSED || error == WSAETIMEDOUT || error == WSAENETUNREACH)
			&& (reps < 10) )
		{
			Sleep(200);
			reps++;
		}
		else
		{
			//MakeErrMsg(error, "Unable to connect to %s on port %d", host, port);
			printf("Unable to connect to %s on port %d: %d\n", host, port, error);
			return error;
		}
	}
	return 0;
}

// Function name	: NT_Tcp_closesocket
// Description	    : 
// Return type		: int 
// Argument         : SOCKET sock
// Argument         : WSAEVENT event
int NT_closesocket(SOCKET sock, WSAEVENT event)
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
int NT_get_sock_info(SOCKET sock, char *name, int *port)
{
	int error;
	sockaddr_in addr;
	int name_len = sizeof(addr);
	error = getsockname(sock, (sockaddr*)&addr, &name_len);
	if (error)
		return error;
	*port = ntohs(addr.sin_port);
	gethostname(name, 100);
	return 0;
}

// Function name	: NT_get_ip
// Description	    : 
// Return type		: int 
// Argument         : char *host
// Argument         : unsigned long *pIP
int NT_get_ip(char *host, unsigned long *pIP)
{
	// I don't know why, but if I try to store the ip directly in the unsigned long
	// this code fails.  I have to store the return value in a SOCKADDR structure
	// and then copy it to pIP.
	SOCKADDR_IN sockAddr;

	sockAddr.sin_addr.s_addr = inet_addr(host);
	//*pIP = inet_addr(host);

	if (sockAddr.sin_addr.s_addr == INADDR_NONE)
	//if ((*pIP) == INADDR_NONE)
	{
		LPHOSTENT lphost;
		lphost = gethostbyname(host);
		if (lphost != NULL)
			sockAddr.sin_addr.s_addr = ((LPIN_ADDR)lphost->h_addr)->s_addr;
			//*pIP = ((LPIN_ADDR)lphost->h_addr)->s_addr;
		else
			return WSAEINVAL;
	}
	*pIP = sockAddr.sin_addr.s_addr;
	return 0;
}

// Function name	: NT_get_host
// Description	    : 
// Return type		: int 
// Argument         : unsigned long nIP
// Argument         : char *host
int NT_get_host(unsigned long nIP, char *host)
{
	LPHOSTENT lphost;
	lphost = gethostbyaddr((const char *)&nIP, sizeof(unsigned long), AF_INET);
	if (lphost != NULL)
	{
		strcpy(host, lphost->h_name);
		return 0;
	}
	return WSAGetLastError();
}
