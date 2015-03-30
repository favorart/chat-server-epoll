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

#define BUFSIZE 1025


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
 char buf[BUFSIZE] = {};

 // CREATING A SOCKET FOR CLIENT
 int client = socket (AF_INET, SOCK_STREAM, 0);

 // FILLING SERVER ADDRESS STRUCTURE
 socklen_t addrlen = sizeof (sockaddr_in);
 struct sockaddr_in  serv_addr;
 serv_addr.sin_family = AF_INET;
 serv_addr.sin_port = htons (3100);
 serv_addr.sin_addr.s_addr = htonl (INADDR_ANY);

 // CONNECTING TO SERVER    
 if ( connect (client, (sockaddr*) &serv_addr, sizeof (serv_addr)) < 0 )
 {
  perror ("Error connecting:\n");
  exit (1);
 }
 set_nonblock (client);
 
 int ch;
 int count = 0;
 int res = 0;
 // WRITING TO CLIENT AND RECIEVING MESSAGES
 while ( (ch = getchar ()) != EOF )
 {
  if ( ch == '\n' )
  {
   buf[count++] = ch;
   buf[count  ] = '\0';
   
   printf ("count = %d, buf = %s", count, buf);
   
   // SENDING MESSAGE TO SERVER
   send (client, buf, count, 0);
   
   memset (buf, 0, BUFSIZE);
   count = 0;
   
   // RECIEVING MESSAGES FROM SERVER
   while ( 0 < (res = recvfrom (client, buf, (BUFSIZE - 1U), 0, (struct sockaddr*) &serv_addr, &addrlen)) )
   {
    // TODO: DO NOT RECIVE THE MESSAGES FROM OTHER CLIENTS!!!
    buf[res] = '\0';   
    printf ("Message from server: %s", buf);
   }
   if( res <= 0 )
    printf ("Nothing from server\n");
  }
  else
   buf[count++] = ch; 
 }

 shutdown (client, SHUT_RDWR);
 close    (client);
 return 0;
}
