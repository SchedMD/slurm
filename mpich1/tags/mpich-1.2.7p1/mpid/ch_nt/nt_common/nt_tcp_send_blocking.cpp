#include "nt_tcp_sockets.h"

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
            //Sleep(0);
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

int g_nStreamSize = 8*1024;

int SendStreamBlocking(SOCKET sock, char *buffer, int length, int type)
{
    WSABUF wsabuf[3];
    DWORD num_sent;
    int total = length;
    DWORD flag;

    if (length < 0)
    {
	MakeErrMsg(-1, "SendStreamBlocking cannot send %d bytes.", length);
	return SOCKET_ERROR;
    }

    wsabuf[0].buf = (char*)&type;
    wsabuf[0].len = sizeof(int);
    wsabuf[1].buf = (char*)&total;
    wsabuf[1].len = sizeof(int);
    wsabuf[2].buf = (char*)buffer;
    wsabuf[2].len = min(length, g_nStreamSize);
    
    while (WSASend(sock, wsabuf, 3, &num_sent, 0, NULL, NULL) == SOCKET_ERROR)
    {
	if (WSAGetLastError() != WSAEWOULDBLOCK)
	{
	    return SOCKET_ERROR;
	}
	//printf(".");fflush(stdout);
	// make the socket blocking again
	flag = 0;
	ioctlsocket(sock, FIONBIO, &flag);
    }
    
    num_sent -= 2*sizeof(int); // remove the type and length from the num_sent calculation
    
    length -= num_sent;
    buffer += num_sent;

    /*
    if (length)
    {
	SendBlocking(sock, buffer, length, 0);
    }
    */
    while (length > 0)
    {
	wsabuf[0].buf = buffer;
	wsabuf[0].len = min(length, g_nStreamSize);

	while (WSASend(sock, wsabuf, 1, &num_sent, 0, NULL, NULL) == SOCKET_ERROR)
	{
	    if (WSAGetLastError() != WSAEWOULDBLOCK)
	    {
		return SOCKET_ERROR;
	    }
	    //printf(".");fflush(stdout);
	    // make the socket blocking again
	    flag = 0;
	    ioctlsocket(sock, FIONBIO, &flag);
	}

	length -= num_sent;
	buffer += num_sent;
    }

    return total;
}

/*
HANDLE g_hStreamEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
int g_nStreamSize = 8*1024;

int SendStreamBlocking(SOCKET sock, char *buffer, int length, int type)
{
    WSABUF wsabuf[3];
    DWORD num_sent, flags;
    OVERLAPPED ovl;
    int total = length;
    
    ovl.hEvent = g_hStreamEvent;

    wsabuf[0].buf = (char*)&type;
    wsabuf[0].len = sizeof(int);
    wsabuf[1].buf = (char*)&total;
    wsabuf[1].len = sizeof(int);
    wsabuf[2].buf = (char*)buffer;
    wsabuf[2].len = min(length, g_nStreamSize);
    
    if (WSASend(sock, wsabuf, 3, &num_sent, 0, &ovl, NULL) == SOCKET_ERROR)
    {
	if (WSAGetLastError() != WSA_IO_PENDING)
	{
	    nt_error_socket("SendStreamBlocking failed to send the type and length", WSAGetLastError());
	    return SOCKET_ERROR;
	}
	
	if (!WSAGetOverlappedResult(sock, &ovl, &num_sent, TRUE, &flags))
	{
	    nt_error_socket("SendStreamBlocking: WSAGetOverlappedResult failed", WSAGetLastError());
	    return SOCKET_ERROR;
	}
    }
    
    if (num_sent < 2*sizeof(int))
    {
	nt_error("SendStreamBlocking failed to send the type and length", -1);
	return SOCKET_ERROR;
    }
    num_sent -= 2*sizeof(int); // remove the type and length from the num_sent calculation
    
    length -= num_sent;
    buffer += num_sent;

    while (length)
    {
	wsabuf[0].buf = buffer;
	wsabuf[0].len = min(length, g_nStreamSize);

	if (WSASend(sock, wsabuf, 1, &num_sent, 0, &ovl, NULL) == SOCKET_ERROR)
	{
	    if (WSAGetLastError() != WSA_IO_PENDING)
	    {
		nt_error_socket("SendStreamBlocking failed", WSAGetLastError());
		return SOCKET_ERROR;
	    }
	    
	    if (!WSAGetOverlappedResult(sock, &ovl, &num_sent, TRUE, &flags))
	    {
		nt_error_socket("SendStreamBlocking: WSAGetOverlappedResult failed", WSAGetLastError());
		return SOCKET_ERROR;
	    }
	}

	length -= num_sent;
	buffer += num_sent;
    }

    return total;
}
*/
