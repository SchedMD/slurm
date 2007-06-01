#include "nt_tcp_sockets.h"
#include <stdio.h>

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
		DPRINTF(("num_received: %d, num_remaining: %d\n", num_received, len));
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

		while (true)
		{
		    if (WSAEnumNetworkEvents(sock, event, &nevents) == 0)
			break;
		    error = WSAGetLastError();
		    if (error != WSAEWOULDBLOCK)
			return error;
		}

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
				DPRINTF(("num_received: %d, num_remaining: %d\n", num_received, len));
			}
		}
		else
		{
			if (nevents.lNetworkEvents & FD_CLOSE)
			{
				DPRINTF(("process %d: Receive_Blocking: socket closed.\n", g_nIproc));
				return 1;
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

		while (true)
		{
		    if (WSAEnumNetworkEvents(sock, event, &nevents) == 0)
			break;
		    error = WSAGetLastError();
		    if (error != WSAEWOULDBLOCK)
			return error;
		}

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
				DPRINTF(("process %d: Receive_Blocking: socket closed.\n", g_nIproc));
				return 1;
			}
		}
	}
	return 0;
}
