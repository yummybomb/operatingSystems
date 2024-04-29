// File:	thread-worker.c

// List all group member's name:
/* Nicholas Chen nhc29
   Pavan Kumar Kokkiligadda pkk46
 */ 
// username of iLab:
// iLab Server:
//ilab1

#include "thread-worker.h"
#include "thread_worker_types.h"

#define STACK_SIZE 16 * 1024
#define QUANTUM 10 * 1000
#define THREAD_AMT 4096

#define NUM_LEVELS 4 // You can adjust the number of priority levels as needed
q_t mlfq[NUM_LEVELS];
// Add a variable to keep track of the current priority level
// int current_priority = 0;
// Add a variable to store the time quantum for each priority level
int quantum[NUM_LEVELS] = {10, 20, 40, 80};

static int is_running[THREAD_AMT];
static void *return_value[THREAD_AMT];

// INITIALIZE ALL YOUR OTHER VARIABLES HERE
int init_sched_finish = 0;
worker_t id = 0;
ucontext_t sched_context, main_context;
q_t q;

// Forward Declarations
void pre_schedule();
void init_scheduler();
static void sched_rr();
static void sched_mlfq();
void enqueue(tcb *thread);
tcb *dequeue(q_t *q);
ucontext_t create_start_worker_context(ucontext_t *uctx);
void start_worker(ucontext_t *ctx);
void mlfq_enqueue(tcb *thread, int priority);
tcb *mlfq_dequeue(int priority);

/* create a new thread */
int worker_create(worker_t *thread, pthread_attr_t *attr,
                  void *(*function)(void *), void *arg)
{
    // Create Thread Control Block (TCB)
    tcb *new_tcb = (tcb *)malloc(sizeof(tcb));
    if (new_tcb == NULL)
    {
        perror("MallocTCB");
        exit(1);
    }

    ucontext_t *context = malloc(sizeof(ucontext_t));
    if (context == NULL)
    {
        free(context);
    }

    // Set thread ID
    new_tcb->thread_id = id++;
    *thread = id - 1;
    is_running[id - 1] = 1;
    // Set thread status
    new_tcb->status = THREAD_STATUS_READY;
    // set priority for MLFQ as random number
    new_tcb->priority = rand() % NUM_LEVELS;
    // Create and initialize the context of this worker thread
    if (getcontext(context) < 0)
    {
        free(new_tcb);
        exit(1); // Failed to get context
    }
    // Allocate space for the stack
    void *stack = malloc(STACK_SIZE);

    if (stack == NULL)
    {
        free(new_tcb);
        free(context);
        exit(1); // Failed to allocate memory for the stack
    }

    context->uc_stack.ss_sp = stack;
    context->uc_stack.ss_size = STACK_SIZE;
    context->uc_link = NULL;
    context->uc_flags = 0;
    context->uc_stack.ss_flags = 0;

    // Set up the new context to execute the function when it is swapped in.
    makecontext(context, (void (*)(void))function, 1, arg);
    new_tcb->context = create_start_worker_context(context);

    // After everything is set, push this thread into run queue and make it ready for the execution.
    // push_to_run_queue(new_tcb);
    enqueue(new_tcb);
    // push to mlfq
    mlfq_enqueue(new_tcb, new_tcb->priority);

    if (init_sched_finish == 0)
    {
        init_scheduler();
    }
    return 0;
}

/* give CPU possession to other user-level worker threads voluntarily */
int worker_yield()
{
    if (swapcontext(&q.head->data->context, &sched_context) < 0)
    {
        perror("swapcontext");
        exit(1);
    }

    return 0;
}

/* terminate a thread */
void worker_exit(void *value_ptr)
{
    // Set status of current thread and free the stack and the tcb
    q.head->data->status = THREAD_STATUS_FINISHED;
    return_value[q.head->data->thread_id] = value_ptr;
    is_running[q.head->data->thread_id] = 0;
    free(q.head->data->context.uc_stack.ss_sp);
    mlfq_dequeue(q.head->data->priority);
    // Move to schedule context
    setcontext(&sched_context);
    exit(1);
    return;
}

/* Wait for thread termination */
int worker_join(worker_t thread, void **value_ptr)
{

    q.head->data->yield_id = thread;
    q.head->data->status = THREAD_STATUS_BLOCKED;
    // - wait for a specific thread to terminate
    // - if value_ptr is provided, retrieve return value from joining thread
    // - de-allocate any dynamic memory created by the joining thread

    if (swapcontext(&q.head->data->context, &sched_context) < 0)
    {
        perror("swapcontext");
        exit(1);
    }
    if (value_ptr == NULL)
    {
        return 0;
    }
    *value_ptr = return_value[q.head->data->yield_id];
    return 0;
};

/* initialize the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex,
                      const pthread_mutexattr_t *mutexattr)
{
    //- initialize data structures for this mutex
    __atomic_clear(&mutex->is_locked, __ATOMIC_SEQ_CST);
    return 0;
};

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex)
{
    while (__atomic_test_and_set(&mutex->is_locked, __ATOMIC_SEQ_CST))
    {
        worker_yield();
    }
    return 0;
};

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex)
{
    __atomic_clear(&mutex->is_locked, __ATOMIC_SEQ_CST);

    return 0;
};

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex)
{
    __atomic_clear(&mutex->is_locked, __ATOMIC_SEQ_CST);

    // - make sure mutex is not being used
    // - de-allocate dynamic memory created in worker_mutex_init

    return 0;
};

/* scheduler */
static void schedule()
{
    // - every time a timer interrupt occurs, your worker thread library
    // should be contexted switched from a thread context to this
    // schedule() function

    // - invoke scheduling algorithms according to the policy (RR or MLFQ)

    // - schedule policy
    pre_schedule();

#ifndef MLFQ
    // Choose RR
    sched_rr();
#else
    // Choose MLFQ
    sched_mlfq();

#endif
}

static void sched_rr()
{
    // heart of program
    tnode_t *temp_node;
    tcb *t;

    while (q.head != NULL)
    {
        t = q.head->data;

        if (t->status == THREAD_STATUS_FINISHED)
        {
            dequeue(&q);
            free(t);
        }
        else if (t->status == THREAD_STATUS_BLOCKED)
        {
            if (is_running[t->yield_id] == 0)
            {
                t->status = THREAD_STATUS_READY;
            }
            dequeue(&q);
            enqueue(t);
        }
        else if (t->status == THREAD_STATUS_READY)
        {
            t->status = THREAD_STATUS_RUNNING;
            if (swapcontext(&sched_context, &t->context) < 0)
            {
                perror("swapcontext");
                exit(1);
            }
            if (t->status == THREAD_STATUS_RUNNING)
            {
                t->status = THREAD_STATUS_READY;
            }
            dequeue(&q);
            enqueue(t);
        }
    }

    free(sched_context.uc_stack.ss_sp); // empty stack
    exit(0);
}

/* Preemptive MLFQ scheduling algorithm */

static void sched_mlfq()
{
    // Choose the thread from the highest-priority non-empty runqueue
    int i;
    for (i = 0; i < NUM_LEVELS; i++)
    {
        if (mlfq[i].size > 0)
        {
            tcb *t = mlfq_dequeue(i);
            t->status = THREAD_STATUS_RUNNING;

            if (swapcontext(&sched_context, &t->context) < 0)
            {
                perror("swapcontext");
                exit(1);
            }

            // Check if the thread used up its time quantum
            if (t->status == THREAD_STATUS_RUNNING)
            {
                // Move the thread to a lower-priority queue
                if (i < NUM_LEVELS - 1)
                {
                    mlfq_enqueue(t, i + 1);
                }
                else
                {
                    // If it was in the lowest-priority queue, keep it there
                    mlfq_enqueue(t, i);
                }
            }
            else
            {
                // The thread yielded or blocked, keep it in the same queue
                mlfq_enqueue(t, i);
            }

            break; // Only choose one thread from the highest-priority queue
        }
    }
}

// Feel free to add any other functions you need.
// You can also create separate files for helper functions, structures, etc.
// But make sure that the Makefile is updated to account for the same.

// HELPER FUNCTIONS HERE

void enqueue(tcb *thread)
{
    tnode_t *node = (tnode_t *)malloc(sizeof(tnode_t));
    node->next = NULL;
    node->data = thread;

    if (q.head == NULL)
    {
        q.head = node;
        q.tail = node;
        q.size = 1;
    }
    else
    {
        q.tail->next = node;
        q.tail = node;
        q.size++;
    }
    return;
}

tcb *dequeue(q_t *q)
{
    if (q->head == NULL)
        return NULL; // queue must be empty

    tnode_t *temp_node = q->head;
    tcb *thread = temp_node->data;

    q->head = q->head->next; // remove beginning node;
    free(temp_node);
    return thread;
}

void timer_signal_handler(int signum)
{
    ucontext_t curr_context;
    getcontext(&curr_context);
    if (curr_context.uc_stack.ss_sp != curr_context.uc_stack.ss_sp)
    {

        if (swapcontext(&q.head->data->context, &sched_context) < 0)
        {
            perror("swapcontext");
            exit(1);
        }
    }
    return;
}

void init_scheduler()
{
    getcontext(&sched_context);
    sched_context.uc_stack.ss_sp = malloc(STACK_SIZE);
    sched_context.uc_stack.ss_size = STACK_SIZE;
    sched_context.uc_stack.ss_flags = 0;
    sched_context.uc_link = 0;
    makecontext(&sched_context, &schedule, 0);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &timer_signal_handler;
    sigaction(SIGPROF, &sa, NULL);

    struct itimerval timer;

    timer.it_interval.tv_usec = QUANTUM;
    timer.it_interval.tv_sec = 0;
    timer.it_value.tv_usec = QUANTUM;
    timer.it_value.tv_sec = 0;

    // Set the timer up (start the timer)
    setitimer(ITIMER_PROF, &timer, NULL);

    tcb *starttcb = (tcb *)malloc(sizeof(tcb));
    starttcb->thread_id = id++;
    is_running[id] = 1;
    starttcb->status = THREAD_STATUS_READY;

    init_sched_finish = 1;
    enqueue(starttcb);

    swapcontext(&main_context, &sched_context);

    return;
}

ucontext_t create_start_worker_context(ucontext_t *uctx)
{
    ucontext_t context;
    if (getcontext(&context) < 0)
    {
        perror("getContext err");
        exit(1);
    };
    void *stack = malloc(STACK_SIZE);
    context.uc_link = NULL;
    context.uc_stack.ss_sp = stack;
    context.uc_stack.ss_size = STACK_SIZE;
    context.uc_stack.ss_flags = 0;
    makecontext(&context, (void *)&start_worker, 1, uctx);
    return context;
}

void start_worker(ucontext_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }
    setcontext(ctx);

    // on failure we will free all the data
    free(ctx->uc_stack.ss_sp);
    free(ctx);
    worker_exit(NULL);
    return;
}

void pre_schedule()
{
    tnode_t *temp = q.head;

    while (temp != NULL)
    {
        if (temp->data->thread_id != 1)
        {
            temp = temp->next;
        }
        temp->data->context = create_start_worker_context(&main_context);
        break;
    }
}

void mlfq_enqueue(tcb *thread, int priority)
{
    tnode_t *node = (tnode_t *)malloc(sizeof(tnode_t));
    node->next = NULL;
    node->data = thread;

    if (mlfq[priority].head == NULL)
    {
        mlfq[priority].head = node;
        mlfq[priority].tail = node;
        mlfq[priority].size = 1;
    }
    else
    {
        mlfq[priority].tail->next = node;
        mlfq[priority].tail = node;
        mlfq[priority].size++;
    }
    return;
}

tcb *mlfq_dequeue(int priority)
{
    if (mlfq[priority].head == NULL)
        return NULL; // queue must be empty

    tnode_t *temp_node = mlfq[priority].head;
    tcb *thread = temp_node->data;

    mlfq[priority].head = mlfq[priority].head->next; // remove beginning node;
    free(temp_node);
    return thread;
}