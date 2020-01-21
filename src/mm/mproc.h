/* This table has one slot per process.  It contains all the memory management
 * information for each process.  Among other things, it defines the text, data
 * and stack segments, uids and gids, and various flags.  The kernel and file
 * systems have tables that are also indexed by process, with the contents
 * of corresponding slots referring to the same process in all three.
 */

EXTERN struct mproc {
  struct mem_map mp_seg[NR_SEGS];/* points to text, data, stack */
  char mp_exitstatus;		/* storage for status when process exits */
  char mp_sigstatus;		/* storage for signal # for killed procs */
  pid_t mp_pid;			/* process id */
  pid_t mp_procgrp;		/* pid of process group (used for signals) */
  pid_t mp_wpid;		/* pid this process is waiting for */
  int mp_parent;		/* index of parent process */

  /* Real and effective uids and gids. */
  uid_t mp_realuid;		/* process' real uid */
  uid_t mp_effuid;		/* process' effective uid */
  gid_t mp_realgid;		/* process' real gid */
  gid_t mp_effgid;		/* process' effective gid */

  /* File identification for sharing. */
  ino_t mp_ino;			/* inode number of file */
  dev_t mp_dev;			/* device number of file system */
  time_t mp_ctime;		/* inode changed time */

  /* Signal handling information. */
  sigset_t mp_ignore;		/* 1 means ignore the signal, 0 means don't */
  sigset_t mp_catch;		/* 1 means catch the signal, 0 means don't */
  sigset_t mp_sigmask;		/* signals to be blocked */
  sigset_t mp_sigmask2;		/* saved copy of mp_sigmask */
  sigset_t mp_sigpending;	/* signals being blocked */
  struct sigaction mp_sigact[_NSIG + 1]; /* as in sigaction(2) */
  vir_bytes mp_sigreturn; 	/* address of C library __sigreturn function */

  /* Backwards compatibility for signals. */
  sighandler_t mp_func;		/* all sigs vectored to a single user fcn */

  unsigned mp_flags;		/* flag bits */
  vir_bytes mp_procargs;        /* ptr to proc's initial stack arguments */
  struct mproc *mp_swapq;	/* queue of procs waiting to be swapped in */
  message mp_reply;		/* reply message to be sent to one */
#if (MACHINE == ATARI && SHADOWING && ENABLE_SWAP)
  phys_clicks mp_memadr;	/* physical address in RAM of swapped mem */
#endif /* MACHINE == ATARI && SHADOWING && ENABLE_SWAP */
} mproc[NR_PROCS];

/* Flag values */
#define IN_USE          0x001	/* set when 'mproc' slot in use */
#define WAITING         0x002	/* set by WAIT system call */
#define ZOMBIE          0x004	/* set by EXIT, cleared by WAIT */
#define PAUSED          0x008	/* set by PAUSE system call */
#define ALARM_ON        0x010	/* set when SIGALRM timer started */
#define SEPARATE	0x020	/* set if file is separate I & D space */
#define	TRACED		0x040	/* set if process is to be traced */
#define STOPPED		0x080	/* set if process stopped for tracing */
#define SIGSUSPENDED 	0x100	/* set by SIGSUSPEND system call */
#define REPLY	 	0x200	/* set if a reply message is pending */
#define ONSWAP	 	0x400	/* set if data segment is swapped out */
#define SWAPIN	 	0x800	/* set if on the "swap this in" queue */
#if (SHADOWING && ENABLE_SWAP)
#define	MM_DONT_SWAP	0x1000	/* don't swap out shadowed processes */
#endif /* SHADOWING && ENABLE_SWAP */

#define NIL_MPROC ((struct mproc *) 0)
