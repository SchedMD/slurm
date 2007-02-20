
#ifndef _NET_H
#define _NET_H

/* open a stream socket on an ephemereal port and put it into 
 * the listen state. fd and port are filled in with the new
 * socket's file descriptor and port #.
 */
int net_stream_listen(int *fd, int *port);

/* accept the incoming connection on the stream socket fd
 */
int net_accept_stream(int fd);

/* set low water mark on socket
 */
int net_set_low_water(int sock, size_t size);


#endif /* !_NET_H */
