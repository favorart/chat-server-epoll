#include <iostream>
#include <algorithm>
#include <set>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include <sys/epoll.h>

#include <errno.h>
#include <string.h>

#define MAX_EVENTS 32
#define USR_LIMIT  31
#define MSG_LIMIT  1025

int  set_nonblock (int fd)
{
 int flags;
#if defined (O_NONBLOCK)
 if ( -1 == (flags = fcntl (fd, F_GETFL, 0)) )
  flags = 0;
 return fcntl (fd, F_SETFL, flags | O_NONBLOCK);
#else
 flags = 1;
 return ioctl (fd, FIOBIO, &flags);
#endif
} 

int  main (int argc, char **argv)
{
 // ������� � ��������� �������� ����������
 // ���������� �� ��������� �������
 int  MasterSocket = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
 if ( MasterSocket == -1 )
 {
  std::cout << strerror (errno) << std::endl;
  return 1;
 }
 
 int  so_reuseaddr = 1; // ������������ �������, �������������� �� ������.
 if ( setsockopt (MasterSocket, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, sizeof (so_reuseaddr)) )
 {
  std::cout << strerror (errno) << std::endl;
  return 1;
 }

 struct sockaddr_in SockAddr;
 SockAddr.sin_family = AF_INET; /* Return IPv4 and IPv6 choices */
 // hints.ai_socktype = SOCK_STREAM; /* We want a TCP socket */
 SockAddr.sin_port = htons (3100);
 SockAddr.sin_addr.s_addr = htonl (INADDR_ANY);

 // ����������� � ������ �����
 int  Result = bind (MasterSocket, (struct sockaddr*) &SockAddr, sizeof (SockAddr));
 if ( Result == -1 )
 {
  std::cout << strerror (errno) << std::endl;
  return 1;
 }

 // �� ����������� �����, ��� �������������������
 set_nonblock (MasterSocket);

 // ����� �������� ���������� - ������ ����
 // � ���������� ����������, ����� - backlock
 // ��������� TCP �������
 Result = listen (MasterSocket, SOMAXCONN);
 if ( Result == -1 )
 {
  std::cout << strerror (errno) << std::endl;
  return 1;
 }

 // ������� �������� e-poll �������
 struct epoll_event  Event;
 Event.data.fd = MasterSocket;
 Event.events = EPOLLIN | EPOLLET; // ET - trigger regime
 // ������ ������ �� ����� �� �����, ���� �� �������� ��, ��� ��� ������.

 epoll_event *Events = (epoll_event*) calloc (MAX_EVENTS, sizeof (epoll_event));
 // ������ ������ EPoll - ��� �� ���������� ��������� �� ������� ������ ���� ��
 int EPoll = epoll_create1 (0);
 // ������� ������ ������ ����������� - ����� � ������ ��������� ����������
 epoll_ctl (EPoll, EPOLL_CTL_ADD, MasterSocket, &Event);

 // ------------------------
 // ������ ����� ������� ������������� ������
 char  user_buffer[USR_LIMIT][MSG_LIMIT] = {};
 size_t       user_buffer_len[MSG_LIMIT] = {};
 // ------------------------
 
 while (true)
 {
  // ������������������� - ��� ����� ����
  // ���� ������������ � �� ��� ������ �������, ������� �� ������ ���� ���������.
  // �� �� ������� ������ ���� ��� ��� ���� �������, �� ��� ����� ��������������...
  int  N = epoll_wait (EPoll, Events, MAX_EVENTS, -1); 
  for ( unsigned int i = 0; i < N; i++ )
  {
   if( (Events[i].events & EPOLLERR)
    || (Events[i].events & EPOLLHUP) )
   {
    // ���� ������ - ���������
    shutdown (Events[i].data.fd, SHUT_RDWR);
    close (Events[i].data.fd);
   }
   else if ( Events[i].data.fd == MasterSocket )
   {
    int  SlaveSocket;
    // ��������� ����������
    while ( (SlaveSocket = accept (MasterSocket, 0, 0)) != -1 )
    {
     int  so_reuseaddr = 1; // ������������ �������, �������������� �� ������.
     if ( setsockopt (SlaveSocket, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, sizeof (so_reuseaddr)) )
     {
      std::cout << strerror (errno) << std::endl;
      return 1;
     }

     // ������� �� ������ � �� ������
     set_nonblock (SlaveSocket);

     // c����� ��� ���� epoll
     struct epoll_event  Event;
     Event.data.fd = SlaveSocket;
     Event.events = EPOLLIN | EPOLLET;  // ET - ������ �� ���� � ������ �� ����
    
     // EPoll - �������� ���������� �� ���������� ������ �� �������
     // EPOLL_CTL_ADD - �������� ��� ����������
     epoll_ctl (EPoll, EPOLL_CTL_ADD, SlaveSocket, &Event);

     // logging
     std::cout << "accepted connection" << std::endl;

     const char *welcome = "Welcome\n";
     if( -1 == send (SlaveSocket, welcome, strlen (welcome), MSG_NOSIGNAL) )
     {
      shutdown (SlaveSocket, SHUT_RDWR);
      close    (SlaveSocket);
      std::cout << "connection terminated" << std::endl;
     }
     continue;
    }
   }
   else
   {
    char *recv_ptr2nline;
    char  recv_buffer[MSG_LIMIT];
    
    std::cout  << "!!!" << std::endl;
    
    int recv_len;
    // ������ �� ������ (read ��� signal)
    do
    {
     recv_len = recv (Events[i].data.fd, recv_buffer, (MSG_LIMIT - 1U), MSG_NOSIGNAL);
     if( !recv_len )
     { 
      std::cout  << "Nothing!" << std::endl;
      break;
      }
     /*||*/
     if( recv_len == -1 )
     {
      // ��������� ����������
      shutdown (Events[i].data.fd, SHUT_RDWR);
      close    (Events[i].data.fd);
      std::cout << "connection terminated" << std::endl;
      // continue;//
      break;
     }
    
     recv_buffer[recv_len] = '\0';
     std::cout  << "Here!" << std::endl; // */ << recv_buffer << std::endl;
     recv_ptr2nline = (char*) memchr (recv_buffer, '\n', recv_len + 1);
     if ( !recv_ptr2nline )
      std::cout << ">> NULL\n";
     else
      std::cout << (recv_ptr2nline - recv_buffer);
      
     // ������ ���������
     while ( (recv_ptr2nline = (char*) memchr (recv_buffer, '\n', recv_len + 1)) )
     {
      char    msg_line[MSG_LIMIT];
      size_t  msg_len = (++recv_ptr2nline - recv_buffer);
     
      // if too long
      if( msg_len + user_buffer_len[i] >= MSG_LIMIT )
      {
       size_t  shift = (msg_len + user_buffer_len[i]) - (MSG_LIMIT - 1);

       recv_ptr2nline -= shift;
       msg_len        -= shift;
      
       std::cout << "shift: " << shift << std::endl;
      }
      memcpy (msg_line, user_buffer[i], user_buffer_len[i]);
      
      std::cout << "\n!!! 1 !!!\n";     
     /* std::cout << ">> b_len: "   << user_buffer_len[i] << " buff_i: \"" << user_buffer[i] << "\"" << std::endl;
      std::cout << ">> rcv_len: " << recv_len           << " recv: \""   << recv_buffer    << "\"" << std::endl;
      std::cout << ">> msg_len: " << msg_len            << " msg: \""    <<  msg_line      << "\"" << std::endl;*/     
     
      memcpy (msg_line + user_buffer_len[i], recv_buffer, msg_len);
      msg_len += user_buffer_len[i];
      user_buffer_len[i] = 0;
                                 
      memmove (recv_buffer, recv_ptr2nline, ((recv_buffer + recv_len) - recv_ptr2nline) );         
      recv_len -= (recv_ptr2nline - recv_buffer);
     
      std::cout << "\n!!! 2 !!!\n";
   /*  std::cout << ">> b_len: "   << user_buffer_len[i] << " buff_i: \"" << user_buffer[i] << "\"" << std::endl;*/
     /*std::cout << ">> rcv_len: " << recv_len           << " recv: \""   << recv_buffer    << "\"" << std::endl;*/
    /* std::cout << ">> msg_len: " << msg_len            << " msg: \""    <<  msg_line      << "\"" << std::endl;*/
     
      msg_line[msg_len] = '\0';
      std::cout << msg_line << std::endl;
     
      // to everybody
      // for( unsignde int j = 1U; j < MAX_EVENTS; ++j )
      // non-block sending
      send (Events[i].data.fd, msg_line, msg_len, MSG_NOSIGNAL); // just echo
      // !!! Check for -1

      // msg_line[msg_len] = '\0'; 
      // std::cout /*<< "log: " */ << msg_line << std::endl;
     } // end while recv_ptr2nline
    
     // std::cout << "It is here.\n";
     if ( !recv_ptr2nline )
      std::cout << ">> NULL\n";
     else
      std::cout << (recv_ptr2nline - recv_buffer);
    
     if ( recv_len )
     {
      memcpy (user_buffer[i], recv_buffer, recv_len);
      user_buffer_len[i] = recv_len;
      recv_len = 0U;
     }
     else if( recv_len + user_buffer_len[i] >= MSG_LIMIT )
     {
      size_t  shift = (recv_len + user_buffer_len[i]) - (MSG_LIMIT - 1);

      memcpy (user_buffer + user_buffer_len[i], recv_buffer, recv_len - shift);

      send (Events[i].data.fd, user_buffer[i], (MSG_LIMIT - 1), MSG_NOSIGNAL); // just echo      
      // !!! Check for -1
      /*
      std::cout << "\n!!! 2.5 !!!\n";
      std::cout << ">> b_len: "   << user_buffer_len[i] << " buff_i: \"" << user_buffer[i] << "\"" << std::endl; */
      memcpy (user_buffer[i], recv_buffer + (recv_len - shift), shift);
      user_buffer_len[i] = shift;
      recv_len = 0U;
     }
    /*
     std::cout << "\n!!! 3 !!!\n";
     std::cout << ">> b_len: "   << user_buffer_len[i] << " buff_i: \"" << user_buffer[i] << "\"" << std::endl;
     std::cout << ">> rcv_len: " << recv_len           << " recv: \""   << recv_buffer    << "\"" << std::endl;
    */
     } while (1);
    } // end else
   } // end for Event [i]
 } // end while true

 shutdown (MasterSocket, SHUT_RDWR);
 close (MasterSocket);

 free (Events);

 return 0;
}
