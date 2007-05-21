#include "mpdutil.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/timeb.h>

bool ReadStringMax(SOCKET sock, char *str, int max)
{
    int n;
    char *str_orig = str;
    int count = 0;

    do {
	n = easy_receive(sock, str, 1);
	if (n == SOCKET_ERROR)
	{
	    err_printf("eReadString failed, error %d\n", WSAGetLastError());
	    return false;
	}
	if (n == 0)
	{
	    err_printf("eReadString failed, socket closed\n");
	    return false;
	}
	count++;
	if (count == max && *str != '\0')
	{
	    *str = '\0';
	    // truncate, read and discard all further characters of the string
	    char ch;
	    do {
		n = easy_receive(sock, &ch, 1);
		if (n == SOCKET_ERROR)
		{
		    err_printf("eReadString failed, error %d\n", WSAGetLastError());
		    return false;
		}
		if (n == 0)
		{
		    err_printf("eReadString failed, socket closed\n");
		    return false;
		}
	    } while (ch != '\0');
	}
    } while (*str++ != '\0');
    //dbg_printf("RSM(%s) '%s'\n", bto_string(sock), str_orig);
    //dbg_printf_color(FOREGROUND_RED | FOREGROUND_INTENSITY, "RSM(%s) '%s'\n", bto_string(sock), str_orig);
    //return strlen(str_orig);
    return true;
}

bool ReadStringMaxTimeout(SOCKET sock, char *str, int max, int timeout)
{
    int n;
    char *str_orig = str;
    int count = 0;
    struct _timeb tin, tout;

    _ftime(&tin);
    do {
	n = easy_receive_timeout(sock, str, 1, timeout);
	if (n == SOCKET_ERROR)
	{
	    err_printf("eReadString failed, error %d\n", WSAGetLastError());
	    return false;
	}
	if (n == 0)
	{
	    n = WSAGetLastError();
	    _ftime(&tout);
	    if (tout.time - tin.time + 1 < timeout)
	    {
		err_printf("ReadStringMaxTimeout returning timeout too early: timeout = %d, elapsed time = %d, last error code = %d\n",
		    timeout, tout.time - tin.time, n);
	    }
	    WSASetLastError(ERROR_TIMEOUT);
	    return false;
	}
	count++;
	if (count == max && *str != '\0')
	{
	    *str = '\0';
	    // truncate, read and discard all further characters of the string
	    char ch;
	    do {
		n = easy_receive_timeout(sock, &ch, 1, timeout);
		if (n == SOCKET_ERROR)
		{
		    err_printf("eReadString failed, error %d\n", WSAGetLastError());
		    return false;
		}
		if (n == 0)
		{
		    n = WSAGetLastError();
		    _ftime(&tout);
		    if (tout.time - tin.time + 1 < timeout)
		    {
			err_printf("ReadStringMaxTimeout returning timeout too early: timeout = %d, elapsed time = %d, last error code = %d\n",
			    timeout, tout.time - tin.time, n);
		    }
		    WSASetLastError(ERROR_TIMEOUT);
		    return false;
		}
	    } while (ch != '\0');
	}
    } while (*str++ != '\0');
    //dbg_printf("RSM(%s) '%s'\n", bto_string(sock), str_orig);
    //dbg_printf_color(FOREGROUND_RED | FOREGROUND_INTENSITY, "RSM(%s) '%s'\n", bto_string(sock), str_orig);
    //return strlen(str_orig);
    return true;
}

bool ReadStringTimeout(SOCKET sock, char *str, int timeout)
{
    int n;
    char *str_orig = str;
    struct _timeb tin, tout;

    //dbg_printf("reading from %d\n", bget_fd(bfd));
    _ftime(&tin);
    do {
	n = 0;
	while (!n)
	{
	    n = easy_receive_timeout(sock, str, 1, timeout);
	    if (n == SOCKET_ERROR)
	    {
		err_printf("eReadStringTimeout failed, error %d\n", WSAGetLastError());
		return false;
	    }
	    if (n == 0)
	    {
		n = WSAGetLastError();
		_ftime(&tout);
		if (tout.time - tin.time + 1 < timeout)
		{
		    err_printf("ReadStringTimeout returning timeout too early: timeout = %d, elapsed time = %d, last error code = %d\n",
			timeout, tout.time - tin.time, n);
		}
		WSASetLastError(ERROR_TIMEOUT);
		return false;
	    }
	}
    } while (*str++ != '\0');
    //dbg_printf("RST(%s) '%s'\n", bto_string(bfd), str_orig);
    //dbg_printf_color(FOREGROUND_RED | FOREGROUND_INTENSITY, "RST(%s) '%s'\n", bto_string(bfd), str_orig);
    //return strlen(str_orig);
    return true;
}

bool ReadString(SOCKET sock, char *str)
{
    int n;
    char *str_orig = str;

    do {
	n = easy_receive(sock, str, 1);
	if (n == SOCKET_ERROR)
	{
	    err_printf("eReadString failed, error %d\n", WSAGetLastError());
	    return false;
	}
	if (n == 0)
	{
	    err_printf("eReadString failed, socket closed\n");
	    return false;
	}
    } while (*str++ != '\0');
    //dbg_printf("RS (%s) '%s'\n", bto_string(sock), str_orig);
    //dbg_printf_color(FOREGROUND_RED | FOREGROUND_INTENSITY, "RS (%s) '%s'\n", bto_string(sock), str_orig);
    //return strlen(str_orig);
    return true;
}

int WriteString(SOCKET sock, char *str)
{
    int ret_val;
    ret_val = easy_send(sock, str, strlen(str)+1);
    //dbg_printf("WS (%s), '%s'\n", bto_string(sock), str);
    /*
    if (ret_val != SOCKET_ERROR)
	dbg_printf_color(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY, "WS (%s), '%s'\n", bto_string(sock), str);
    else
	dbg_printf_color(FOREGROUND_RED | FOREGROUND_GREEN, "WS (%s) failed, '%s'\n", bto_string(sock), str);
	*/
    return ret_val;
}
