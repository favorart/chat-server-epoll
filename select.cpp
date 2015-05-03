#include <iostream>
#include <algorithm>
#include <map>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>

#include <errno.h>
#include <string.h>

#define  PORT     3100U
#define  BUFSIZE  1024U

int set_nonblock (int fd)
{
  int flags;
#if defined(O_NONBLOCK)
  if ( -1 == (flags = fcntl (fd, F_GETFL, 0)) )
  {
    flags = 0;
  }
  return fcntl (fd, F_SETFL, flags | O_NONBLOCK);
#else
  flags = 1;
  return ioctl (fd, FIOBIO, &flags);
#endif
}

int main (int argc, char **argv)
{
  int  sendSocket = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if ( sendSocket == -1 )
  {
    std::cout << strerror (errno) << std::endl;
    return 1;
  }

  sockaddr_in SockAddr = { 0 };
  SockAddr.sin_family = AF_INET;
  SockAddr.sin_port = htons (PORT);
  SockAddr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

  if ( connect (sendSocket, (struct sockaddr *) (&SockAddr), sizeof (SockAddr)) == -1 )
  {
    std::cout << strerror (errno) << std::endl;
    return 1;
  }
  
  set_nonblock (sendSocket);
  set_nonblock (STDIN_FILENO);

  char buffer[BUFSIZE + 1U];
  int size;

  while ( true )
  {
    fd_set readset, errorset;
 
    FD_ZERO (&readset);
    FD_SET  (STDIN_FILENO, &readset);
    FD_SET  (sendSocket,   &readset);

    if ( select (std::max (0, sendSocket) + 1, &readset, NULL, NULL, NULL) == -1 )
    {
      std::cout << "Error occured\n";
      return 1;
    }

    if ( FD_ISSET (sendSocket, &readset) )
    {
      while ( true )
      {
        int  recvSize = recv (sendSocket, buffer, MAX_BUFFER, MSG_NOSIGNAL);
        if ( recvSize <= 0 )
        {
          break;
        }
        buffer[recvSize] = 0;
        std::cout << "Message from server: " << buffer;
      }
    }

    if ( FD_ISSET (STDIN_FILENO, &readset) )
    {
      fgets (buffer, MAX_BUFFER, stdin);
      int len = strlen (buffer);
      std::cout << "\x1b[1A\x1b[2K";
      send (sendSocket, buffer, len, MSG_NOSIGNAL);
    }
  }

  return 0;
}

