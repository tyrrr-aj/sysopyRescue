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
#include <strings.h>
#include <signal.h>

#include "protocol.h"

#define LOCAL 0
#define NET 1

int CLIENT_SHUTDOWN = 0;

void stop_client(int signum) {
    CLIENT_SHUTDOWN = 1;
}

int count_words(char* text) {
    
}

void respond_to_ping(int socket_fd) {
    struct Ping_message message;
    message.type = PING;
    if (write(socket_fd, (void*) &message, sizeof(message)) < 0)
        perror("Couldn't answer ping\n");
}

void wait_for_orders(int socket_fd) {
    struct Message message;
    if (read(socket_fd, (void*) &message, sizeof(message)) > 0) {
        switch (message.type) {
            case PING:
                respond_to_ping(socket_fd);
                break;

            case TASK:

                break;
        }
    }
    else {
        printf("Server broke comunication! Shuting down...\n");
        CLIENT_SHUTDOWN = 1;
    }
}

int introduce_yourself(int socket_fd, char* name) {
    struct Init_message message;
    bzero(&message, sizeof(message));
    message.type = INIT;
    strcpy(message.client_name, name);
    write(socket_fd, (void*) &message, sizeof(message));
    struct Server_response response;
    read(socket_fd, (void*) &response, sizeof(response));
    if (response.response == OK) {
        printf("Connection with server established\n");
        return 0;
    }
    else if (response.response == NAME_USED) {
        printf("Failed to connect to server: name %s already in use\n", name);
        return -1;
    }
    else {
        printf("Failed to connect to server: unknown response\n");
        return -1;
    }
}

int initialize_net_socket(int* socket_fd, uint16_t port_no, struct sockaddr_in* net_socket_address) {
    net_socket_address->sin_family = AF_INET;
    net_socket_address->sin_port = htobe16(port_no);
    if ((*socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("[ERROR] Failed to create net socket");
        return -1;
    }
    if (connect(*socket_fd, (struct sockaddr*) net_socket_address, sizeof(*net_socket_address)) < 0) {
        perror("[ERROR] Failed to connect net socket to sever");
        return -1;
    }
    return 0;
}

int initialize_local_socket(int* socket_fd, char* unix_socket_path, struct sockaddr_un* local_socket_address) {
    local_socket_address->sun_family = AF_UNIX;
    strcpy(local_socket_address->sun_path, unix_socket_path);
    if ((*socket_fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        perror("[ERROR] Failed to create local socket");
        return -1;
    }
    if (connect(*socket_fd, (struct sockaddr*) local_socket_address, sizeof(*local_socket_address)) < 0) {
        perror("[ERROR] Failed to conect local socket to server");
        return -1;
    }
    return 0;
}

void destroy_socket(int socket) {
    if (shutdown(socket, SHUT_RDWR) < 0)
        perror("[ERROR] Failed to shutdown local socket");
    else if (close(socket) < 0)
        perror("[ERROR] Failed to close local socket");
}

int parse_arguments(int argc, char** argv, char** client_name, int* mode, struct sockaddr_in* net_socket_address, uint16_t* port_no, char** unix_socket_path) {
    if (argc < 4) {
        printf("[ERROR] Too few arguments given. Please specify client name, communication mode (n - net or l - local) and IPv4 address and port number or UNIX socket path respectively\n");
        return -1;
    }
    *client_name = argv[1];
    if (!strcmp(argv[2], "l") || !strcmp(argv[2], "local"))
        *mode = LOCAL;
    else if (!strcmp(argv[2], "n") || !strcmp(argv[2], "net"))
        *mode = NET;
    else {
        printf("[ERROR] Unknown communication mode: %s. Possible modes are local (l) or net (n)\n", argv[2]);
        return -1;
    }
    if (*mode == LOCAL)
        *unix_socket_path = argv[3];
    else if (argc < 5) {
        printf("[ERROR] Too few arguments for chosen communication mode: NET provided. Specify IPv4 address and port number\n");
        return -1;
    }
    else if (inet_aton(argv[3], &net_socket_address->sin_addr) == 0) {
        printf("[ERROR] Wrong IPv4 address specified: %s\n", argv[3]);
        return -1;
    }
    else if (sscanf(argv[4], "%u", port_no) != 1) {
        printf("[ERROR] Wrong port number specified: %s\n", argv[4]);
        return -1;
    }
    return 0;
}

int main(int argc, char** argv) {
    int local_socket, net_socket, mode, socket_fd;
    uint16_t port_no;
    char *unix_socket_path, *client_name;
    struct sockaddr_in net_address;
    struct sockaddr_un local_address;
    if (parse_arguments(argc, argv, &client_name, &mode, &net_address, &port_no, &unix_socket_path) < 0)
        return -1;
    switch (mode) {
        case NET:
            if (initialize_net_socket(&socket_fd, port_no, &net_address) < 0)
                return -1;
            break;
        
        case LOCAL:
            if (initialize_local_socket(&socket_fd, unix_socket_path, &local_address) < 0)
                return -1;
            break;
    }

    if (introduce_yourself(socket_fd, client_name) < 0)
        goto cleanup;

    struct sigaction act;
    act.sa_handler = stop_client;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGINT, &act, NULL);

    while (!CLIENT_SHUTDOWN)
        wait_for_orders(socket_fd);
    
cleanup:
    destroy_socket(socket_fd);
    return 0;
}