#include <stdio.h>
#include <stdlib.h>
#include "so_scheduler.h"
#include <pthread.h>
#include <semaphore.h>
#include <string.h>

#define MAXSIZE 256
#define QUEUE_MAX_SIZE 1024

#define NEW "new"
#define RUNNING "running"
#define TERMINATED "terminated"
#define READY "ready"
#define BLOCKED "blocked"

enum flag{FALSE, TRUE};

typedef struct thread_t {
	so_handler *func;
	sem_t t_sem;
	char status[MAXSIZE];
	tid_t tid;
	unsigned int thread_priority;
	unsigned int thr_quantum;
	unsigned int device;
	enum flag T_FLAG;
} thread_t;

typedef struct queue_t {
	unsigned int max_size;
	unsigned int **priorities;
	thread_t *buff[QUEUE_MAX_SIZE << 1];
	unsigned int size;
	unsigned int priorities_size;
} queue_t;

typedef struct scheduler_t {
	unsigned int priority;
	unsigned int quantum;
	unsigned int max_events;
	enum flag FLAG;
	thread_t *current_thread;
	queue_t threads;
	queue_t pqueue;
} scheduler_t;

static scheduler_t *scheduler;
static unsigned int is_initialized;
static pthread_attr_t attr;
static int glob = 0;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

static void *thread_func(void *args);

// function that handles certain errors

void error_handler(const char* args) 
{
	printf("%s\n", args);
	exit(EXIT_FAILURE);
}

int check_if_is_initialized(unsigned int is_initialized)
{
	if (is_initialized == 1)
		return -1;
	return 1;
}

// function that creates and initializes a thread

void q_create() 
{
	scheduler->threads.size = 0;
	scheduler->threads.priorities_size = 0;
	scheduler->pqueue.size = 0;
	scheduler->pqueue.priorities_size = 0;
	scheduler->threads.max_size = QUEUE_MAX_SIZE << 1;
	scheduler->pqueue.max_size = QUEUE_MAX_SIZE << 1;
	
	scheduler->pqueue.priorities = calloc(1, sizeof(int *) * (QUEUE_MAX_SIZE << 1));
	
	if (!scheduler->pqueue.priorities)
		error_handler("calloc failed");

	scheduler->threads.priorities = calloc(1, sizeof(int *) * (QUEUE_MAX_SIZE << 1));

	if (!scheduler->threads.priorities)
		error_handler("calloc failed");
}

// function that initializes the current thread

void init_current_thread() 
{
	scheduler->current_thread = NULL;
	scheduler->FLAG = FALSE;
}

// function that changes the thread's status

void change_thread_status(thread_t *thr, char string[]) 
{
	if (!strcmp(string, BLOCKED)) {
		memcpy(thr->status, BLOCKED, sizeof(BLOCKED));
	} else if (!strcmp(string, READY)) {
		memcpy(thr->status, READY, sizeof(READY));
	} else if (!strcmp(string, NEW)) {
		memcpy(thr->status, NEW, sizeof(NEW));
	} else if (!strcmp(string, RUNNING)) {
		memcpy(thr->status, RUNNING, sizeof(RUNNING));
	} else if (!strcmp(string, TERMINATED)) {
		memcpy(thr->status, TERMINATED, sizeof(TERMINATED));
	}
}

void so_start_queue() 
{
	scheduler->pqueue.buff[scheduler->pqueue.size - 1]->T_FLAG = FALSE;
	scheduler->pqueue.buff[scheduler->pqueue.size - 1] = NULL;
	scheduler->pqueue.size--;
}

static void so_start(thread_t *thr) 
{
	if (thr == NULL)
		return;
	so_start_queue();
	change_thread_status(thr, RUNNING);
	thr->thr_quantum = scheduler->quantum;
	thr->T_FLAG = FALSE;
	int s = sem_post(&thr->t_sem);
}

// function that pushes a thread into the queue

void push_pqueue(int pos, thread_t *thr) 
{
	if (thr != NULL && scheduler->pqueue.size < scheduler->pqueue.max_size) {
		scheduler->pqueue.buff[pos] = thr;
		scheduler->pqueue.size++;
	}
}


int find_pos(thread_t *thr) 
{
	int pos = 0;
	unsigned int pqueue_size = scheduler->pqueue.size;
	thread_t *queue_thread = scheduler->pqueue.buff[pos];

	while (pos < pqueue_size && thr->thread_priority > queue_thread->thread_priority) {
		pos++;
		queue_thread = scheduler->pqueue.buff[pos];
	}
	return pos;
}

// adding the thread into the queue

static void so_register(thread_t *thr)
{
	if (!thr)
		error_handler("thread does not exist");

	int pos = 0;
	pos = find_pos(thr);

	size_t size = scheduler->pqueue.size;

	for (size_t i = size; i > pos; i--)
		scheduler->pqueue.buff[i] = scheduler->pqueue.buff[i - 1];
	
	push_pqueue(pos, thr);
	change_thread_status(scheduler->pqueue.buff[pos], READY);
	thr->T_FLAG = FALSE;
}

// peek function for the queue

thread_t* queue_peek_at(unsigned int pos) 
{
	thread_t *thr;
	if (pos <= scheduler->pqueue.size)
		thr = scheduler->pqueue.buff[pos];
	return thr;
}

int so_check_update_next(unsigned int current_priority, unsigned int next_priority) 
{
	if (current_priority == next_priority) 
		return 0;
	if (current_priority < next_priority)
		return 2;
	return 1;
}

// function that updates the scheduler and resets the thread's quantum

void so_update()
{
	int s;
	thread_t *next, *current = scheduler->current_thread;
	
	if (scheduler->pqueue.size == 0) {
		sem_post(&current->t_sem);
		return;

	} 
	next = queue_peek_at(scheduler->pqueue.size - 1);


	if (current == NULL || !strcmp(current->status, BLOCKED)
		       	|| !strcmp(current->status, TERMINATED)) {
		scheduler->current_thread = next;
		so_start(next);
		return;
	}

	int check = so_check_update_next(current->thread_priority, next->thread_priority);
	int priorities_check = 0;

	
       	if (current->thr_quantum < 1) {
		int do_next = 0;
		if (so_check_update_next(current->thread_priority, next->thread_priority) == 0) {
			so_register(current);
			scheduler->current_thread = next;
			so_start(next);
			do_next = 1;
		}
		if (do_next == 1) {
			current->thr_quantum = scheduler->quantum;
			priorities_check = 1;
		}
		current->thr_quantum = scheduler->quantum;
	}


	if (check == 2) {
		so_register(current);
		scheduler->current_thread = next;
		so_start(next);
		scheduler->FLAG = TRUE;
		priorities_check = 1;

	}

	if (priorities_check == 1) {
		next->T_FLAG = FALSE;
		return;

	}

	sem_post(&current->t_sem);
}

int so_init(unsigned int time_quantum, unsigned int io) 
{
	int value = check_if_is_initialized(is_initialized);

	if (value == -1) {
		return -1;
	} else {
		if (time_quantum <= 0 || io > SO_MAX_NUM_EVENTS)
			return -1;
		is_initialized = 1;

		scheduler = calloc(1, sizeof(scheduler_t));
		if (!scheduler)
			error_handler("calloc failed");

		scheduler->max_events = io;
		scheduler->quantum = time_quantum;
		q_create();
		init_current_thread();

	}
	return 0;
}

// checking the parameters of the so_fork function

int check_so_fork_params(so_handler *func, unsigned int priority) 
{
	if (func == NULL)
		return -1;
	if (priority > SO_MAX_PRIO)
		return -1;
	return 1;
}

// function that creates a thread and initializez its fields

thread_t *thread_create(so_handler *func, unsigned int priority) 
{
	thread_t *thr = calloc(1, sizeof(thread_t));

	if (!thr)
		error_handler("calloc failed");

	memcpy(thr->status, NEW, sizeof(NEW));

	thr->device = SO_MAX_NUM_EVENTS;
	thr->func = func;
	thr->thread_priority = priority;
	thr->tid = -1;
	thr->thr_quantum = scheduler->quantum;
	thr->T_FLAG = FALSE;

	int pshared = 0, value = 0;
	int s = sem_init(&thr->t_sem, pshared, value);

	if (s)
		error_handler("sem init failed");
	s = pthread_attr_setscope(&attr, PTHREAD_SCOPE_PROCESS);
	
	glob = 1;
	s = pthread_cond_signal(&cond);

	return thr;
}

void so_fork_action() 
{
	if (scheduler->FLAG == TRUE) {
		so_exec();
	} else if (scheduler->FLAG == FALSE)
		so_update();
}

// push function for the queue

void push(thread_t *thr, int flag) 
{
	if (flag == 0) {
		if (scheduler->threads.size < scheduler->threads.max_size)
			scheduler->threads.buff[scheduler->threads.size++] = thr;
	}
}

tid_t so_fork(so_handler *func, unsigned int priority)
{
	int s;
	thread_t *new_thread;
	int valid = check_so_fork_params(func, priority);
	if (valid == -1)
		return INVALID_TID;
	
	// creating a new thread

	new_thread = thread_create(func, priority);
	if (!new_thread) {
		perror("calloc failed");
		return INVALID_TID;
	}

	pthread_attr_t attr;
	s = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	int ps = pthread_create(&new_thread->tid, NULL, &thread_func, (void*)new_thread);
	if (ps)
		error_handler("pthread create failed");

	push(new_thread, 0);
	so_register(new_thread);

	if (scheduler->current_thread == NULL) {
		scheduler->FLAG = FALSE;
	} else {
		scheduler->FLAG = TRUE;
	}
	so_fork_action();
	return new_thread->tid;
}

void so_exec() 
{
	size_t flag = 0;

	if (scheduler->FLAG == FALSE)
		flag = -1;
	if (scheduler->pqueue.size >= scheduler->pqueue.max_size)
		flag = -1;

	if (flag == -1) {
		perror("could not perform so_exec() function");
		return;
	}
		
	// decreasing how much time left the current thread has
	thread_t *thr = scheduler->current_thread;
	thr->thr_quantum--;
	so_update();
	int s = sem_wait(&thr->t_sem);

	if (s)
		error_handler("sem wait failed");
}

// checking the parameters for the wait function

int check_so_wait_params(unsigned int io) 
{
	if (io < 0)
		return -1;
	if (io >= scheduler->max_events)
		return -1;
	return 0;
}


int so_wait(unsigned int io) 
{
	int valid = check_so_wait_params(io);
	if (valid == -1) {
		return -1;
	} else {
		if (scheduler->FLAG == FALSE) {
			perror("could not execute so_wait function");
			return -1;
		}
		
		// changing the status of the thread

		change_thread_status(scheduler->current_thread, BLOCKED);
		scheduler->current_thread->device = io;
		so_exec();
	}
	return 0;
}

// verifying the parameters of the so_signal function

int check_so_signal_params(unsigned int io) 
{
	if (io < 0)
		return -1;
	if (io >= scheduler->max_events)
		return -1;
	return 0;
}

int check_valid_threads_signal(thread_t *thr, unsigned int io, unsigned int is_not_blocked)
{
	// using the flag of the thread to mark the blocked threads
	int do_next = 0;

	if (is_not_blocked == 0)
		thr->T_FLAG = TRUE;
	if (thr->T_FLAG == TRUE && thr->device == io)
		do_next = 1;
	return do_next;
}

int so_signal(unsigned int io)
{
	int cnt = 0;
	
	int valid = check_so_signal_params(io);

	if (valid == -1)
		return -1;

	size_t size = scheduler->threads.size;

	for (size_t i = 0; i < size; i++) {
		thread_t *thr = scheduler->threads.buff[i];
		unsigned int is_not_blocked = strcmp(thr->status, BLOCKED);
	
		// checking whether the threads are not blocked and are valid

		int next = check_valid_threads_signal(thr, io, is_not_blocked);

		if (next == 1) {
			thr->device = SO_MAX_NUM_EVENTS;
			change_thread_status(thr, READY);
			// adding the thread
			so_register(thr);
			cnt++;
		} else
			continue;
	}
	so_exec();
	return cnt;
}

// function that clears the queue from memory

void queue_clear(void)
{
	size_t thr_size = scheduler->threads.size;
	
	for (size_t i = 0; i < thr_size; i++)
		free(scheduler->threads.buff[i]);

	size_t q_size = scheduler->pqueue.size;

	for (size_t i = 0; i < q_size; i++) {
		int s = pthread_attr_destroy(&attr);

		free(scheduler->pqueue.buff[i]);
	}
	free(scheduler->threads.priorities);	
	free(scheduler->pqueue.priorities);
}

// function that is called after the so_init function, frees everything from memory

void so_end(void)
{
	int s;
	if (scheduler == NULL)
		return;

	for (size_t i = 0; i < scheduler->threads.size; i++)
		s = pthread_join(scheduler->threads.buff[i]->tid, NULL);

	queue_clear();
	is_initialized = 0;
	free(scheduler);
	scheduler = NULL;
}


//  creating a thread function after initializing one

static void *thread_func(void *args)
{
	thread_t *thr = (thread_t *)args;
	int s = sem_wait(&thr->t_sem);

	thr->func(thr->thread_priority);
	change_thread_status(thr, TERMINATED);
	thr->T_FLAG = TRUE;
	so_update();
	return NULL;
}

