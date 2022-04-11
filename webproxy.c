/*
*server.c built off of multithreaded echo server template provided by:
*http://www.csc.villanova.edu/~mdamian/sockets/echoC.htm
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>      /* for fgets */
#include <strings.h>     /* for bzero, bcopy */
#include <unistd.h>      /* for read, write */
#include <sys/socket.h>  /* for socket use */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>
#include <linux/limits.h>
#include <openssl/md5.h>
#include <sys/time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#define MAXLINE  8192  /* max text line length */
#define MAXBUF   8192  /* max I/O buffer size */
#define LISTENQ  1024  /* second argument to listen() */

int timeout;
char cacheDNS[MAXLINE];
pthread_mutex_t dns_lock;
pthread_mutex_t cache_lock;

/* 
 * open_listenfd - open and return a listening socket on port
 * Returns -1 in case of failure 
 */
int open_listenfd(int port); //http://www.csc.villanova.edu/~mdamian/sockets/echoC.htm
void * thread(void *vargp); // http://www.csc.villanova.edu/~mdamian/sockets/echoC.htm
void echo(int connfd);
void sighandler(int); // https://www.tutorialspoint.com/c_standard_library/c_function_signal.htm
void errorhandler(int connfd, char* msg);
void str2md5(const char *str, int length, char * out); //https://stackoverflow.com/questions/7627723/how-to-create-a-md5-hash-of-a-string-in-c
int checkCache();
int searchCache();
static volatile int run = 1; //for handling interupts
void sigpipeHandler(int);

int main (int argc, char **argv) {
   int listenfd, *connfdp, port, clientlen=sizeof(struct sockaddr_in);
   struct sockaddr_in clientaddr;
   pthread_t tid; 

   signal(SIGINT, sighandler);
   signal(SIGPIPE, sigpipeHandler);
   //make sure program was run with correct # of arguments
   /*if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(0);
   }*/
    if (argc != 2 && argc != 3) {
		fprintf(stderr, "usage: %s <port> [timeout]\n", argv[0]);
		exit(0);
	} else {
		port = atoi(argv[1]);
		if (argc == 3) {
			timeout = atoi(argv[2]);

			if (timeout <= 0) {
				perror("Invalid cache timeout. Must be greater than 0");
				return 1;
			}
		}
	}
   listenfd = open_listenfd(port); // set up tcp port
   if(pthread_mutex_init(&cache_lock, NULL) != 0){
      printf("Cannot init mutex\n");
      return -1;
    }
   printf("starting server\n");
   while(run) {
    //printf("Going to sleep for a second...\n");
    //sleep(1);
    connfdp = malloc(sizeof(int)); //I never free this but neither does the example template. 
	*connfdp = accept(listenfd, (struct sockaddr*)&clientaddr,(socklen_t *) &clientlen);
    pthread_create(&tid, NULL, thread, connfdp);
   }
   return(0);
}
//http://www.csc.villanova.edu/~mdamian/sockets/echoC.htm
void * thread(void * vargp) 
{  
    //printf("entering thread handler\n");
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self()); 
    free(vargp);
    //printf("entering echo\n");
    echo(connfd);
    close(connfd);
    return NULL;
}


void sighandler(int signum) { //exit gracefully
   
   printf("Caught interupt signal, closing server now");
   pthread_mutex_destroy(&cache_lock);

   run = 0;
   exit(1);
}
void sigpipeHandler(int signo) {
    //pthread_exit(NULL);
}

void errorhandler(int connfd, char* msg)
{
    //char errormsg[MAXLINE];
    //sprintf(errormsg, msg);
    send(connfd, msg, strlen(msg), 0);
}

void echo (int connfd) {
    char error400msg[] = "HTTP/1.1 400 Bad Request\r\nContent-Type:text/plain\r\nContent-Length:0\r\n\r\n";
    char error403msg[] = "HTTP/1.1 403 Forbidden\r\n\r\n<h1>You are forbidden from visiting this site</h1>\r\n\r\n";
    char error404msg[] = "HTTP/1.1 404 Not Found\r\nContent-Type:text/plain\r\nContent-Length:0\r\n\r\n";
    char sendBuf[MAXBUF];
    char receiveBuf[MAXBUF];
    FILE *blocklistfile;
    FILE *cacheFile;
    size_t bytesCopied;
    long fSize;
    char *tokptr;
    char *method;
    char *version;
    char *hostname;
    char *temp;
    //char *type;
    char fPath[39]; // cache/ + hash should be 6 + 33
    struct sockaddr_in serveraddr;
    struct in_addr cache_addr;
    struct hostent* server;
    int sockfd;
    int size;
    int portNum = 80;
    char fname[MAXLINE];
    char hBuf[33];
    char IP [20];
    char line[MAXLINE];
    int blocklist =0;
    //char *wwwPath = realpath("./www/", NULL);

	bzero(receiveBuf, MAXBUF);
    bzero(sendBuf, MAXBUF);
    printf("reading socket\n");

    read(connfd, receiveBuf, MAXBUF);
    printf("received the following packet:\n%s", receiveBuf);

    printf("\n--parsing request--\n");

    method = strtok_r(receiveBuf, " ", &tokptr);
    //printf("got token");
    //strncpy(method, tok1, strlen(tok1));
    //printf("method:%s\n", method);

    hostname = strtok_r(NULL, " ", &tokptr);
    //strncpy(hostname, tok2, strlen(tok2));
    //printf("hostname:%s\n", hostname);
    version = strtok_r(NULL, "\r\n", &tokptr);
    //strncpy(version, tok3, strlen(tok3));
    //printf("version:%s\n", version);

    //printf("checking if method/hostname/version exists\n");
    if(method && hostname && version){
        printf("checking method\n");
        if (strcmp(method, "GET") == 0) {

            if(strstr(hostname, "//")){
                hostname = strstr(hostname, "//")+2;
            }       
            printf("hostname:%s\n", hostname);
            temp = strchr(hostname, '/');
    
            if(!temp || *(temp+1) == '\0'){ 
    	        printf("default\n");
    	        strcpy(fname, "index.html");
            }
            else {
                printf("not default\n"); 
                strcpy(fname, temp+1);
                char *ptr;
                ptr = strchr(hostname, '/');
                if (ptr != NULL) {
                    *ptr = '\0';
                }
            }
            char hInput[strlen(hostname)+ strlen(fname) + 2];
            strcpy(hInput, hostname);
            strcat(hInput, "/");
            strcat(hInput,fname);
            bzero(hBuf,strlen(hBuf));

            //hBuf = str2md5(hInput,strlen(hInput));
            str2md5(hInput, strlen(hInput), hBuf);
            strcpy(fPath, "cache/");
            strcat(fPath, hBuf);
            //cache time!
            if (checkCache() == 0) {
                if(searchCache(fPath) == 0) {

                    cacheFile = fopen(fPath, "rb");
                    if(!cacheFile){
                        printf("Error opening file %s\n", fname);
                        return;
                    }
                    fseek(cacheFile, 0L, SEEK_END);
                    size = ftell(cacheFile);
                    rewind(cacheFile);
                    char cBuf[size];
                    fread(cBuf, 1, size, cacheFile);
                    send(connfd, cBuf, size, MSG_NOSIGNAL);
                }
            }


            //socket stuff
            sockfd = socket(AF_INET, SOCK_STREAM, 0);

            if (sockfd < 0) {
                perror("ERROR opening socket\n");
            }
            printf("opened socket\n");
            bzero((char *) &serveraddr, sizeof(serveraddr));
            serveraddr.sin_family = AF_INET;
            serveraddr.sin_port = htons(portNum);
            printf("hostname:%s\n", hostname);
            int hostLength = strlen(hostname);
            /*if (hostname[hostLength -1] == '/') {
                char tString[strlen(hostname)];
                strcpy(tString,hostname);
                printf("tstring:%s\n", tString);
                bzero(hostname,strlen(hostname));
                printf("hostname:%s\n", hostname);
                strncpy(hostname, tString, (size_t) hostLength-1);
            }*/
            printf("hostname:%s\n", hostname);
            server = gethostbyname(hostname);
            if(server == NULL){
                perror("cannot resolve host");
                errorhandler(connfd, error404msg);
    		    return;
            }
                printf("getting host address\n");
                if (inet_ntop(AF_INET, (char *)&serveraddr.sin_addr.s_addr, IP, (socklen_t)20) == NULL) {
                    printf("ERROR in converting hostname to IP\n");
                    return;
                }
                if (access("blocklist", F_OK) == -1) {
                    printf("No blocklist");
                }
                else {
                    blocklistfile = fopen("blocklist", "r");
                    while(fgets(line, sizeof(line), blocklistfile)){
                        if(strstr(line, hostname) || strstr(line, IP)){
                            printf("blocklist match found: %s\n", line);
                            blocklist = 1;
                        }
                    }
                    fclose(blocklistfile);
                }
                if(blocklist == 0) {
                    bcopy((char *)server->h_addr, (char *)&serveraddr.sin_addr.s_addr, server->h_length);
                    
                    size = connect(sockfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr));
                    if (size < 0){
                        printf("ERROR in connect\n");
                        return;
                    }

                    bzero(sendBuf, MAXLINE);
                    sprintf(sendBuf, "GET /%s %s\r\nHost: %s\r\n\r\n", fname, version, hostname);
                    printf("sending following packet:\n");
                    printf("%s\n", sendBuf);
                    size = send(sockfd, sendBuf, sizeof(sendBuf), 0);
                    if (size < 0){
                        printf("ERROR in sendto\n");
                        return;
                    }

                    int total_size = 0;
                    bzero(sendBuf, sizeof(sendBuf));

                    FILE* fp;
                    fp = fopen(fPath, "wb");
                    pthread_mutex_lock(&cache_lock);
                    while((size = read(sockfd, sendBuf, sizeof(sendBuf)))> 0){
                    if (size < 0){
                        printf("ERROR in recvfrom\n");
                        return;
                    }

                    total_size += size;

                    send(connfd, sendBuf, size, 0);
                    fwrite(sendBuf, 1, size, fp);
                    memset(sendBuf, 0, sizeof(sendBuf));
                    }
                    pthread_mutex_unlock(&cache_lock);
                    fclose(fp);
                    if(size == -1){
                        printf("Error in read - errno:%d\n", errno);
                        return;
                    }
                    printf("received a total of %d bytes\n", total_size);
                }
                else {
                    perror("forbidden");
                    errorhandler(connfd, error403msg);
    		        return;  
                }
        }
        else {
            perror("unsupported method");
            errorhandler(connfd, error400msg);
            return;
        }
    }
    else {
        perror("bad request\n");
        errorhandler(connfd, error400msg);
        return;
    }

            
   
}
void str2md5(const char *str, int length, char * out) {
    printf("in md5 function\n");
    int n;
    MD5_CTX c;
    unsigned char digest[16];
    //char *out = (char*)malloc(33);

    MD5_Init(&c);

    while (length > 0) {
        if (length > 512) {
            MD5_Update(&c, str, 512);
        } else {
            MD5_Update(&c, str, length);
        }
        length -= 512;
        str += 512;
    }

    MD5_Final(digest, &c);

    for (n = 0; n < 16; ++n) {
        snprintf(&(out[n*2]), 16*2, "%02x", (unsigned int)digest[n]);
    }
    printf("exiting md5 function\n");
    //return out;
}

int checkCache (){
    DIR* cache = opendir("./cache");
    if(errno = ENOENT){
		printf("cache does not exist, making now\n");
		mkdir("cache", 0777);
		closedir(cache);
    	return 0;
	}
    else if(!cache) {
		printf("cache cannot be opened\n");
		return 1;
	}
    else { //no clue if this covers my bases
        return 0;
    }
}
int searchCache (char * fPath) {
    struct stat fStat;
    if (stat(fPath, &fStat) == 0) {
        printf("file found in cache\n");
        printf("checking timeout");
        if (timeout == 0) {
            return 0;
        }
        else {
            time_t fTime = fStat.st_mtime;
            time_t cTime = time(NULL);
            if (difftime(cTime, fTime) < timeout) {
                printf("file is still valid \n");
                //int tLeft = timeout - ((int) difftime(cTime, fTime));
                //printf("%s", tLeft);
                return 0;
            }
            else {
               printf("file is mo longer valid \n");
               return 1; 
            }
        }
    }
    else {
        printf("file not found in cache\n");
        return 1;
    }
}



/* 
 * http://www.csc.villanova.edu/~mdamian/sockets/echoC.htm
 * open_listenfd - open and return a listening socket on port
 * Returns -1 in case of failure 
 */
int open_listenfd(int port) 
{
    int listenfd, optval=1;
    struct sockaddr_in serveraddr;
  
    /* Create a socket descriptor */
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1;

    /* Eliminates "Address already in use" error from bind. */
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
                   (const void *)&optval , sizeof(int)) < 0)
        return -1;

    /* listenfd will be an endpoint for all requests to port
       on any IP address for this host */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET; 
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    serveraddr.sin_port = htons((unsigned short)port); 
    if (bind(listenfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr)) < 0)
        return -1;

    /* Make it a listening socket ready to accept connection requests */
    if (listen(listenfd, LISTENQ) < 0) 
        return -1;
    return listenfd;
} /* end open_listenfd */