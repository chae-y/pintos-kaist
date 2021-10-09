#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void check_address(void *addr);

void halt(void);
int write(int fd, const void *buffer, unsigned size);
void exit(int status);
// bool create (const char *file, unsigned initial_size); 

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	//project 6
	// printf("--------------syscall: %d-------------\n", f->R.rax);
	switch(f->R.rax){
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			// thread_exit();
			exit(f->R.rdi);
			break;
		case SYS_FORK:
			// f->R.rax = process_fork(f->R.rdi, f);
			break;
		case SYS_EXEC:
			// if(exec(f->R.rdi) == -1)
				// exit(-1);
			break;
		case SYS_WAIT:
			break;
		case SYS_CREATE:
			// check_address(f->R.rdi);
			// f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			// f->R.rax = remove(f->R.rdi);
			break;
		case SYS_OPEN:
			// f->R.rax = open(f->R.rdi);
			break;	
		case SYS_FILESIZE:
			// f->R.rax = filesize(f->R.rdi);
			break;
		case SYS_READ:
			// f->R.rax = write(f->R.rdi);
			break;
		case SYS_WRITE:
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			// seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			// f->R.rax = tell(f->R.rsi);
			break;
		case SYS_CLOSE:
			// exit(-1);
			break;
	}
	// SYS_HALT,                   /* Halt the operating system. */
	// SYS_EXIT,                   /* Terminate this process. */
	// SYS_FORK,                   /* Clone current process. */
	// SYS_EXEC,                   /* Switch current process. */
	// SYS_WAIT,                   /* Wait for a child process to die. */
	// SYS_CREATE,                 /* Create a file. */
	// SYS_REMOVE,                 /* Delete a file. */
	// SYS_OPEN,                   /* Open a file. */
	// SYS_FILESIZE,               /* Obtain a file's size. */
	// SYS_READ,                   /* Read from a file. */
	// SYS_WRITE,                  /* Write to a file. */
	// SYS_SEEK,                   /* Change position in a file. */
	// SYS_TELL,                   /* Report current position in a file. */
	// SYS_CLOSE,                  /* Close a file. */

}

//주소 유효성 검사- 포인터가 가리키는 주소가 사용자 영역인지 확인
void check_address(void *addr){
	if(!is_user_vaddr(addr)){
		exit(-1);
	}

}

//pintos를 종료시키는 시스템 콜
void halt(void){
	power_off();
}
int write (int fd, const void *buffer, unsigned size){
	if(fd == 1){
		putbuf(buffer, size);
		return size;
	}

	return -1;
}

void exit(int status){
	printf ("%s: exit(%d)\n", thread_name(), status);
	thread_exit();
}

