#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#define TBUF_SIZE 2048
char tbuf[TBUF_SIZE];

char* get_string_resp( char* message, char* current )
{
	tbuf[0] = '\0';
	printf("%s (%s) = ", message, current );
	fgets( tbuf, TBUF_SIZE, stdin );
	if ( tbuf[0] != '\n' )
	{
		printf( "%s\n", tbuf);
		return strdup( tbuf ); 
	}
	printf( "%s\n", current);
	return current;
}

int get_int_resp( char* message, int current )
{
	int ret_val = 0;
	tbuf[0] = '\0';
	printf("%s (%d) = ", message, current );
	fgets( tbuf, TBUF_SIZE, stdin );
	ret_val = atoi( tbuf );
	if ( ret_val )
	{
		printf( "%d\n", ret_val );
		return ret_val;
	}
	printf( "%d\n", current );
	return current;
}

uint8_t get_tf_resp( char* message, uint8_t current )
{
	tbuf[0] = '\0';
	printf("%s (%c) = ", message, current ? 't': 'f' );
	fgets( tbuf, TBUF_SIZE, stdin );
	if ( tbuf[0] == 'f' || tbuf[0] == 'F' )
	{
		printf("false\n" );
		return 0;
	}
	if ( tbuf[0] == 't' || tbuf[0] == 'T' )
	{
		printf("true\n" );
		return 1;
	}
	printf("%s\n", current ?"true" :"false"  );
	return current;	
}


