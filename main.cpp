#include <iostream>
#include <set>
#include <algorithm>

#include <string>
#include <sstream>
#include <iomanip>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>

#include <arpa/inet.h>
#include <string.h>
#include <errno.h>


#define MAX_EVENTS 32  // максимальное количество событий за раз
#define BUFFER_SIZE 1024

// Функция для установки неблокирующего режима работы файлового дескриптора (сокета)
int set_nonblock(int fd)
{
    int flags;
#if defined(O_NONBLOCK)
    // Получаем текущие флаги файлового дескриптора
    if(-1 == (flags = fcntl(fd, F_GETFL, 0)))
        flags = 0;
    // Устанавливаем флаг O_NONBLOCK (неблокирующий режим)
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    // Альтернативный способ для систем без O_NONBLOCK
    flags = 1;
    return ioctl(fd, FIOBIO, &flags);
#endif
}

std::string get_client_ip(int fd)
{
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    getpeername(fd, (struct sockaddr*)&addr, &addr_len);
    return inet_ntoa(addr.sin_addr);
}

void broadcast_message(int sender_fd, const std::string& message, const std::set<int>& clients)
{
    for (int client_fd : clients)
    {
        if (client_fd != sender_fd)
        { // Не отправляем сообщение отправителю
            send(client_fd, message.c_str(), message.size(), MSG_NOSIGNAL);
        }
    }
}

int main()
{
    // Создание главного сокета для прослушивания подключений
    int MasterSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    // Настройка адреса сервера
    struct sockaddr_in SockAddr;
    SockAddr.sin_family = AF_INET;          // IPv4
    SockAddr.sin_port = htons(12345);       // Порт 12345
    SockAddr.sin_addr.s_addr = htonl(INADDR_ANY); // Принимать соединения на всех интерфейсах

    // Привязываем сокет к адресу
    bind(MasterSocket, (struct sockaddr*)&SockAddr, sizeof(SockAddr));

    set_nonblock(MasterSocket);  // Неблокирующий режим для главного сокета
    listen(MasterSocket, SOMAXCONN);  // Очередь подключений

    // Создаем epoll-дескриптор
    int EPoll = epoll_create1(0);

    // Настраиваем событие для главного сокета
    struct epoll_event Event;
    Event.data.fd = MasterSocket; // Указываем файловый дескриптор
    Event.events = EPOLLIN;       // Нас интересуют события ввода (подключения)

    // Добавляем главный сокет в epoll
    epoll_ctl(EPoll, EPOLL_CTL_ADD, MasterSocket, &Event);

    std::set<int> Clients; // Множество всех подключенных клиентов

    while(true)
    {
        // Ожидаем события
        struct epoll_event Events[MAX_EVENTS];
        int N = epoll_wait(EPoll, Events, MAX_EVENTS, -1); // -1 означает бесконечное ожидание

        for (unsigned int i = 0; i < N; ++i)
        {
            // Если событие произошло на главном сокете - новое подключение
            if(Events[i].data.fd == MasterSocket)
            {
                // Принимаем новое подключение
                int SlaveSocket = accept(MasterSocket, 0, 0);
                set_nonblock(SlaveSocket); // Устанавливаем неблокирующий режим

                // Настраиваем событие для нового сокета
                struct epoll_event Event;
                Event.data.fd = SlaveSocket; // Указываем файловый дескриптор
                Event.events = EPOLLIN;      // Нас интересуют события ввода (данные)

                // Добавляем новый сокет в epoll
                epoll_ctl(EPoll, EPOLL_CTL_ADD, SlaveSocket, &Event);

                Clients.insert(SlaveSocket);

                // Получаем IP нового клиента
                std::string client_ip = get_client_ip(SlaveSocket);
                std::string join_msg = "[" + client_ip + "] has joined the chat\n";

                // Рассылаем сообщение о новом подключении всем клиентам
                broadcast_message(-1, join_msg, Clients);
            }
            else
            {
                // Обработка данных от клиента
                static char Buffer[BUFFER_SIZE];
                // Читаем данные (без генерации SIGPIPE при разрыве)
                int RecvResult = recv(Events[i].data.fd, Buffer, BUFFER_SIZE, MSG_NOSIGNAL);

                // Если соединение закрыто или ошибка (кроме EAGAIN)
                if((RecvResult == 0) || (RecvResult == -1 && errno != EAGAIN))
                {
                    // Клиент отключился
                    std::string client_ip = get_client_ip(Events[i].data.fd);
                    std::string leave_msg = "[" + client_ip + "] has left the chat\n";

                    // Закрываем соединение корректно
                    shutdown(Events[i].data.fd, SHUT_RDWR);
                    close(Events[i].data.fd);

                    // Удаляем из множества клиентов
                    Clients.erase(Events[i].data.fd);

                    // Рассылаем сообщение об отключении
                    broadcast_message(-1, leave_msg, Clients);
                }
                else if(RecvResult > 0)
                {
                    // Отправляем обратно полученные данные (эхо-сервер)
                    //send(Events[i].data.fd, Buffer, RecvResult, MSG_NOSIGNAL);

                    // Получаем IP отправителя
                    std::string client_ip = get_client_ip(Events[i].data.fd);

                    // Формируем сообщение с IP
                    std::string msg = "[" + client_ip + "]: " + std::string(Buffer, RecvResult);

                    // Рассылаем сообщение всем клиентам
                    broadcast_message(Events[i].data.fd, msg, Clients);
                }
            }
        }
    }
    return 0;
}
