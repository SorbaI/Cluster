#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <endian.h>
#include <arpa/inet.h>
#include <time.h>
#include <math.h>
#include "manager.h"
#include <netdb.h>

typedef enum
{
    CONNECTION_EMPTY,
    GET_INFO,   // -> WAIT_TASK
    WAIT_TASK, //-> WAIT_ANS, WORK_FINISHED 
    WAIT_ANS, // -> WAIT_TASK
    WORK_FINISHED
} WORK_STATE;

typedef struct
{
    // Дескриптор сокета для обмена данными с клиентом.
    int client_sock_fd;
    // Количество ядер на рабочем узле.
    size_t n_cores;
    // Текущее состояние протокола обмена данными с данным клиентом.
    WORK_STATE state;
    // Количество задач отправленное в последний раз
    size_t num_last_tasks_send;
} WORK_CONNECTION;


char* create_task_structure(size_t num_tasks, size_t *task_sizes, char *tasks){
    size_t size_of_structure = (num_tasks) * sizeof(*task_sizes);
    for(size_t i = 0; i < num_tasks; ++i) {
        size_of_structure += task_sizes[i];
    }
    char *ans = calloc(size_of_structure, 1);
    char *ret = ans;
    if (ans == NULL) {
        return NULL;
    }
    for(size_t i = 0; i < num_tasks; ++i) {
        *((size_t*) ans) = task_sizes[i];
        ans += sizeof(size_t);
        if (ans != memcpy(ans,tasks,task_sizes[i])) {
            fprintf(stderr,"[create_task_structure] memcpy fail!\n");
            return NULL;
        }
        ans += task_sizes[i];
        tasks += task_sizes[i];
    }
    return ret;
}

void info_manager_init(INFO_MANAGER *manager, const char *addr, const char *port, time_t seconds, int num_nodes) {
    struct addrinfo hints, *res;
    int status;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if ((status = getaddrinfo(addr, port, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }

    manager->listen_addr = *res->ai_addr;
    manager->max_time = seconds;
    manager->num_nodes = num_nodes;
    manager->is_init = true;
    freeaddrinfo(res);
}

static bool manager_init_socket(INFO_MANAGER* manager)
{
    if (manager->is_init == false) {
        fprintf(stderr, "[manager_init] Not init Info Manager!\n");
        return false;
    }
    // Создаём сокет, слушающий подключения клиентов.
    manager->listen_sock_fd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);
    if (manager->listen_sock_fd == -1)
    {
        fprintf(stderr, "[manager_init] Unable to create socket!\n");
        return false;
    }

    // Запрещаем перевод слушающего сокета в состояние TIME_WAIT.
    int setsockopt_yes = 1;
    if (setsockopt(manager->listen_sock_fd, SOL_SOCKET, SO_REUSEADDR, &setsockopt_yes, sizeof(setsockopt_yes)) == -1)
    {
        fprintf(stderr, "[manager_init] Unable to set SO_REUSEADDR socket option\n");
        return false;
    }

    if (bind(manager->listen_sock_fd, (struct sockaddr*) &(manager->listen_addr), sizeof(manager->listen_addr)) == -1)
    {
        fprintf(stderr, "[manager_init] Unable to bind\n");
        return false;
    }

    // Активируем очередь запросов на подключение.
    if (listen(manager->listen_sock_fd, manager->num_nodes /* Размер очереди запросов на подключение */) == -1)
    {
        fprintf(stderr, "[manager_init] Unable to listen() on a socket\n");
        return false;
    }
    return true;
}

static bool manager_close_listen_socket(INFO_MANAGER* manager) {

    if (close(manager->listen_sock_fd) == -1)
    {
        fprintf(stderr, "[manager_close_listen_socket] Unable to close() listen-socket\n");
        return false;
    }
    return true;
}

static bool manager_accept_connection_request(INFO_MANAGER* manager, WORK_CONNECTION* conn)
{
    DEBUG("Wait for worker_node to connect\n");

    // Создаём сокет для клиента из очереди на подключение.
    conn->client_sock_fd = accept(manager->listen_sock_fd, NULL, NULL);
    if (conn->client_sock_fd == -1)
    {
        fprintf(stderr, "[manager_accept_connection_request] Unable to accept() connection on a socket\n");
        return false;
    }

    // Disable Nagle's algorithm:
    int setsockopt_arg = 1;
    if (setsockopt(conn->client_sock_fd, IPPROTO_TCP, TCP_NODELAY, &setsockopt_arg, sizeof(setsockopt_arg)) == -1)
    {
        fprintf(stderr, "[manager_accept_connection_request] Unable to enable TCP_NODELAY socket option");
        return false;
    }

    // Disable corking:
    setsockopt_arg = 0;
    if (setsockopt(conn->client_sock_fd, IPPROTO_TCP, TCP_CORK, &setsockopt_arg, sizeof(setsockopt_arg)) == -1)
    {
        fprintf(stderr, "[manager_accept_connection_request] Unable to disable TCP_CORK socket option");
        return false;
    }

    DEBUG("Worker connected\n");
    conn->state = GET_INFO;
    return true;
}

static bool manager_get_worker_info(WORK_CONNECTION *work)
{
    size_t bytes_read = recv(work->client_sock_fd, &(work->n_cores), sizeof(work->n_cores), MSG_WAITALL);
    if (bytes_read != sizeof(work->n_cores))
    {
        fprintf(stderr, "Unable to recv n_cores info from worker\n");
        return false;
    }
    work->state = WAIT_TASK;
    DEBUG("Connect worker with cores : %lu\n",work->n_cores);
    return true;
}

static size_t manager_send_tasks(WORK_CONNECTION *work, size_t num_tasks, char *data) {

    work->num_last_tasks_send = num_tasks;
    size_t size_data = 0;
    char *data_ptr = data;
    for (size_t i = 0; i < num_tasks; ++i) {
        size_data += *((size_t *)data_ptr) + sizeof(size_data);
        data_ptr += sizeof(size_data) + (size_t)*data_ptr;
    }
    DEBUG("manager send num_tasks: %lu\n",num_tasks);
    size_t bytes_written = write(work->client_sock_fd,&num_tasks,sizeof(num_tasks));
    if (bytes_written != sizeof(num_tasks))
    {
        fprintf(stderr, "Unable to send num_tasks to client,\
            Send %lu --- need %lu\n", bytes_written, sizeof(num_tasks));
        return 0;
    }
    DEBUG("manager send size_data: %lu\n", size_data);
    bytes_written = write(work->client_sock_fd, &size_data, sizeof(size_data));
    if (bytes_written != sizeof(size_data))
    {
        fprintf(stderr, "Unable to send size_data to client,\
            Send %lu --- need %lu\n", bytes_written, size_data);
        return 0;
    }
    bytes_written = write(work->client_sock_fd, data, size_data);
    if (bytes_written != size_data)
    {
        fprintf(stderr, "Unable to send tasks to client,\
            Send %lu --- need %lu\n", bytes_written, size_data);
        return 0;
    }
    DEBUG("Send data with size: %lu\n",size_data);
    work->state = WAIT_ANS;
    return size_data;
}

static bool manager_get_worker_ans(WORK_CONNECTION *work, char **ans) {
    for(size_t num_ans = 0; num_ans < work->num_last_tasks_send; ++num_ans){
        size_t ans_size = 0;
        size_t bytes_read = recv(work->client_sock_fd, &ans_size, sizeof(ans_size), MSG_WAITALL);
        if (bytes_read != sizeof(ans_size))
        {
            fprintf(stderr, "can't get size: get %lu bytes from worker, expected %ld\n",bytes_read, sizeof(ans_size));
            return false;
        }
        bytes_read = recv(work->client_sock_fd, *ans, ans_size, MSG_WAITALL);
        if (bytes_read != ans_size)
        {
            fprintf(stderr, "Get %lu bytes from worker, expected %lu\n",bytes_read, ans_size);
            return false;
        }
        DEBUG("Get Ans from worker - size: %lu\n",ans_size);
        *ans = *ans + bytes_read;
    }
    return true;
}

static bool manager_close_worker_socket(WORK_CONNECTION *work) {
    size_t end_tasks = 0;
    write(work->client_sock_fd,&end_tasks,sizeof(end_tasks));
    if (close(work->client_sock_fd) == -1)
    {
        fprintf(stderr, "[manager_close_worker_socket] Unable to close() worker-socket\n");
        return false;
    }
    work->client_sock_fd = -1;
    return true;
}