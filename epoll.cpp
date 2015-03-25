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

int set_nonblock(int fd)
{
	int flags;
#if defined(O_NONBLOCK)
	if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
		flags = 0;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
	flags = 1;
	return ioctl(fd, FIOBIO, &flags);
#endif
} 

int main(int argc, char **argv)
{
 // Слушает и принимает входящие соединения
 // отличается от остальных сокетов
	int MasterSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if(MasterSocket == -1)
	{
		std::cout << strerror(errno) << std::endl;
		return 1;
	}

	struct sockaddr_in SockAddr;
	SockAddr.sin_family = AF_INET;
	SockAddr.sin_port = htons(12345);
	SockAddr.sin_addr.s_addr = htonl(INADDR_ANY);

 // привязываем к сокету адрес
	int Result = bind(MasterSocket, (struct sockaddr *)&SockAddr, sizeof(SockAddr));

	if(Result == -1)
	{
		std::cout << strerror(errno) << std::endl;
		return 1;
	}

 // не блокирующий режим, при мультиплексировании
	set_nonblock(MasterSocket);

 // режим ожидание соединений - флажок ядра
 // и принимание соединения, буфер - backlock
	Result = listen(MasterSocket, SOMAXCONN);
 // принимает TCP запросы

	if(Result == -1)
	{
		std::cout << strerror(errno) << std::endl;
		return 1;
	}

	struct epoll_event Event;
	Event.data.fd = MasterSocket;
	Event.events = EPOLLIN; // | EPOLLET;

	struct epoll_event * Events;
	Events = (struct epoll_event *) calloc(MAX_EVENTS, sizeof(struct epoll_event));

	int EPoll = epoll_create1(0);
	epoll_ctl(EPoll, EPOLL_CTL_ADD, MasterSocket, &Event);

 char buffers[1024][1024] = {0};
	while(true)
	{
  // Мультиплексировании
  // куча дескрипторов и ждём разных событий и они не должны быть однородны,
  // мы не обязаны писать один код для всех случаев, и нам нужна дифференциация
		int N = epoll_wait(EPoll, Events, MAX_EVENTS, -1); 
		for(unsigned int i = 0; i < N; i++)
		{
			if((Events[i].events & EPOLLERR) || (Events[i].events & EPOLLHUP))
			{
    // если ошибка - закрываем
				shutdown(Events[i].data.fd, SHUT_RDWR);
				close(Events[i].data.fd);
			}
			else if(Events[i].data.fd == MasterSocket)
			{
    // Принимаем соединение
    // accept in cycle
				int SlaveSocket = 0;
    while ( -1 != (SlaveSocket = accept(MasterSocket, 0, 0)) )
    {
     // слушаем на чтение и на запись
				 set_nonblock(SlaveSocket);
     
     // cоздаём под него epoll
				 struct epoll_event Event;
				 Event.data.fd = SlaveSocket;
				 Event.events = EPOLLIN | EPOLLET; // ET - читать до нуля и писать до нуля
     
     // EPoll - файловый десериптор на структурку данных на события
     // EPOLL_CTL_ADD - изменить эту структурку
				 epoll_ctl(EPoll, EPOLL_CTL_ADD, SlaveSocket, &Event);

     std::cout << "accepted connection" << std::endl;     
    }
			}
			else
			{
				static char Buffer[1024];

				// читаем из сокета (read без signal)
    // buffer для каждого Event-socket
    int RecvSize = recv(Events[i].data.fd, Buffer, 1024, MSG_NOSIGNAL);

    if( ( = std::memchar (Buffer, '\n')) )
    {
     char outline[1024];

     // отправляем в сеть
     std::cout << "Welcome" << std::endl;
			 	//send(Events[i].data.fd, Buffer, RecvSize, MSG_NOSIGNAL);
    }
			}
		}
	}

	return 0;
}
