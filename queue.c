#include "queue.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>

// inspired by https://www.hackerearth.com/practice/data-structures/linked-list/singly-linked-list/tutorial/

node_t* head = NULL;
node_t* tail = NULL;

// add client request to the queue
void enqueue(int *client_sockd) {
	// printf("socket %d enqueued\n", *client_sockd);
    //printf("Linked list enqueueing client_sockd: %d", *client_sockd);
	node_t *newnode = malloc(sizeof(node_t));
	newnode->client_sockd = client_sockd;
	newnode->next = NULL;
	if(tail == NULL) {
		head = newnode;
	}
	else {
		tail->next = newnode;
	}

	tail = newnode;
}

// returns next available client requests to be sent by dispatcher
// returns NULL if no enqueued client requests
int* dequeue() {
	if(head == NULL) {
		return NULL;
	}
	else{
		int *result = head->client_sockd;
		node_t *temp = head;
		head = head->next;
		if(head == NULL) {
			tail = NULL;
		}
		free(temp);
		// printf("socket %d dequeued\n", *result);
		return result;
	}
}
