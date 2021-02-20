#ifndef MYQUEUE_H_
#define QUEUE_H_

struct node {
    struct node* next;
    int *client_sockd;
};
typedef struct node node_t;

void enqueue(int *client_sockd);
int* dequeue();

#endif
