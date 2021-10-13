#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif

#include "threads/synch.h"


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

//project 5
#define NICE_DEFAULT 0
#define RECENT_CPU_DEFAULT 0
#define LOAD_AVG_DEFAULT 0

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */
	int64_t wakeup_tick;	//project 1 
	int init_priority;	//project 4

	//project 5
	int nice; //양보하는 정도(잘 양보하는지)
	int recent_cpu; //최근에 얼마나 많은 cpu 타임을 사용했는지
	struct list_elem allelem;

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */

	//project 4
	struct list donations; //mutiple donation을 고려하기 위해 사용, 자신에게 priority를 나누어준 스레드들의 리스트
	struct lock *wait_on_lock; // 해당 스레드가 대기하고 있는 lock자료구조의 주소를 저장, 스레드가 현재 얻기 위해 기다리고 있는 lock으로 스레드는 이 lock이 release 되기를 기다린다. do
	struct list_elem donation_elem; // mutiple donation을 고려하기 위해 사용

	/* Project 2 */
	// 2-3 Parent-child hierarchy
	struct list child_list;		 // keep children
	struct list_elem child_elem; // used to put current thread into 'children' list
	// 2-3 wait syscall
	struct semaphore wait_sema; // used by parent to wait for child
	int exit_status;			// used to deliver child exit_status to parent
	// 2-3 fork syscall
	struct intr_frame parent_if; // to preserve my current intr_frame and pass it down to child in fork ('parent_if' in child's perspective)
	struct semaphore fork_sema;	 // parent wait (process_wait) until child fork completes (__do_fork)
	struct semaphore free_sema;	 // Postpone child termination (process_exit) until parent receives its exit_status in 'wait' (process_wait)
	// 2-4 file descripter
	struct file **fdTable; // allocation in threac_create (thread.c)
	int fdIdx;			   // an index of an open spot in fdTable
	// 2-5 deny exec writes
	struct file *running; // executable ran by current process (process.c load, process_exit)
	// 2-extra - count the number of open stdin/stdout
	// dup2 may copy stdin or stdout; stdin or stdout is not really closed until these counts goes 0
	int stdin_count;
	int stdout_count;


#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */

#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs; // -mlfqs 옵션이 들어오면 true

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

//project 1
void thread_sleep(int64_t ticks);
void thread_awake(int64_t ticks);
void update_next_tick_to_awake(int64_t ticks);
int64_t get_next_tick_to_awake(void);

//project 2
void test_max_priority(void);
bool cmp_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);//unused가 뭔지 모르겠다

//project 4
void donate_priority(void);
void remove_with_lock(struct lock *lock);
void refresh_priority(void);

//project 5
int thread_get_nice(void);
void thread_set_nice(int);
int thread_get_recent_cpu(void);
int thread_get_load_avg(void);

//project 5
void mlfqs_priority(struct thread *t);
void mlfqs_recent_cpu(struct thread *t);
void mlfqs_load_avg(void);
void mlfqs_increment(void);
void mlfqs_recalc_recent_cpu(void);
void mlfqs_recalc_priority(void);

//project 7
#define FDT_PAGES 3						  // pages to allocate for file descriptor tables (thread_create, process_exit)
#define FDCOUNT_LIMIT FDT_PAGES *(1 << 9) // Limit fdIdx

#endif /* threads/thread.h */
