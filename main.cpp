#include <iostream>
#include <set>
#include <algorithm>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>


#define MAX_EVENTS 32  // максимальное количество событий за раз

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
            }
            else
            {
                // Обработка данных от клиента
                static char Buffer[1024];
                // Читаем данные (без генерации SIGPIPE при разрыве)
                int RecvResult = recv(Events[i].data.fd, Buffer, 1024, MSG_NOSIGNAL);

                // Если соединение закрыто или ошибка (кроме EAGAIN)
                if((RecvResult == 0) && (errno != EAGAIN))
                {
                    // Закрываем соединение корректно
                    shutdown(Events[i].data.fd, SHUT_RDWR);
                    close(Events[i].data.fd);
                }
                else if(RecvResult > 0)
                {
                    // Отправляем обратно полученные данные (эхо-сервер)
                    send(Events[i].data.fd, Buffer, RecvResult, MSG_NOSIGNAL);
                }
            }
        }
    }
    return 0;
}
