#include "mpdutil.h"
#include <stdio.h>
#include <time.h>

int g_beasy_connection_attempts = 10;

int easy_socket_init()
{
    WORD wVersionRequested;
    WSADATA wsaData;

    wVersionRequested = MAKEWORD( 2, 2 );
    return WSAStartup( wVersionRequested, &wsaData );
}

int easy_socket_finalize()
{
    return WSACleanup();
}

int easy_create(SOCKET *sock, int port /*=0*/, unsigned long addr /*=INADDR_ANY*/)
{
    struct linger linger;
    int optval, len;
    SOCKET temp_sock;
    BOOL b;

    // create the socket
    temp_sock = WSASocket(PF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (temp_sock == INVALID_SOCKET)
	return WSAGetLastError();
    
    SOCKADDR_IN sockAddr;
    memset(&sockAddr,0,sizeof(sockAddr));
    
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_addr.s_addr = addr;
    sockAddr.sin_port = htons((unsigned short)port);
    
    if (bind(temp_sock, (SOCKADDR*)&sockAddr, sizeof(sockAddr)) == SOCKET_ERROR)
	return WSAGetLastError();
    
    b = TRUE;
    setsockopt(temp_sock, IPPROTO_TCP, TCP_NODELAY, (char*)&b, sizeof(BOOL));

    /* Set the linger on close option */
    linger.l_onoff = 1 ;
    linger.l_linger = 60;
    setsockopt(temp_sock, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));
    
    len = sizeof(int);
    if (!getsockopt(temp_sock, SOL_SOCKET, SO_RCVBUF, (char*)&optval, &len))
    {
	optval = 32*1024;
	setsockopt(temp_sock, SOL_SOCKET, SO_RCVBUF, (char*)&optval, sizeof(int));
    }
    len = sizeof(int);
    if (!getsockopt(temp_sock, SOL_SOCKET, SO_SNDBUF, (char*)&optval, &len))
    {
	optval = 32*1024;
	setsockopt(temp_sock, SOL_SOCKET, SO_SNDBUF, (char*)&optval, sizeof(int));
    }

    DuplicateHandle(
	GetCurrentProcess(), (HANDLE)temp_sock,
	GetCurrentProcess(), (HANDLE*)sock,
	0, FALSE, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS);
    return 0;
}

SOCKET easy_accept(SOCKET sock)
{
    BOOL b;
    struct linger linger;
    struct sockaddr addr;
    int len;
    SOCKET temp_sock, client;

    len = sizeof(addr);
    temp_sock = accept(sock, &addr, &len);

    if (temp_sock == INVALID_SOCKET)
    {
	return INVALID_SOCKET;
    }

    linger.l_onoff = 1;
    linger.l_linger = 60;
    setsockopt(temp_sock, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));

    b = TRUE;
    setsockopt(temp_sock, IPPROTO_TCP, TCP_NODELAY, (char*)&b, sizeof(BOOL));

    DuplicateHandle(
	GetCurrentProcess(), (HANDLE)temp_sock,
	GetCurrentProcess(), (HANDLE*)&client,
	0, FALSE, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS);
    return client;
}

int easy_connect_quick(SOCKET sock, char *host, int port)
{
    struct hostent *lphost;
    struct sockaddr_in sockAddr;
    memset(&sockAddr,0,sizeof(sockAddr));
    
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_addr.s_addr = inet_addr(host);
    
    if (sockAddr.sin_addr.s_addr == INADDR_NONE || sockAddr.sin_addr.s_addr == 0)
    {
	lphost = gethostbyname(host);
	if (lphost != NULL)
	    sockAddr.sin_addr.s_addr = ((struct in_addr *)lphost->h_addr)->s_addr;
	else
	    return SOCKET_ERROR;
    }
    
    sockAddr.sin_port = htons((u_short)port);
    
    return connect(sock, (SOCKADDR*)&sockAddr, sizeof(sockAddr));
}

int easy_connect(SOCKET sock, char *host, int port)
{
    int error;
    int reps = 0;
    struct sockaddr_in sockAddr;
    /* use this array to make sure the warning only gets logged once */
    BOOL bWarningLogged[4] = { FALSE, FALSE, FALSE, FALSE };
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
	    WSASetLastError(WSAEINVAL);
	    return SOCKET_ERROR;
	}
    }
    
    sockAddr.sin_port = htons((u_short)port);
    
    while (connect(sock, (SOCKADDR*)&sockAddr, sizeof(sockAddr)) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	srand(clock());
	if( (error == WSAECONNREFUSED || error == WSAETIMEDOUT || error == WSAENETUNREACH || error == WSAEADDRINUSE)
	    && (reps < g_beasy_connection_attempts) )
	{
	    double d = (double)rand() / (double)RAND_MAX;
	    Sleep(200 + (int)(d*200));
	    reps++;
	    switch (error)
	    {
	    case WSAECONNREFUSED:
		if (!bWarningLogged[0])
		{
		    /*err_printf("WSAECONNREFUSED error, re-attempting easy_connect(%s)\n", host);*/
		    bWarningLogged[0] = TRUE;
		}
		break;
	    case WSAETIMEDOUT:
		if (!bWarningLogged[1])
		{
		    err_printf("easy_connect::WSAETIMEDOUT error, re-attempting easy_connect(%s)\n", host);
		    bWarningLogged[1] = TRUE;
		}
		break;
	    case WSAENETUNREACH:
		if (!bWarningLogged[2])
		{
		    err_printf("easy_connect::WSAENETUNREACH error, re-attempting easy_connect(%s)\n", host);
		    bWarningLogged[2] = TRUE;
		}
		break;
	    case WSAEADDRINUSE:
		if (!bWarningLogged[3])
		{
		    err_printf("easy_connect::WSAEADDRINUSE error, re-attempting easy_connect(%s)\n", host);
		    bWarningLogged[3] = TRUE;
		}
		break;
	    default:
		err_printf("easy_connect::error %d, re-attempting easy_connect\n", error);
		break;
	    }
	}
	else
	{
	    return SOCKET_ERROR;
	}
    }
    return 0;
}

int easy_connect_timeout(SOCKET sock, char *host, int port, int seconds)
{
    BOOL b;
    clock_t start, current;
    int error;
    int reps = 0;
    struct hostent *lphost;
    struct sockaddr_in sockAddr;
    /* use this array to make sure the warning only gets logged once */
    BOOL bWarningLogged[4] = { FALSE, FALSE, FALSE, FALSE };

    start = clock();

    memset(&sockAddr,0,sizeof(sockAddr));
    
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_addr.s_addr = inet_addr(host);
    
    if (sockAddr.sin_addr.s_addr == INADDR_NONE || sockAddr.sin_addr.s_addr == 0)
    {
	lphost = gethostbyname(host);
	if (lphost != NULL)
	    sockAddr.sin_addr.s_addr = ((struct in_addr *)lphost->h_addr)->s_addr;
	else
	{
	    WSASetLastError(WSAEINVAL);
	    return SOCKET_ERROR;
	}
    }
    
    sockAddr.sin_port = htons((u_short)port);
    
    while (connect(sock, (SOCKADDR*)&sockAddr, sizeof(sockAddr)) == SOCKET_ERROR)
    {
	current = clock();
	if (((current - start) / CLOCKS_PER_SEC) > seconds)
	{
	    WSASetLastError(WSAETIMEDOUT);
	    return SOCKET_ERROR;
	}
	error = WSAGetLastError();
	srand(clock());
	if( (error == WSAECONNREFUSED || error == WSAETIMEDOUT || error == WSAENETUNREACH || error == WSAEADDRINUSE)
	    && (reps < g_beasy_connection_attempts) )
	{
	    double d = (double)rand() / (double)RAND_MAX;
	    Sleep(200 + (int)(d*200));
	    reps++;
	    switch (error)
	    {
	    case WSAECONNREFUSED:
		if (!bWarningLogged[0])
		{
		    /*err_printf("WSAECONNREFUSED error, re-attempting easy_connect_timeout(%s)", host);*/
		    bWarningLogged[0] = TRUE;
		}
		break;
	    case WSAETIMEDOUT:
		if (!bWarningLogged[1])
		{
		    err_printf("easy_connect_timeout::WSAETIMEDOUT error, re-attempting easy_connect_timeout(%s)\n", host);
		    bWarningLogged[1] = TRUE;
		}
		break;
	    case WSAENETUNREACH:
		if (!bWarningLogged[2])
		{
		    err_printf("easy_connect_timeout::WSAENETUNREACH error, re-attempting easy_connect_timeout(%s)\n", host);
		    bWarningLogged[2] = TRUE;
		}
		break;
	    case WSAEADDRINUSE:
		if (!bWarningLogged[3])
		{
		    err_printf("easy_connect_timeout::WSAEADDRINUSE error, re-attempting easy_connect_timeout(%s)\n", host);
		    bWarningLogged[3] = TRUE;
		}
		break;
	    default:
		err_printf("easy_connect_timeout::error %d, re-attempting easy_connect_timeout\n", error);
		break;
	    }
	}
	else
	{
	    return SOCKET_ERROR;
	}
    }

    b = TRUE;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&b, sizeof(BOOL));
    return 0;
}

int easy_closesocket(SOCKET sock)
{
    /*dbg_printf("easy_closesocket(%d)\n", sock);*/
    shutdown(sock, SD_BOTH);
    closesocket(sock);
    return 0;
}

int easy_get_sock_info(SOCKET sock, char *name, int *port)
{
    sockaddr_in addr;
    int name_len = sizeof(addr);
    getsockname(sock, (sockaddr*)&addr, &name_len);
    *port = ntohs(addr.sin_port);
    gethostname(name, 100);
    return 0;
}

int easy_get_sock_info_ip(SOCKET sock, char *ipstr, int *port)
{
    char *str;
    sockaddr_in addr;
    int name_len = sizeof(addr);
    getsockname(sock, (sockaddr*)&addr, &name_len);
    *port = ntohs(addr.sin_port);
    str = inet_ntoa(addr.sin_addr);
    if (str)
	strcpy(ipstr, str);
    else
	*ipstr = '\0';
    if (*ipstr == '\0' || strcmp(ipstr, "0.0.0.0") == 0)
	easy_get_ip_string(ipstr);
    return 0;
}

int easy_get_ip_string(char *host, char *ipstr)
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

int easy_get_ip_string(char *ipstring)
{
    char hostname[100];
    unsigned int a, b, c, d;
    struct hostent *pH;

    gethostname(hostname, 100);
    pH = gethostbyname(hostname);
    if (pH == NULL)
	return SOCKET_ERROR;

    a = (unsigned char)(pH->h_addr_list[0][0]);
    b = (unsigned char)(pH->h_addr_list[0][1]);
    c = (unsigned char)(pH->h_addr_list[0][2]);
    d = (unsigned char)(pH->h_addr_list[0][3]);
    sprintf(ipstring, "%u.%u.%u.%u", a, b, c, d);

    return 0;
}

int easy_get_ip(unsigned long *ip)
{
    char hostname[100];
    struct hostent *pH;

    gethostname(hostname, 100);
    pH = gethostbyname(hostname);
    *ip = *((unsigned long *)(pH->h_addr_list));
    return 0;
}

int easy_send(SOCKET sock, char *buffer, int length)
{
    int error;
    int num_sent;

    while ((num_sent = send(sock, buffer, length, 0)) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	if (error == WSAEWOULDBLOCK && error == WSAEINTR && error == WSAEINPROGRESS)
	{
            /*Sleep(0);*/
	    continue;
	}
	if (error == WSAENOBUFS)
	{
	    /* If there is no buffer space available then split the buffer in half and send each piece separately.*/
	    if (easy_send(sock, buffer, length/2) == SOCKET_ERROR)
		return SOCKET_ERROR;
	    if (easy_send(sock, buffer+(length/2), length - (length/2)) == SOCKET_ERROR)
		return SOCKET_ERROR;
	    return length;
	}
	WSASetLastError(error);
	return SOCKET_ERROR;
    }
    
    return length;
}

int easy_receive(SOCKET sock, char *buffer, int length)
{
    int ret_val;
    int num_received;
    fd_set readfds;
    int total = length;
    int error;

    num_received = recv(sock, buffer, length, 0);
    if (num_received == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	if (error != WSAEWOULDBLOCK && error != ERROR_IO_PENDING)
	    return SOCKET_ERROR;
    }
    else
    {
	length -= num_received;
	buffer += num_received;
    }
    
    while (length)
    {
	FD_ZERO(&readfds); 
	FD_SET(sock, &readfds);
	
	ret_val = select(0, &readfds, NULL, NULL, NULL);
	if (ret_val == 1)
	{
	    num_received = recv(sock, buffer, length, 0);
	    if (num_received == SOCKET_ERROR)
	    {
		error = WSAGetLastError();
		if (error != WSAEWOULDBLOCK && error != ERROR_IO_PENDING)
		    return SOCKET_ERROR;
	    }
	    else
	    {
		if (num_received == 0)
		{
		    return 0;
		}
		length -= num_received;
		buffer += num_received;
	    }
	}
	else
	{
	    if (ret_val == SOCKET_ERROR)
	    {
		error = WSAGetLastError();
		if (error != WSAEWOULDBLOCK && error != ERROR_IO_PENDING)
		    return SOCKET_ERROR;
	    }
	}
    }

    return total;
}

int easy_receive_some(SOCKET sock, char *buffer, int len)
{
    int ret_val;
    int num_received;
    fd_set readfds;
    int error;

    num_received = recv(sock, buffer, len, 0);
    if (num_received == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	if (error != WSAEWOULDBLOCK && error != ERROR_IO_PENDING)
	    return SOCKET_ERROR;
    }
    else
    {
	if (num_received > 0)
	    return num_received;
    }
    
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    
    ret_val = select(0, &readfds, NULL, NULL, NULL);
    if (ret_val == 1)
    {
	num_received = recv(sock, buffer, len, 0);
	if (num_received == SOCKET_ERROR)
	{
	    error = WSAGetLastError();
	    if (error != WSAEWOULDBLOCK && error != ERROR_IO_PENDING)
		return SOCKET_ERROR;
	}
	else
	{
	    if (num_received == 0)
	    {
		/*BPRINTF("easy_receive_some: socket closed\n");*/
	    }
	    return num_received;
	}
    }

    return SOCKET_ERROR;
}

int easy_receive_timeout(SOCKET sock, char *buffer, int len, int timeout)
{
    int ret_val;
    int num_received;
    fd_set readfds;
    struct timeval tv;
    int total = len;
    int error;

    /*dbg_printf("easy_receive_timeout\n");*/
    
    while (len)
    {
	FD_ZERO(&readfds); 
	FD_SET(sock, &readfds);
	tv.tv_sec = timeout;
	tv.tv_usec = 0;
	
	ret_val = select(0, &readfds, NULL, NULL, &tv);
	if (ret_val == 1)
	{
	    num_received = recv(sock, buffer, len, 0);
	    if (num_received == SOCKET_ERROR)
	    {
		error = WSAGetLastError();
		if (error != WSAEWOULDBLOCK && error != ERROR_IO_PENDING && error != WSAEINTR && error != WSAEINPROGRESS)
		    return SOCKET_ERROR;
	    }
	    else
	    {
		if (num_received == 0)
		{
		    /*BPRINTF("easy_receive_timeout: socket closed\n");*/
		    return total - len;
		}
		len -= num_received;
		buffer += num_received;
	    }
	}
	else
	{
	    if (ret_val == SOCKET_ERROR)
	    {
		error = WSAGetLastError();
		if (error != WSAEWOULDBLOCK && error != ERROR_IO_PENDING && error != WSAEINTR && error != WSAEINPROGRESS)
		    return SOCKET_ERROR;
	    }
	    else
	    {
		return total - len;
	    }
	}
    }
    return total;
}

void MakeLoopAsync(SOCKET *pRead, SOCKET *pWrite)
{
    SOCKET sock;
    char host[100];
    int port;
    sockaddr addr;
    int len;
    static char ipstr[20] = ""; /* cached local ip string */

    // Create a listener
    if (easy_create(&sock, ADDR_ANY, INADDR_ANY) == SOCKET_ERROR)
    {
	*pRead = INVALID_SOCKET;
	*pWrite = INVALID_SOCKET;
	return;
    }
    listen(sock, 5);
    easy_get_sock_info(sock, host, &port);
    if (ipstr[0] == '\0')
    {
	easy_get_ip_string(host, ipstr);
    }
    
    // Connect to myself
    if (easy_create(pWrite, ADDR_ANY, INADDR_ANY) == SOCKET_ERROR)
    {
	easy_closesocket(sock);
	*pRead = INVALID_SOCKET;
	*pWrite = INVALID_SOCKET;
	return;
    }
    if (easy_connect(*pWrite, ipstr, port) == SOCKET_ERROR)
    {
	easy_closesocket(*pWrite);
	easy_closesocket(sock);
	*pRead = INVALID_SOCKET;
	*pWrite = INVALID_SOCKET;
	return;
    }

    // Accept the connection from myself
    len = sizeof(addr);
    *pRead = accept(sock, &addr, &len);

    easy_closesocket(sock);
}
