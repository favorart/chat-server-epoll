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
#define USR_LIMIT 31
#define MSG_LIMIT 1025

// #define _DEBUG
// #define _DEBUG_1

int set_nonblock (int fd)
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


int main (int argc, char **argv)
{
    // Слушает и принимает входящие соединения
    // отличается от остальных сокетов
    int MasterSocket = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if ( MasterSocket == -1 )
    {
        std::cout << strerror (errno) << std::endl;
        return 1;
    }
    
    int so_reuseaddr = 1; // манипулируем флагами, установленными на сокете.
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
    int Result = bind (MasterSocket, (struct sockaddr*) &SockAddr, sizeof (SockAddr));
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
    struct epoll_event Event;
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
    char user_buffer[USR_LIMIT][MSG_LIMIT] = {};
    size_t user_buffer_len[USR_LIMIT] = {};
    
    // ------------------------

    while (true)
    {
        // Мультиплексирование - это когда есть
        // куча дескрипторов и мы ждём разных событий, которые не должны быть однородны.
        // Мы не обязаны писать один код для всех случаев, но нам нужна дифференциация...
        int N = epoll_wait (EPoll, Events, MAX_EVENTS, -1);
#ifdef _DEBUG_1
        std::cout << "Num of Events: " << N << "\n" << std::endl;
#endif
        for ( unsigned int i = 0; i < N; i++ )
        {
#ifdef _DEBUG_1
            std::cout << "Inside iteration: " << i << "\n" << std::endl;
#endif
            if( (Events[i].events & EPOLLERR)
             || (Events[i].events & EPOLLHUP)
             || (Events[i].events & EPOLLOUT) )
            {
                // если ошибка - закрываем
                shutdown (Events[i].data.fd, SHUT_RDWR);
                close    (Events[i].data.fd);
                std::cout << "connection terminated" << std::endl;
                
                memset (user_buffer[i], 0, MSG_LIMIT);
                user_buffer_len[i] = 0;
            }
            
            else if ( Events[i].data.fd == MasterSocket )
            {
                int SlaveSocket;
                // Принимаем соединение
                while ( (SlaveSocket = accept (MasterSocket, 0, 0)) != -1 )
                {
                    int so_reuseaddr = 1; // манипулируем флагами, установленными на сокете.
                    if ( setsockopt (SlaveSocket, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, sizeof (so_reuseaddr)) )
                    {
                        std::cout << strerror (errno) << std::endl;
                        return 1;
                    }
                    
                    // слушаем на чтение и на запись
                    set_nonblock (SlaveSocket);
                    
                    // cоздаём под него epoll
                    struct epoll_event Event;
                    Event.data.fd = SlaveSocket;
                    Event.events = EPOLLIN | EPOLLET; // ET - читать до нуля и писать до нуля
                    
                    // EPoll - файловый десериптор на структурку данных на события
                    // EPOLL_CTL_ADD - изменить эту структурку
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
                int   recv_len;
                
                do
                {
                    // читаем из сокета (read без signal)
                    recv_len = recv (Events[i].data.fd, recv_buffer, (MSG_LIMIT - 1U), MSG_NOSIGNAL);
                    recv_buffer[recv_len] = '\0';
#ifdef _DEBUG                    
                    std::cout << "!!!" << std::endl;
                    std::cout << "recv_len: " << recv_len << " recv_buffer: \"" << recv_buffer << "\"" << std::endl;
#elif _DEBUG_1
                    std::cout << "Recv Len: "<< recv_len << " Error number: "<< strerror (errno) << std::endl;    
#endif
                    
                    if( recv_len == 0 )
                    {
#ifdef _DEBUG
                        std::cout << "Nothing!" << std::endl;
#endif
                        // завершаем соединение
                        shutdown (Events[i].data.fd, SHUT_RDWR);
                        close    (Events[i].data.fd);
                        std::cout << "connection terminated" << std::endl;
                        break;
                    }
                    
                    if( recv_len == -1 )
                    {
                        if( errno != EAGAIN )
                        {
#ifdef _DEBUG
                            std::cout << "Error!" << std::endl;
#endif
                            // завершаем соединение
                            shutdown (Events[i].data.fd, SHUT_RDWR);
                            close (Events[i].data.fd);
                            std::cout << "connection terminated" << std::endl;
                            
                            memset (user_buffer[i], 0, MSG_LIMIT);
                            user_buffer_len[i] = 0;
                         }
                         else
                         {
                            // TODO: Write from buffer                            
                         }
                         break;
                    }

#ifdef _DEBUG
                    std::cout << "recv_len: " << recv_len << " recv_buffer: \"" << recv_buffer << "\"" << std::endl;
                    // Debug info
                    if ( (recv_ptr2nline = (char*) memchr (recv_buffer, '\n', recv_len)) )
                    {
                        std::cout << "\\n found, position: NULL\n";
                    }
                    else
                    {
                        std::cout << "\\n found, position: " << (recv_ptr2nline - recv_buffer) << std::endl;
                    }
#endif

                    // парсим сообщения
                    while ( (recv_ptr2nline = (char*) memchr (recv_buffer, '\n', recv_len)) )
                    {
                        char msg_line[MSG_LIMIT];
                        size_t msg_len = (++recv_ptr2nline - recv_buffer);

                        // if too long
                        if( msg_len + user_buffer_len[i] >= MSG_LIMIT )
                        {
                            size_t shift = (msg_len + user_buffer_len[i]) - (MSG_LIMIT - 1);
                            recv_ptr2nline -= shift;
                            msg_len -= shift;
#ifdef _DEBUG
                            std::cout << "shift: " << shift << std::endl;
#endif
                        }
                        memcpy (msg_line, user_buffer[i], user_buffer_len[i]);
#ifdef _DEBUG
                        std::cout << "\n!!! 1 !!!\n";
                        std::cout << ">> b_len: " << user_buffer_len[i] << " buff_i: \"" << user_buffer[i] << "\"" << std::endl;
                        std::cout << ">> rcv_len: " << recv_len << " recv: \"" << recv_buffer << "\"" << std::endl;
#endif

                        memcpy (msg_line + user_buffer_len[i], recv_buffer, msg_len);
                        msg_len += user_buffer_len[i];
                        user_buffer_len[i] = 0;
                        
                        memmove (recv_buffer, recv_ptr2nline, ((recv_buffer + recv_len) - recv_ptr2nline) );
                        recv_len -= (recv_ptr2nline - recv_buffer);

#ifdef _DEBUG
                        std::cout << "\n!!! 2 !!!\n";
                        std::cout << ">> b_len: " << user_buffer_len[i] << " buff_i: \"" << user_buffer[i] << "\"" << std::endl;
                        std::cout << ">> rcv_len: " << recv_len << " recv: \"" << recv_buffer << "\"" << std::endl;
#endif
                        for( unsigned int j = 0U; j < N; ++j ) // Sending to everybody
                        {
                            // non-block sending
                            if ( -1 == send (Events[j].data.fd, msg_line, msg_len, MSG_NOSIGNAL) )
                            {
                                shutdown (Events[j].data.fd, SHUT_RDWR);
                                close (Events[j].data.fd);
                                std::cout << "connection terminated" << std::endl;
                                
                                memset (user_buffer[j], 0, MSG_LIMIT);
                                user_buffer_len[j] = 0;   
                            }
                        }
                        
                        // logging
                        if ( msg_line[msg_len - 1U] == '\n' ) msg_line[msg_len - 1U] = '\0';
                        else msg_line[msg_len] = '\0';
                        std::cout << "Message from : " << msg_line << std::endl;
                        
                    } // end parsing messages (searching for '\n')
                    
                    if ( recv_len != 0 )
                    {
                        memcpy (user_buffer[i], recv_buffer, recv_len);
                        user_buffer_len[i] = recv_len;
                        recv_len = 0U;
                        memset (recv_buffer, 0, MSG_LIMIT);
                    }

                    else if( recv_len + user_buffer_len[i] >= MSG_LIMIT )
                    {
                        size_t shift = (recv_len + user_buffer_len[i]) - (MSG_LIMIT - 1);
                        memcpy (user_buffer + user_buffer_len[i], recv_buffer, recv_len - shift);

                        for( unsigned int j = 0U; j < N; ++j )
                            if( -1 == send (Events[j].data.fd, user_buffer[i], (MSG_LIMIT - 1), MSG_NOSIGNAL) )
                            {
                                shutdown (Events[j].data.fd, SHUT_RDWR);
                                close (Events[j].data.fd);
                                std::cout << "connection terminated" << std::endl;
                                
                                memset (user_buffer[j], 0, MSG_LIMIT);
                                user_buffer_len[j] = 0;  
                            }

#ifdef _DEBUG
                        std::cout << "\n!!! 2.5 !!!\n";
                        std::cout << ">> b_len: " << user_buffer_len[i] << " buff_i: \"" << user_buffer[i] << "\"" << std::endl;
#endif

                        memcpy (user_buffer[i], recv_buffer + (recv_len - shift), shift);
                        user_buffer_len[i] = shift;
                        recv_len = 0U;
                        memset (recv_buffer, 0, MSG_LIMIT);
                    }
                    
                    else
                    {
                        recv_len = 0U;
                        memset (recv_buffer, 0, MSG_LIMIT);  
                    }
#ifdef _DEBUG
                    std::cout << "\n!!! 3 !!!\n";
                    std::cout << ">> b_len: " << user_buffer_len[i] << " buff_i: \"" << user_buffer[i] << "\"" << std::endl;
                    std::cout << ">> rcv_len: " << recv_len << " recv: \"" << recv_buffer << "\"" << std::endl;
#endif
                } while (1);
            } // end else
        }  // end for Event [i]
    } // end while true
    
    shutdown (MasterSocket, SHUT_RDWR);
    close    (MasterSocket);
    free     (Events);
    
    return 0;
}
