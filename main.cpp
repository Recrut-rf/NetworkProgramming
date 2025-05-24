#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>

#include <set>
#include <algorithm>

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
    // Создаём главный сокет (MasterSocket) для прослушивания подключений
    // AF_INET - IPv4, SOCK_STREAM - потоковый сокет (TCP), IPPROTO_TCP - протокол TCP
    int MasterSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    // Множество (set) для хранения дескрипторов подключённых клиентов
    std::set<int> SlaveSockets;

    // Настраиваем адрес для привязки сокета
    struct sockaddr_in SockAddr;
    SockAddr.sin_family = AF_INET;          // Семейство адресов - IPv4
    SockAddr.sin_port = htons(12345);       // Порт 12345 (преобразуем в сетевой порядок)
    SockAddr.sin_addr.s_addr = htonl(INADDR_ANY); // Принимать соединения на все интерфейсы

    // Привязываем сокет к адресу
    bind(MasterSocket, (struct sockaddr*)&SockAddr, sizeof(SockAddr));

    // Устанавливаем неблокирующий режим для главного сокета
    set_nonblock(MasterSocket);

    // Переводим сокет в режим прослушивания с максимальным количеством ожидающих соединений
    listen(MasterSocket, SOMAXCONN);

    // Основной цикл обработки событий
    while(true)
    {
        // Создаём набор файловых дескрипторов для select()
        fd_set Set;
        FD_ZERO(&Set); // Инициализируем набор

        // Добавляем главный сокет в набор
        FD_SET(MasterSocket, &Set);

        // Добавляем все сокеты клиентов в набор
        for(auto Iter = SlaveSockets.begin(); Iter != SlaveSockets.end(); ++Iter)
        {
            FD_SET(*Iter, &Set);
        }

        // Находим максимальный номер дескриптора для select()
        int Max = std::max(MasterSocket, *std::max_element(SlaveSockets.begin(), SlaveSockets.end()));

        // Ожидаем активности на любом из сокетов (бесконечно, пока что-то не произойдёт)
        select(Max + 1, &Set, NULL, NULL, NULL);

        // Проверяем все клиентские сокеты на активность
        for(auto Iter = SlaveSockets.begin(); Iter != SlaveSockets.end(); ++Iter)
        {
            if(FD_ISSET(*Iter, &Set)) // Если сокет готов к чтению
            {
                static char Buffer[1024];
                // Читаем данные из сокета (до 1024 байт)
                int RecvSize = recv(*Iter, Buffer, 1024, MSG_NOSIGNAL);

                // Если получен 0 байт (клиент закрыл соединение) и это не ошибка EAGAIN
                if((RecvSize == 0) && (errno != EAGAIN))
                {
                    // Закрываем соединение
                    shutdown(*Iter, SHUT_RDWR);
                    close(*Iter);
                    // Удаляем сокет из множества
                    SlaveSockets.erase(Iter);
                }
                else if(RecvSize != 0) // Если получили данные
                {
                    // Отправляем их обратно клиенту (эхо-сервер)
                    send(*Iter, Buffer, RecvSize, MSG_NOSIGNAL);
                }
            }
        }

        // Проверяем главный сокет на новое подключение
        if(FD_ISSET(MasterSocket, &Set))
        {
            // Принимаем новое подключение
            int SlaveSocket = accept(MasterSocket, 0, 0);
            // Устанавливаем неблокирующий режим для нового сокета
            set_nonblock(SlaveSocket);
            // Добавляем сокет в множество клиентов
            SlaveSockets.insert(SlaveSocket);
        }
    }

    return 0;
}
