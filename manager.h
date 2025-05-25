#include <stdbool.h>
#include <time.h>
#include <arpa/inet.h>

#ifdef DEBUGTEST
#define DEBUG(...) printf(__VA_ARGS__);
#else
#define DEBUG(...)
#endif

typedef struct
{
    //Адрес для прослушивания запросов на подключение.
    struct sockaddr listen_addr;
    // Максимальное время работы в секундах
    time_t max_time;
    // Количество рабочих узлов, необходимых для запуска вычисления
    size_t num_nodes;

    // Дескриптор слушающего сокета для первоначального подключения клиентов.
    int listen_sock_fd;
    bool is_init;
} INFO_MANAGER;

char* create_task_structure(size_t num_tasks, size_t *task_sizes, char *tasks);

void info_manager_init(INFO_MANAGER *manager, const char *addr, const char *port, time_t seconds, int num_nodes);

int start_manager(INFO_MANAGER *manager, size_t num_tasks, char *tasks, char *ans);