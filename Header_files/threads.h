#ifndef THREADS_C
#define THREADS_C
#define MAX_THREAD 128
#define READY 0
#define RUNNING 1
#define EXITED 2
#include <pthread.h>
#include <setjmp.h>

/*TCB data structure to store all necessary info about our thread*/
typedef struct TCB {
    int thread_id; /*identifier of the specific thread we're working with*/
    int thread_status; /*status of thread, ready, running, or blocked*/
    void *thread_stackptr;  /*ptr to thread's respective stack*/
    void *retval; /*return value of exiting thread*/
    jmp_buf thread_register; /*array of registers, 8 registers per thread*/
}TCB;

extern struct TCB thread_table[MAX_THREAD]; /*this needs to be a global varible, array of all active threads*/
extern int thread_count; /*number of currently running threads, also needs to be global*/
extern int current_thread; /*the current running thread*/

/*linked list struct for the queue in mysem*/
typedef struct thread_node {
    int thread_id;
    struct thread_node *next;
} thread_node_t;

/*structure for mysem_t*/
typedef struct mysem_t{
    int curr_val;
    thread_node_t *head; /*pointer to the head of the list*/
    thread_node_t *tail; /*pointe to the end of the list*/
    int init;
}mysem_t;

extern struct mysem_t sem_table[MAX_THREAD]; /*global array of semaphores, using 128 as a base value*/


/*definition of functions below*/
int pthread_create(
    pthread_t *thread,
    const pthread_attr_t *(attr),
    void *(*start_routine) (void *),
    void *arg
);

void pthread_exit(void *value_ptr);

pthread_t pthread_self(void);

void scheduler();

void alarm_handler();

void call_scheduler(int signum);

void lock();

void unlock();

void pthread_exit_wrapper();

int pthread_join(pthread_t thread, void **value_ptr);

int sem_init(sem_t *sem, int pshared, unsigned value);

int sem_wait(sem_t *sem);

int sem_post(sem_t *sem);

int sem_destroy(sem_t *sem);

#endif
