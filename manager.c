#include "manager-common.h"
#include <memory.h>
#include <poll.h>
#include <math.h>
#include <sched.h>
#include <pthread.h>


static void poll_server_wait_for_worker(struct pollfd* pollfds, INFO_MANAGER* server)
{
    struct pollfd* pollfd = &pollfds[0U];

    pollfd->fd      = server->listen_sock_fd;
    pollfd->events  = POLLIN;
    pollfd->revents = 0U;
}

static void poll_server_do_not_wait_for_workers(struct pollfd* pollfds)
{
    struct pollfd* pollfd = &pollfds[0U];

    pollfd->fd      = -1;
    pollfd->events  = 0U;
    pollfd->revents = 0U;
}

static void poll_manager_wait_for_answer(struct pollfd* pollfds, size_t conn_i, WORK_CONNECTION *work) {
    struct pollfd* pollfd = &pollfds[1 + conn_i];

    pollfd->fd      = work->client_sock_fd;
    pollfd->events  = POLLIN | POLLHUP;
    pollfd->revents = 0U;
}

static void poll_manager_do_not_wait_for_ans(struct pollfd* pollfds, size_t conn_i){
    struct pollfd* pollfd = &pollfds[conn_i + 1];

    pollfd->fd      = -1;
    pollfd->events  = 0U;
    pollfd->revents = 0U;
}

static void poll_manager_wait_work_info(struct pollfd* pollfds, size_t conn_i, WORK_CONNECTION *work) {
    struct pollfd* pollfd = &pollfds[1 + conn_i];

    pollfd->fd      = work->client_sock_fd;
    pollfd->events  = POLLIN|POLLHUP;
    pollfd->revents = 0U;
}

static bool wait_and_get_info_workers(INFO_MANAGER* manager, WORK_CONNECTION *works, struct pollfd *pollfds) {
    size_t num_connected_workers = 0U;
    size_t num_init_workers = 0U;
    while (num_init_workers != manager->num_nodes)
    {
        if (num_connected_workers != manager->num_nodes) {
            poll_server_wait_for_worker(pollfds, manager);
        } else {
            poll_server_do_not_wait_for_workers(pollfds);
        }
        int pollret = poll(pollfds, 1U + num_connected_workers, -1);
        if (pollret == -1)
        {
            fprintf(stderr, "Unable to poll-wait for data on descriptors!\n");
            goto error;
        }

        if (pollfds[0U].revents & POLLIN)
        {
            if(!manager_accept_connection_request(manager, &works[num_connected_workers])){
                goto error;
            }

            poll_manager_wait_work_info(pollfds, num_connected_workers, &works[num_connected_workers]);
            num_connected_workers++;
        } 

        for (size_t conn_i = 0U; conn_i < num_connected_workers; ++conn_i)
        {
            if (pollfds[1U + conn_i].revents & POLLHUP)
            {  
                fprintf(stderr, "Unexpected POLLHUP\n");
                goto error;
            }

            if (pollfds[1U + conn_i].revents & POLLIN)
            {
                switch (works[conn_i].state)
                {
                case CONNECTION_EMPTY:
                case WORK_FINISHED:
                case WAIT_ANS:
                    fprintf(stderr, "Unexpected state!\n");
                    goto error;
                case GET_INFO:
                    if(!manager_get_worker_info(&works[conn_i])) {
                        goto error;
                    }
                    num_init_workers++;
                    break;
                case WAIT_TASK:
                }
            }
        }
    }
    return true;
error:
    for(size_t i = 0; i < num_connected_workers;++i) {
        manager_close_worker_socket(&works[i]);
    }
    return false;
}

int start_manager(INFO_MANAGER *manager, size_t num_tasks, char tasks[], char *ans) {
    if (manager == NULL || tasks == NULL || ans == NULL ||
        manager->max_time == 0 || manager->is_init == false || manager->num_nodes == 0) {
        return -EINVAL;
    }

    WORK_CONNECTION* works = calloc(manager->num_nodes, sizeof(WORK_CONNECTION));
    struct pollfd* pollfds = calloc(manager->num_nodes + 1U, sizeof(struct pollfd));

    if (works == NULL || pollfds == NULL)
    {
        goto error_clear;
    }

    for (size_t conn_i = 0U; conn_i < manager->num_nodes; conn_i++)
    {
        works[conn_i].state = CONNECTION_EMPTY;
    }

    if (!manager_init_socket(manager)) {
        goto error_clear;
    }
    if(!wait_and_get_info_workers(manager, works, pollfds)) {
        manager_close_listen_socket(manager);
        goto error_clear;
    }
    DEBUG("All workers connected\n");

    if (!manager_close_listen_socket(manager)) {
        goto error_close;
    }

    time_t start_time = time(NULL);
    size_t num_tasks_send = 0;
    size_t num_ans_get = 0;
    size_t last_num_tasks_send = 0;
    char *ptr_tasks = tasks;

    for (size_t conn_i = 0; conn_i < manager->num_nodes; ++conn_i) {
        size_t num_tasks_left = num_tasks - num_tasks_send;
        size_t byte_send = 0;
        if (!num_tasks_left) {
            break;
        }
        last_num_tasks_send = works[conn_i].n_cores > num_tasks_left ? 
                                num_tasks_left : works[conn_i].n_cores;

        if(!(byte_send = manager_send_tasks(&works[conn_i], last_num_tasks_send, ptr_tasks))) {
            goto error_close;
        }
        poll_manager_wait_for_answer(pollfds,conn_i,&works[conn_i]);
        num_tasks_send += last_num_tasks_send;
        ptr_tasks += byte_send;
    }
    while(num_ans_get != num_tasks) {
        time_t max_wait_time = time(NULL) - start_time + manager->max_time;
        int pollret = poll(pollfds, 1U + manager->num_nodes, max_wait_time);
        if (pollret == -1)
        {
            fprintf(stderr, "Unable to poll-wait for data on descriptors!\n");
            goto error_close;
        }
        for (size_t conn_i = 0U; conn_i < manager->num_nodes; ++conn_i)
        {
            if (pollfds[1U + conn_i].revents & POLLHUP)
            {  
                fprintf(stderr, "Unexpected POLLHUP\n");
                goto error_close;
            }

            if (pollfds[1U + conn_i].revents & POLLIN)
            {
                switch (works[conn_i].state)
                {
                case CONNECTION_EMPTY:
                case GET_INFO:
                    fprintf(stderr, "Unexpected state!\n");
                    goto error_close;
                case WAIT_ANS:
                    if(!manager_get_worker_ans(&works[conn_i],&ans)) {
                        goto error_close;
                    }
                    num_ans_get += works->num_last_tasks_send;
                    size_t num_tasks_left = num_tasks - num_tasks_send;
                    size_t byte_send = 0;
                    if (!num_tasks_left) {
                        manager_close_worker_socket(&works[conn_i]);
                        works[conn_i].state = WORK_FINISHED;
                        poll_manager_do_not_wait_for_ans(pollfds,conn_i);
                        break;
                    }
                    last_num_tasks_send = works[conn_i].n_cores > num_tasks_left ? 
                                            num_tasks_left : works[conn_i].n_cores;
                    if(!(byte_send = manager_send_tasks(&works[conn_i], last_num_tasks_send, ptr_tasks))) {
                        goto error_close;
                    }
                    poll_manager_wait_for_answer(pollfds,conn_i,&works[conn_i]);
                    num_tasks_send += last_num_tasks_send;
                    ptr_tasks += byte_send;
                    break;
                case WAIT_TASK:
                case WORK_FINISHED:
                }
            }
        }
    }
    free(pollfds);
    free(works);
    return 0;
error_close:
    for(size_t i = 0; i < manager->num_nodes; ++i) {
        manager_close_worker_socket(&works[i]);
    }
    DEBUG("Fall in error_close!\n");
error_clear:
    free(pollfds);
    free(works);
    DEBUG("Fall in error_clear!\n");
    return -1;    
}
