#include "syscall.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"

uint64 sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d va = %x, len = %d", fd, va, len);
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

uint64 sys_gettimeofday(TimeVal *val, int _tz) // TODO: implement sys_gettimeofday in pagetable. (VA to PA)
{
    //get current process
	struct proc *p = curr_proc();

    //get page for user program, return error if it doesnt exist
    uint64 pa = useraddr(p->pagetable, (uint64)val);
    
    if (pa == 0)
        return -1;

    //get safe pointer to time value 
    TimeVal *kptr = (TimeVal *)pa;

    //update based on system
    uint64 cycle = get_cycle();
    kptr->sec = cycle / CPU_FREQ;
    kptr->usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;

    return 0;
}

// TODO: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)
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


/*
* LAB1: you may need to define sys_task_info here
*/
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
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday((TimeVal *)args[0], args[1]);
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
