#include "syscall.h"
#include "console.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"

uint64 console_write(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	tracef("write size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return len;
}

uint64 console_read(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	tracef("read size = %d", len);
	for (int i = 0; i < len; ++i) {
		int c = consgetc();
		str[i] = c;
	}
	copyout(p->pagetable, va, str, len);
	return len;
}

uint64 sys_write(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_write(va, len);
	case FD_INODE:
		return inodewrite(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
}

uint64 sys_read(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_read(va, len);
	case FD_INODE:
		return inoderead(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
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
	debugf("fork!");
	return fork();
}

static inline uint64 fetchaddr(pagetable_t pagetable, uint64 va)
{
	uint64 *addr = (uint64 *)useraddr(pagetable, va);
	return *addr;
}

uint64 sys_exec(uint64 path, uint64 uargv)
{
	struct proc *p = curr_proc();
	char name[MAX_STR_LEN];
	copyinstr(p->pagetable, name, path, MAX_STR_LEN);
	uint64 arg;
	static char strpool[MAX_ARG_NUM][MAX_STR_LEN];
	char *argv[MAX_ARG_NUM];
	int i;
	for (i = 0; uargv && (arg = fetchaddr(p->pagetable, uargv));
	     uargv += sizeof(char *), i++) {
		copyinstr(p->pagetable, (char *)strpool[i], arg, MAX_STR_LEN);
		argv[i] = (char *)strpool[i];
	}
	argv[i] = NULL;
	return exec(name, (char **)argv);
}

uint64 sys_wait(int pid, uint64 va)
{
	struct proc *p = curr_proc();
	int *code = (int *)useraddr(p->pagetable, va);
	return wait(pid, code);
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

//chp5
uint64 sys_spawn(uint64 va)
{
    //get current process
    struct proc *p = curr_proc();

    //check invalid file name by copying instr, check if safe from user space to kernel space
    char name[200];
    if (copyinstr(p->pagetable, name, va, 200) < 0)
        return -1;

    //find program id
	//different stucture
    struct inode *ip = namei(name);
	if (ip == 0) {
		return -1;
	}
	

    //process pool check
    struct proc *np = allocproc();
    if (np == NULL){
		return -1;

	}

	if (init_stdio(np) < 0){
		return -1;

	}
    


	//update loader function
    bin_loader(ip, np);
	iput(ip);

    //set parent of process
    np->parent = p;

    //set runnable, better than fork for overhead and safety to avoid deadlock
    np->state = RUNNABLE;

    //add to queue
    add_task(np);

    return np->pid;
}

//chp5
uint64 sys_set_priority(long long prio){
    
    //check if prio is in range
    if (prio < 2)
        return -1;

    //get process
    struct proc *p = curr_proc();

    //set priority and pass for process
    p->priority = prio;
    p->pass = (1 << 20) / prio;

    return prio;
}


uint64 sys_openat(uint64 va, uint64 omode, uint64 _flags)
{
	struct proc *p = curr_proc();
	char path[200];
	copyinstr(p->pagetable, path, va, 200);
	return fileopen(path, omode);
}

uint64 sys_close(int fd)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d", fd);
		return -1;
	}
	fileclose(f);
	p->files[fd] = 0;
	return 0;
}

//get metadata  about file
int sys_fstat(int fd, uint64 stat) {
    //get current process and file pointer
    struct proc *p = curr_proc();
    struct file *f;

    //stat struct
    Stat statk;

    //fd invalid check
    if (fd < 0 || fd >= FD_BUFFER_SIZE || (f = p->files[fd]) == 0){
        return -1;
    }

    //get inode
    struct inode *ip = f->ip;
    
    //check inode and metadata
    ivalid(ip);

    //add to kernel stat struct
    memset(&statk, 0, sizeof(statk));

    //set statk
    statk.dev = 0;               
    statk.ino = ip->inum; 
    statk.nlink = ip->nlink; 

    //assign mode
    if (ip->type == T_DIR) {
        statk.mode = DIR;
    } 
    else {
        statk.mode = FILE;
    }

    //check stat adress
    if (copyout(p->pagetable, stat, (char *)&statk, sizeof(statk)) < 0)
        return -1;

    return 0;
}

//chp6 create new link to dir
int sys_linkat(int olddirfd, uint64 oldpath, int newdirfd, uint64 newpath, uint64 flags) {
    

    struct inode *ip;
    struct proc *p = curr_proc();
    char old[MAXPATH];
    char new[MAXPATH];
    

    //copy to kernel memory check
    if (copyinstr(p->pagetable, old, oldpath, MAXPATH) < 0 || copyinstr(p->pagetable, new, newpath, MAXPATH) < 0)
        return -1;

    //check if old file exists
    ip = namei(old);
    if (ip == 0){
        return -1;
    }
        
    //check inode and metadata
    ivalid(ip);

    //check if can link to directory
    if (ip->type == T_DIR) {
        iput(ip);
        return -1;
    }

    //check for same name link
    struct inode *check_exist;
    if ((check_exist = dirlookup(root_dir(), new, 0)) != 0) {
        iput(check_exist);
        iput(ip);
        return -1;
    }

    //add link count and update inode
    ip->nlink++;
    iupdate(ip);

    //add new link
    if (dirlink(root_dir(), new, ip->inum) < 0) {
        ip->nlink--;
        iupdate(ip);
        iput(ip);
        return -1;
    }

    //stop using node
    iput(ip);
    return 0;
}

int sys_unlinkat(int dirfd, uint64 name, uint64 flags) {
    
    
    struct inode *ip;
    struct proc *p = curr_proc();

    char path[MAXPATH];

    //copy to kernel memory check
    if (copyinstr(p->pagetable, path, name, MAXPATH) < 0)
        return -1;

    //check if old file exists
    ip = namei(path);
    if (ip == 0){
        return -1;
    }

    //check inode and metadata
    ivalid(ip);

    //check if can link to directory
    if (ip->type == T_DIR) {
        iput(ip);
        return -1;
    }

    //remove link
    if (dirunlink(root_dir(), path) < 0) {
        iput(ip);
        return -1;
    }

    //subtract from node
    ip->nlink--;
    iupdate(ip);
    iput(ip); 

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

	//chp5
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
	case SYS_openat:
		ret = sys_openat(args[0], args[1], args[2]);
		break;
	case SYS_close:
		ret = sys_close(args[0]);
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
		ret = sys_exec(args[0], args[1]);
		break;
	case SYS_wait4:
		ret = sys_wait(args[0], args[1]);
		break;
	case SYS_fstat:
	    ret = sys_fstat(args[0],args[1]);
		break;
	case SYS_linkat:
	    ret = sys_linkat(args[0],args[1],args[2],args[3],args[4]);
		break;
	case SYS_unlinkat:
	    ret = sys_unlinkat(args[0],args[1],args[2]);
		break;
	case SYS_spawn:
		ret = sys_spawn(args[0]);
		break;

	//chp 5, added for setprio
    case SYS_taskinfo:
        ret = sys_task_info((struct TaskInfo *)args[0]);
        break;
	case SYS_mmap:
		ret = sys_mmap(args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_munmap:
		ret = sys_munmap(args[0], args[1]);
		break;
    case SYS_setpriority:
        ret = sys_set_priority(args[0]);
        break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
