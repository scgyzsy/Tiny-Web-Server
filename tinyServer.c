/*
 * tinyServer.c - A simple, multi-process  HTTP/1.0 Web server that uses the
 *    1. GET method to serve static and dynamic content.
 *    2. POST method to server static and dynamic content. 
 *    3. HEAD method to get http header.
 */
#include "tinyServer.h"

// doit - handle one HTTP request/response transaction
void doit(int fd);

//
int read_requesthdrs(rio_t *rp, char *method);

//
int  parse_uri(char *uri, char *filename, char *cgiargs);

//
void serve_static(int fd, char *filename, int filesize, char *method);

//
void get_filetype(char *filename, char *filetype);

//
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);

//
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);

//
void sigchild_handler(int sig); 


int main(int argc, char **argv) 
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    // Check command line args
    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(1);
    }

    int portNum = atoi(argv[1]); 
    listenfd = open_listenfd(portNum);

    // Ignore SIGPIPE signal, so if browser cancels the request, it
    // won't kill the whole process. 
    if(signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        unix_error("mask signal pipe error"); 

    if(signal(SIGCHLD, sigchild_handler) == SIG_ERR)
        unix_error("signal child handler error"); 

    while (1) {
	    clientlen = sizeof(clientaddr);
	    connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
	    if(fork() == 0)
        {
	        close(listenfd);
            doit(connfd);
            close(connfd);
            exit(0);  
        }
        close(connfd);  // Important
    }
}

void sigchild_handler(int sig)
{
    int old_errno = errno; 
    int status; 
    pid_t pid; 
    
    while((pid = waitpid(-1, &status, WNOHANG)) > 0);
    
    errno = old_errno; 
}

/*
 * doit - handle one HTTP request/response transaction
 */
void doit(int fd) 
{
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    // Read request line and headers
    rio_readinitb(&rio, fd);
    if (!rio_readlineb(&rio, buf, MAXLINE)) 
        return;
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);  
    if (!(strcasecmp(method, "GET") == 0 || 
          strcasecmp(method, "HEAD") == 0 ||
          strcasecmp(method, "POST") == 0 )) 
    { 
        clienterror(fd, method, "501", "Not Implemented",
                    "Tiny does not implement this method");
        return;
    } 
    int param_len = read_requesthdrs(&rio, method);
    rio_readnb(&rio, buf, param_len);  

    // Parse URI from GET request
    is_static = parse_uri(uri, filename, cgiargs);
    if (stat(filename, &sbuf) < 0) 
    { 
	    clienterror(fd, filename, "404", "Not found",
		        "Tiny couldn't find this file");
	    return;
    }

    if (is_static)  // Serve static content
    { 
	    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
        {
	        clienterror(fd, filename, "403", "Forbidden",
			    "Tiny couldn't read the file");
	        return;
	    }
	    serve_static(fd, filename, sbuf.st_size, method);
    }
    else {      // Serve dynamic content
	if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
	    clienterror(fd, filename, "403", "Forbidden",
			"Tiny couldn't run the CGI program");
	    return;
	}

    if(strcasecmp(method, "POST") == 0)
        strncpy(cgiargs, buf, MAXLINE);  
	
    serve_dynamic(fd, filename, cgiargs, method);
    }
}

/*
 * read_requesthdrs - read HTTP request headers
 */
int read_requesthdrs(rio_t *rp, char *method) 
{
    char buf[MAXLINE];
    int len = 0; 
    
    do
    {
        rio_readlineb(rp, buf, MAXLINE); 
        printf("%s", buf); 
        if (strcasecmp(method, "POST") == 0 && strncasecmp(buf, "Content-Length:", 15) == 0)
            sscanf(buf, "Content-Length: %d", &len); 
    }while(strcmp(buf, "\r\n")); 

    return len;
}

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
int parse_uri(char *uri, char *filename, char *cgiargs) 
{
    char *ptr;

    if (!strstr(uri, "cgi-bin")) {  // Static content
	strcpy(cgiargs, "");
	strcpy(filename, ".");
	strcat(filename, uri);
	if (uri[strlen(uri)-1] == '/')
	    strcat(filename, "home.html");
	return 1;
    }
    else {  // Dynamic content
	ptr = index(uri, '?');
	if (ptr) {
	    strcpy(cgiargs, ptr+1);
	    *ptr = '\0';
	}
	else 
	    strcpy(cgiargs, "");
	strcpy(filename, ".");
	strcat(filename, uri);
	return 0;
    }
}

/*
 * serve_static - copy a file back to the client 
 */
void serve_static(int fd, char *filename, int filesize, char *method) 
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];
 
    // Send response headers to client
    get_filetype(filename, filetype);
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    rio_writen(fd, buf, strlen(buf));
    printf("Response headers:\n");
    printf("%s", buf);

    if(strcasecmp(method, "HEAD") == 0)
        return; 

    // Send response body to client
    srcfd = open(filename, O_RDONLY, 0);
    srcp = mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    close(srcfd);
    rio_writen(fd, srcp, filesize);
    munmap(srcp, filesize);
}

/*
 * get_filetype - derive file type from file name
 */
void get_filetype(char *filename, char *filetype) 
{
    if (strstr(filename, ".html"))
	strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
	strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png"))
	strcpy(filetype, "image/png");
    else if (strstr(filename, ".jpg"))
	strcpy(filetype, "image/jpeg");
    else if (strstr(filename, ".pdf"))
    strcpy(filetype, "application/pdf"); 
    else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
    else if (strstr(filename, ".pptx"))
    strcpy(filetype, "application/vnd.openxmlformats-officedocument.presentationml.presentation");
    else if(strstr(filename, ".ppt"))
    strcpy(filetype, "application/vnd.ms-powerpoint");
    else if(strstr(filename, ".mpeg"))
    strcpy(filetype, "video/mpeg"); 
    else
	strcpy(filetype, "text/plain");
}  

/*
 * serve_dynamic - run a CGI program on behalf of the client
 */
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method) 
{
    char buf[MAXLINE], *emptylist[] = { NULL };

    // Return first part of HTTP response
    sprintf(buf, "HTTP/1.0 200 OK\r\n"); 
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    rio_writen(fd, buf, strlen(buf));
  
    if (fork() == 0) {  // Child
	// Real server would set all CGI vars here
    if(signal(SIGPIPE, SIG_DFL) == SIG_ERR)
        unix_error("unmask signal pipe error"); 

	setenv("QUERY_STRING", cgiargs, 1); 
    setenv("REQUEST_METHOD", method, 1); 
	dup2(fd, STDOUT_FILENO);         // Redirect stdout to client
	execve(filename, emptylist, environ); // Run CGI program
    }
    // Wait(NULL); // Parent waits for and reaps child
}

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    // Build the HTTP response body
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    // Print the HTTP response
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    rio_writen(fd, buf, strlen(buf));
    rio_writen(fd, body, strlen(body));
}
