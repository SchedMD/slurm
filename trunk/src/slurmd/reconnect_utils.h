#ifndef _SLURMD_RECONNECT_UTILS_H_
#define _SLURMD_RECONNECT_UTILS_H_

/* connect_io_stream
 * called by the io_threads to establish a connection to srun
 */
int connect_io_stream (  task_start_t * task_start , int out_or_err ) ;

/* connect_io_stream
 * called by connect_io_stream to send stream identification info
 */
int send_io_stream_header ( task_start_t * task_start , int out_or_err ) ;
ssize_t read_EINTR(int fd, void *buf, size_t count) ;
ssize_t write_EINTR(int fd, void *buf, size_t count) ;
#endif 

