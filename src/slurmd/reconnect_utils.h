#ifndef _SLURMD_RECONNECT_UTILS_H_
#define _SLURMD_RECONNECT_UTILS_H_
int connect_io_stream (  task_start_t * task_start , int out_or_err ) ;
int send_io_stream_header ( task_start_t * task_start , int out_or_err ) ;
ssize_t read_EINTR(int fd, void *buf, size_t count) ;
#endif 

