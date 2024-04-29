#ifndef MTX_TYPES_H
#define MTX_TYPES_H

#include <stdatomic.h>
#include "thread_worker_types.h"


/* mutex struct definition */
typedef struct worker_mutex_t
{
    /* add something here */
    atomic_flag is_locked;
    // YOUR CODE HERE
} worker_mutex_t;

#endif