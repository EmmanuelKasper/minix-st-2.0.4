/* When a needed block is not in the cache, it must be fetched from the disk.
 * Special character files also require I/O.  The routines for these are here.
 *
 * The entry points in this file are:
 *   dev_open:   FS opens a device
 *   dev_close:  FS closes a device
 *   dev_io:	 FS does a read or write on a device
 *   gen_opcl:   generic call to a task to perform an open/close
 *   gen_io:     generic call to a task to perform an I/O operation
 *   no_dev:     open/close processing for devices that don't exist
 *   tty_opcl:   perform tty-specific processing for open/close
 *   ctty_opcl:  perform controlling-tty-specific processing for open/close
 *   ctty_io:    perform controlling-tty-specific processing for I/O
 *   do_ioctl:	 perform the IOCTL system call
 *   do_setsid:	 perform the SETSID system call (FS side)
 */

#include "fs.h"
#include <fcntl.h>
#include <minix/callnr.h>
#include <minix/com.h>
#include "dev.h"
#include "file.h"
#include "fproc.h"
#include "inode.h"
#include "param.h"


/*===========================================================================*
 *				dev_open				     *
 *===========================================================================*/
PUBLIC int dev_open(dev, proc, flags)
dev_t dev;			/* device to open */
int proc;			/* process to open for */
int flags;			/* mode bits and flags */
{
  int major, r;
  struct dmap *dp;

  /* Determine the major device number call the device class specific
   * open/close routine.  (This is the only routine that must check the
   * device number for being in range.  All others can trust this check.)
   */
  major = (dev >> MAJOR) & BYTE;
  if (major >= max_major) major = 0;
  dp = &dmap[major];
  r = (*dp->dmap_opcl)(DEV_OPEN, dev, proc, flags);
  if (r == SUSPEND) panic("Suspend on open from", dp->dmap_task);
  return(r);
}


/*===========================================================================*
 *				dev_close				     *
 *===========================================================================*/
PUBLIC void dev_close(dev)
dev_t dev;			/* device to close */
{
  (void) (*dmap[(dev >> MAJOR) & BYTE].dmap_opcl)(DEV_CLOSE, dev, 0, 0);
}


/*===========================================================================*
 *				dev_io					     *
 *===========================================================================*/
PUBLIC int dev_io(op, dev, proc, buf, pos, bytes, flags)
int op;				/* DEV_READ, DEV_WRITE, DEV_IOCTL, etc. */
dev_t dev;			/* major-minor device number */
int proc;			/* in whose address space is buf? */
void *buf;			/* virtual address of the buffer */
off_t pos;			/* byte position */
int bytes;			/* how many bytes to transfer */
int flags;			/* special flags, like O_NONBLOCK */
{
/* Read or write from a device.  The parameter 'dev' tells which one. */
  struct dmap *dp;
  message dev_mess;

  /* Determine task dmap. */
  dp = &dmap[(dev >> MAJOR) & BYTE];

  /* Set up the message passed to task. */
  dev_mess.m_type   = op;
  dev_mess.DEVICE   = (dev >> MINOR) & BYTE;
  dev_mess.POSITION = pos;
  dev_mess.PROC_NR  = proc;
  dev_mess.ADDRESS  = buf;
  dev_mess.COUNT    = bytes;
  dev_mess.TTY_FLAGS = flags;

  /* Call the task. */
  (*dp->dmap_io)(dp->dmap_task, &dev_mess);

  /* Task has completed.  See if call completed. */
  if (dev_mess.REP_STATUS == SUSPEND) {
	if (flags & O_NONBLOCK) {
		/* Not supposed to block. */
		dev_mess.m_type = CANCEL;
		dev_mess.PROC_NR = proc;
		dev_mess.DEVICE = (dev >> MINOR) & BYTE;
		(*dp->dmap_io)(dp->dmap_task, &dev_mess);
		if (dev_mess.REP_STATUS == EINTR) dev_mess.REP_STATUS = EAGAIN;
	} else {
		/* Suspend user. */
		suspend(dp->dmap_task);
		return(SUSPEND);
	}
  }
  return(dev_mess.REP_STATUS);
}


/*===========================================================================*
 *				gen_opcl				     *
 *===========================================================================*/
PUBLIC int gen_opcl(op, dev, proc, flags)
int op;				/* operation, DEV_OPEN or DEV_CLOSE */
dev_t dev;			/* device to open or close */
int proc;			/* process to open/close for */
int flags;			/* mode bits and flags */
{
/* Called from the dmap struct in table.c on opens & closes of special files.*/
  struct dmap *dp;
  message dev_mess;

  /* Determine task dmap. */
  dp = &dmap[(dev >> MAJOR) & BYTE];

  dev_mess.m_type   = op;
  dev_mess.DEVICE   = (dev >> MINOR) & BYTE;
  dev_mess.PROC_NR  = proc;
  dev_mess.COUNT    = flags;

  /* Call the task. */
  (*dp->dmap_io)(dp->dmap_task, &dev_mess);

  return(dev_mess.REP_STATUS);
}


/*===========================================================================*
 *				tty_opcl				     *
 *===========================================================================*/
PUBLIC int tty_opcl(op, dev, proc, flags)
int op;				/* operation, DEV_OPEN or DEV_CLOSE */
dev_t dev;			/* device to open or close */
int proc;			/* process to open/close for */
int flags;			/* mode bits and flags */
{
/* This procedure is called from the dmap struct on tty open/close. */
 
  int r;
  register struct fproc *rfp;

  /* Add O_NOCTTY to the flags if this process is not a session leader, or
   * if it already has a controlling tty, or if it is someone elses
   * controlling tty.
   */
  if (!fp->fp_sesldr || fp->fp_tty != 0) {
	flags |= O_NOCTTY;
  } else {
	for (rfp = fproc_addr(LOW_USER); rfp < &fproc[NR_PROCS]; rfp++) {
		if (rfp->fp_tty == dev) flags |= O_NOCTTY;
	}
  }

  r = gen_opcl(op, dev, proc, flags);

  /* Did this call make the tty the controlling tty? */
  if (r == 1) {
	fp->fp_tty = dev;
	r = OK;
  }
  return(r);
}


/*===========================================================================*
 *				ctty_opcl				     *
 *===========================================================================*/
PUBLIC int ctty_opcl(op, dev, proc, flags)
int op;				/* operation, DEV_OPEN or DEV_CLOSE */
dev_t dev;			/* device to open or close */
int proc;			/* process to open/close for */
int flags;			/* mode bits and flags */
{
/* This procedure is called from the dmap struct in table.c on opening/closing
 * /dev/tty, the magic device that translates to the controlling tty.
 */
 
  return(fp->fp_tty == 0 ? ENXIO : OK);
}


/*===========================================================================*
 *				do_setsid				     *
 *===========================================================================*/
PUBLIC int do_setsid()
{
/* Perform the FS side of the SETSID call, i.e. get rid of the controlling
 * terminal of a process, and make the process a session leader.
 */
  register struct fproc *rfp;

  /* Only MM may do the SETSID call directly. */
  if (who != MM_PROC_NR) return(ENOSYS);

  /* Make the process a session leader with no controlling tty. */
  rfp = fproc_addr(slot1);
  rfp->fp_sesldr = TRUE;
  rfp->fp_tty = 0;
}


/*===========================================================================*
 *				do_ioctl				     *
 *===========================================================================*/
PUBLIC int do_ioctl()
{
/* Perform the ioctl(ls_fd, request, argx) system call (uses m2 fmt). */

  struct filp *f;
  register struct inode *rip;
  dev_t dev;

  if ( (f = get_filp(ls_fd)) == NIL_FILP) return(err_code);
  rip = f->filp_ino;		/* get inode pointer */
  if ( (rip->i_mode & I_TYPE) != I_CHAR_SPECIAL
	&& (rip->i_mode & I_TYPE) != I_BLOCK_SPECIAL) return(ENOTTY);
  dev = (dev_t) rip->i_zone[0];

#if ENABLE_BINCOMPAT
  if ((m.TTY_REQUEST >> 8) == 't') {
	/* Obsolete sgtty ioctl, message contains more than is sane. */
	struct dmap *dp;
	message dev_mess;

	dp = &dmap[(dev >> MAJOR) & BYTE];

	dev_mess = m;	/* Copy full message with all the weird bits. */
	dev_mess.m_type   = DEV_IOCTL;
	dev_mess.PROC_NR  = who;
	dev_mess.TTY_LINE = (dev >> MINOR) & BYTE;	

	/* Call the task. */
	(*dp->dmap_io)(dp->dmap_task, &dev_mess);

	m1.TTY_SPEK = dev_mess.TTY_SPEK;	/* erase and kill */
	m1.TTY_FLAGS = dev_mess.TTY_FLAGS;	/* flags */
	return(dev_mess.REP_STATUS);
  }
#endif

  return(dev_io(DEV_IOCTL, dev, who, m.ADDRESS, 0L, m.REQUEST, f->filp_flags));
}


/*===========================================================================*
 *				gen_io					     *
 *===========================================================================*/
PUBLIC void gen_io(task_nr, mess_ptr)
int task_nr;			/* which task to call */
message *mess_ptr;		/* pointer to message for task */
{
/* All file system I/O ultimately comes down to I/O on major/minor device
 * pairs.  These lead to calls on the following routines via the dmap table.
 */

  int r, proc_nr;
  message local_m;

  proc_nr = mess_ptr->PROC_NR;

  while ((r = sendrec(task_nr, mess_ptr)) == ELOCKED) {
	/* sendrec() failed to avoid deadlock. The task 'task_nr' is
	 * trying to send a REVIVE message for an earlier request.
	 * Handle it and go try again.
	 */
	if ((r = receive(task_nr, &local_m)) != OK) break;

	/* If we're trying to send a cancel message to a task which has just
	 * sent a completion reply, ignore the reply and abort the cancel
	 * request. The caller will do the revive for the process.
	 */
	if (mess_ptr->m_type == CANCEL && local_m.REP_PROC_NR == proc_nr)
		return;

	/* Otherwise it should be a REVIVE. */
	if (local_m.m_type != REVIVE) {
		printf(
		"fs: strange device reply from %d, type = %d, proc = %d\n",
			local_m.m_source,
			local_m.m_type, local_m.REP_PROC_NR);
		continue;
	}

	revive(local_m.REP_PROC_NR, local_m.REP_STATUS);
  }

  /* The message received may be a reply to this call, or a REVIVE for some
   * other process.
   */
  for (;;) {
	if (r != OK) panic("call_task: can't send/receive", NO_NUM);

  	/* Did the process we did the sendrec() for get a result? */
  	if (mess_ptr->REP_PROC_NR == proc_nr) break;

	/* Otherwise it should be a REVIVE. */
	if (mess_ptr->m_type != REVIVE) {
		printf(
		"fs: strange device reply from %d, type = %d, proc = %d\n",
			mess_ptr->m_source,
			mess_ptr->m_type, mess_ptr->REP_PROC_NR);
		continue;
	}
	revive(mess_ptr->REP_PROC_NR, mess_ptr->REP_STATUS);

	r = receive(task_nr, mess_ptr);
  }
}


/*===========================================================================*
 *				ctty_io					     *
 *===========================================================================*/
PUBLIC void ctty_io(task_nr, mess_ptr)
int task_nr;			/* not used - for compatibility with dmap_t */
message *mess_ptr;		/* pointer to message for task */
{
/* This routine is only called for one device, namely /dev/tty.  Its job
 * is to change the message to use the controlling terminal, instead of the
 * major/minor pair for /dev/tty itself.
 */

  struct dmap *dp;

  if (fp->fp_tty == 0) {
	/* No controlling tty present anymore, return an I/O error. */
	mess_ptr->REP_STATUS = EIO;
  } else {
	/* Substitute the controlling terminal device. */
	dp = &dmap[(fp->fp_tty >> MAJOR) & BYTE];
	mess_ptr->DEVICE = (fp->fp_tty >> MINOR) & BYTE;
	(*dp->dmap_io)(dp->dmap_task, mess_ptr);
  }
}


/*===========================================================================*
 *				no_dev					     *
 *===========================================================================*/
PUBLIC int no_dev(op, dev, proc, flags)
int op;				/* operation, DEV_OPEN or DEV_CLOSE */
dev_t dev;			/* device to open or close */
int proc;			/* process to open/close for */
int flags;			/* mode bits and flags */
{
/* Called when opening a nonexistent device. */

  return(ENODEV);
}


/*===========================================================================*
 *				clone_opcl				     *
 *===========================================================================*/
PUBLIC int clone_opcl(op, dev, proc, flags)
int op;				/* operation, DEV_OPEN or DEV_CLOSE */
dev_t dev;			/* device to open or close */
int proc;			/* process to open/close for */
int flags;			/* mode bits and flags */
{
/* Some devices need special processing upon open.  Such a device is "cloned",
 * i.e. on a succesful open it is replaced by a new device with a new unique
 * minor device number.  This new device number identifies a new object (such
 * as a new network connection) that has been allocated within a task.
 */
  struct dmap *dp;
  int minor;
  message dev_mess;

  /* Determine task dmap. */
  dp = &dmap[(dev >> MAJOR) & BYTE];
  minor = (dev >> MINOR) & BYTE;

  dev_mess.m_type   = op;
  dev_mess.DEVICE   = minor;
  dev_mess.PROC_NR  = proc;
  dev_mess.COUNT    = flags;

  /* Call the task. */
  (*dp->dmap_io)(dp->dmap_task, &dev_mess);

  if (op == DEV_OPEN && dev_mess.REP_STATUS >= 0) {
	if (dev_mess.REP_STATUS != minor) {
		/* A new minor device number has been returned.  Create a
		 * temporary device file to hold it.
		 */
		struct inode *ip;

		/* Device number of the new device. */
		dev = (dev & ~(BYTE << MINOR)) | (dev_mess.REP_STATUS << MINOR);

		ip = alloc_inode(root_dev, ALL_MODES | I_CHAR_SPECIAL);
		if (ip == NIL_INODE) {
			/* Oops, that didn't work.  Undo open. */
			(void) clone_opcl(DEV_CLOSE, dev, proc, 0);
			return(err_code);
		}
		ip->i_zone[0] = dev;

		put_inode(fp->fp_filp[fd]->filp_ino);
		fp->fp_filp[fd]->filp_ino = ip;
	}
	dev_mess.REP_STATUS = OK;
  }
  return(dev_mess.REP_STATUS);
}
