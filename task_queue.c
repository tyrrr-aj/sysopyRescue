#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <limits.h>
#include <endian.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

#include "task_queue.h"

struct Task* waiting_tasks = NULL;
pthread_mutex_t waiting_tasks_mutex = PTHREAD_MUTEX_INITIALIZER;

void add_task(char* text, int taskID) {

}

void pass_task(struct Task* task) {
    pthread_mutex_lock(&waiting_tasks_mutex);
    task->next = waiting_tasks;
    waiting_tasks = task;
    pthread_mutex_unlock(&waiting_tasks_mutex);
}