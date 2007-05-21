#include "stdafx.h"
#include "sockets.h"
#include <stdio.h>

// Function name	: Simple_create_bind_socket
// Description	    : 
// Return type		: int 
// Argument         : SOCKET *sock
// Argument         : WSAEVENT *event
// Argument         : int port /*=0*/
// Argument         : unsigned long addr /*=INADDR_ANY*/
int Simple_create_bind_socket(SOCKET *sock, WSAEVENT *event, int port /*=0*/, unsigned long addr /*=INADDR_ANY*/)
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

// Function name	: Simple_connect
// Description	    : 
// Return type		: int 
// Argument         : SOCKET sock
// Argument         : char *host
// Argument         : int port
int Simple_connect(SOCKET sock, char *host, int port)
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
			printf("Unable to connect to %s on port %d. Error: %d", host, port, error);
			break;
		}
	}
	return 0;
}

// Function name	: Simple_closesocket
// Description	    : 
// Return type		: int 
// Argument         : SOCKET sock
// Argument         : WSAEVENT event
int Simple_closesocket(SOCKET sock, WSAEVENT event)
{
	int ret_val;
	shutdown(sock, SD_BOTH);
	ret_val = closesocket(sock);
	if (event)
		WSACloseEvent(event);
	return ret_val;
}

// Function name	: Simple_get_sock_info
// Description	    : 
// Return type		: int 
// Argument         : SOCKET sock
// Argument         : char *name
// Argument         : int *port
int Simple_get_sock_info(SOCKET sock, char *name, int *port)
{
	sockaddr_in addr;
	int name_len = sizeof(addr);
	getsockname(sock, (sockaddr*)&addr, &name_len);
	*port = ntohs(addr.sin_port);
	gethostname(name, 100);
	return 0;
}

// Function name	: SendBlocking
// Description	    : 
// Return type		: int 
// Argument         : SOCKET sock
// Argument         : char *buffer
// Argument         : int length
// Argument         : int flags
int SendBlocking(SOCKET sock, char *buffer, int length, int flags)
{
	WSABUF buf;
	int error;
	DWORD num_sent;

	buf.buf = buffer;
	buf.len = length;

	while (WSASend(sock, &buf, 1, &num_sent, flags, NULL, NULL) == SOCKET_ERROR)
	{
		error = WSAGetLastError();
		if (error == WSAEWOULDBLOCK)
		{
			continue;
		}
		if (error == WSAENOBUFS)
		{
			// If there is no buffer space available then split the buffer in half and send each piece separately.
			SendBlocking(sock, buf.buf, buf.len/2, flags);
			SendBlocking(sock, buf.buf+(buf.len/2), buf.len - (buf.len/2), flags);
			return length;
		}
		WSASetLastError(error);
		return SOCKET_ERROR;
	}

	return length;
}

// Function name	: ReceiveBlocking
// Description	    : 
// Return type		: int 
// Argument         : SOCKET sock
// Argument         : WSAEVENT event
// Argument         : char *buffer
// Argument         : int len
// Argument         : int flags
int ReceiveBlocking(SOCKET sock, WSAEVENT event, char *buffer, int len, int flags)
{
	int num_received, error;
	WSANETWORKEVENTS nevents;
	DWORD ret_val;
	
	num_received = recv(sock, buffer, len, flags);
	if (num_received == SOCKET_ERROR)
	{
		error = WSAGetLastError();
		if (error != WSAEWOULDBLOCK)
			return error;
	}
	else
	{
		len -= num_received;
		buffer += num_received;
	}

	while (len)
	{
	
		//if (WSAWaitForMultipleEvents(1, &event, TRUE, INFINITE, FALSE) != WSA_WAIT_EVENT_0)
		//if (WSAWaitForMultipleEvents(1, &event, TRUE, 3000, FALSE) != WSA_WAIT_EVENT_0)
		//	return WSAGetLastError();
		//ret_val = WSAWaitForMultipleEvents(1, &event, TRUE, 3000, FALSE);
		ret_val = WSAWaitForMultipleEvents(1, &event, TRUE, INFINITE, FALSE);
		if (ret_val == WSA_WAIT_FAILED)
			return WSAGetLastError();
		if (ret_val != WSA_WAIT_EVENT_0)
			return ret_val;

		if (WSAEnumNetworkEvents(sock, event, &nevents) == SOCKET_ERROR)
			return WSAGetLastError();

		if (nevents.lNetworkEvents & FD_READ)
		{
			num_received = recv(sock, buffer, len, flags);

			if (num_received == SOCKET_ERROR)
			{
				error = WSAGetLastError();
				if (error != WSAEWOULDBLOCK)
					return error;
			}
			else
			{
				len -= num_received;
				buffer += num_received;
			}
		}
		else
		{
			if (nevents.lNetworkEvents & FD_CLOSE)
			{
				return 1;
			}
		}
	}
	return 0;
}

// Function name	: ReceiveSome
// Description	    : 
// Return type		: int 
// Argument         : SOCKET sock
// Argument         : WSAEVENT event
// Argument         : char *buffer
// Argument         : int len
// Argument         : int flags
int ReceiveSome(SOCKET sock, WSAEVENT event, char *buffer, int len, int flags)
{
	int num_received, error;
	WSANETWORKEVENTS nevents;
	DWORD ret_val;

	num_received = recv(sock, buffer, len, flags);
	if (num_received == SOCKET_ERROR)
	{
		error = WSAGetLastError();
		if (error != WSAEWOULDBLOCK)
			return SOCKET_ERROR;
	}
	else
	{
		return num_received;
	}

	while (len)
	{
		ret_val = WSAWaitForMultipleEvents(1, &event, TRUE, INFINITE, FALSE);
		if (ret_val == WSA_WAIT_FAILED)
			return SOCKET_ERROR;
		if (ret_val != WSA_WAIT_EVENT_0)
			return SOCKET_ERROR;

		if (WSAEnumNetworkEvents(sock, event, &nevents) == SOCKET_ERROR)
			return SOCKET_ERROR;

		if (nevents.lNetworkEvents & FD_READ)
		{
			num_received = recv(sock, buffer, len, flags);

			if (num_received == SOCKET_ERROR)
			{
				error = WSAGetLastError();
				if (error != WSAEWOULDBLOCK)
					return SOCKET_ERROR;
			}
			else
				return num_received;
		}
		else
		{
			if (nevents.lNetworkEvents & FD_CLOSE)
			{
				return SOCKET_ERROR;
				//return 0;
			}
		}
	}
	return 0;
}

// Function name	: ReceiveBlockingTimeout
// Description	    : 
// Return type		: int 
// Argument         : SOCKET sock
// Argument         : WSAEVENT event
// Argument         : char *buffer
// Argument         : int len
// Argument         : int flags
// Argument         : int timeout
int ReceiveBlockingTimeout(SOCKET sock, WSAEVENT event, char *buffer, int len, int flags, int timeout)
{
	int num_received, error;
	WSANETWORKEVENTS nevents;
	DWORD ret_val;
	
	num_received = recv(sock, buffer, len, flags);
	if (num_received == SOCKET_ERROR)
	{
		error = WSAGetLastError();
		if (error != WSAEWOULDBLOCK)
			return error;
	}
	else
	{
		len -= num_received;
		buffer += num_received;
	}

	while (len)
	{
		ret_val = WSAWaitForMultipleEvents(1, &event, TRUE, timeout, FALSE);
		if (ret_val == WSA_WAIT_FAILED)
			return WSAGetLastError();
		if (ret_val != WSA_WAIT_EVENT_0)
			return ret_val;

		if (WSAEnumNetworkEvents(sock, event, &nevents) == SOCKET_ERROR)
			return WSAGetLastError();

		if (nevents.lNetworkEvents & FD_READ)
		{
			num_received = recv(sock, buffer, len, flags);

			if (num_received == SOCKET_ERROR)
			{
				error = WSAGetLastError();
				if (error != WSAEWOULDBLOCK)
					return error;
			}
			else
			{
				len -= num_received;
				buffer += num_received;
			}
		}
		else
		{
			if (nevents.lNetworkEvents & FD_CLOSE)
			{
				return 1;
			}
		}
	}
	return 0;
}
