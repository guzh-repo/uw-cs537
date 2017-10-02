#include "types.h"
#include "x86.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "sysfunc.h"
#include "spinlock.h"

extern struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;
extern int nextpid;
extern void forkret(void);
extern void trapret(void);

// one-argument clone works just like fork();
int sys_clone(void) {
    char *stack_low_addr;
    if (argptr(0, &stack_low_addr, PGSIZE) != 0) return -1;
    // find a place in process table
    struct proc *p = NULL;
    acquire(&ptable.lock);
    int i;
    for (i=0; i<NPROC; i++) {
        if (ptable.proc[i].state == UNUSED) {
            p = &ptable.proc[i];
            break;
        }
    }
    if (p != NULL) {
        p->state = EMBRYO;
        p->pid = nextpid++;
    }
    release(&ptable.lock);
    if (p == NULL) { // proc table full
        return -1;
    }

    // kernal pages layout:
    // - kernel stack bottom
    // - trapframe : hold information for traps, syscall etc
    // - ret_addr  : setup the stack as if caused by some trap, so return to trapret
    // - context   : hold information for context switch, scheduling etc
    if((p->kstack = kalloc()) == 0){
      p->state = UNUSED;
      return -1;
    }
    char* sp = p->kstack + KSTACKSIZE; // stack pointer
    p->state = UNPARKED;

    // Leave room for trap frame.
    sp -= sizeof *p->tf;
    p->tf = (struct trapframe*)sp;

    // Set up new context to start executing at forkret,
    // which returns to trapret.
    sp -= 4;
    *(uint*)sp = (uint)trapret;

    sp -= sizeof *p->context;
    p->context = (struct context*)sp;
    memset(p->context, 0, sizeof *p->context);
    p->context->eip = (uint)forkret;

    // userspace stack
    //cprintf("stack_low_addr = 0x%x\n", stack_low_addr);
    sp = stack_low_addr + PGSIZE;
    sp -= 4;// *(uint*)sp = data_addr; // argument
    sp -= 4;// *(uint*)sp = 0xffffffff; // fake return

    // copy necessary data
    *p->tf = *proc->tf;
    p->tf->esp = (uint)sp;
    p->pgdir = proc->pgdir;
    p->sz = proc->sz;
    p->parent = proc;

    safestrcpy(p->name, proc->name, sizeof(proc->name));
    
    for(i = 0; i < NOFILE; i++)
      if(proc->ofile[i])
        p->ofile[i] = filedup(proc->ofile[i]);
    p->cwd = idup(proc->cwd);

    p->is_thread = 1;
    p->thread_stack = stack_low_addr;
    p->tf->eax = 0;
    //cprintf("eip-p:0x%x esp:0x%x eip-proc:0x%x\n", p->tf->eip, p->tf->esp, proc->tf->eip);
    int pid = p->pid;
    p->state = RUNNABLE;
    return pid;
}

// mostly copied from wait()
int sys_join(void) {
    void **stack_low_addr_container;
    if (argptr(0, (char**)&stack_low_addr_container, 4) != 0) return -1;

    int havekids;
    struct proc *p;
    uint pid;

    acquire(&ptable.lock);
    for (;;) {
        havekids = 0;
        int i;
        for (i=0;i<NPROC;i++) {
            p = &ptable.proc[i];
            if (p->parent != proc || !p->is_thread)
                continue;
            havekids = 1;
            if (p->state == ZOMBIE) { // found a thread
                *stack_low_addr_container = p->thread_stack;
                pid = p->pid;
                kfree(p->kstack);
                p->kstack = 0;
                p->state = UNUSED;
                p->pid = 0;
                p->parent = 0;
                p->name[0] = 0;
                p->killed = 0;
                p->thread_stack = NULL;
                release(&ptable.lock);
                return pid;
            }
        }
        if(!havekids || proc->killed){
          release(&ptable.lock);
          return -1;
        }
        sleep(proc, &ptable.lock);  //DOC: wait-sleep
    }
    return -1;
}
/*           | UNPARKED  | SETPARKED   | SETUNPARKED | PARKED
 * ------------------------------------------------------------
 * park()    | PARKED    | PARKED      | UNPARKED    | -
 * setpark() | SETPARKED | -           | -           | -
 * unpark()  | -         | SETUNPARKED | -           | UNPARKED
 * "-" = INVALID
 */
int sys_park(void) {
    acquire(&ptable.lock);
    if (proc->pstate == SETUNPARKED) {
        proc->pstate = UNPARKED;
    } else {
        proc->pstate = PARKED;
        proc->state = RUNNABLE;
        sched();
    }
    release(&ptable.lock);
    return 0;
}

int sys_setpark(void) {
    acquire(&ptable.lock);
    if (proc->pstate == UNPARKED) {
        proc->pstate = SETPARKED;
        release(&ptable.lock);
        return 0;
    } else {
        release(&ptable.lock);
        return -1;
    }
}

int sys_unpark(void) {
    int target_pid; argint(0, &target_pid);

    acquire(&ptable.lock);
    struct proc *p = NULL;
    int i;
    for (i=0;i<NPROC;i++)
        if (ptable.proc[i].pid == target_pid) {
            p = ptable.proc+i;
            break;
        }
    if (p == NULL) goto fail;
    switch(p->pstate) {
        case UNPARKED:
        case SETUNPARKED:
            goto fail;
        case SETPARKED: p->pstate = SETUNPARKED; break;
        case PARKED: p->pstate = UNPARKED; break;
        default: panic("unknown pstate");
    }
    release(&ptable.lock);
    return 0;
fail:
    release(&ptable.lock);
    return -1;
}
