#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H

struct Task {
    int taskID;
    char* text;
    struct Task* next;
};

void add_task(char* text, int taskID);
void pass_task(struct Task* task);

#endif