#include "cs537.h"
#include "request.h"

// 
// server.c: A very, very simple web server
//
// To run:
//  server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

// CS537: Make the server parse the new arguments too
int listenfd, port, clientlen;
struct sockaddr_in clientaddr;
int thread_num, connfd, tmp_connfd, sched_flag, epoch_size;
int req_tot_put, req_tot_get, req_tot;
int buf_size;

struct timeval stat_arrival[MAXBUF];
struct timeval stat_pickup[MAXBUF];   

int pickedup_arrive[MAXBUF]; 
int pickedup_now[MAXBUF];  

int *count_total;
int *count_stat;
int *count_dyn;
extern int is_static;

struct buf_t
{
	int connfd;
	int file_size;
	int request_id; 
};
struct buf_t *buffer;

//producer/consumer problem
int fill = 0;
int use = 0;
int count = 0;

pthread_cond_t      empty   = PTHREAD_COND_INITIALIZER;
pthread_cond_t      filled  = PTHREAD_COND_INITIALIZER;
pthread_mutex_t     mutex   = PTHREAD_MUTEX_INITIALIZER; // PTHREAD_RECURSIVE_MUTEX_INITIALIZER; 


void getargs(int *port, int argc, char *argv[])
{
	if (argc < 5)
	{
		fprintf(stderr, "Usage: %s <portnum> <threads> <buffers> <schedalg> <N (for SFF-BS only)>\n", argv[0]);
		exit(1);
	}
	else if (strcmp(argv[4],"SFF-BS") == 0 && argc == 5)
	{
		fprintf(stderr, "Usage: %s <portnum> <threads> <buffers> <schedalg> <N (for SFF-BS only)>\n", argv[0]);
		exit(1);
	}
	else if (strcmp(argv[4],"SFF-BS") == 0)
	{
		epoch_size = atoi(argv[5]);
		if(epoch_size <= 0)
		{
			fprintf(stderr, "Epoch size can not be smaller than 1\n");
			exit(1);
		}
	}

	*port = atoi(argv[1]);
	thread_num = atoi(argv[2]);
	buf_size = atoi(argv[3]);

	sched_flag = 0;
	if(strcmp(argv[4],"FIFO") == 0)
	{
		sched_flag = 1;
	}
	else if(strcmp(argv[4],"SFF") == 0)
	{
		sched_flag = 2;
	}
	else if(strcmp(argv[4],"SFF-BS") == 0)
	{
		sched_flag = 3;
	}
	else
	{
		fprintf(stderr, "Wrong scheduling algorithm\n");
		exit(1);
	}
}

void put_dyn(int connfd, struct buf_t *buffer, int file_size)
{
	req_tot_put++;
	int tmp_tail = use + count - 1;
	int i = (tmp_tail + 1) % buf_size;

	gettimeofday(&stat_arrival[req_tot_put], NULL); //get the arrival time of the request
	pickedup_arrive[req_tot_put] = req_tot_get;       // req_tot_get should be in the CR region

	(buffer + i)->connfd = connfd;
	(buffer + i)->file_size = file_size;
	(buffer + i)->request_id = req_tot_put % buf_size;
	count++;

}


void put(int connfd, struct buf_t *buffer, int file_size)
{
	int tmp_size, tmp_pos, tmp_cnt, tmp_bcnt, tmp_ind, tmp_tail, tmp_start, epoch_ind, tmp_count, i;
	req_tot_put++;

	gettimeofday(&stat_arrival[req_tot_put], NULL); //get the arrival time of the request
	pickedup_arrive[req_tot_put] = req_tot_get;       // req_tot_get should be in the CR region

	if(sched_flag == 1) //FIFO scheduling
	{	
		(buffer + fill)->connfd = connfd;
		(buffer + fill)->file_size = 0;
		(buffer + fill)->request_id = req_tot_put; 


		fill = (fill + 1) % buf_size;
		count++;
	}
	else if(sched_flag == 2) //SFF scheduling
	{
		tmp_size = file_size;
		tmp_cnt = 0; //# of requests whose size is smaller than that of the incoming request
		tmp_start = use;
		if(count == 0) //the buffer is empty
		{
			(buffer + tmp_start)->connfd = connfd;
			(buffer + tmp_start)->file_size = file_size;
			(buffer + tmp_start)->request_id = req_tot_put;

		}
		else //the buffer is not empty
		{
			for(tmp_pos = tmp_start; tmp_cnt < count; tmp_cnt++)
			{
				if((buffer + tmp_pos)->file_size > tmp_size) //finding the first scheduled request whose size is bigger than that of the incoming request
				{								
					tmp_tail = use + count - 1;   // the last element in buffer
					tmp_bcnt = count - tmp_cnt; //# of requests which need moving backwards
					i = (tmp_tail + 1) % buf_size;  // the first free space
					while(tmp_bcnt > 0) //moving the following requests one step backwards
					{
						tmp_ind = tmp_tail % buf_size;

						if ((buffer + tmp_ind)->file_size != -1){                            
							(buffer + i)->connfd = (buffer + tmp_ind)->connfd;
							(buffer + i)->file_size = (buffer + tmp_ind)->file_size;
							(buffer + i)->request_id = (buffer + tmp_ind)->request_id; 
							i = tmp_ind;
							tmp_bcnt--;
							tmp_tail--;			
						}
						else if ((buffer + tmp_ind)->file_size == -1){
							tmp_tail--;
							tmp_bcnt--;
						}
					}
					//add the new request in the buffer
					//not quite sure whether this will change the buffer or not \\pxq
					(buffer + tmp_pos)->connfd = connfd;
					(buffer + tmp_pos)->file_size = file_size;
					(buffer + tmp_pos)->request_id = req_tot_put;


					break;				
				}
				tmp_pos = (tmp_pos + 1) % buf_size;
			}
			if(tmp_cnt == count)
			{
				//the incoming request has the largest file size
				(buffer + tmp_pos)->connfd = connfd;
				(buffer + tmp_pos)->file_size = file_size;
				(buffer + tmp_pos)->request_id = req_tot_put;

			}
		}
		count++;
	}
	else if(sched_flag == 3) //SFF-BS scheduling
	{
		tmp_size = file_size;		
		tmp_cnt = 0; //# of requests whose size is smaller than that of the incoming request
		if(req_tot_get == 0)
		{
			epoch_ind = (int)((req_tot_put - 1)/epoch_size);
			tmp_start = use + epoch_ind * epoch_size;
		}
		else
		{
			epoch_ind = (int)((req_tot_put - 1)/epoch_size) - (int)((req_tot_get - 1)/epoch_size); //difference between the epoch indice of produced and consumed requests
		}

		if(epoch_ind == 0 || (epoch_ind == 1 && req_tot_put - req_tot_get == 1))
		{
			tmp_start = use; //beginning point of the epoch sub-buffer
		}
		else if(req_tot_get % epoch_size == 0 && req_tot_get != 0)
		{
			tmp_start = use + (epoch_ind - 1) * epoch_size;
		}
		else
		{
			tmp_start = use + (epoch_ind - 1) * epoch_size + (epoch_size - req_tot_get % epoch_size);
		}

		if((tmp_start == use && count == 0) || req_tot_put % epoch_size == 1) //the sub-buffer of the same epoch is empty
		{
			tmp_count = 0;
		}
		else 
		{
			tmp_count = count - (tmp_start - use);                                                        
		}

		tmp_start = tmp_start % buf_size;

		if(tmp_count == 0) //the buffer is empty
		{
			(buffer + tmp_start)->connfd = connfd;
			(buffer + tmp_start)->file_size = file_size;
			(buffer + tmp_start)->request_id = req_tot_put;
			//count++;	
		}
		else
		{
			for(tmp_pos = tmp_start; tmp_cnt < tmp_count; tmp_cnt++)
			{
				if((buffer + tmp_pos)->file_size > tmp_size) //finding the first scheduled request whose size is bigger than that of the incoming request
				{								
					tmp_tail = tmp_start + tmp_count - 1;
					tmp_bcnt = tmp_count - tmp_cnt; //# of requests which need moving backwards
					i = (tmp_tail + 1) % buf_size;
					while(tmp_bcnt > 0) //moving the following requests one step backwards
					{
						tmp_ind = tmp_tail % buf_size;

						if ((buffer + tmp_ind)->file_size != -1){

							(buffer + i)->connfd = (buffer + tmp_ind)->connfd;
							(buffer + i)->file_size = (buffer + tmp_ind)->file_size; 
							(buffer + i)->request_id =  (buffer + tmp_ind)->request_id;
							i = tmp_ind;
							tmp_bcnt--;
							tmp_tail--;			
						}
						else if ((buffer + tmp_ind)->file_size == -1){
							tmp_tail--;
							tmp_bcnt--;
						}
					}

					(buffer + tmp_pos)->connfd = connfd;
					(buffer + tmp_pos)->file_size = file_size;
					(buffer + tmp_pos)->request_id = req_tot_put;

					break;				
				}
				tmp_pos = (tmp_pos + 1) % buf_size;
			}
			if(tmp_cnt == tmp_count)
			{
				(buffer + tmp_pos)->connfd = connfd;
				(buffer + tmp_pos)->file_size = file_size;
				(buffer + tmp_pos)->request_id = req_tot_put;

			}
		}
		count++;			
	}
}  

void producer(int connfd, struct buf_t *buffer, int file_size)
{
	pthread_mutex_lock(&mutex);
	while(count == buf_size)
	{
		pthread_cond_wait(&empty,&mutex);
	}

	if (file_size != -1)
	{
		put(connfd, buffer, file_size);
	}
	else if (file_size == -1)
	{
		put_dyn(connfd, buffer, file_size);
	}

	pthread_cond_signal(&filled);
	pthread_mutex_unlock(&mutex);
}

void Handle_req_master() //works should be done by the master thread
{
	int f_size;	
	req_tot++;

	if(sched_flag == 1)//FIFO
	{		
		producer(connfd, buffer, 0);   //placing the connection descriptor into the buffer
		return;
	}
	else //SFF and SFF-BS
	{
		f_size = requestHandle_master(connfd);


		if(f_size != -1)
		{
			producer(connfd, buffer, f_size);   //placing the connection descriptor into the buffer 
		}
		else {

			producer(connfd, buffer, -1);   //placing the connection descriptor into the buffer 

		}

		return;


	}

}


void* worker_loop(void *argc)
{
	int thread_id_local = *((int*)argc);  
	int request_id_local = -1;              //add the request id
	int tmp_connfd; 

	while(1){
		pthread_mutex_lock(&mutex);
		while(count == 0)
		{
			pthread_cond_wait(&filled,&mutex);
		}

		tmp_connfd = (buffer + use)->connfd;
		request_id_local = (buffer + use)->request_id; 
		pickedup_now[request_id_local] = req_tot_get;
		req_tot_get++;
		use = (use + 1) % buf_size;
		count--;

		pthread_cond_signal(&empty);
		pthread_mutex_unlock(&mutex);


		gettimeofday(&stat_pickup[request_id_local], NULL); //get the ending time of reading static content

		(*(count_total+thread_id_local))++;

		if(sched_flag == 1) //FIFO
		{
			requestHandle_FIFO(tmp_connfd, thread_id_local, request_id_local);
		}
		else //SFF and SFF-BS
		{
			requestHandle_SFF(tmp_connfd, thread_id_local, request_id_local);
		}
		Close(tmp_connfd);
	}

}

int main(int argc, char *argv[])
{

	getargs(&port, argc, argv);

	buffer = malloc(buf_size*sizeof(struct buf_t));   //buffer to store connection descriptors
	pthread_t p_worker[thread_num];   //worker threads

	count_total = (int*)malloc(thread_num*sizeof(int));
	count_stat = (int*)malloc(thread_num*sizeof(int));
	count_dyn = (int*)malloc(thread_num*sizeof(int));

	memset(count_total,0,thread_num);
	memset(count_stat,0,thread_num);
	memset(count_dyn,0,thread_num);

	int thread_id[thread_num];

	// 
	// Create some worker threads using pthread_create ...
	//
	pthread_mutex_init(&mutex,NULL);
	pthread_cond_init(&empty,NULL);
	pthread_cond_init(&filled,NULL);

	int i; 
	//create fixed-size pool of worker thread

	for (i=0; i<thread_num; i++)
	{
		thread_id[i] = i; 
		pthread_create(&p_worker[i], NULL, worker_loop, (void *) &thread_id[i]);
	}

	req_tot_put = 0; //total # of produced requests
	req_tot_get = 0; //total # of consumed requests
	req_tot = 0;

	listenfd = Open_listenfd(port);
	while (1) {
		clientlen = sizeof(clientaddr);
		connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen);

		Handle_req_master();


	}

}






