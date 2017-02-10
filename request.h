#ifndef __REQUEST_H__

void requestHandle_FIFO(int fd, int thread_id, int request_id);
void requestHandle_SFF(int fd, int thread_id, int request_id);
int requestHandle_master(int fd);

#endif
