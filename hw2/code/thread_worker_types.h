#ifndef TW_TYPES_H
#define TW_TYPES_H

#include <ucontext.h>

typedef unsigned int worker_t;

typedef enum {
    THREAD_STATUS_READY,
    THREAD_STATUS_RUNNING,
    THREAD_STATUS_BLOCKED,
    THREAD_STATUS_FINISHED
} thread_status_t;

typedef struct TCB
{
    worker_t thread_id; // unique thread ID
    worker_t yield_id; // yielding id
    thread_status_t status; // thread status
    ucontext_t context; // thread context
    int priority;           // Priority level of the thread
} tcb;

typedef struct ThreadNode {
    tcb *data;
    struct ThreadNode *next;
    int test_var;
} tnode_t;

typedef struct Queue {
    tnode_t *head;
    tnode_t *tail;
    int size;
} q_t;

#endif