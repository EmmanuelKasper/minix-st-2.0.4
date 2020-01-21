/* This file deals with the suspension and revival of processes.  A process can
 * be suspended because it wants to read or write from a pipe and can't, or
 * because it wants to read or write from a special file and can't.  When a
 * process can't continue it is suspended, and revived later when it is able
 * to continue.
 *
 * The entry points into this file are
 *   do_pipe:	  perform the PIPE system call
 *   pipe_check:  check to see that a read or write on a pipe is feasible now
 *   suspend:	  suspend a process that cannot do a requested read or write
 *   release:	  check to see if a suspended process can be released and do it
 *   revive:	  mark a suspended process as able to run again
 *   do_unpause:  a signal has been sent to a process; see if it suspended
 */

#include "fs.h"
#include <fcntl.h>
#include <signal.h>
#include <minix/callnr.h>
#include <minix/com.h>
#include "dev.h"
#include "file.h"
#include "fproc.h"
#include "inode.h"
#include "param.h"

/*===========================================================================*
 *				do_pipe					     *
 *===========================================================================*/
PUBLIC int do_pipe()
{
/* Perform the pipe(fil_des) system call. */

  register struct fproc *rfp;
  register struct inode *rip;
  int r;
  struct filp *fil_ptr0, *fil_ptr1;
  int fil_des[2];		/* reply goes here */

  /* Acquire two file descriptors. */
  rfp = fp;
  if ( (r = get_fd(0, R_BIT, &fil_des[0], &fil_ptr0)) != OK) return(r);
  rfp->fp_filp[fil_des[0]] = fil_ptr0;
  fil_ptr0->filp_count = 1;
  if ( (r = get_fd(0, W_BIT, &fil_des[1], &fil_ptr1)) != OK) {
	rfp->fp_filp[fil_des[0]] = NIL_FILP;
	fil_ptr0->filp_count = 0;
	return(r);
  }
  rfp->fp_filp[fil_des[1]] = fil_ptr1;
  fil_ptr1->filp_count = 1;

  /* Make the inode on the pipe device. */
  if ( (rip = alloc_inode(root_dev, I_REGULAR) ) == NIL_INODE) {
	rfp->fp_filp[fil_des[0]] = NIL_FILP;
	fil_ptr0->filp_count = 0;
	rfp->fp_filp[fil_des[1]] = NIL_FILP;
	fil_ptr1->filp_count = 0;
	return(err_code);
  }

  if (read_only(rip) != OK) panic("pipe device is read only", NO_NUM);
 
  rip->i_pipe = I_PIPE;
  rip->i_mode &= ~I_REGULAR;
  rip->i_mode |= I_NAMED_PIPE;	/* pipes and FIFOs have this bit set */
  fil_ptr0->filp_ino = rip;
  fil_ptr0->filp_flags = O_RDONLY;
  dup_inode(rip);		/* for double usage */
  fil_ptr1->filp_ino = rip;
  fil_ptr1->filp_flags = O_WRONLY;
  rw_inode(rip, WRITING);	/* mark inode as allocated */
  reply_i1 = fil_des[0];
  reply_i2 = fil_des[1];
  rip->i_update = ATIME | CTIME | MTIME;
  return(OK);
}


/*===========================================================================*
 *				pipe_check				     *
 *===========================================================================*/
PUBLIC int pipe_check(rip, rw_flag, oflags, bytes, position, canwrite)
register struct inode *rip;	/* the inode of the pipe */
int rw_flag;			/* READING or WRITING */
int oflags;			/* flags set by open or fcntl */
register int bytes;		/* bytes to be read or written (all chunks) */
register off_t position;	/* current file position */
int *canwrite;			/* return: number of bytes we can write */
{
/* Pipes are a little different.  If a process reads from an empty pipe for
 * which a writer still exists, suspend the reader.  If the pipe is empty
 * and there is no writer, return 0 bytes.  If a process is writing to a
 * pipe and no one is reading from it, give a broken pipe error.
 */

  /* If reading, check for empty pipe. */
  if (rw_flag == READING) {
	if (position >= rip->i_size) {
		/* Process is reading from an empty pipe. */
		int r = 0;
		if (find_filp(rip, W_BIT) != NIL_FILP) {
			/* Writer exists */
			if (oflags & O_NONBLOCK) {
				r = EAGAIN;
			} else {
				suspend(XPIPE);	/* block reader */
				r = SUSPEND;
			}
			/* If need be, activate sleeping writers. */
			if (susp_count > 0) release(rip, WRITE, susp_count);
		}
		return(r);
	}
  } else {
	/* Process is writing to a pipe. */
	if (find_filp(rip, R_BIT) == NIL_FILP) {
		/* Tell kernel to generate a SIGPIPE signal. */
		sys_kill((int)(fp - fproc), SIGPIPE);
		return(EPIPE);
	}

	if (position + bytes > PIPE_SIZE) {
		if ((oflags & O_NONBLOCK) && bytes < PIPE_SIZE) 
			return(EAGAIN);
		else if ((oflags & O_NONBLOCK) && bytes > PIPE_SIZE) {
			if ( (*canwrite = (PIPE_SIZE - position)) > 0)  {
				/* Do a partial write. Need to wakeup reader */
				release(rip, READ, susp_count);
				return(1);
			} else {
				return(EAGAIN);
			}
		     }
		if (bytes > PIPE_SIZE) {
			if ((*canwrite = PIPE_SIZE - position) > 0) {
				/* Do a partial write. Need to wakeup reader
				 * since we'll suspend ourself in read_write()
				 */
				release(rip, READ, susp_count);
				return(1);
			}
		}
		suspend(XPIPE);	/* stop writer -- pipe full */
		return(SUSPEND);
	}

	/* Writing to an empty pipe.  Search for suspended reader. */
	if (position == 0) release(rip, READ, susp_count);
  }

  *canwrite = 0;
  return(1);
}


/*===========================================================================*
 *				suspend					     *
 *===========================================================================*/
PUBLIC void suspend(task)
int task;			/* who is proc waiting for? (PIPE = pipe) */
{
/* Take measures to suspend the processing of the present system call.
 * Store the parameters to be used upon resuming in the process table.
 * (Actually they are not used when a process is waiting for an I/O device,
 * but they are needed for pipes, and it is not worth making the distinction.)
 * The SUSPEND pseudo error should be returned after calling suspend().
 */

  if (task == XPIPE || task == XPOPEN) susp_count++;/* #procs susp'ed on pipe*/
  fp->fp_suspended = SUSPENDED;
  fp->fp_fd = fd << 8 | fs_call;
  fp->fp_task = -task;
  if (task == XLOCK) {
	fp->fp_buffer = (char *) name1;	/*  third arg to fcntl() */
	fp->fp_nbytes =request;		/* second arg to fcntl() */
  } else {
	fp->fp_buffer = buffer;		/* for reads and writes */
	fp->fp_nbytes = nbytes;
  }
}


/*===========================================================================*
 *				release					     *
 *===========================================================================*/
PUBLIC void release(ip, call_nr, count)
register struct inode *ip;	/* inode of pipe */
int call_nr;			/* READ, WRITE, OPEN or CREAT */
int count;			/* max number of processes to release */
{
/* Check to see if any process is hanging on the pipe whose inode is in 'ip'.
 * If one is, and it was trying to perform the call indicated by 'call_nr',
 * release it.
 */

  register struct fproc *rp;
#if OPTIMIZE_FOR_SPEED
  register int i;
#endif /* OPTIMIZE_FOR_SPEED */

  /* Search the proc table. */
#if OPTIMIZE_FOR_SPEED
  for (rp = &fproc[0], i = 0; i < NR_PROCS; rp++, i++) {
#else
  for (rp = &fproc[0]; rp < &fproc[NR_PROCS]; rp++) {
#endif /* OPTIMIZE_FOR_SPEED */
	if (rp->fp_suspended == SUSPENDED &&
			rp->fp_revived == NOT_REVIVING &&
			(rp->fp_fd & BYTE) == call_nr &&
			rp->fp_filp[rp->fp_fd>>8]->filp_ino == ip) {
#if OPTIMIZE_FOR_SPEED
		revive(i, 0);
#else
		revive((int)(rp - fproc), 0);
#endif /* OPTIMIZE_FOR_SPEED */
		susp_count--;	/* keep track of who is suspended */
		if (--count == 0) return;
	}
  }
}


/*===========================================================================*
 *				revive					     *
 *===========================================================================*/
PUBLIC void revive(proc_nr, bytes)
int proc_nr;			/* process to revive */
int bytes;			/* if hanging on task, how many bytes read */
{
/* Revive a previously blocked process. When a process hangs on tty, this
 * is the way it is eventually released.
 */

  register struct fproc *rfp;
  register int task;

  if (proc_nr < 0 || proc_nr >= NR_PROCS) panic("revive err", proc_nr);

  rfp = fproc_addr(proc_nr);

  if (rfp->fp_suspended == NOT_SUSPENDED || rfp->fp_revived == REVIVING)return;

  /* The 'reviving' flag only applies to pipes.  Processes waiting for TTY get
   * a message right away.  The revival process is different for TTY and pipes.
   * For TTY revival, the work is already done, for pipes it is not: the proc
   * must be restarted so it can try again.
   */
  task = -rfp->fp_task;
  if (task == XPIPE || task == XLOCK) {
	/* Revive a process suspended on a pipe or lock. */
	rfp->fp_revived = REVIVING;
	reviving++;		/* process was waiting on pipe or lock */
  } else {
	rfp->fp_suspended = NOT_SUSPENDED;
	if (task == XPOPEN) /* process blocked in open or create */
		reply(proc_nr, rfp->fp_fd>>8);
	else {
		/* Revive a process suspended on TTY or other device. */
		rfp->fp_nbytes = bytes;	/*pretend it wants only what there is*/
		reply(proc_nr, bytes);	/* unblock the process */
	}
  }
}


/*===========================================================================*
 *				do_unpause				     *
 *===========================================================================*/
PUBLIC int do_unpause()
{
/* A signal has been sent to a user who is paused on the file system.
 * Abort the system call with the EINTR error message.
 */

  register struct fproc *rfp;
  int proc_nr, task, fild;
  struct filp *f;
  dev_t dev;
  message mess;

  if (who > MM_PROC_NR) return(EPERM);
  proc_nr = pro;
  if (proc_nr < 0 || proc_nr >= NR_PROCS) panic("unpause err 1", proc_nr);
  rfp = &fproc[proc_nr];
  if (rfp->fp_suspended == NOT_SUSPENDED) return(OK);
  task = -rfp->fp_task;

  switch (task) {
	case XPIPE:		/* process trying to read or write a pipe */
		break;

	case XLOCK:		/* process trying to set a lock with FCNTL */
		break;

	case XPOPEN:		/* process trying to open a fifo */
		break;

	default:		/* process trying to do device I/O (e.g. tty)*/
		fild = (rfp->fp_fd >> 8) & BYTE;/* extract file descriptor */
		if (fild < 0 || fild >= OPEN_MAX)panic("unpause err 2",NO_NUM);
		f = rfp->fp_filp[fild];
		dev = (dev_t) f->filp_ino->i_zone[0];	/* device hung on */
		mess.TTY_LINE = (dev >> MINOR) & BYTE;
		mess.PROC_NR = proc_nr;

		/* Tell kernel R or W. Mode is from current call, not open. */
		mess.COUNT = (rfp->fp_fd & BYTE) == READ ? R_BIT : W_BIT;
		mess.m_type = CANCEL;
		fp = rfp;	/* hack - ctty_io uses fp */
		(*dmap[(dev >> MAJOR) & BYTE].dmap_io)(task, &mess);
  }

  rfp->fp_suspended = NOT_SUSPENDED;
  reply(proc_nr, EINTR);	/* signal interrupted call */
  return(OK);
}
