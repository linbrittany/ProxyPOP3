#ifndef QUEUE_H
#define QUEUE_H


// A utility function to create an empty queue
struct Queue* create_queue();
// The function to add a key k to q
void enqueue(struct Queue* q, void * data);
// Function to remove a key from given queue q
void* dequeue(struct Queue* q);
#endif
