#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>

#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sched.h>
#include "worker.h"

//==================
// Управление сетью
//==================
static bool worker_connect_to_manager(INFO_WORKER* worker)
{
    if (connect(worker->server_conn_fd, &worker->server_addr, sizeof(worker->server_addr)) == -1)
    {
        return false;
    }
    return true;
}

static bool worker_close_socket(INFO_WORKER* worker)
{
    if (close(worker->server_conn_fd) == -1)
    {
        fprintf(stderr, "[worker_close_socket] Unable to close() worker socket\n");
        return false;
    }
    return true;
}


//=================================
// Передача данных по сети.
//=================================

static size_t get_tasks(INFO_WORKER* worker, char **tasks_ans)
{
    size_t num_of_tasks = 0;
    size_t bytes_read = recv(worker->server_conn_fd, &num_of_tasks, sizeof(num_of_tasks), MSG_WAITALL);
    if (bytes_read != sizeof(num_of_tasks))
    {
        fprintf(stderr, "Unable to recv num_of_tasks from server\n");
        return 0;
    }
    // Сервер отключился
    if (num_of_tasks == 0) {
        return 0;
    }
    DEBUG("Worker get num_of_tasks : %lu\n",num_of_tasks);
    size_t tasks_size = 0;
    bytes_read = recv(worker->server_conn_fd, &tasks_size, sizeof(tasks_size), MSG_WAITALL);
    if (bytes_read != sizeof(tasks_size))
    {
        fprintf(stderr, "Unable to recv tasks_size from server\n");
        return 0;
    }
    DEBUG("Worker get tasks_size : %lu\n", tasks_size);
    char *tasks = calloc(tasks_size,sizeof(*tasks));
    if(tasks == NULL) {
        fprintf(stderr,"[get_tasks]: No memory for task!\n");
        return 0;
    }
    bytes_read = recv(worker->server_conn_fd, tasks, tasks_size, MSG_WAITALL);
    if (bytes_read != tasks_size)
    {
        fprintf(stderr, "Unable to recv tasks from server get:%lu want: %lu\n",
                bytes_read, tasks_size);
        return 0;
    }
    *tasks_ans = tasks;
    return num_of_tasks;
}

static bool send_result(INFO_WORKER *worker, size_t ans_size, char *ans)
{
    size_t bytes_written = write(worker->server_conn_fd, ans, ans_size);
    if (bytes_written != ans_size)
    {
        fprintf(stderr, "Unable to send result to server\n");
        return false;
    }
    DEBUG("Worker send %lu bytes!\n",ans_size);
    return true;
}

static bool send_node_info(INFO_WORKER *worker)
{
    DEBUG("Worker start send node_info!\n");
    size_t bytes_written = write(worker->server_conn_fd, &worker->n_cores, sizeof(worker->n_cores));
    if (bytes_written != sizeof(worker->n_cores))
    {
        fprintf(stderr, "Unable to send node info to server\n");
        return false;
    }
    DEBUG("Worker end send node_info!\n");
    return true;
}


//============================
// Распределение задач
//============================
struct thread_args
{
    int func_id;
    long long parts;
    double left;
    double step;
    double retval;
};


static size_t distributed_counting(INFO_WORKER *worker, char *tasks, size_t num_of_tasks,
    void *(func(void *)),char **ans)
{
    // Проверка валидности запрашиваемого числа ядер
    if (worker->n_cores > (size_t)get_nprocs() || num_of_tasks > (size_t)worker->n_cores) {
        fprintf(stderr, "[distributed_counting] the number of processors currently \
                available in the system is less than required\n");
    }

    int threads_num = num_of_tasks;
    pthread_t threads[threads_num];

    for (int i = 0; i < threads_num; ++i) {
        // Выбор ядра для выполнения потока.
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(i, &cpuset);

        pthread_attr_t thread_attr;
        if(pthread_attr_init(&thread_attr)) {
            fprintf(stderr, "pthread_attr_init returns with error\n");
            return 0;
        }

        // Устанавливаем аффинность потока.
        if (pthread_attr_setaffinity_np(&thread_attr, sizeof(cpuset), &cpuset)) {
            fprintf(stderr, "pthread_attr_setaffinity_np returns with error\n");
            return 0;
        }


        if (pthread_create(&threads[i], &thread_attr, func, tasks)) {
            fprintf(stderr, "Unable to create thread\n");
            return 0;
        }
        tasks += *((size_t*)tasks) + sizeof(size_t);

        // Удаляем объект аттрибутов потока.
        if (pthread_attr_destroy(&thread_attr)) {
            fprintf(stderr, "Unable to destroy a thread attributes object\n");
            return 0;
        }

    }
    
    char *ans_total = calloc(INIT_ANS_SIZE,sizeof(*ans_total));
    size_t ans_size = INIT_ANS_SIZE;
    size_t current_ans_size = 0;
    for (int i = 0; i < threads_num; ++i)
    {
        char *ans_task;
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        time_t start_time = ts.tv_sec;
        ts.tv_sec += worker->max_time;
        int wait = pthread_timedjoin_np(threads[i], (void**) &ans_task,&ts);
        if (wait != 0) {
            fprintf(stderr, "Unable to join a thread\n");
            if(wait == ETIMEDOUT) {
                fprintf(stderr, "ETIMEDOUT add 10 sec!!!\n");
                worker->max_time += 10;
                --i;
                continue;
            }
            return 0;
        }
        worker->max_time -= time(NULL) - start_time;
        if(ans_task == NULL) {
           continue; 
        }
        size_t ans_size_i = *((size_t*) ans_task);
        size_t new_curr_sz = ans_size_i + sizeof(ans_size_i) + current_ans_size;
        if (new_curr_sz > ans_size) {
            ans_size = new_curr_sz > ans_size * 2 ? new_curr_sz : ans_size * 2;
            ans_total = realloc(ans_total,ans_size);
            if(ans_total == NULL) {
                fprintf(stderr, "No memory for ans!\n");
                return 0;
            }
        }
        memcpy(ans_total + current_ans_size, ans_task, ans_size_i + sizeof(ans_size_i));
        current_ans_size += ans_size_i + sizeof(ans_size_i);
        free(ans_task);
    }
    DEBUG("Ans from worker size: %lu\n",current_ans_size);
    *ans = ans_total;
    return current_ans_size;
}

//============================
// Интерфейс исполнителя
//============================

int init_worker(INFO_WORKER *worker,size_t n_cores, time_t max_time, const char *addr, const char *port,void*(func(void*)))
{
    worker->n_cores = n_cores;
    worker->max_time = max_time;

    worker->server_conn_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (worker->server_conn_fd == -1)
    {
        fprintf(stderr, "[init_worker] Unable to create socket()\n");
        return -1;
    }

    // Формируем желаемый адрес для подключения.
    struct addrinfo hints;

    struct addrinfo* res;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = 0;
    hints.ai_protocol = 0;

    if (getaddrinfo(addr, port, &hints, &res) != 0)
    {
        fprintf(stderr, "[init_worker] Unable to call getaddrinfo()\n");
        return -1;
    }

    // Проверяем, что для данного запроса существует только один адрес.
    if (res->ai_next != NULL)
    {
        fprintf(stderr, "[init_worker] Ambigous result of getaddrinfo()\n");
        return -1;
    }

    worker->server_addr = *res->ai_addr;
    worker->func = func;
    freeaddrinfo(res);
    return 0;
}

int worker_start(INFO_WORKER *worker) {
    // Подключение к серверу.
    bool connected_to_server = worker_connect_to_manager(worker);
    while (!connected_to_server)
    {
        // Ожидаем, пока сервер проснётся.
        sleep(1U);

        DEBUG("Wait for server to start\n");

        connected_to_server = worker_connect_to_manager(worker);
    }

    // Отправка данных об узле.
    bool success = send_node_info(worker);
    if (!success)
    {
        goto error_close;
    }

    char *tasks = NULL;
    char *ans = NULL;
    // Обработка поступающих задач, Время отслеживается в destributing_counting
    while (true) {
        ans = tasks = NULL;
        size_t ans_size = 0;
        size_t num_of_tasks = get_tasks(worker, &tasks);
        // Сервер закрыл соединение
        if (!num_of_tasks)
        {
            return 0;
        }

        // Вычисление результата.
        if(!(ans_size = distributed_counting(worker,tasks,num_of_tasks,worker->func,&ans))) {
            goto error_free;
        }
        // Отправка результата.
        success = send_result(worker,ans_size,ans);
        if (!success)
        {
            goto error_free;
        }
        free(ans);
        free(tasks);
    }
    //if we here free was!
    goto error_close;
error_free: 
    free(ans);
    free(tasks);
error_close:
    worker_close_socket(worker);
    return -1;
}

void worker_close(INFO_WORKER *worker)
{
    // Освобождение сокета.
    if (worker->server_conn_fd >= 0)
        worker_close_socket(worker);
}
