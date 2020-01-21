/*	pty.c - pseudo terminal driver			Author: Kees J. Bot
 *								30 Dec 1995
 * PTYs can be seen as a bidirectional pipe with TTY
 * input and output processing.  For example a simple rlogin session:
 *
 *	keyboard -> rlogin -> in.rld -> /dev/ptypX -> /dev/ttypX -> shell
 *	shell -> /dev/ttypX -> /dev/ptypX -> in.rld -> rlogin -> screen
 *
 * This file takes care of copying data between the tty/pty device pairs and
 * the open/read/write/close calls on the pty devices.  The TTY task takes
 * care of the input and output processing (interrupt, backspace, raw I/O,
 * etc.) using the pty_read() and pty_write() functions as the "keyboard" and
 * "screen" functions of the ttypX devices.
 * Be careful when reading this code, the terms "reading" and "writing" are
 * used both for the tty and the pty end of the pseudo tty.  Writes to one
 * end are to be read at the other end and vice-versa.
 */

#include "kernel.h"
#include <termios.h>
#include <signal.h>
#include <minix/com.h>
#include <minix/callnr.h>
#include "tty.h"
#include "proc.h"

#if NR_PTYS > 0

/* PTY bookkeeping structure, one per pty/tty pair. */
typedef struct pty {
  tty_t		*tty;		/* associated TTY structure */
  char		state;		/* flags: busy, closed, ... */

  /* Read call on /dev/ptypX. */
  char		rdrepcode;	/* reply code, TASK_REPLY or REVIVE */
  char		rdcaller;	/* process making the call (usually FS) */
  char		rdproc;		/* process that wants to read from the pty */
  vir_bytes	rdvir;		/* virtual address in readers address space */
  int		rdleft;		/* # bytes yet to be read */
  int		rdcum;		/* # bytes written so far */

  /* Write call to /dev/ptypX. */
  char		wrrepcode;	/* reply code, TASK_REPLY or REVIVE */
  char		wrcaller;	/* process making the call (usually FS) */
  char		wrproc;		/* process that wants to write to the pty */
  vir_bytes	wrvir;		/* virtual address in writers address space */
  int		wrleft;		/* # bytes yet to be written */
  int		wrcum;		/* # bytes written so far */

  /* Output buffer. */
  int		ocount;		/* # characters in the buffer */
  char		*ohead, *otail;	/* head and tail of the circular buffer */
  char		obuf[128];	/* buffer for bytes going to the pty reader */
} pty_t;

#define PTY_ACTIVE	0x01	/* pty is open/active */
#define TTY_CLOSED	0x02	/* tty side has closed down */
#define PTY_CLOSED	0x04	/* pty side has closed down */

PRIVATE pty_t pty_table[NR_PTYS];	/* PTY bookkeeping */


FORWARD _PROTOTYPE( void pty_write, (tty_t *tp)				);
FORWARD _PROTOTYPE( void pty_echo, (tty_t *tp, int c)			);
FORWARD _PROTOTYPE( void pty_start, (pty_t *pp)				);
FORWARD _PROTOTYPE( void pty_finish, (pty_t *pp)			);
FORWARD _PROTOTYPE( void pty_read, (tty_t *tp)				);
FORWARD _PROTOTYPE( void pty_close, (tty_t *tp)				);
FORWARD _PROTOTYPE( void pty_icancel, (tty_t *tp)			);
FORWARD _PROTOTYPE( void pty_ocancel, (tty_t *tp)			);


/*==========================================================================*
 *				do_pty					    *
 *==========================================================================*/
PUBLIC void do_pty(tp, m_ptr)
tty_t *tp;
message *m_ptr;
{
/* Perform an open/close/read/write call on a /dev/ptypX device. */
  pty_t *pp = tp->tty_priv;
  int r;

  switch (m_ptr->m_type) {
    case DEV_READ:
	/* Check, store information on the reader, do I/O. */
	if (pp->state & TTY_CLOSED) {
		r = 0;
		break;
	}
	if (pp->rdleft != 0) {
		r = EIO;
		break;
	}
	if (m_ptr->COUNT <= 0) {
		r = EINVAL;
		break;
	}
	if (numap(m_ptr->PROC_NR, (vir_bytes) m_ptr->ADDRESS,
							m_ptr->COUNT) == 0) {
		r = EFAULT;
		break;
	}
	pp->rdrepcode = TASK_REPLY;
	pp->rdcaller = m_ptr->m_source;
	pp->rdproc = m_ptr->PROC_NR;
	pp->rdvir = (vir_bytes) m_ptr->ADDRESS;
	pp->rdleft = m_ptr->COUNT;
	pty_start(pp);
	handle_events(tp);
	if (pp->rdleft == 0) return;			/* already done */

	if (m_ptr->TTY_FLAGS & O_NONBLOCK) {
		r = EAGAIN;				/* don't suspend */
		pp->rdleft = pp->rdcum = 0;
	} else {
		r = SUSPEND;				/* do suspend */
		pp->rdrepcode = REVIVE;
	}
	break;

    case DEV_WRITE:
	/* Check, store information on the writer, do I/O. */
	if (pp->state & TTY_CLOSED) {
		r = EIO;
		break;
	}
	if (pp->wrleft != 0) {
		r = EIO;
		break;
	}
	if (m_ptr->COUNT <= 0) {
		r = EINVAL;
		break;
	}
	if (numap(m_ptr->PROC_NR, (vir_bytes) m_ptr->ADDRESS,
							m_ptr->COUNT) == 0) {
		r = EFAULT;
		break;
	}
	pp->wrrepcode = TASK_REPLY;
	pp->wrcaller = m_ptr->m_source;
	pp->wrproc = m_ptr->PROC_NR;
	pp->wrvir = (vir_bytes) m_ptr->ADDRESS;
	pp->wrleft = m_ptr->COUNT;
	handle_events(tp);
	if (pp->wrleft == 0) return;			/* already done */

	if (m_ptr->TTY_FLAGS & O_NONBLOCK) {		/* don't suspend */
		r = pp->wrcum > 0 ? pp->wrcum : EAGAIN;
		pp->wrleft = pp->wrcum = 0;
	} else {
		pp->wrrepcode = REVIVE;			/* do suspend */
		r = SUSPEND;
	}
	break;

    case DEV_OPEN:
	r = pp->state != 0 ? EIO : OK;
	pp->state |= PTY_ACTIVE;
	break;

    case DEV_CLOSE:
	r = OK;
	if (pp->state & TTY_CLOSED) {
		pp->state = 0;
	} else {
		pp->state |= PTY_CLOSED;
		sigchar(tp, SIGHUP);
	}
	break;

    case CANCEL:
	if (m_ptr->PROC_NR == pp->rdproc) {
		/* Cancel a read from a PTY. */
		pp->rdleft = pp->rdcum = 0;
	}
	if (m_ptr->PROC_NR == pp->wrproc) {
		/* Cancel a write to a PTY. */
		pp->wrleft = pp->wrcum = 0;
	}
	r = EINTR;
	break;

    default:
	r = EINVAL;
  }
  tty_reply(TASK_REPLY, m_ptr->m_source, m_ptr->PROC_NR, r);
}


/*==========================================================================*
 *				pty_write				    *
 *==========================================================================*/
PRIVATE void pty_write(tp)
tty_t *tp;
{
/* (*dev_write)() routine for PTYs.  Transfer bytes from the writer on
 * /dev/ttypX to the output buffer.
 */
  pty_t *pp = tp->tty_priv;
  int count, ocount;
  phys_bytes user_phys;

  /* PTY closed down? */
  if (pp->state & PTY_CLOSED) {
	if (tp->tty_outleft > 0) {
		tty_reply(tp->tty_outrepcode, tp->tty_outcaller,
							tp->tty_outproc, EIO);
		tp->tty_outleft = tp->tty_outcum = 0;
	}
	return;
  }

  /* While there is something to do. */
  for (;;) {
	ocount = buflen(pp->obuf) - pp->ocount;
	count = bufend(pp->obuf) - pp->ohead;
	if (count > ocount) count = ocount;
	if (count > tp->tty_outleft) count = tp->tty_outleft;
	if (count == 0 || tp->tty_inhibited) break;

	/* Copy from user space to the PTY output buffer. */
	user_phys = proc_vir2phys(proc_addr(tp->tty_outproc), tp->tty_out_vir);
	phys_copy(user_phys, vir2phys(pp->ohead), (phys_bytes) count);

	/* Perform output processing on the output buffer. */
	out_process(tp, pp->obuf, pp->ohead, bufend(pp->obuf), &count, &ocount);
	if (count == 0) break;

	/* Assume echoing messed up by output. */
	tp->tty_reprint = TRUE;

	/* Bookkeeping. */
	pp->ocount += ocount;
	if ((pp->ohead += ocount) >= bufend(pp->obuf))
		pp->ohead -= buflen(pp->obuf);
	pty_start(pp);
	tp->tty_out_vir += count;
	tp->tty_outcum += count;
	if ((tp->tty_outleft -= count) == 0) {
		/* Output is finished, reply to the writer. */
		tty_reply(tp->tty_outrepcode, tp->tty_outcaller,
					tp->tty_outproc, tp->tty_outcum);
		tp->tty_outcum = 0;
	}
  }
  pty_finish(pp);
}


/*==========================================================================*
 *				pty_echo				    *
 *==========================================================================*/
PRIVATE void pty_echo(tp, c)
tty_t *tp;
int c;
{
/* Echo one character.  (Like pty_write, but only one character, optionally.) */

  pty_t *pp = tp->tty_priv;
  int count, ocount;

  ocount = buflen(pp->obuf) - pp->ocount;
  if (ocount == 0) return;		/* output buffer full */
  count = 1;
  *pp->ohead = c;			/* add one character */

  out_process(tp, pp->obuf, pp->ohead, bufend(pp->obuf), &count, &ocount);
  if (count == 0) return;

  pp->ocount += ocount;
  if ((pp->ohead += ocount) >= bufend(pp->obuf)) pp->ohead -= buflen(pp->obuf);
  pty_start(pp);
}


/*==========================================================================*
 *				pty_start				    *
 *==========================================================================*/
PRIVATE void pty_start(pp)
pty_t *pp;
{
/* Transfer bytes written to the output buffer to the PTY reader. */
  int count;
  phys_bytes user_phys;

  /* While there are things to do. */
  for (;;) {
	count = bufend(pp->obuf) - pp->otail;
	if (count > pp->ocount) count = pp->ocount;
	if (count > pp->rdleft) count = pp->rdleft;
	if (count == 0) break;

	/* Copy from the output buffer to the readers address space. */
	user_phys = proc_vir2phys(proc_addr(pp->rdproc), pp->rdvir);
	phys_copy(vir2phys(pp->otail), user_phys, (phys_bytes) count);

	/* Bookkeeping. */
	pp->ocount -= count;
	if ((pp->otail += count) == bufend(pp->obuf)) pp->otail = pp->obuf;
	pp->rdvir += count;
	pp->rdcum += count;
	pp->rdleft -= count;
  }
}


/*==========================================================================*
 *				pty_finish				    *
 *==========================================================================*/
PRIVATE void pty_finish(pp)
pty_t *pp;
{
/* Finish the read request of a PTY reader if there is at least one byte
 * transferred.
 */

  if (pp->rdcum > 0) {
	tty_reply(pp->rdrepcode, pp->rdcaller, pp->rdproc, pp->rdcum);
	pp->rdleft = pp->rdcum = 0;
  }
}


/*==========================================================================*
 *				pty_read				    *
 *==========================================================================*/
PRIVATE void pty_read(tp)
tty_t *tp;
{
/* Offer bytes from the PTY writer for input on the TTY.  (Do it one byte at
 * a time, 99% of the writes will be for one byte, so no sense in being smart.)
 */
  pty_t *pp = tp->tty_priv;
  phys_bytes user_phys;
  char c;

  if (pp->state & PTY_CLOSED) {
	if (tp->tty_inleft > 0) {
		tty_reply(tp->tty_inrepcode, tp->tty_incaller, tp->tty_inproc,
								tp->tty_incum);
		tp->tty_inleft = tp->tty_incum = 0;
	}
	return;
  }

  while (pp->wrleft > 0) {
	/* Transfer one character to 'c'. */
	user_phys = proc_vir2phys(proc_addr(pp->wrproc), pp->wrvir);
	phys_copy(user_phys, vir2phys(&c), 1L);

	/* Input processing. */
	if (in_process(tp, &c, 1) == 0) break;

	/* PTY writer bookkeeping. */
	pp->wrvir++;
	pp->wrcum++;
	if (--pp->wrleft == 0) {
		tty_reply(pp->wrrepcode, pp->wrcaller, pp->wrproc, pp->wrcum);
		pp->wrcum = 0;
	}
  }
}


/*==========================================================================*
 *				pty_close				    *
 *==========================================================================*/
PRIVATE void pty_close(tp)
tty_t *tp;
{
/* The tty side has closed, so shut down the pty side. */
  pty_t *pp = tp->tty_priv;

  if (!(pp->state & PTY_ACTIVE)) return;

  if (pp->rdleft > 0) {
	tty_reply(pp->rdrepcode, pp->rdcaller, pp->rdproc, 0);
	pp->rdleft = pp->rdcum = 0;
  }

  if (pp->wrleft > 0) {
	tty_reply(pp->wrrepcode, pp->wrcaller, pp->wrproc, EIO);
	pp->wrleft = pp->wrcum = 0;
  }

  if (pp->state & PTY_CLOSED) pp->state = 0; else pp->state |= TTY_CLOSED;
}


/*==========================================================================*
 *				pty_icancel				    *
 *==========================================================================*/
PRIVATE void pty_icancel(tp)
tty_t *tp;
{
/* Discard waiting input. */
  pty_t *pp = tp->tty_priv;

  if (pp->wrleft > 0) {
	tty_reply(pp->wrrepcode, pp->wrcaller, pp->wrproc,
						pp->wrcum + pp->wrleft);
	pp->wrleft = pp->wrcum = 0;
  }
}


/*==========================================================================*
 *				pty_ocancel				    *
 *==========================================================================*/
PRIVATE void pty_ocancel(tp)
tty_t *tp;
{
/* Drain the output buffer. */
  pty_t *pp = tp->tty_priv;

  pp->ocount = 0;
  pp->otail = pp->ohead;
}


/*==========================================================================*
 *				pty_init				    *
 *==========================================================================*/
PUBLIC void pty_init(tp)
tty_t *tp;
{
  pty_t *pp;
  int line;

  /* Associate PTY and TTY structures. */
  line = tp - &tty_table[NR_CONS + NR_RS_LINES];
  pp = tp->tty_priv = &pty_table[line];
  pp->tty = tp;

  /* Set up output queue. */
  pp->ohead = pp->otail = pp->obuf;

  /* Fill in TTY function hooks. */
  tp->tty_devread = pty_read;
  tp->tty_devwrite = pty_write;
  tp->tty_echo = pty_echo;
  tp->tty_icancel = pty_icancel;
  tp->tty_ocancel = pty_ocancel;
  tp->tty_close = pty_close;
}
#endif /* NR_PTYS > 0 */
