#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/fixed_point.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

//project 5
int load_avg;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

//project 1
static struct list sleep_list;//slepp중인 thread가 들어갈 sleep리스트
static int64_t next_tick_to_awake; // 다음에 깨어날 list의 wakeup_tick값(최소값)을 저장할 변수

//project 5
static struct list all_list; //모든 스레드 모음

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
   //main()을 실행하고 있는 현재의 실행흐름을 하나의 커널스레드로 생각하고 초기화 한다
   //최초의 커널쓰레드
   //구조체가 여기서 초기화
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&destruction_req);

	//project 1
	list_init(&sleep_list);

	//project 5
	list_init(&all_list);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
//idle 스레드 만들기 인터럽트 활성화
//idel 스레드 ->아무것도 하지않는 스레드 -->ready queue가 비어있을 때 idle thread가 동작하므로써 cpu가 무조건 하나의 커널 스레드를 실행시키는 상태를 유지하게 만들어준다
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	//세마포어 1->0 만들고
	sema_init (&idle_started, 0);

	thread_create ("idle", PRI_MIN, idle, &idle_started);
	//크리에이트 하는 동안 보호(idle함수에서 다시 세마포어 1로 만듦)

	//project 5
	load_avg = LOAD_AVG_DEFAULT;

	/* Start preemptive thread scheduling. */
	intr_enable ();//인터럽트 활성화->인터럽트가 활성화 되어야만 스케줄링이 작동할 수 있음

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started); //sema값을 1감소(이미 0이라면 실행을 멈추고 sema가 1이 될 때까지 기다림)
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
//타이머 이터럽트가 발생하면 thread_tick이 호출되며 현재 스레드가 실행 된 후 타이머 인터럽트가 발생한 횟수를 센다.
//그 횟수가 4번 이상이면 intr_yield_on_return을 호출하여 인터럽트를 완료 후 thread_yeild를 호출되게 하여 다른 스레드가 실행 될 수 있도록 한다.
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
//프로세스 디스크립터 생성 및 초기화
//페이지 테이블을 저장하기 위한 메모리 할당
//커널 스택 할당 후 커널 스레드가 수행할 함수를 등록
//커널 스레드를 run queue에 추가
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO); //스레드를 위한 메모리 할당-4kb
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority); //스레드 구조체 초기화
	tid = t->tid = allocate_tid ();// tid할당

	//여기가 스택인가
	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. */
	//ready queue에 넣는다
	thread_unblock (t);

	//project 2
	if (t->priority > thread_current() -> priority){//새롭게 만들어진 스레드의 우선순위가 높다면 양보해라
		thread_yield();
	}

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
//인터럽트가 꺼진 상태에서 호출 이게 끝나면 인터럽트 킨다
//스레드 실행을 중지
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	// list_push_back (&ready_list, &t->elem); //리스트의 맨 뒤에 넣음(round-robin방식)
	
	//project 2
	list_insert_ordered(&ready_list, &t->elem, cmp_priority, NULL);//우선순위로 정렬 왜 여기서는 양보 안해주지???잘 모르겠네

	t->status = THREAD_READY;
	intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
//현재 스레드를 끝낸다
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	//project 5
	list_remove(&thread_current()->allelem);
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
//schedule에 의해 다시 실행 될 때까지 스레드를 대기시키는 함수
// 현재 스레드를 비활성화 시키고 ready_list에 삽입한다.
//들어오는 모든 인터럽트 무시함 
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread){
		// list_push_back (&ready_list, &curr->elem);

		//project 2
		list_insert_ordered(&ready_list, &curr->elem, cmp_priority, NULL);
	}
	//스케쥴이 리턴 된 훟 인터럽트를 원래대로 돌린다
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
//Sets the current thread's priority to new priority. If the current thread no longer has the highest priority, yields.
//스레드의 우선순위가 변경되었을 때 우선순위에 따라 선점이 발생하도록 한다
void
thread_set_priority (int new_priority) { // 유저
	// thread_current ()->priority = new_priority;
	//project 2
	// if(!thread_mlfqs){///현재는 다단계큐이니까 thread_mlfqs는 다단계 피드백큐일때? 사실 잘 모르겠다
	// 	int previous_priority = thread_current()->priority; //현재꺼를 pre로
	// 	thread_current()->priority = new_priority;//new를 현재로 바꾼다

	// 	if(thread_current()->priority < previous_priority) // 순위가 내려갔다면
	// 		test_max_priority(); //readylist의 스레드들과 우선순위를 비교할 수 있도록 한다
	// }

	//project 5
	if(thread_mlfqs){
		return ;
	}

	//project 4
	thread_current() -> init_priority = new_priority;
    refresh_priority();
    test_max_priority();
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started); //세마포어 업

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
//이게 실행하는 건가?
//새로 생성된 스레드의 경우 scheule()에서 리턴 된 것 같으므로 인터럽트를 다시 켠다/
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* Because The scheduler runs with interrupts off. */ //인터럽트를 킨다
	function (aux);       /* Execute the thread function. */ //어떤 기능을 하는 함수를 실행한다
	thread_exit ();       /* If function() returns, kill the thread. */ // 함수가 끝나면 kerner thread를 종료한다
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;

	//project 4
	t->init_priority = priority;
	t->wait_on_lock = NULL;
	list_init(&t->donations);

	//project 5
	t->nice = NICE_DEFAULT;
	t->recent_cpu = RECENT_CPU_DEFAULT;
	list_push_back(&all_list, &t->allelem);

	//project 7
	#ifdef USERPROG
	sema_init(&(t->child_lock), 0);
	sema_init(&(t->mem_lock), 0);
	list_init(&(t->child));
	list_push_back(&(running_thread()->child), &(t->child_elem));
	#endif
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list)) 
		return idle_thread;//비어 있을 때는 idle
	else //아니면 맨앞에서 꺼냄
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
//새로운 스레드가 기동함에 따라 context switching을 수행한다
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
//현재 스레드를 status로 바꾸고 스케줄 실행 - 이거 실행 전에 무조건 인터럽트 끈다
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

//running스레드를 빼내고 next스레드를 running시킨다
static void
schedule (void) {
	struct thread *curr = running_thread (); //현재
	struct thread *next = next_thread_to_run (); //다음꺼(없으면 idle)

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used bye the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}

//project 1
void
thread_sleep (int64_t ticks){
	struct thread *this;
	this = thread_current();

	if(this == idle_thread){// 아이들이면 멈춰라(잘못된것)
		ASSERT(0);
	}
	else{
		enum intr_level old_level;
		old_level = intr_disable(); // 인터럽트 막는다

		update_next_tick_to_awake(this->wakeup_tick = ticks); //깨어나야할 스레드의 tick값 갱신

		list_push_back(&sleep_list, &this->elem); //sleep리스트에 넣기

		thread_block(); //지금 쓰레드 블락하고

		intr_set_level(old_level); //continue interrupt 인터럽트 다시받기
	}
}

//project 1
void
thread_awake (int64_t wakeup_tick){
	next_tick_to_awake = INT64_MAX; //초기화하고

	struct list_elem *sleeping;
	sleeping = list_begin(&sleep_list);//헤드를 sleeping변수로 가져온다

	while(sleeping != list_end(&sleep_list)){//sleep리스트를 순회하며 꺠워야 할 스레드를 sleep리스트에서 제거 하고 unblock한다
		struct thread *th = list_entry(sleeping, struct thread, elem);	//take sleeping thread 이것도 잘 모르겠음^^

		if(wakeup_tick >= th->wakeup_tick){ // 시간이 됐으면
			sleeping = list_remove(&th->elem);//sleep리스트에서 삭제하고
			thread_unblock(th);//블락을 푼다
		}else{
			sleeping = list_next(sleeping);//다음거로 이동
			update_next_tick_to_awake(th->wakeup_tick);// awake를 바꾸고
		}
	}
}

//project 1
void
update_next_tick_to_awake(int64_t ticks){
	next_tick_to_awake = (next_tick_to_awake >ticks)? ticks :  next_tick_to_awake; //작은 tick을 찾는다
}

//project 1
int64_t
get_next_tick_to_awake(void){
	return next_tick_to_awake;
}

//project 2
void
test_max_priority(void){
	struct thread *cp = thread_current();
	struct thread *first_thread;

	if(intr_context() || list_empty(&ready_list))
		return;

	first_thread = list_entry(list_front(&ready_list), struct thread, elem);

	if(cp->priority < first_thread ->priority){ //현재와 첫번째꺼중 비교해서 현재가 더 높으면 양보한다
		thread_yield();
	}
}

//project 2
bool
cmp_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED){
	struct thread *thread_a = list_entry(a, struct thread , elem);
	struct thread *thread_b = list_entry(b, struct thread , elem);

	return thread_a -> priority > thread_b ->priority;

	// if(thread_a != NULL && thread_b != NULL){
	// 	if(thread_a->priority > thread_b->priority){
	// 		return true;
	// 	}else{
	// 		return false;
	// 	} 
	// }
	// return false; //여기는 왜 false를 줄까?
}

//project 4
void donate_priority(void){
	int depth;
	struct thread *cur = thread_current();

	for (depth =0; depth < 8; depth++){//타고타고 가서 순위 다 올려줘야함
		if(!cur -> wait_on_lock)	break; //lock안걸려있음
		struct thread *holder = cur->wait_on_lock -> holder;
		holder->priority = cur->priority;
		cur = holder;
	}
}

//project 4
//lock를 해지 했을 때 donations리스트에서 해당 엔트리를 삭제하기 위한 함수
void remove_with_lock(struct lock *lock){
	struct list_elem *e;
	struct thread *cur = thread_current();

	for(e = list_begin(&cur->donations); e !=list_end(&cur->donations); e = list_next(e)){ //이걸 왜 다 도는지 모르겠어
		struct thread *t = list_entry(e, struct thread, donation_elem);
		if (t->wait_on_lock == lock){
			list_remove(&t->donation_elem);
		}
	}
}

//project 4
void refresh_priority(void){
	struct thread *cur = thread_current();
	cur->priority = cur->init_priority;
	if (!list_empty(&cur->donations))
	{
		list_sort(&cur->donations, &cmp_priority, NULL);
		struct thread *high;
		high = list_entry(list_front(&cur->donations), struct thread, donation_elem);
		if (high->priority > cur->priority)
		{
			cur->priority = high->priority;
		}
	}
}

//project 5
void mlfqs_priority(struct thread *t){
	if (t == idle_thread)	return ; //idle이면 안해도 돼
	//priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)
  	t->priority = fp_to_int (add_mixed (div_mixed (t->recent_cpu, -4), PRI_MAX - t->nice * 2));
}

//project 5
void mlfqs_recent_cpu(struct thread *t){
	if (t == idle_thread) 	return ;//idle이면 안해도 돼
	//recent_cpu = (2 * load_avg) / (2 * load_avg + 1) * recent_cpu + nice
  	t->recent_cpu = add_mixed (mult_fp (div_fp (mult_mixed (load_avg, 2), add_mixed (mult_mixed (load_avg, 2), 1)), t->recent_cpu), t->nice);
}

//project 5
void mlfqs_load_avg(void){
	int ready_threads;

	if(thread_current() == idle_thread){
		ready_threads = list_size(&ready_list);
	}else{
		ready_threads = list_size(&ready_list)+1;
	}
	//load_avg = (59/60) * load_avg + (1/60) * ready_threads
	load_avg = add_fp(mult_fp(div_fp(int_to_fp(59), int_to_fp(60)), load_avg), 
		mult_mixed(div_fp(int_to_fp(1), int_to_fp(60)),ready_threads));
}

//project 5
void mlfqs_increment(void){
	if(thread_current() != idle_thread){
		thread_current()->recent_cpu = add_mixed(thread_current()->recent_cpu, 1);
	}
}

//project 5
void mlfqs_recalc_recent_cpu(void){
	struct list_elem *e;
	for(e = list_begin(&all_list); e!=list_end(&all_list); e = list_next(e)){
		struct thread *t = list_entry(e, struct thread, allelem);
		mlfqs_recent_cpu(t);
	}
}

//project 5
void mlfqs_recalc_priority(void){
	struct list_elem *e;
	for(e = list_begin(&all_list); e!=list_end(&all_list); e = list_next(e)){
		struct thread *t = list_entry(e, struct thread, allelem);
		mlfqs_priority(t);
	}
}

//project 5
//현재 스레드의 nice값을 반환
int thread_get_nice(void){	
	enum intr_level old_level = intr_disable();
	int nice = thread_current()->nice;
	intr_set_level(old_level);
	return nice;
}

//project 5
//현재 스레드의 nice값을 새 값으로 설정
void thread_set_nice(int nice UNUSED){
	enum intr_level old_level = intr_disable();
	thread_current()->nice = nice;
	mlfqs_priority(thread_current());
	test_max_priority();
	intr_set_level(old_level);
}

//project 5
int thread_get_load_avg(void){
	enum intr_level old_level = intr_disable();
	int load_avg_value = fp_to_int_round(mult_mixed(load_avg, 100));
	intr_set_level(old_level);
	return load_avg_value;
}

//project 5
int thread_get_recent_cpu(void){
	enum intr_level old_level = intr_disable();
	int recent_cpu= fp_to_int_round(mult_mixed(thread_current()->recent_cpu, 100));
	intr_set_level(old_level);
	return recent_cpu;
}