//
// request.c: Does the bulk of the work for the web server.
// 

#include "cs537.h"
#include "request.h"

extern int req_tot_put, req_tot_get, req_tot;
extern int buf_size;

extern struct timeval stat_arrival[MAXBUF];
extern struct timeval stat_pickup[MAXBUF];   //stat_req_dispatch = stat_pickup - stat_arrival; 
extern int pickedup_arrive[MAXBUF]; 
extern int pickedup_now[MAXBUF];  //pickedup number before this thread is pickedup
  			//stat_req_age = pickedup_now - pickedup_arrive 
extern int *count_total;
extern int *count_stat;
extern int *count_dyn;

//struct timeval stat_pickup[MAXBUF];   //stat_req_dispatch = stat_pickup - stat_arrival; 
struct timeval stat_read_start[MAXBUF];
struct timeval stat_read_end[MAXBUF];  //stat_req_read = stat_read_end - stat_read_start; 
struct timeval stat_print[MAXBUF];  //stat_req_complete = stat_print - stat_arrival;  

void requestError(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];
    
    printf("Request ERROR\n");
    
    // Create the body of the error message
    sprintf(body, "<html><title>CS537 Error</title>");
    sprintf(body, "%s<body bgcolor=""fffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr>CS537 Web Server\r\n", body);
    
    // Write out the header information for this response
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    printf("%s", buf);
    
    sprintf(buf, "Content-Type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    printf("%s", buf);
    
    sprintf(buf, "Content-Length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    printf("%s", buf);
    
    // Write out the content
    Rio_writen(fd, body, strlen(body));
    printf("%s", body);
    
}


//
// Reads and discards everything up to an empty text line
//
void requestReadhdrs(rio_t *rp)
{
    char buf[MAXLINE];
    
    Rio_readlineb(rp, buf, MAXLINE);
    while (strcmp(buf, "\r\n")) {
	Rio_readlineb(rp, buf, MAXLINE);
    }
    return;
}

//
// Return 1 if static, 0 if dynamic content
// Calculates filename (and cgiargs, for dynamic) from uri
//
int requestParseURI(char *uri, char *filename, char *cgiargs) 
{
    char *ptr;
    
    if (!strstr(uri, "cgi")) {
	// static
	strcpy(cgiargs, "");
	sprintf(filename, ".%s", uri);
	if (uri[strlen(uri)-1] == '/') {
	    strcat(filename, "home.html");
	}
	return 1;
    } else {
	// dynamic
	ptr = index(uri, '?');
	if (ptr) {
	    strcpy(cgiargs, ptr+1);
	    *ptr = '\0';
	} else {
	    strcpy(cgiargs, "");
	}
	sprintf(filename, ".%s", uri);
	return 0;
    }
}

//
// Fills in the filetype given the filename
//
void requestGetFiletype(char *filename, char *filetype)
{
    if (strstr(filename, ".html")) 
	strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif")) 
	strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg")) 
	strcpy(filetype, "image/jpeg");
    else 
	strcpy(filetype, "test/plain");
}

void requestServeDynamic(int fd, char *filename, char *cgiargs, int thread_id, int request_id)
{
    char buf[MAXLINE], *emptylist[] = {NULL};
    
    // The server does only a little bit of the header.  
    // The CGI script has to finish writing out the header.
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%s Server: Tiny Web Server\r\n", buf);
    
    /* CS537: Your statistics go here -- fill in the 0's with something useful! */
    sprintf(buf, "%s Stat-req-arrival: %d\r\n", buf, (int)(stat_arrival[request_id].tv_sec*1000000 + stat_arrival[request_id].tv_usec));
    sprintf(buf, "%s Stat-req-dispatch: %d\r\n", buf, (int)((stat_pickup[request_id].tv_sec - stat_arrival[request_id].tv_sec) * 1000000 + (stat_pickup[request_id].tv_usec - stat_arrival[request_id].tv_usec)));
    sprintf(buf, "%s Stat-thread-id: %d\r\n", buf, thread_id);
    sprintf(buf, "%s Stat-thread-count: %d\r\n", buf, count_total[thread_id]);
    sprintf(buf, "%s Stat-thread-static: %d\r\n", buf, count_stat[thread_id]);
    sprintf(buf, "%s Stat-thread-dynamic: %d\r\n", buf, count_dyn[thread_id]);
    
    Rio_writen(fd, buf, strlen(buf));
    
    if (Fork() == 0) {
	/* Child process */
	Setenv("QUERY_STRING", cgiargs, 1);
	/* When the CGI process writes to stdout, it will instead go to the socket */
	Dup2(fd, STDOUT_FILENO);
	Execve(filename, emptylist, environ);
    }
    Wait(NULL);
}


void requestServeStatic(int fd, char *filename, int filesize, int thread_id, int request_id) 
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];
    char tmp = 0;
    int i;
    
    requestGetFiletype(filename, filetype);
    
    srcfd = Open(filename, O_RDONLY, 0);
    
    // Rather than call read() to read the file into memory, 
    // which would require that we allocate a buffer, we memory-map the file
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    Close(srcfd);

    gettimeofday(&stat_read_end[request_id], NULL); //get the ending time of reading static content
    
    // The following code is only needed to help you time the "read" given 
    // that the file is memory-mapped.  
    // This code ensures that the memory-mapped file is brought into memory 
    // from disk.
    
    // When you time this, you will see that the first time a client 
    //requests a file, the read is much slower than subsequent requests.
    for (i = 0; i < filesize; i++) {
	tmp += *(srcp + i);
    }
    
    gettimeofday(&stat_print[request_id], NULL); //get the time at printing

    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%s Server: CS537 Web Server\r\n", buf);
    
    // CS537: Your statistics go here -- fill in the 0's with something useful!
    sprintf(buf, "%s Stat-req-arrival: %d\r\n", buf, (int)(stat_arrival[request_id].tv_sec*1000000 + stat_arrival[request_id].tv_usec));
    sprintf(buf, "%s Stat-req-dispatch: %d\r\n", buf, (int)((stat_pickup[request_id].tv_sec - stat_arrival[request_id].tv_sec) * 1000000 + (stat_pickup[request_id].tv_usec - stat_arrival[request_id].tv_usec)));
    sprintf(buf, "%s Stat-req-read: %d\r\n", buf, (int)((stat_read_end[request_id].tv_sec - stat_read_start[request_id].tv_sec) * 1000000 + (stat_read_end[request_id].tv_usec - stat_read_start[request_id].tv_usec)));
    sprintf(buf, "%s Stat-req-complete: %d\r\n", buf, (int)((stat_print[request_id].tv_sec - stat_arrival[request_id].tv_sec) * 1000000 + (stat_print[request_id].tv_usec - stat_arrival[request_id].tv_usec)));
    sprintf(buf, "%s Stat-req-age: %d\r\n", buf, pickedup_now[request_id] - pickedup_arrive[request_id]);
    sprintf(buf, "%s Stat-thread-id: %d\r\n", buf, thread_id);
    sprintf(buf, "%s Stat-thread-count: %d\r\n", buf, count_total[thread_id]);
    sprintf(buf, "%s Stat-thread-static: %d\r\n", buf, count_stat[thread_id]);
    sprintf(buf, "%s Stat-thread-dynamic: %d\r\n", buf, count_dyn[thread_id]);
    
    sprintf(buf, "%s Content-Length: %d\r\n", buf, filesize);
    sprintf(buf, "%s Content-Type: %s\r\n\r\n", buf, filetype);
    
    Rio_writen(fd, buf, strlen(buf));
    
    //  Writes out to the client socket the memory-mapped file 
    Rio_writen(fd, srcp, filesize);
    Munmap(srcp, filesize);
    
}

int is_static;
struct stat sbuf;
char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
char filename[MAXLINE], cgiargs[MAXLINE];
rio_t rio;

// handle a request by worker thread for FIFO
void requestHandle_FIFO(int fd, int thread_id, int request_id)
{
    
    //int is_static;
    //struct stat sbuf;
    //char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    //char filename[MAXLINE], cgiargs[MAXLINE];
    //rio_t rio;
    int t_id = thread_id;
    int r_id = request_id; 
    
    Rio_readinitb(&rio, fd);
    Rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);
    
    printf("%s %s %s\n", method, uri, version);
    
    if (strcasecmp(method, "GET")) {
	requestError(fd, method, "501", "Not Implemented", 
		     "CS537 Server does not implement this method");
	return;
    }
    requestReadhdrs(&rio);
    
    is_static = requestParseURI(uri, filename, cgiargs);
    if (stat(filename, &sbuf) < 0) {
	requestError(fd, filename, "404", "Not found", "CS537 Server could not find this file");
	return;
    }
    
    if (is_static) {	
//	pickedup_now[req_tot_get % buf_size] = req_tot_get;
//errors also count
        (*(count_stat+t_id))++; //# static requests handled by this thread id

	if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
	    requestError(fd, filename, "403", "Forbidden", "CS537 Server could not read this file");
	    return;
	}
//	count_stat[t_id]++; //# static requests handled by this thread id
	gettimeofday(&stat_read_start[r_id], NULL); //get the starting time of reading static content
	requestServeStatic(fd, filename, sbuf.st_size, t_id, r_id);
    } else {

        count_dyn[t_id]++; //# dynamic requests handled by this thread id

	if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
	    requestError(fd, filename, "403", "Forbidden", "CS537 Server could not run this CGI program");
	    return;
	}
//	count_dyn[t_id]++; //# dynamic requests handled by this thread id
	requestServeDynamic(fd, filename, cgiargs, t_id, r_id);
    }
}

// handle a request by worker thread for SSF and SSF-BS
void requestHandle_SFF(int fd, int thread_id, int request_id)
{
    
    //int is_static;
    //struct stat sbuf;
    //char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    //char filename[MAXLINE], cgiargs[MAXLINE];
    //rio_t rio;
    
    //Rio_readinitb(&rio, fd);
    //Rio_readlineb(&rio, buf, MAXLINE);
    //sscanf(buf, "%s %s %s", method, uri, version);
    
    //printf("%s %s %s\n", method, uri, version);
    
    //if (strcasecmp(method, "GET")) {
	//requestError(fd, method, "501", "Not Implemented", 
	//	     "CS537 Server does not implement this method");
	//return;
    //}
    //requestReadhdrs(&rio);
    
    //is_static = requestParseURI(uri, filename, cgiargs);
    //if (stat(filename, &sbuf) < 0) {
//	requestError(fd, filename, "404", "Not found", "CS537 Server could not find this file");
//	return;
  //  }
    int t_id = thread_id;
    int r_id = request_id; 

    if (is_static) {
//	pickedup_now[req_tot_get % buf_size] = req_tot_get;

        (*(count_stat+t_id))++; //# static requests handled by this thread id

	if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
	    requestError(fd, filename, "403", "Forbidden", "CS537 Server could not read this file");
	    return;
	}
//	(*(count_stat+t_id))++; //# static requests handled by this thread id
	gettimeofday(&stat_read_start[r_id], NULL); //get the starting time of reading static content
	requestServeStatic(fd, filename, sbuf.st_size, t_id, r_id);
    } else {

        count_dyn[t_id]++; //# dynamic requests handled by this thread id

	if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
	    requestError(fd, filename, "403", "Forbidden", "CS537 Server could not run this CGI program");
	    return;
	}
//	count_dyn[t_id]++; //# dynamic requests handled by this thread id
	requestServeDynamic(fd, filename, cgiargs, t_id, r_id);
    }
}

// get the file size of the static request in SFF and SFF-BS
int requestHandle_master(int fd)
{
    //int is_static;
    //struct stat sbuf;
    //char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    //char filename[MAXLINE], cgiargs[MAXLINE];
    //rio_t rio;
    
    Rio_readinitb(&rio, fd);
    Rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);
    
    printf("%s %s %s\n", method, uri, version);
    
    if (strcasecmp(method, "GET")) {
	requestError(fd, method, "501", "Not Implemented", 
		     "CS537 Server does not implement this method");
	return -1;
    }
    requestReadhdrs(&rio);
    
    is_static = requestParseURI(uri, filename, cgiargs);
    if (stat(filename, &sbuf) < 0) {
	requestError(fd, filename, "404", "Not found", "CS537 Server could not find this file");
	return -1;
    }
    
    if (is_static) {
	if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
	    requestError(fd, filename, "403", "Forbidden", "CS537 Server could not read this file");
	    return -1;
	}
	else
	{
		return sbuf.st_size;
	}
    }
    else
    {
    	return -1;
    }
}


