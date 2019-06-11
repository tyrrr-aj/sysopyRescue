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
#include <sys/epoll.h>
#include <strings.h>

#include "protocol.h"
#include "client_queue.h"
#include "task_queue.h"

int net_socket, local_socket, epoll_fd, nextID;

int SERVER_SHUTDOWN = 0;
extern struct Client* client_list;
extern struct Task* waiting_tasks;

extern pthread_mutex_t client_list_mutex;
extern pthread_mutex_t waiting_tasks_mutex;


pthread_mutex_t net_socket_writing_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t local_socket_writing_mutex = PTHREAD_MUTEX_INITIALIZER;

void server_shutdown() {
    SERVER_SHUTDOWN = 1;
}

void accept_clients(void* arg) {
    int* socket = (int*) arg;
    int client_fd;
    struct sockaddr new_client_address;
    socklen_t addrlen = sizeof(new_client_address);
    struct Message message;
    struct Server_response response;
    bzero(&message, sizeof(message));
    while (!SERVER_SHUTDOWN) {
            client_fd = accept(*socket, &new_client_address, &addrlen);
            if (client_fd > 0) {
            read(client_fd, (void*) &message, sizeof(message));
            if (message.type == INIT && add_client(((struct Init_message*) &message)->client_name, client_fd) == 0) {
                response.response = OK;
                struct epoll_event* event = malloc(sizeof(struct epoll_event));
                event->events = EPOLLIN;
                event->data.fd = client_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, event) < 0)
                    perror("ERROR Client not aded to epoll");
                printf("Added new client: %s\n", ((struct Init_message*) (&message))->client_name);
            }
            else {
                response.response = NAME_USED;
                printf("Rejected to register new client %s - that name is already in use\n", ((struct Init_message*) (&message))->client_name);
            }
            response.type = RESPONSE;
            write(client_fd, (void*) &response, sizeof(response));
        }
    }
    pthread_exit(NULL);
}

int ping(struct Client* client) {
    printf("pinging %s\n", client->name);
    client->is_alive = 0;
    struct Ping_message message_to;
    message_to.type = PING;
    send(client->socket_fd, (void*) &message_to, sizeof(message_to), MSG_DONTWAIT);
}

void ping_clients(void) {
    struct timespec ping_timeout;
    ping_timeout.tv_sec = PING_TIMEOUT / 1000000000;
    ping_timeout.tv_nsec = PING_TIMEOUT % 1000000000;
    struct timespec ping_frequency;
    ping_frequency.tv_sec = PING_FREQUENCY / 1000000000;
    ping_frequency.tv_nsec = PING_FREQUENCY % 1000000000;
    while (!SERVER_SHUTDOWN) {
        for (struct Client* client = client_list; client != NULL; client = client->next) {
            ping(client);
        }
        nanosleep(&ping_timeout, NULL);
        for (struct Client* client = client_list; client != NULL; client = client->next) {
            if (client->is_alive == 0) {
                if (client->current_task != NULL) {
                    printf("Client %s lost connection while doing task %d, passing task...\n", client->name, client->current_task->taskID);
                    pass_task(client->current_task);
                }
                else
                    printf("Client %s lost connection\n", client->name);
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->socket_fd, NULL);
                delete_client(client->name);
            }
        }
        nanosleep(&ping_frequency, NULL);
    }
    pthread_exit(NULL);
}

void hear_from_clients(void) {
    struct epoll_event events[MAX_NUMBER_OF_LOCAL_CLIENTS + MAX_NUMBER_OF_NET_CLIENTS];
    int number_of_events;
    struct Message message;
    ssize_t bytes_read;
    while (!SERVER_SHUTDOWN) {
        number_of_events = epoll_wait(epoll_fd, events, MAX_NUMBER_OF_LOCAL_CLIENTS + MAX_NUMBER_OF_NET_CLIENTS, 1);
        for (int i = 0; i < number_of_events; i++) {
            bzero(&message, sizeof(message));
            bytes_read = read(events[i].data.fd, (void*) &message, sizeof(message));
            printf("Got message of type %d\n", message.type);
            switch (message.type) {
                case PING:
//                    printf("message type: %d, name: %s\n", message.type, ((struct Init_message*) (&message))->client_name);
                    mark_as_living(events[i].data.fd);
                    break;
            }
        }
    }
    close(epoll_fd);
    pthread_exit(NULL);
}

char* read_line() {
    size_t line_length;
    char* line = NULL;
    getline(&line, &line_length, stdin);
    return line;
}

void shell() {
    char* line = NULL;
    char* command = NULL;
    FILE* input;
    struct Task* task;
    while(!SERVER_SHUTDOWN) {
        line = read_line();
        command = strsep(&line, " \n");
        if (!strcmp(command, "STOP")) {
            SERVER_SHUTDOWN = 1;
        }
        else if (!strcmp(command, "STAT")) {
            input = fopen(line, "r");
            if (input < 0)
                perror("[ERROR] Couldn't open desired file");
            else {
                task = malloc(sizeof(struct Task));
                task->taskID = nextID++;
                pass_task(task);
            }
        }
    }
    free(command);
    free(line);
}

int initialize_sockets(uint16_t port_no, char* unix_socket_path, struct sockaddr_in* net_socket_address, struct sockaddr_un* local_socket_address) {
    net_socket_address->sin_family = AF_INET;
    net_socket_address->sin_port = htobe16(port_no);
    net_socket_address->sin_addr.s_addr = INADDR_ANY;
    local_socket_address->sun_family = AF_UNIX;
    strcpy(local_socket_address->sun_path, unix_socket_path);
    if ((net_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("[ERROR] Failed to create NET socket");
        return -1;
    }
    if ((local_socket = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        perror("[ERROR] Failed to create LOCAL socket");
        return -1;
    }
    if (bind(net_socket, (struct sockaddr*) net_socket_address, sizeof(*net_socket_address)) < 0) {
        perror("[ERROR] Failed to bind NET socket to it's address");
        return -1;
    }
    else
        printf("Net socket opened at address: %s\n", inet_ntoa(net_socket_address->sin_addr));
    if (bind(local_socket, (struct sockaddr*) local_socket_address, sizeof(*local_socket_address)) < 0) {
        perror("[ERROR] Failed to bind LOCAL socket to it's address");
        return -1;
    }
    if (listen(net_socket, MAX_NUMBER_OF_NET_CLIENTS) < 0) {
        perror("[ERROR] Failed to start listning on NET socket");
        return -1;
    }
    if (listen(local_socket, MAX_NUMBER_OF_LOCAL_CLIENTS) < 0) {
        perror("[ERROR] Failed to start listning on LOCAL socket");
        return -1;
    }
}

void destroy_sockets(char* unix_socket_path) {
    if (shutdown(net_socket, SHUT_RDWR) < 0)
        perror("[ERROR] Failed to shutdown NET socket");
    else if (close(net_socket) < 0)
        perror("[ERROR] Failed to close NET socket");
    if (shutdown(local_socket, SHUT_RDWR) < 0)
        perror("[ERROR] Failed to shutdown LOCAL socket");
    else if (close(local_socket) < 0)
        perror("[ERROR] Failed to close LOCAL socket");
    else if (unlink(unix_socket_path) < 0)
        perror("[ERROR] Failed to unlink LOCAL socket's descriptor file");
}

int parse_arguments(int argc, char** argv, uint16_t* port_no, char** unix_socket_path) {
    if (argc < 3) {
        printf("[ERROR] Too few arguments given. Please specify port number and UNIX socket path\n");
        return -1;
    }
    if (sscanf(argv[1], "%d", port_no) != 1) {
        printf("[ERROR] Wrong port number given: %s.\n", argv[1]);
        return -1;
    }
    *unix_socket_path = argv[2];
    return 0;
}

int main(int argc, char** argv) {
    uint16_t port_no;
    char* unix_socket_path;
    if (parse_arguments(argc, argv, &port_no, &unix_socket_path) < 0)
        return -1;
    struct sockaddr_in net_socket_address;
    struct sockaddr_un local_socket_address;
    if (initialize_sockets(port_no, unix_socket_path, &net_socket_address, &local_socket_address) < 0)
        goto cleanup;
    struct sigaction act;
    act.sa_handler = server_shutdown;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    sigaction(SIGINT, &act, NULL);

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("[ERROR] Failed to start listening to clients. Company is dead.\n");
        kill(SIGINT, getpid());
        pthread_exit(NULL);
    }

    pthread_t working_threads[4];
    pthread_create(&working_threads[0], NULL, (void*) accept_clients, (void*) &net_socket);
    pthread_create(&working_threads[1], NULL, (void*) accept_clients, (void*) &local_socket);
    pthread_create(&working_threads[2], NULL, (void*) ping_clients, NULL);
    pthread_create(&working_threads[3], NULL, (void*) hear_from_clients, NULL);

    getc(stdin);    
    SERVER_SHUTDOWN = 1;
    for (int i = 0; i < 4; i++) {
        pthread_kill(working_threads[i], SIGINT);
        pthread_join(working_threads[i], NULL);
    }
//    pthread_kill(working_threads[3], SIGINT);
//    pthread_join(working_threads[3], NULL);

cleanup:
    destroy_sockets(unix_socket_path);
    struct Client* tmp_client;
    while (client_list != NULL) {
        free(client_list->current_task);
        free(client_list->name);
        tmp_client = client_list;
        client_list = client_list->next;
        free(tmp_client);
    }
    struct Task* tmp_task;
    while (waiting_tasks != NULL) {
        tmp_task = waiting_tasks;
        waiting_tasks = waiting_tasks->next;
        free(tmp_task);
    }
    pthread_mutex_destroy(&client_list_mutex);
    pthread_mutex_destroy(&waiting_tasks_mutex);
    return 0;
}