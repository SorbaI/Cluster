//================
// Данные исполнителя.
//================
#include <stdbool.h>
#include <time.h>
#include <arpa/inet.h>

#define INIT_ANS_SIZE 1024


#ifdef DEBUGTEST
#define DEBUG(...) printf(__VA_ARGS__);
#else
#define DEBUG(...)
#endif


typedef struct
{
    // Дескриптор сокета для подключения к серверу.
    int server_conn_fd;

    // Адрес для подключению к серверу.
    struct sockaddr server_addr;

    // Максимальное время вычисления.
    time_t max_time;

    // Количество ядер.
    size_t n_cores;
    
    void *(*func)(void *);
} INFO_WORKER;


//================
// Интерфейс исполнителя.
//================

// Инициализация структуры исполнителя.
int init_worker(INFO_WORKER* worker,size_t n_cores, time_t max_time, const char *addr, const char *port,void*(func(void*)));

// Работа с сервером.
int worker_start(INFO_WORKER *worker);

// Закрытие открытого сокета.
void worker_close(INFO_WORKER *worker);

// Получение задачи.
size_t parse_task(char *buf, void **recv) {

    size_t recv_size = *((size_t*)buf);
    *recv = buf + sizeof(recv_size);
    return recv_size;
}

// Форматировать задачи для отправки
void * format_ans(size_t size, void *buf) {
    void *ret = calloc(size + sizeof(size), 1);
    if(ret == NULL) {
        fprintf(stderr, "[format_ans] : ENOMEM\n");
        return NULL;
    }
    *((size_t*) ret) = size;
    memcpy(ret + sizeof(size),buf,size);
    return ret;
}