#ifndef CLIENT_QUEUE_H
#define Client_QUEUE_H

struct Client {
    char* name;
    struct Task* current_task;
    int socket_fd;
    int is_alive;
    struct Client* next;
};

int add_client(char* name, int socket_fd);
void delete_client(char* name);
void mark_as_living(int socket_fd);

#endif