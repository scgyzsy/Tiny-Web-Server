#include <arpa/inet.h>
#include <signal.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>

#define MAXLINE     1024    // Max text line length 
#define MAXBUF      8192    // Max I/O buffer size
#define LISTENQ     1024    // Second argument to listen()

// External variables
extern char **environ;      // Defined by libc

// Our error-handing functions
void unix_error(char *msg)
{
    fprintf(stderr, "%s: %s\n", msg, strerror(errno));
    exit(0); 
} 

void posix_error(int code, char *msg)
{
    fprintf(stderr, "%s: %s\n", msg, strerror(code)); 
    exit(0); 
}

// Robust I/O functions for network
typedef struct
{
    int rio_fd;                 // Descriptor for this internal buf
    int rio_cnt;                // Unread bytes in internal buf
    char *rio_bufptr;           // Next unread byte in internal buf
    char rio_buf[MAXBUF];  // Internal buffer
} rio_t; 

void rio_readinitb(rio_t *rp, int fd)
{
	rp->rio_fd = fd; 
	rp->rio_cnt = 0; 
	rp->rio_bufptr = rp->rio_buf; 
}

ssize_t rio_writen(int fd, void *usrbuf, size_t n)
{
	size_t nleft = n; 
	ssize_t nwritten; 
	char *bufp = usrbuf; 

	while(nleft > 0)
	{
		if((nwritten = write(fd, bufp, nleft)) <= 0)
		{
			if(errno == EINTR)		// interrupted by sig handler return
				nwritten = 0;		// and call write()	again
			else 
				return -1; 			// errorno set by write()
		}
		nleft -= nwritten; 
		bufp += nwritten; 
	}

	return n;
}

/*
 * rio_read - This is a wrapper for the Unix read() function that 
 * transfers min(n, rio_cnt) bytes from an internal buffer to a user
 * buffer, where n is the number of bytes requested by the user and 
 * rio_cnt is the number of unread bytes in the internal buffer. On
 * entry, rio_read() refills the internal buffer via a call to 
 * read() if the internal buffer is empty. 
 */
static ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n)
{
    int cnt; 
    
    while(rp->rio_cnt <= 0) // refill if buf is empty. Note: rp->rio_cnt may < 0
    {                       // when Interrupt by sigornal. 
        rp->rio_cnt = read(rp->rio_fd, rp->rio_buf, sizeof(rp->rio_buf)); 

        if(rp->rio_cnt < 0)
        {
            if(errno != EINTR)  // Interrupt by sig handler return
                return -1; 
        }
        else if(rp->rio_cnt == 0)   // EOF
            return 0; 
        else
            rp->rio_bufptr = rp->rio_buf;   // Reset buffer ptr
    }

    // Copy min(n, rp->rio_cnt) bytes from internal buf to user buf
    cnt = n; 
    if(rp->rio_cnt < n)
        cnt = rp->rio_cnt; 
    memcpy(usrbuf, rp->rio_bufptr, cnt); 
    rp->rio_bufptr += cnt; 
    rp->rio_cnt -= cnt; 
    return cnt; 
}

/*
 * rio_readlineb - robustly read a text line(buffered)
 */
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen)
{
    int n, rc; 
    char c, *bufp = usrbuf; 

    for(n = 1; n < maxlen; n++)
    {
        if((rc = rio_read(rp, &c, 1)) == 1)
        {
            *bufp++ = c; 
            if (c == '\n')
                break; 
        }
        else if(rc == 0)
        {
            if(n == 1)
                return 0;   // EOF, no data read
            else 
                break;      // EOF, some data was read
        }
        else
            return -1;      // Error
    }
    *bufp = 0; 
    return n; 
}

/*
 * rio_readnb - Robustly read n bytes (buffered)
 */
ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n)
{
    size_t nleft = n; 
    ssize_t nread; 
    char *bufp = usrbuf; 

    while(nleft > 0)
    {
        if((nread = rio_read(rp, bufp, nleft)) < 0)
        {
            if(errno == EINTR)  // Interrept by sig handler return
                nread = 0;      // Call read() again
            else
                return -1;      // errno set by read()
        }
        else if(nread == 0)
            break;              // EOF
        nleft -= nread; 
        bufp += nread; 
    }

    return (n - nleft);     // Return >= 0
}


// Simplifies calls to bind(), connect(), and accept()
typedef struct sockaddr SA; 

int open_listenfd(int port)
{
    int listenfd, optval = 1; 
    struct sockaddr_in serveraddr; 

    // Create a socket descriptor
    if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1;

    // Eliminates "Address already in use" error from bind.
    if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
                    (const void *)&optval, sizeof(int)) < 0)
        return -1; 

    // 6 is TCP's protocal number
    // enable this, mush faster : 4000 req/s -> 17000 req/s
    if(setsockopt(listenfd, 6, TCP_CORK,
                    (const void *)&optval, sizeof(int)) < 0)
        return -1; 

    // Listenfd will be an endpoint for all requests to port
    //  on any IP address for this host
    memset(&serveraddr, 0, sizeof(serveraddr)); 
    serveraddr.sin_family = AF_INET; 
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    serveraddr.sin_port = htons((unsigned short)port); 
    if (bind(listenfd, (SA *)&serveraddr, sizeof(serveraddr)) < 0)
        return -1; 

    // Make it a listening socket ready to accept connection requests
    if(listen(listenfd, LISTENQ) < 0)
        return -1; 
    return listenfd; 
}

