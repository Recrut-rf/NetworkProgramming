#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include <set>
#include <algorithm>

#define Poll_SIZE 2048  // Максимальное количество отслеживаемых дескрипторов

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

    // Множество для хранения дескрипторов подключённых клиентов
    std::set<int> SlaveSockets;

    // Настройка адреса сервера
    struct sockaddr_in SockAddr;
    SockAddr.sin_family = AF_INET;          // IPv4
    SockAddr.sin_port = htons(12345);       // Порт 12345
    SockAddr.sin_addr.s_addr = htonl(INADDR_ANY); // Принимать соединения на всех интерфейсах

    bind(MasterSocket, (struct sockaddr*)&SockAddr, sizeof(SockAddr));
    set_nonblock(MasterSocket);  // Неблокирующий режим для главного сокета
    listen(MasterSocket, SOMAXCONN);  // Очередь подключений

    // Структура для poll()
    pollfd Set[Poll_SIZE];
    Set[0].fd = MasterSocket;    // Первый элемент - главный сокет
    Set[0].events = POLLIN;      // Нас интересуют события ввода (новые подключения)

    while(true)
    {
        // Заполняем массив pollfd клиентскими сокетами
        unsigned int Index = 1;
        for(auto Iter = SlaveSockets.begin(); Iter != SlaveSockets.end(); ++Iter)
        {
            Set[Index].fd = *Iter;
            Set[Index].events = POLLIN;  // Отслеживаем возможность чтения
            Index++;
        }

        // Размер массива для poll(): MasterSocket + клиентские сокеты
        unsigned int SetSize = 1 + SlaveSockets.size();

        // Ожидаем события (таймаут -1 означает бесконечное ожидание)
        poll(Set, SetSize, -1);

        // Обработка произошедших событий
        for(unsigned int i = 0; i < SetSize; ++i)
        {
            if(Set[i].revents & POLLIN)
            {  // Проверяем событие чтения
                if(i)
                {  // Если это клиентский сокет
                    static char Buffer[1024];
                    int RecvSize = recv(Set[i].fd, Buffer, 1024, MSG_NOSIGNAL);

                    // Обработка закрытия соединения
                    if((RecvSize == 0) && (errno != EAGAIN))
                    {
                        shutdown(Set[i].fd, SHUT_RDWR);
                        close(Set[i].fd);
                        SlaveSockets.erase(Set[i].fd);
                    }
                    // Эхо-ответ при получении данных
                    else if(RecvSize > 0)
                    {
                        send(Set[i].fd, Buffer, RecvSize, MSG_NOSIGNAL);
                    }
                }
                else
                {  // Если это главный сокет (новое подключение)
                    int SlaveSocket = accept(MasterSocket, 0, 0);
                    set_nonblock(SlaveSocket);  // Неблокирующий режим
                    SlaveSockets.insert(SlaveSocket);  // Добавляем в множество
                }
            }
        }
    }
    return 0;
}
