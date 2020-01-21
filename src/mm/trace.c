/* This file handles the memory manager's part of debugging, using the 
 * ptrace system call. Most of the commands are passed on to the system
 * task for completion.
 *
 * The debugging commands available are:
 * T_STOP	stop the process 
 * T_OK		enable tracing by parent for this process
 * T_GETINS	return value from instruction space 
 * T_GETDATA	return value from data space 
 * T_GETUSER	return value from user process table
 * T_SETINS	set value in instruction space
 * T_SETDATA	set value in data space
 * T_SETUSER	set value in user process table 
 * T_RESUME	resume execution 
 * T_EXIT	exit
 * T_STEP	set trace bit 
 * 
 * The T_OK and T_EXIT commands are handled here, and the T_RESUME and
 * T_STEP commands are partially handled here and completed by the system
 * task. The rest are handled entirely by the system task. 
 */

#include "mm.h"
#include <sys/ptrace.h>
#include <signal.h>
#include "mproc.h"
#include "param.h"

#define NIL_MPROC	((struct mproc *) 0)

FORWARD _PROTOTYPE( struct mproc *findproc, (pid_t lpid) );

/*===========================================================================*
 *				do_trace  				     *
 *===========================================================================*/
PUBLIC int do_trace()
{
  register struct mproc *child;

  /* the T_OK call is made by the child fork of the debugger before it execs  
   * the process to be traced
   */
  if (request == T_OK) {/* enable tracing by parent for this process */
	mp->mp_flags |= TRACED;
	mp->mp_reply.m2_l2 = 0;
	return(OK);
  }
  if ((child = findproc(pid)) == NIL_MPROC || !(child->mp_flags & STOPPED)) {
	return(ESRCH);
  }
  /* all the other calls are made by the parent fork of the debugger to 
   * control execution of the child
   */
  switch (request) {
  case T_EXIT:		/* exit */
	mm_exit(child, (int)data);
	mp->mp_reply.m2_l2 = 0;
	return(OK);
  case T_RESUME: 
  case T_STEP: 		/* resume execution */
	if (data < 0 || data > _NSIG) return(EIO);
	if (data > 0) {		/* issue signal */
		child->mp_flags &= ~TRACED;  /* so signal is not diverted */
		sig_proc(child, (int) data);
		child->mp_flags |= TRACED;
	}
	child->mp_flags &= ~STOPPED;
  	break;
  }
  if (sys_trace(request, (int) (child - mproc), taddr, &data) != OK)
	return(-errno);
  mp->mp_reply.m2_l2 = data;
  return(OK);
}

/*===========================================================================*
 *				findproc  				     *
 *===========================================================================*/
PRIVATE struct mproc *findproc(lpid)
pid_t lpid;
{
  register struct mproc *rmp;

  for (rmp = mproc_addr(INIT_PROC_NR + 1); rmp < &mproc[NR_PROCS]; rmp++)
	if (rmp->mp_flags & IN_USE && rmp->mp_pid == lpid) return(rmp);
  return(NIL_MPROC);
}

/*===========================================================================*
 *				stop_proc  				     *
 *===========================================================================*/
PUBLIC void stop_proc(rmp, signo)
register struct mproc *rmp;
int signo;
{
/* A traced process got a signal so stop it. */

#if 1
  register struct mproc *rpmp = mproc_addr(rmp->mp_parent);
#else
  register struct mproc *rpmp = mproc + rmp->mp_parent;
#endif

  if (sys_trace(-1, (int) (rmp - mproc), 0L, (long *) 0) != OK) return;
  rmp->mp_flags |= STOPPED;
  if (rpmp->mp_flags & WAITING) {
	rpmp->mp_flags &= ~WAITING;	/* parent is no longer waiting */
	rpmp->reply_res2 = 0177 | (signo << 8);
	setreply(rmp->mp_parent, rmp->mp_pid);
  } else {
	rmp->mp_sigstatus = signo;
  }
  return;
}
