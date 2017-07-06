# Tiny-Web-Server
A very simple, fast, multiprocessing Web Server write in C.<br/> 
I met a very nice book last year —— [Computer Systems: A Programmer's Perspective(深入理解操作系统)](http://csapp.cs.cmu.edu/). It taught me how 
to write a tiny in ASCII C.  Now I'm reading another nice book —— [Advanced Programming in the UNIX® Environment(Unix环境高级编程)](http://www.apuebook.com/apue3e.html)
As I understand the OS is not so shallow as before, I  will keeping improving this tiny Web Server. 

see [http://123.207.54.56:8888/](http://123.207.54.56:8888/) to test What the Tiny-Web-Server can do. 

#### Features

* Basic MIME mapping
* implement HTTP GET, POST, HEAD method. 
* send file to brower.  
* Support Accept-Ranges: bytes(for in browser PDF, MP4 playing)
* use Memory mapping to load static files 
* Clean deal with SIGCHLD, SIGPIPE signal and EPIPE error

### Usage
`tinyServer <port>`, opens a server in the current directory. Then you can use your favorite brower or telnet to test it.


### TODO
* HTTP persistent connection (for HTTP/1.1)
* multithread version with thread pool

### Compile and run
```shell
make
./tinyServer <port>
```
