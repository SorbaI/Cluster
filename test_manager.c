#include "manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define NUM_TASKS 8
#define LEFT -10000
#define RIGHT 20
#define PRECISION 0.000000001

struct task_integral {
    double left;
    double step;
    uint64_t num_steps;
};

static double get_step(double right, double precision) {
    double max_derivative_2 = exp(right);
    if (max_derivative_2 == 0) {
        return 1;
    }
    return cbrt(24 * precision / max_derivative_2);
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <address> <port> <max_time> <num_nodes>\n", argv[0]);
        return 1;
    }
    INFO_MANAGER info_manager;

    char *endptr = argv[3];
    time_t max_time = strtol(argv[3], &endptr, 10);
    if (*argv[3] == '\0' || *endptr != '\0')
    {
        fprintf(stderr, "Unable to parse time!\n");
        return 1;
    }

    endptr = argv[4];
    long num_workers = strtol(argv[4], &endptr, 10);
    if (*argv[4] == '\0' || *endptr != '\0')
    {
        fprintf(stderr, "Unable to parse number of workers!\n");
        return 1;
    }
    info_manager_init(&info_manager, argv[1], argv[2], max_time, num_workers);
    double step = get_step(RIGHT, PRECISION);
    uint64_t num_count = (uint64_t)(ceil(fabs((double)RIGHT - LEFT) / step)) + 2;
    // Избавляемся от неполных шагов
    step = ((double)(RIGHT - LEFT)) / num_count;
    struct task_integral *tasks = calloc(NUM_TASKS,sizeof(*tasks));
    size_t *task_sizes = calloc(NUM_TASKS,sizeof(*task_sizes));
    if(tasks == NULL || task_sizes == NULL) {
        free(tasks);
        free(task_sizes);
        printf("NO MEMORY!\n");
        return 1;
    }
    uint64_t num_count_i = num_count / NUM_TASKS;
    for (int i = 0; i < NUM_TASKS; ++i) {
        tasks[i].left = LEFT + step * num_count_i * i;
        tasks[i].num_steps = num_count_i;
        task_sizes[i] = sizeof(*tasks);
        tasks[i].step = step; 
    }

    tasks[NUM_TASKS - 1].num_steps += num_count % NUM_TASKS;
    char *tasks_prepare = create_task_structure(NUM_TASKS,task_sizes,(char *)tasks);
    free(tasks);
    free(task_sizes);
    if(tasks_prepare == NULL) {
        printf("Error in task_prepare!\n");
        return 1;
    }

    double *ans_manager = calloc (NUM_TASKS,sizeof(*ans_manager));
    if (start_manager(&info_manager,NUM_TASKS,tasks_prepare,(char*)ans_manager) < 0) {
        free(tasks_prepare);
        free(ans_manager);
        printf("Error in start manager!\n");
        return 1;
    }
    double ans = 0;
    for (int i = 1; i < NUM_TASKS; ++i) {
        ans += ans_manager[i];
    }
    free(tasks_prepare);
    free(ans_manager);
    printf("ANSWER: %lf!\n",ans);
}