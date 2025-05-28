#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include "worker.h"

struct work_info {
    double left;
    double step;
    long long num_steps;
};

void * calculate_integral(void *buf) {
    struct work_info *task;
    size_t size_task = parse_task(buf, (void**)&task);
    if (sizeof(*task) != size_task) {
        fprintf(stderr, "Unexpected task_size!\n");
        return NULL;
    }
    double res = 0;
    double left = task->left;
    for (long long i = 0; i < task->num_steps; ++i) {
        res += left + task->step;
        left += task->step;
    }
    return format_ans(sizeof(res),(void *)&res);
}

int main(int argc, char** argv)
{
    time_t max_time = 12;
    if (argc != 4)
    {
        fprintf(stderr, "Usage: worker <node> <service> <cores>\n");
        exit(EXIT_FAILURE);
    }

    char *endptr = argv[3];
    int n_cores = strtol(argv[3], &endptr, 10);
    if (*argv[3] == '\0' || *endptr != '\0')
    {
        fprintf(stderr, "Unable to parse cores!\n");
        return 1;
    }

    // Данные исполнителя.
    INFO_WORKER worker;
    if(init_worker(&worker, n_cores, max_time, argv[1], argv[2], calculate_integral) < 0) {
        printf("Error in init_worker!\n");
        return 1;
    }

    worker_start(&worker);

    worker_close(&worker);

    printf("Worker finish!\n");

    return EXIT_SUCCESS;
}