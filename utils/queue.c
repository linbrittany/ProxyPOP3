// A C program to demonstrate linked list based implementation of queue
#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

// A linked list (LL) node to store a queue entry
struct QNode {
    void* data;
    struct QNode* next;
};

// The queue, front stores the front node of LL and rear stores the
// last node of LL
struct Queue {
    struct QNode *front, *rear;
};

// A utility function to create a new linked list node.
struct QNode* newNode(void *data)
{
    struct QNode* temp = (struct QNode*)malloc(sizeof(struct QNode));
    temp->data = data;
    temp->next = NULL;
    return temp;
}
// A utility function to create an empty queue
struct Queue* create_queue()
{
    struct Queue* q = (struct Queue*)malloc(sizeof(struct Queue));
    q->front = q->rear = NULL;
    return q;
}
// The function to add a key k to q
void enqueue(struct Queue* q, void *data)
{
    // Create a new LL node
    struct QNode* temp = newNode(data);
    // If queue is empty, then new node is front and rear both
    if (q->rear == NULL) {
        q->front = q->rear = temp;
        return;
    }
    // Add the new node at the end of queue and change rear
    q->rear->next = temp;
    q->rear = temp;
}
// Function to remove a key from given queue q
void * dequeue(struct Queue* q)
{
    // If queue is empty, return NULL.
    if (q->front == NULL)
        return NULL;
    // Store previous front and move front one node ahead
    struct QNode* node_to_dequeue = q->front;
    void* data = node_to_dequeue->data;
    q->front = q->front->next;
    // If front becomes NULL, then change rear also as NULL
    if (q->front == NULL)
        q->rear = NULL;
    free(node_to_dequeue);
    return data;
}
