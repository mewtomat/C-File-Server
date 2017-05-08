#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <asm/errno.h>
#include <pthread.h>
#include <unistd.h>
#include <queue>
#include <iostream>
#include <signal.h>

using namespace std;

bool breaktrue;

#define MAXCON 1

pthread_mutex_t my_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t consumer;
pthread_cond_t producer;
pthread_t* worker_threads;
int num_wthreads;

queue<int> requests;
int file_buffer_size = 1024;
int fname_size = 21;
int thread_ids[9999];

void sigproc()				// if ^C pressed, cleanup and exit
{ 		 
	breaktrue = true;		// This bool is checked in threads, if true, they will exit
	int i;
	for(i=0;i<num_wthreads;++i)
	{
		pthread_join(worker_threads[i],NULL);
	}
 	/*clean up*/
	pthread_mutex_destroy(&my_mutex);
  	pthread_cond_destroy(&producer);
  	pthread_cond_destroy(&consumer);
	free(worker_threads);
	exit(0);
}

void* thread_function(void* thread_num)
{
	int t_num = *((int*)thread_num);
	int newsockfd,n,i;
	bool skip=false;
	char buffer[256], fname[fname_size], ch, file_buffer[file_buffer_size];
	while(true)
	{
		skip=false;
		if(breaktrue) break;						// if ^C was pressed, breaktrue will be set true, thread will exit
													// Acquire the lock, since you are accessing and modifying the shared
													// data structure. Also, you don't want to miss the wakeup call.
		pthread_mutex_lock(&my_mutex);
		while(requests.size()==0)					// If the queue is empty, there is nothing to pop, wakeup the producer
													// to fill the queue. The thread sleeps in meantime.
		{
			pthread_cond_wait(&consumer, &my_mutex);
		}
		newsockfd = requests.front();				// Read the top of the queue and pop it. Signal the producer, since there
													// is empty slot in queue now, so producer can fill it.
		requests.pop();
		pthread_cond_signal(&producer);
		pthread_mutex_unlock(&my_mutex);			// Lock can be released now

		bzero(buffer,256);							// Request serving begins
		n = read(newsockfd, buffer, fname_size);
		if (n < 0) {
		   perror("ERROR reading from socket");
		   close(newsockfd);
		   continue;								
		}

		for(i = 0 ; i < fname_size ; i++)
			fname[i] = buffer[i + 4];

		for(i = 0 ; i < fname_size ; i++)
			if(fname[i] == '\n')
				fname[i] = 0;


		FILE *fp;
		fp = fopen(fname, "r");
		if(fp == NULL) {
			char errormsg[500];
			sprintf(errormsg,"Error while opening the file, requested file: %s", fname);
			perror(errormsg);
			close(newsockfd);
			continue;
		}

		int curr = 0;
		bzero(file_buffer, file_buffer_size);
		int total=0;
		while((ch = fgetc(fp)) != EOF) {
			if(curr == file_buffer_size) {
				n = write(newsockfd, file_buffer, file_buffer_size);
				 total+=n;
				bzero(file_buffer, file_buffer_size);
				if (n < 0) {
				   perror("ERROR writing to socket");
				   skip = true; break;
				}
				curr = 0;
			}
			file_buffer[curr++] = ch;
		}
		if(skip)
		{
			close(newsockfd);
			fclose(fp);
			continue;
		}
		if(curr != 0) {
			n = write(newsockfd, file_buffer, file_buffer_size);	
			total+=n;
			bzero(file_buffer, file_buffer_size);
			if (n < 0) {
			   perror("ERROR writing to socket");
			   skip = true;
			}
		}
		fclose(fp);
		close(newsockfd);
	}
	pthread_exit(NULL);
}


int main(int argc, char *argv[])
{

	pthread_mutex_init(&my_mutex, NULL);			// intialise the mutexes and conditional variables
  	pthread_cond_init (&producer, NULL);
  	pthread_cond_init (&consumer, NULL);

	int sockfd, newsockfd, portno, request_size;
	char buffer[256], fname[fname_size], ch, file_buffer[file_buffer_size];
	struct sockaddr_in serv_addr, cli_addr;
	int n, i,create_status;
	if (argc < 4) {
	    fprintf(stderr,"Error: Incorrect Format. Usage: %s Port_No Num_Worker_threads Req_Queue_Size\n", argv[0]);
	    exit(1);
	}
	/* create socket */

	sockfd = socket(AF_INET, SOCK_STREAM, 0);  //create a  non blocking listening socket
	if (sockfd < 0) 
	   perror("ERROR opening socket");

	/* fill in port number to listen on. IP address can be anything (INADDR_ANY) */
	bzero((char *) &serv_addr, sizeof(serv_addr));
	portno = atoi(argv[1]);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(portno);

	/* bind socket to this port number on this machine */
	if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		perror("ERROR on binding");
		exit(1);
	}

	num_wthreads = atoi(argv[2]);						// acquire the params from command line
	request_size = atoi(argv[3]);	

	worker_threads = (pthread_t*)malloc(num_wthreads* sizeof(pthread_t)); // Allocate space for placeholder of worker threads
	for(i=0;i<num_wthreads;++i)
	{
		thread_ids[i] = i;								// Create the worker threads
		create_status = pthread_create(&worker_threads[i],NULL, thread_function,&thread_ids[i]);
		if(create_status < 0) {
			perror("Error creating thread\n");
			exit(1);
		}
	}

	/* listen for incoming connection requests */
	listen(sockfd, MAXCON);		  				// start listening. At most 1500 requests queued up
	socklen_t clilen = sizeof(cli_addr);

	while(1) 
	{
		if(breaktrue) break;					// If ^C was pressed, breaktrue would be true
		if(request_size!=0)						// request_size == 0 => unbounded queue size, thus no need to check for
												// the current size of queue, directly proceed to push the reuqest on the queue
		{
			pthread_mutex_lock(&my_mutex);		// Since accessing shared data structure, acquire lock
			while(requests.size()>=request_size)// If current queue size has reached the limit, the main thread sleeps
												// The consumers on consuming something from the queue, will wake this 
												// thread
			{
				pthread_cond_wait(&producer, &my_mutex);
			}
			pthread_mutex_unlock(&my_mutex);
		}
		newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
												// at this moment accept has been passed, which implies the main thread
												// just accepted a new request
		pthread_mutex_lock(&my_mutex);			//because manipulating a shared data structure, the queue
		requests.push(newsockfd);				// Push the new request on the queue.
		pthread_cond_signal(&consumer);			// Queue has some pending requests, signal the consumers to start working
		pthread_mutex_unlock(&my_mutex);
	}

	/*clean up*/
	pthread_mutex_destroy(&my_mutex);
  	pthread_cond_destroy(&producer);
  	pthread_cond_destroy(&consumer);
	free(worker_threads);
	return 0; 
}
