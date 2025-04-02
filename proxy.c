#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/**
 * Samuel McIlrath
 * samuelmcilrath
 */

//struct for a cache line
struct cache_line {
	
	int valid;
	int LRU;
	int size;
	char url[MAXLINE];
	char buf[MAX_OBJECT_SIZE];
	
};

/* GLOBAL VARIABLES */
int timer;
struct cache_line *cache;
pthread_mutex_t timerM; //lock for timer
pthread_mutex_t cacheM; //lock for cache

//functions
void *thread_handler(void *acceptPtr);
void http_transaction(int acceptfd);
int check_cache(char link[MAXLINE]);
void replace_line(char link[MAXLINE], char buf[MAX_OBJECT_SIZE], int size);
void create_request(char *link, char *fullReq, char *port, char *host, int acceptfd, rio_t rio);
int send_request(char req[MAXLINE], char host[MAXLINE], char port[MAXLINE]);
/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:56.0) Gecko/20100101 Firefox/56.0\r\n";

int main(int argc, char **argv)
{
	/* initialize mutex locks */
	pthread_mutex_init(&timerM, NULL);
	pthread_mutex_init(&cacheM, NULL);
	
	/**
	 *Create cache
	 */
	cache = malloc(10 * sizeof(struct cache_line));
	
	//initialize each cache_line to be invalid
	int i;
	for(i = 0; i < 10; i++){
		cache[i].valid = 0; //make invalid
	}

	//listen
	int listenfd;
	listenfd = open_listenfd(argv[1]);
	if(listenfd == -1){ //error
		perror("open_listen");
		close(listenfd);
		return 1;
	}
	
	/*
	 * infinite loop to handle connections
	 */
	for(;;){
			

		//accept
		int acceptfd = accept(listenfd, NULL, NULL);
		if(acceptfd == -1){//handle error
				perror("accept");
				close(listenfd);
				return 1;
		}
		
	
		pthread_t thread; //create thread
		int *acceptPtr = malloc(sizeof(int)); //malloc space for thread argument
		*acceptPtr = acceptfd; //set pointer to connection fd

		if(pthread_create(&thread, NULL, thread_handler, acceptPtr)){ //create thread and handle error at the same time
			perror("pthread_create\n");
			close(listenfd);
			return 1;
		}
		
		
		
	}

	close(listenfd); //close listen fd

	//destroy locks
	pthread_mutex_destroy(&timerM);
	pthread_mutex_destroy(&cacheM);

	free(cache); //free the cache
    printf("%s", user_agent_hdr);
    return 0;
}

/*
 * handles the thread and the arg it passes, originally tried doing this in http_trans
 * but it got too messy
 * */
void *thread_handler(void *acceptPtr){
		
	int acceptfd = *((int *)acceptPtr); //grab int that acceptPtr points to 
	
	http_transaction(acceptfd);	 //perform transaction

	//cleanup
	close(acceptfd);
	free(acceptPtr);

	return NULL;
}

/**
 * Listen to client, send request to server and write back to client the server's response
 */
void http_transaction(int acceptfd){

	printf("in transaction\n");

	
	char buf[MAXLINE],get[MAXLINE], link[MAXLINE], version[MAXLINE], fullReq[MAXLINE]; //bufs for breaking down headers
	rio_t rio;//variable for rio r/w
    int rioCheck;//check r/w errs

    //reads request, got this from Tiny web
    rio_readinitb(&rio, acceptfd);
	rioCheck = rio_readlineb(&rio, buf, MAXLINE); //grab GET header
	if(rioCheck == -1){//handle error
        perror("readlineb");
		return;
	}
	printf("%s", buf);

	/*
	 * parse the GET
	 * */
	//break down line
	if(sscanf(buf, "%s %s %s", get, link, version) <= 0){
		perror("sscanf");
		return;
	}
	
	//make sure it's GET command
	if(strcmp(get, "GET") != 0){
		printf("this is not implemented\n");
		return;
	}
	
	/*check to see if url is already in cache*/
	int cacheIndex = check_cache(link);
	
	//if not in cache
	if(cacheIndex == -1){

 		/**
	 	*create the request to send to server
	 	*/
		char port[MAXLINE], host[MAXLINE];
		create_request(link, fullReq, port, host, acceptfd, rio);
			
		/**
		 * send request to server and recieve response
		 */
		int serverfd = send_request(fullReq, host, port); //get back the server file descriptor 
		if(serverfd == -1){//handle error
			printf("couldn't connect");
			return;
		}
		char completeResponse[MAX_OBJECT_SIZE]; //store the complete repsonse for the cache 
		char response[MAXLINE]; //var to put response
		int bytesRead;	//for how many bytes were read
		int totalBytes = 0; //totalbytes that have been read

		rio_readinitb(&rio, serverfd);//initialize reads to server
		
		while((bytesRead = rio_readnb(&rio, response, MAXLINE)) > 0){//loop through server respons
			
			rio_writen(acceptfd, response, bytesRead);//forward to client

			if((totalBytes + bytesRead) > MAX_OBJECT_SIZE){ //prevent buffer overflow
				break;
			}

			memcpy(&(completeResponse[totalBytes]), response, bytesRead); //copy bytes over to complete response
			totalBytes += bytesRead; //update bytes read
		}
		
		if(totalBytes < MAX_OBJECT_SIZE){ //make sure object will fit in cache_line
			
			replace_line(link, completeResponse, totalBytes); //store in cache line 
		}

		close(serverfd); //close connection
	}
	else{//url is in cache
		rio_writen(acceptfd, cache[cacheIndex].buf, cache[cacheIndex].size); //write to client
	}
	
	return;
}

/*return index of link*/
int check_cache(char link[MAXLINE]){
	int i;

	//loop through cache
	for(i = 0; i < 10; i++){
		
		//check if equal
		if(strcmp(cache[i].url, link) == 0){
			
			/*lock before making changes to timer and cache LRU*/
			pthread_mutex_lock(&timerM);
			cache[i].LRU = timer; 
			timer++;
			pthread_mutex_unlock(&timerM);
			return i;
		}
	}
	
	return -1;
}

/* find invalid or 'LRU' cache_line */
void replace_line(char link[MAXLINE], char buf[MAX_OBJECT_SIZE], int size){
	
	/*set defaults for LRU*/
	int i, LRU, LRUindex;
	LRU = timer - cache[0].LRU;
	LRUindex = 0;

	/*first check for invalid lines or the LRU */
	for(i = 0; i < 10; i++){
		
		if(cache[i].valid == 0){ //invalid line case
			
			//update invalid line
			pthread_mutex_lock(&cacheM); //first lock when writing to cache
			cache[i].valid = 1;
			cache[i].LRU = timer;
			cache[i].size = size;
			strcpy(cache[i].url , link);
			memcpy(cache[i].buf, buf, size);
			timer++; //inc timer
			pthread_mutex_unlock(&cacheM); //unlock
			return;
		}
		else if ((timer - cache[i].LRU) > LRU){//find LRU
			//update LRU
			LRU = timer - cache[i].LRU;
			LRUindex = i;

		}
	}

	/* replace LRU */
	//need to LOCK
	pthread_mutex_lock(&cacheM);
	cache[LRUindex].valid = 1;
	cache[LRUindex].LRU = timer;
	cache[LRUindex].size = size;
	strcpy(cache[LRUindex].buf , buf);
	strcpy(cache[LRUindex].url , link);
	timer++;
	pthread_mutex_lock(&cacheM);//unlock
	return;


}

void create_request(char *link, char *fullReq, char *port, char *host, int acceptfd, rio_t rio){
	char *newLink = &link[0]; //will be adjusted to after the first occurence of a colon
    char path[MAXLINE]; //new port #
    char *target, *newPath; //for finding target chars
	

    newLink += 7; //adjust link after 'http://'

    //need to get hostname
	char getHdr[MAXLINE]; //header for GET request 

    if((target = strchr(newLink, ':')) == NULL){//default port
        strcpy(port, "80");
		sprintf(getHdr, "GET HTTP/1.0\r\n"); //create request header
		strncpy(host, newLink, strlen(newLink));
	}
    else{//handle defined port

        //isolate host
        strncpy(host , newLink, ((int)(target - newLink)));
        //printf("this is host %s\n", host);
        newLink += (int)(target - newLink) + 1; //should now start with port#

        target = strchr(newLink, '/'); //points to char after last port#

        //isolate the path
        strcpy(path, newLink);
        newPath = &path[0];
        newPath += (int)(target - newPath); //should just be the path now
        //printf("this is path %s\n", newPath);

        strncpy(port, newLink, ((int)(target - newLink))); //copies only the port# into port
		sprintf(getHdr, "GET %s HTTP/1.0\r\n", newPath); //create request header
    }

	/**
     *Need to finish reading the request headers.
     **/
    char hdrs[MAXLINE];
    char hostHdr[MAXLINE];
    char connHdr[MAXLINE];
    char proxyHdr[MAXLINE];
    char otherHdr[MAXLINE] = "";

    //start with what we need
    sprintf(hostHdr,"Host: %s\r\n", host); //create host header
    sprintf(connHdr, "Connection: close\r\n"); //create connection header
    sprintf(proxyHdr, "Proxy-Connection: close\r\n"); //create proxy-conn header

    //start with what we know will be there
    while(rio_readlineb(&rio, hdrs, MAXLINE) > 0){

        //check if at end of request
        if(strcmp(hdrs, "\r\n") == 0){
            break;
        }

        //get the name of the header
        char hdrName[MAXLINE];
        char *hdrContent = &hdrs[0];
        target = strchr(hdrs, ':');

        if(target == NULL){ //improper req format
            perror("invalid request");
            return;

        }

        //handle headers we already have
        strncpy(hdrName , hdrs, ((int)(target - hdrs)));    //grab hdr name

        if(strcmp(hdrName, "Host") == 0){//host header
            //do nothing
        }
        else if(strcmp(hdrName, "User-Agent") == 0){
        	//do nothing
		}
        else if(strcmp(hdrName, "Proxy-Connection") == 0){//proxy header

            hdrContent += (int)(target - hdrContent) + 1;   //grab content
            sprintf(proxyHdr, "Proxy-Connection:%s",hdrContent); //update buffer
        }
        else if(strcmp(hdrName, "Connection") == 0){//proxy header

            hdrContent += (int)(target - hdrContent) + 1;   //grab content
            sprintf(connHdr, "Connection: %s",hdrContent); //update buffer
        }
        else{//header not given

            strcat(otherHdr, hdrs);
        }
    }

    /**
     *Put together entire request
     */
    sprintf(fullReq,"%s%s%s%s%s%s\r\n", getHdr, hostHdr, connHdr, proxyHdr,user_agent_hdr, otherHdr);
    //printf("full req: %s\n", fullReq);
	return;
}

/*send request to server and return fd*/
int send_request(char req[MAXLINE], char host[MAXLINE], char port[MAXLINE]){
	
	int serverfd = open_clientfd(host, port);
	if(serverfd == -1){ //handle error
		perror("open_client");
		close(serverfd);
		return -1;
	}

	rio_writen(serverfd, req, MAXLINE); //write to server
	
	return serverfd;
}

















