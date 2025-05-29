#define READY 0 /*using this for TCB*/
#define RUNNING 1 /*using this for TCB*/
#define EXITED 2 /*using this for TCB*/
#define BLOCKED 3 /*Thread will be assgined status blocked when attempting to join a thread that is still running*/
#define X 50000 /*for the alarm*/
#define MAX_THREAD 128 /*we asuume only 128 threads can exist at a time*/
#define STACK_MAX 32767 /*value provided in the HW desc*/
#define JB_RBX 0
#define JB_RBP 1
#define JB_R12 2
#define JB_R13 3
#define JB_R14 4
#define JB_R15 5
#define JB_RSP 6
#define JB_PC 7
#include "threads.h" /*all header files in here*/
#include "ec440threads.h"
#include <stdlib.h> /*for malloc and free*/
#include <pthread.h> /*required to test hw*/
#include <setjmp.h> /*for registers*/
#include <unistd.h> /*for sigaction*/
#include <signal.h>
#include <stdio.h>
#include <sys/time.h>
#include "semaphore.h" /*need this for hw 3*/

struct TCB thread_table[MAX_THREAD]; /*this needs to be a global varible, array of all active threads*/
int thread_count = 0; /*number of currently running threads, also needs to be global*/
int current_thread = 0; /*the current running thread*/

/*function that creates a new thread, sets up the context, the TCB, and preserves context of currently running thread*/
int pthread_create(
    pthread_t *thread,
    const pthread_attr_t *(attr),
    void *(*start_routine) (void *),
    void *arg
)
{
    if (thread_count >= MAX_THREAD) 
    {
        return -1; /*too many threads exist */
    }

    /*if this is the first thread created, register the main thread */
    if (thread_count == 0) 
    {
        struct TCB *main_thread_info = &thread_table[thread_count];
        {
            main_thread_info->thread_status = RUNNING; /*main thread is running */
            main_thread_info->thread_id = thread_count; /*ID for main thread is 0 */
            main_thread_info->thread_stackptr = NULL; /*main thread stack is not dynamically allocated */
            thread_count++; /*increment thread count */
            alarm_handler(); /*this is what I was missing, once your first thread is created, this needs to be called*/
        }
    }
    struct TCB *thread_info = &thread_table[thread_count];

    thread_info->thread_stackptr = malloc(STACK_MAX); /*allocate space for the stack */
    if (thread_info->thread_stackptr == NULL) 
    {
        return -1;
    }

    unsigned long *stack_top = (unsigned long *)((char *)thread_info->thread_stackptr + STACK_MAX); /* Top of the stack */
    *--stack_top = (unsigned long) pthread_exit_wrapper; /*push pthread_exit_wrapper onto the stack, i changed this from pthread_exit*/

    /*preserve the context of the new thread */
        thread_info->thread_register[0].__jmpbuf[JB_R12] = (unsigned long)start_routine;
        thread_info->thread_register[0].__jmpbuf[JB_R13] = (unsigned long)arg;
        thread_info->thread_register[0].__jmpbuf[JB_PC] = ptr_mangle((unsigned long)start_thunk); 
        thread_info->thread_register[0].__jmpbuf[JB_RSP] = ptr_mangle((unsigned long)stack_top); 
        thread_info->thread_status = READY; /* Set thread status to ready */
        thread_info->thread_id = thread_count; /* Assign thread ID */
        *thread = thread_count++; /*store the thread ID in the location referenced by thread and increment thread count*/
        return 0;
}


/*function that frees all the resources used up by a thread, then we need to set status to EXIT*/
void pthread_exit(void *value_ptr)
{
    thread_table[current_thread].thread_status = EXITED; /*set status to exited*/
    thread_table[current_thread].retval = value_ptr; /*make sure we save the return value of the thread*/
    

    int threads_active = 0; /*number of currently running threads*/
    int j = 0;
    for(j = 0; j<MAX_THREAD; j++)
    {
        if(thread_table[j].thread_status != EXITED) /*so if a thread is running, ready, or blocked*/
        {
            threads_active++;
        }
    }
    
    /*There are no active threads, so process should exit with an exit status of 0*/
    if(threads_active == 0)
    {
        exit(0);
    }
    scheduler(); /*once the terminating thread is dealt with, we MUST schedule*/

    __builtin_unreachable();
}

/*function that returns the current threads id number*/
pthread_t pthread_self(void)
{
    return thread_table[current_thread].thread_id; /*thread table is an arry that contains all active threads*/
}

/*function that handles scheduling in a round robin manner*/
void scheduler()
{
    if(thread_table[current_thread].thread_status == RUNNING) /*added this if statement*/
    {
        thread_table[current_thread].thread_status = READY; /*stop thread from running and set it to ready*/
    }

    if(setjmp(thread_table[current_thread].thread_register) == 0)
    {
        int next_thread = current_thread;
        int found = 0;
        int i = 0;
        for(i = 0; i < thread_count; i++)
        {
            next_thread = (current_thread+1+i) % thread_count; //need the +1 because this is 0 indexed

            if(thread_table[next_thread].thread_status == READY) /*this should also handle skipping over threads with a BLOCKED or EXITED status*/
            {
                found = 1; /*this indicates we found the next availiable thread to run*/
                break;
            }
        }

        if(!found) /*if found is equal to 0 that means there are no threads we can schedule to run*/
        {
            exit(0);
        }
        current_thread = next_thread;
        thread_table[current_thread].thread_status = RUNNING;
        longjmp(thread_table[current_thread].thread_register, 1);
    }
}

/*scheduler function that uses SIGALRM to context switch every 50ms*/
void alarm_handler()
{
    struct sigaction alarm;
    alarm.sa_handler = call_scheduler;
    alarm.sa_flags = SA_NODEFER; /*allows multiple signals to be detected*/
    sigemptyset(&alarm.sa_mask);
    sigaction(SIGALRM,&alarm,NULL);

    struct itimerval timer; /*trying this instead of ualarm*/
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = X;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = X;
    setitimer(ITIMER_REAL, &timer, NULL);
}

void call_scheduler(int signum) /*just a call function for the scheduler*/
{
    scheduler();
}


sigset_t block, unblock; /*these are used for lock and unlock*/
struct mysem_t sem_table[MAX_THREAD]; /*global array of semaphores, using 128 as a base value*/

/*function that disables intterupts whenever a thread calls it*/
void lock()
{
    sigemptyset(&block);
	sigaddset(&block, SIGALRM);
	sigprocmask(SIG_BLOCK,&block,&unblock);
}

/*opposite of lock, when a thread calls unlock(), interrupts are reenabled*/
void unlock()
{
    sigemptyset(&unblock);
	sigaddset(&unblock, SIGALRM);
	sigprocmask(SIG_UNBLOCK,&unblock,NULL);
  
}

/*function that accesses the return value which is used in pthread_join()*/
void pthread_exit_wrapper()
{
unsigned long int res;
asm("movq %%rax, %0\n":"=r"(res));
pthread_exit((void *) res);
}

//function which ensures threads succesfully complete and terminate
int pthread_join(pthread_t thread, void **value_ptr)
{
    struct TCB *target_thread = NULL; /*pointer which will help us find the target thread*/
    int i;
    for(i =0 ; i < MAX_THREAD; i++)
    {
        if(thread_table[i].thread_id == thread) /*finding the ID that is associated with the target thread*/
        {
            target_thread = &thread_table[i]; /*set the target thread once we find its ID*/
            break; /*exit the loop*/
        }
    }

    while(target_thread->thread_status != EXITED) /*waiting for target thread to finish*/
    {
        scheduler(); /*we then want to call scheduler() while this thread is blocked*/
    }

    thread_table[current_thread].thread_status = READY; /*set status of current thread to ready*/

    if(value_ptr != NULL)
    {
        *value_ptr = target_thread->retval; //accessing the threads return value
    }

    /*now we want to free the thread's stack*/
    if(target_thread->thread_id == 0)
    {
        return 0;
    } else {
        free(target_thread->thread_stackptr); /*we want to do this here instead of pthread_exit*/
        return 0; /*succesful return value of pthread_join()*/
    }
}

int sem_init(sem_t *sem, int pshared, unsigned value)
{
    if(pshared != 0)
    {
        perror("Pshared should have a value of 0");
        return -1; 
    }

    lock(); /*preventing multiple threads from accessing this section*/

    int sem_id = -1; 
    int i;
    for (i = 0; i < 128; i++) 
    {
        if (sem_table[i].init == 0) /*finding an unused sem_table slot*/
        { 
            sem_id = i;
            sem_table[i].init = 1; /*initializing the semaphore*/
            sem_table[i].curr_val = value;
            sem_table[i].tail = NULL;
            sem_table[i].head = NULL;
            break;
        }
    }

    if (sem_id == -1) /*this would mead we didn't find an unused seamphore slot in the array*/
    {
        unlock();
        perror("No available semaphores");
        return -1;
    }

    sem->__align = (long int)sem_id; /*linking the index in sem_table to the sem_t struct*/

    unlock();
    return 0;
}

int sem_wait(sem_t *sem) 
{
    int sem_id = sem->__align; 
    mysem_t *mysem = &sem_table[sem_id];

    lock();
    /*this is the normal case, all we do is decrement and return*/
    if (mysem->curr_val > 0) 
    {
        mysem->curr_val--; 
    } else {
        /*special case where val equals 0 so we need to block*/
        thread_node_t *new_node = malloc(sizeof(thread_node_t));
        new_node->thread_id = current_thread; /*make a new node in the queue referencing the current thread*/
        new_node->next = NULL;

        // Insert the new node into the queue
        if (mysem->tail == NULL) 
        {
            mysem->head = mysem->tail = new_node;
        } else {
            mysem->tail->next = new_node;
            mysem->tail = new_node;
        }

        /*block the current thread and call scheduler*/
        thread_table[current_thread].thread_status = BLOCKED;
        unlock();
        scheduler(); 
        lock();
    }
    unlock();
    return 0;
}

int sem_post(sem_t *sem) 
{
    int sem_id = sem->__align;
    mysem_t *mysem = &sem_table[sem_id];

    lock();
    mysem->curr_val++; /*as mentioned, we need to increment the value*/

    if (mysem->curr_val > 0 && mysem->head != NULL) /*check curr_val and if the queue contains blocked threads*/
    {
        thread_node_t *next_thread_node = mysem->head; /*grabbing the next available thread from the queue*/
        int next_thread_id = next_thread_node->thread_id;

        mysem->head = mysem->head->next; /*updating the head of the queue*/
        if (mysem->head == NULL) 
        {
            mysem->tail = NULL;  /*setting the queue to empty*/
        }

        thread_table[next_thread_id].thread_status = READY;

        free(next_thread_node); /*we can get rid of the node once we mark it ready*/
    }

    unlock();
    scheduler();  
    return 0;
}


int sem_destroy(sem_t *sem) 
{
    int sem_id = sem->__align;
    mysem_t *mysem = &sem_table[sem_id];

    lock();
    if (mysem->init == 0) 
    {
        unlock();
        perror("This semaphore has not been initialized or has already been destroyed");
        return -1;
    }

    if (mysem->head != NULL) 
    {
        unlock();
        perror("Cannot destroy semaphore while threads are waiting");
        return -1;
    }

    thread_node_t *current = mysem->head;
    while (current != NULL) 
    {
        thread_node_t *temp = current;
        current = current->next;
        free(temp); /*freeing nodes in the list*/
    }

    /*reset the state of mysem and set align to 0, so it won't point to any thread*/
    mysem->init = 0;
    mysem->curr_val = 0;
    mysem->head = NULL;
    mysem->tail = NULL;
    sem->__align = 0;

    unlock();
    return 0;
}

/*POTENTIAL FIXES*/

/*The assignment mentions that lock and unlock should be used to prevent threads from being
  interrupted, such as when we're altering global variables. It then provides an example of 
  one type of scenario, which is when we want to alter the list of running threads. Does
  that mean I need to use lock and unlock for my scheduler function?*/

/*could the while loop inside of pthread_join be an isse? I have it set to check whether or
  not the target thread has a status of EXITED, but do I need to change this to just check
  if the target thread is running? That's what it says in the function description but I've
  had errors when I tried that before.*/

/*could be having an issue with sem_post in the if statement which checks the curr val and
 queue or/and the case in which, "Note that when a thread is woken up and takes the lock as
 part of sem_post, the value of the semaphore will remain zero." */

 /*in the description of sem_wait, it mentions that if the val is equal to 0, we have to wait
   until the semaphores value is incremented. Obviously this is referring to sem_post, but does
   that mean we need to call sem_post in sem_wait? Or wait for another thread to call sem_post
   and wait for the blocked thread in sem_wait to be selcted from the queue?*/

 /*maybe the way i'm mapping my custom sempahore to sem is incorrect?*/
