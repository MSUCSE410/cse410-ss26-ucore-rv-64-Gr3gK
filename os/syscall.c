#include "syscall.h"
#include "console.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"

uint64 sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d str = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
}

uint64 sys_read(int fd, uint64 va, uint64 len)
{
	debugf("sys_read fd = %d str = %x, len = %d", fd, va, len);
	if (fd != STDIN)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	for (int i = 0; i < len; ++i) {
		int c = consgetc();
		str[i] = c;
	}
	copyout(p->pagetable, va, str, len);
	return len;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(uint64 val, int _tz)
{
	struct proc *p = curr_proc();
	uint64 cycle = get_cycle();
	TimeVal t;
	t.sec = cycle / CPU_FREQ;
	t.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	copyout(p->pagetable, val, (char *)&t, sizeof(TimeVal));
	return 0;
}

uint64 sys_mmap(uint64 start, uint64 len, int port, int flags, int fd)
{
    //get current process
    struct proc *p = curr_proc();
    //return directly
    if (len == 0)
        return 0;

    //check for size make sure its less than 1gb
    uint64 one_GB = 1ULL << 30;
    if (len > one_GB)
        return -1;
    //check permissions or ports
    /*
        bit 0 indicates whether it is readable, bit 1 indicates whether it is
        writable, and bit 2 indicates whether it is executable. Other bits are invalid
        (must be 0)
    */
    if ((port & ~0x7) != 0)
        return -1;
    if ((port & 0x7) == 0)
        return -1;
    //check for page alignment
    if (start % PGSIZE != 0)
        return -1;
    
    //get start and end of vritual adress page
    uint64 va_start = start;
    uint64 va_end   = PGROUNDUP(start + len);
    //check if page has already been mapped
    for (uint64 va = va_start; va < va_end; va += PGSIZE) {
        if (useraddr(p->pagetable, va) != 0)
            return -1;  
    }
    //check bit flag and assign permissions
    int perm = PTE_U;
    if (port == 1) {
        perm = PTE_U | PTE_R;
    } 
    else if (port == 2) {
        perm = PTE_U | PTE_W;
    } 
    else if (port == 3) {
        perm = PTE_U | PTE_R | PTE_W;
    } 
    else if (port == 4) {
        perm = PTE_U | PTE_X;
    } 
    else if (port == 5) {
        perm = PTE_U | PTE_R | PTE_X;
    } 
    else if (port == 6) {
        perm = PTE_U | PTE_W | PTE_X;
    } 
    else if (port == 7) {
        perm = PTE_U | PTE_R | PTE_W | PTE_X;
    }
    //for loop at map va to pa
    for (uint64 va = va_start; va < va_end; va += PGSIZE) {
        //allocate memory
        char *pa = kalloc();
        //if no pa 
        if (!pa) {
            return -1;
        }
        //zero out page
        memset(pa, 0, PGSIZE);
        
        //if mapping fails return error
        if (mappages(p->pagetable, va, PGSIZE, (uint64)pa, perm) != 0) {
            return -1;
        }
    }
    return 0;
}
uint64 sys_munmap(uint64 start, uint64 len)
{   
    //get current process
    struct proc *p = curr_proc();
    //return error for len 0
    if (len == 0)
        return -1;
    uint64 one_GB = 1ULL << 30;
    if (len > one_GB)
        return -1;
    //check start and legnth compared to page size
    if ((start % PGSIZE != 0) || (len % PGSIZE != 0))
        return -1;
    //get number of pages
    uint64 npages = ((start + len) - start) / PGSIZE;
    //unmap pages
    if (npages > 0)
        uvmunmap(p->pagetable, start, npages, 1);
    return 0;
}


uint64 sys_task_info(struct TaskInfo *ti)
{
    //get curr process
	struct proc *p = curr_proc();
    //get user program address
	uint64 pa = useraddr(p->pagetable, (uint64)ti);
    
    //doesnt exist throw error
    if (pa == 0)
        return -1;
    //create pointer to process for kernel
    struct TaskInfo *kptr = (struct TaskInfo *)pa;
    //update status 
    if (p->state == RUNNING)
        kptr->status = Running;
    else if (p->state == RUNNABLE)
        kptr->status = Ready;
    else if (p->state == UNUSED || p->state == ZOMBIE)
        kptr->status = Exited;
    else
        kptr->status = UnInit;
    //update syscall times
    for (int i = 0; i < MAX_SYSCALL_NUM; i++)
        kptr->syscall_times[i] = p->syscall_times[i];
    //add time for task
    kptr->time = (get_cycle() / (CPU_FREQ / 1000)) - p->start_time;
    return 0;
}


uint64 sys_getpid()
{
	return curr_proc()->pid;
}

uint64 sys_getppid()
{
	struct proc *p = curr_proc();
	return p->parent == NULL ? IDLE_PID : p->parent->pid;
}

uint64 sys_clone()
{
	debugf("fork!\n");
	return fork();
}

uint64 sys_exec(uint64 va)
{
	struct proc *p = curr_proc();
	char name[200];
	copyinstr(p->pagetable, name, va, 200);
	debugf("sys_exec %s\n", name);
	return exec(name);
}

uint64 sys_wait(int pid, uint64 va)
{
	struct proc *p = curr_proc();
	int *code = (int *)useraddr(p->pagetable, va);
	return wait(pid, code);
}

//chp5 set up syscall function 400 for spawn
uint64 sys_spawn(uint64 va)
{
    //get current process
	struct proc *p = curr_proc();

    //check file name, same as sys_exec
    char name[200];
    if (copyinstr(p->pagetable, name, va, 200) < 0)
        return -1;

    //spawn process
    int pid = spawn(name);
    if (pid < 0)
        return -1;

    return pid;
}

//chp5, set priority for process
uint64 sys_set_priority(long long prio)
{
    //get current process
    struct proc *p = curr_proc();

    //check if priority in range, cap it, no zeros or letting a procces run too much
    if (prio > 32 || prio < 2)
        return -1;

    p->priority = prio;

    //big stride is predefine large constant
    int bigStride = 100000;

    //set pass based on priority
    p->pass =  bigStride / prio;

    return prio;
}


extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);

	/*
	* LAB1: you may need to update syscall counter for task info here
	*/
	struct proc *p = curr_proc();
	if (p && id >= 0 && id < MAX_SYSCALL_NUM)
		p->syscall_times[id]++;
	
		
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_read:
		ret = sys_read(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday(args[0], args[1]);
		break;
	case SYS_getpid:
		ret = sys_getpid();
		break;
	case SYS_getppid:
		ret = sys_getppid();
		break;
	case SYS_clone: // SYS_fork
		ret = sys_clone();
		break;
	case SYS_execve:
		ret = sys_exec(args[0]);
		break;
	case SYS_wait4:
		ret = sys_wait(args[0], args[1]);
		break;
	case SYS_spawn:
		ret = sys_spawn(args[0]);
		break;
    
    //chp5
    case SYS_setpriority:
        ret = sys_set_priority(args[0]);
        break;


	/*
	* LAB1: you may need to add SYS_taskinfo case here
	*/
	case SYS_taskinfo:
		ret = sys_task_info((struct TaskInfo *)args[0]);
		break;

	case SYS_mmap:
		ret = sys_mmap(args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_munmap:
		ret = sys_munmap(args[0], args[1]);
		break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
