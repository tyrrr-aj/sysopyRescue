#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdint.h>

#define MAX_NUMBER_OF_NET_CLIENTS 15
#define MAX_NUMBER_OF_LOCAL_CLIENTS 15

#define PING_TIMEOUT 1000000000
#define PING_FREQUENCY 1000000000

#define TEXT_PACK_SIZE 8192
#define CLIENT_NAME_MAX_LENGTH 20

enum Message_type {INIT=1, PING, TASK, RESPONSE};
enum Response {OK, NAME_USED};

struct Message {
    enum Message_type type;
    char message[TEXT_PACK_SIZE];
};

struct Init_message {
    enum Message_type type;
    char client_name[CLIENT_NAME_MAX_LENGTH];
};

struct Server_response {
    enum Message_type type;
    enum Response response;
};

struct Ping_message {
    enum Message_type type;
};

struct Text_task {
    enum Message_type type;
    char text[TEXT_PACK_SIZE];
};

struct Word_frequency {
    uint8_t frequency;
    char* word;
};

struct Text_stat {
    int number_of_words;
    struct Word_frequency* frequencies;
};