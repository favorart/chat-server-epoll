#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <strings.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <signal.h>

#define BUFSIZE 1024


int  set_nonblock (int fd)
{
 int flags;
#ifdef O_NONBLOCK
 if ( -1 == (flags = fcntl (fd, F_GETFL, 0)) )
  flags = 0;
 return fcntl (fd, F_SETFL, flags | O_NONBLOCK);
#endif
 flags = 1;
 return ioctl (fd, FIONBIO, &flags);
}

int  main (int argc, char **argv)
{
 signal (SIGPIPE, SIG_IGN);
 char *buf = (char*) calloc (BUFSIZE, 1);

 // CREATING A SOCKET FOR CLIENT
 int client = socket (AF_INET, SOCK_STREAM, 0);

 // FILLING SERVER ADDRESS STRUCTURE
 socklen_t addrlen = sizeof (struct sockaddr_in);
 struct sockaddr_in serv_addr;
 serv_addr.sin_family = AF_INET;
 serv_addr.sin_port = htons (3100);
 serv_addr.sin_addr.s_addr = htonl (INADDR_ANY);

 // CONNECTING TO SERVER    
 if ( connect (client, (struct sockaddr *) &serv_addr, sizeof (serv_addr)) < 0 )
 {
  perror ("Error connecting:\n");
  exit (1);
 }
 set_nonblock (client);

 // WRITING TO CLIENT AND RECIEVING MESSAGES
 int ch;
 while ( (ch = getchar ()) != EOF )
 {
  if ( ch == '\n' )
  {
   // SENDING MESSAGE TO SERVER
   if ( strlen (buf) )
    send (client, buf, BUFSIZE, 0);
   
   // RECIEVING MESSAGES FROM SERVER
   while ( recvfrom (client, buf, BUFSIZE, 0, (struct sockaddr*) &serv_addr, &addrlen) > 0 )
    printf("Message from server: \"%s\"\n", buf);
   
   bzero (buf, BUFSIZE);
   continue;
  }
  buf[strlen (buf)] = ch; 
 }

 free (buf);
 shutdown (client, SHUT_RDWR);
 close (client);
 return 0;
}