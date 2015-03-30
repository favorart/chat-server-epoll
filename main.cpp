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
 // Слушает и принимает входящие соединения
 // отличается от остальных сокетов
	int  MasterSocket = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if ( MasterSocket == -1 )
	{
		std::cout << strerror (errno) << std::endl;
		return 1;
	}
 
 int  so_reuseaddr = 1; // манипулируем флагами, установленными на сокете.
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

 // привязываем к сокету адрес
	int  Result = bind (MasterSocket, (struct sockaddr*) &SockAddr, sizeof (SockAddr));
	if ( Result == -1 )
	{
		std::cout << strerror (errno) << std::endl;
		return 1;
	}

 // не блокирующий режим, при мультиплексировании
	set_nonblock (MasterSocket);

 // режим ожидание соединений - флажок ядра
 // и принимание соединения, буфер - backlock
 // принимает TCP запросы
	Result = listen (MasterSocket, SOMAXCONN);
	if ( Result == -1 )
	{
		std::cout << strerror (errno) << std::endl;
		return 1;
	}

 // Событие создания e-poll объекта
	struct epoll_event  Event;
	Event.data.fd = MasterSocket;
	Event.events = EPOLLIN | EPOLLET; // ET - trigger regime
 // ничего нового не придёт на сокет, пока не вычитать всё, что уже пришло.

	epoll_event *Events = (epoll_event*) calloc (MAX_EVENTS, sizeof (epoll_event));
 // Создаём объект EPoll - что бы мониторить изменения на сокетах внутри ядра ОС
	int EPoll = epoll_create1 (0);
 // Добляем первый объект мониторинга - сокет в режиме получения соединений
	epoll_ctl (EPoll, EPOLL_CTL_ADD, MasterSocket, &Event);

 // ------------------------
 // Личный буфер каждого подключённого клента
 char  user_buffer[USR_LIMIT][MSG_LIMIT] = {};
 size_t       user_buffer_len[MSG_LIMIT] = {};
	// ------------------------
 
 while (true)
	{
  // Мультиплексирование - это когда есть
  // куча дескрипторов и мы ждём разных событий, которые не должны быть однородны.
  // Мы не обязаны писать один код для всех случаев, но нам нужна дифференциация...
		int  N = epoll_wait (EPoll, Events, MAX_EVENTS, -1); 
		for ( unsigned int i = 0; i < N; i++ )
		{
			if( (Events[i].events & EPOLLERR)
    || (Events[i].events & EPOLLHUP) )
			{
    // если ошибка - закрываем
				shutdown (Events[i].data.fd, SHUT_RDWR);
				close (Events[i].data.fd);
			}
			else if ( Events[i].data.fd == MasterSocket )
			{
				int  SlaveSocket;
    // Принимаем соединение
				while ( (SlaveSocket = accept (MasterSocket, 0, 0)) != -1 )
				{
     int  so_reuseaddr = 1; // манипулируем флагами, установленными на сокете.
     if ( setsockopt (SlaveSocket, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, sizeof (so_reuseaddr)) )
     {
      std::cout << strerror (errno) << std::endl;
	    	return 1;
     }

     // слушаем на чтение и на запись
					set_nonblock (SlaveSocket);

     // cоздаём под него epoll
					struct epoll_event  Event;
					Event.data.fd = SlaveSocket;
					Event.events = EPOLLIN | EPOLLET;  // ET - читать до нуля и писать до нуля
				
     // EPoll - файловый десериптор на структурку данных на события
     // EPOLL_CTL_ADD - изменить эту структурку
					epoll_ctl (EPoll, EPOLL_CTL_ADD, SlaveSocket, &Event);

     // logging
     std::cout << "accepted connection" << std::endl;

     char* welcome = "Welcome\n";
     if( -1 == send (Events[i].data.fd, welcome, std::strlen (welcome), MSG_NOSIGNAL) )
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
				
    // читаем из сокета (read без signal)
    int recv_len = recv (Events[i].data.fd, recv_buffer, MSG_LIMIT, MSG_NOSIGNAL);
    if( !recv_len || recv_len == -1 )
    {
     // завершаем соединение
     shutdown (events[i].data, SHUT_RDWR);
     close    (events[i].data.fd);
     std::cout << "connection terminated" << std::endl;
    }

    // парсим сообщения
    while ( (recv_ptr2nline = memchr (recv_buffer, '\n', recv_len)) )
    {
     char    msg_line[MSG_LIMIT];
     size_t  msg_len = (++recv_ptr2nline - recv_buffer);

     // if too long
     if( msg_len + user_buffer_len[i] >= MSG_LIMIT )
     {
      size_t  shift = (msg_len + user_buffer_len[i]) - (MSG_LIMIT - 1);

      recv_ptr2nline -= shift;
      msg_len        -= shift;
     }
     memcpy (msg_line, user_buffer[i], user_buffer_len[i]);

     memcpy (msg_line + user_buffer_len[i], recv_buffer, msg_len);
     msg_len += user_buffer_len[i];
     user_buffer_len[i] = 0;
                                 
     memmove (recv_buffer, recv_ptr2nline, ((recv_buffer + recv_len) - recv_ptr2nline) );         
     recv_len -= (recv_ptr2nline - recv_buffer);
     
     // to everybody
     // for( unsignde int j = 1U; j < MAX_EVENTS; ++j )
     // non-block sending
     send (Events[i].data.fd, msg_line, msg_len, MSG_NOSIGNAL); // just echo
     // !!! Check for -1

     msg_line[msg_len] = '\0';
     std::cout << msg_line << std::endl;
    } // end while recv_ptr2nline
    
    if ( recv_len )
    {
     memcpy (user_buffer[i], buffer, recv_len);
     user_buffer_len[i] = recv_len;
     recv_len = 0;
    }
    else if( recv_len + user_buffer_len[i] >= MSG_LIMIT )
    {
     size_t  shift = (recv_len + user_buffer_len[i]) - (MSG_LIMIT - 1);

     if( msg_len )
     {
      memcpy (user_buffer + user_buffer_len[i], recv_buffer, recv_len - shift);

      send (Events[i].data.fd, user_buffer, (MSG_LIMIT - 1), MSG_NOSIGNAL); // just echo      
      // !!! Check for -1

      memcpy (user_buffer, recv_buffer + (recv_len - shift), shift);
      user_buffer_len[i] = shift;
     }

     recv_ptr2nline -= shift;
     msg_len        -= shift;
    
     memmove (recv_buffer, recv_ptr2nline, ((recv_buffer + recv_len) - recv_ptr2nline) );    
    }

			} // end else
		} // end for Event [i]
	} // end while true

 shutdown (MasterSocket, SHUT_RDWR);
 close (MasterSocket);

 free (Events);

	return 0;
}
