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

#include "client_queue.h"

struct Client* client_list = NULL;
pthread_mutex_t client_list_mutex = PTHREAD_MUTEX_INITIALIZER;

int add_client(char* name, int socket_fd) {
    pthread_mutex_lock(&client_list_mutex);
    for (struct Client* tmp_client = client_list; tmp_client != NULL; tmp_client = tmp_client->next)
        if (!strcmp(tmp_client->name, name)) {
            pthread_mutex_unlock(&client_list_mutex);
            return -1;
        }
    struct Client* new_client = malloc(sizeof(struct Client));
    new_client->name = malloc(strlen(name) + 1);                                                    // +1 for null byte
    strcpy(new_client->name, name);
    new_client->current_task = NULL;
    new_client->is_alive = 1;
    new_client->socket_fd = socket_fd;
    new_client->next = client_list;
    client_list = new_client;
    pthread_mutex_unlock(&client_list_mutex);
    return 0;
}

void delete_client(char* name) {
    pthread_mutex_lock(&client_list_mutex);
    struct Client* client = client_list;
    struct Client* tmp;
    if (client->next == NULL && !strcmp(client->name, name)) {
        tmp = client;
        client_list = client->next;
        close(tmp->socket_fd);
        free(tmp->name);
        free(tmp->current_task);
        free(tmp);
    }
    else {
        while (client->next != NULL && strcmp(client->next->name, name))
            client = client->next;
        if (client->next != NULL) {
            tmp = client->next;
            client->next = client->next->next;
            close(tmp->socket_fd);
            free(tmp->name);
            free(tmp->current_task);
            free(tmp);
        }
    }
    pthread_mutex_unlock(&client_list_mutex);
}

void mark_as_living(int socket_fd) {
    struct Client* client = client_list;
    while (client != NULL && client->socket_fd != socket_fd)
        client = client->next;
    if (client != NULL)
        client->is_alive = 1;
}