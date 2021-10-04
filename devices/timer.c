#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

static struct list sleep_list; //project 1

/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. */
void
timer_init (void) {
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
	   nearest. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* Approximate loops_per_tick as the largest power-of-two
	   still less than one timer tick. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
// 들어온 인자 시간으로 부터 얼마나 시간이 지났는지 반환
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}

/* Suspends execution for approximately TICKS timer ticks. */
// 현재 스레드를 ticks만큼 재우는 역할(sleep(ticks))
void
timer_sleep (int64_t ticks) {
	int64_t start = timer_ticks ();

	ASSERT (intr_get_level () == INTR_ON);

/* 원래 코드
	//busy waiting 방식을 사용
	// sleep중인 스레드가 계속해서 cpu 점유
	while (timer_elapsed (start) < ticks)//시간이 덜 됐으면
		thread_yield ();
		//위 함수가 호출되면 해당 함수를 호출 한 스레드는 다른스레드에게 cpu를 양보한다. 
		//그리고 실행 될 준비가 되어있는 스레드들이 존재하는 ready_list에 들어가게 된다.
		//일어날 때까지 계속계속 확인하는 것
		//이는 쓸모없는 context switching이 많이 일어난다는 것이고 cpu를 많이 점유하고 있는 것이다.
		//그래서 이를 한번만 일어나도록 하는게 목표이다.
		//->이를 방지하기 위해 완전히 sleep상태에 들어가 스레드를 저장할 sleep_list를 구현한다
		//sleep함수를 호출하면 해당 스레드를 sleep_list에 넣고 이 스레드를 깨워야 할 때 thread_awake() 함수를 호출한다
		//깨어난 스레드를 다시 ready_list로 옮기는 식으로 구현하면 sleep_list에 있는 스레드는 cpu를 점유하지 않고 자신이 꺠어나야 할 때를까지 기다릴 수 있다.
*/
	//project 1
	thread_sleep(start+ticks);
}

/* Suspends execution for approximately MS milliseconds. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Timer interrupt handler. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;
	thread_tick ();

	//project 5
	if(thread_mlfqs){
		mlfqs_increment();
		if(ticks%4 == 0){
			mlfqs_recalc_priority();
			if(ticks % TIMER_FREQ == 0){
				mlfqs_recalc_recent_cpu();
				mlfqs_load_avg();
			}
		}
	}

	//project 1
	//매 tick마다 sleep리스트를 체크하며 깨어날 스레드가 있는지 확인
	if(get_next_tick_to_awake() <= ticks){
		thread_awake(ticks);
	}	
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops) {
	/* Wait for a timer tick. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* Run LOOPS loops. */
	start = ticks;
	busy_wait (loops);

	/* If the tick count changed, we iterated too long. */
	barrier ();
	return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
//스레드를 양보한다 자고있는 스레드들을 ready_list에 넣지 말고 다른 곳에 넣어뒀다가 매 tick마다 거기서 일어날 때가 된 애들을 꺠워서 readylist에 넣어줌
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* Convert NUM/DENOM seconds into timer ticks, rounding down.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* We're waiting for at least one full timer tick.  Use
		   timer_sleep() because it will yield the CPU to other
		   processes. */
		timer_sleep (ticks);
	} else {
		/* Otherwise, use a busy-wait loop for more accurate
		   sub-tick timing.  We scale the numerator and denominator
		   down by 1000 to avoid the possibility of overflow. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
