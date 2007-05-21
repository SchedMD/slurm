#include "sockets.h"

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
