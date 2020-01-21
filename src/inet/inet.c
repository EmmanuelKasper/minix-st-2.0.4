/*	this file contains the interface of the network software with rest of
	minix. Furthermore it contains the main loop of the network task.

Copyright 1995 Philip Homburg

The valid messages and their parameters are:

from FS:
 __________________________________________________________________
|		|           |         |       |          |         |
| m_type	|   DEVICE  | PROC_NR |	COUNT |	POSITION | ADDRESS |
|_______________|___________|_________|_______|__________|_________|
|		|           |         |       |          |         |
| NW_OPEN 	| minor dev | proc nr | mode  |          |         |
|_______________|___________|_________|_______|__________|_________|
|		|           |         |       |          |         |
| NW_CLOSE 	| minor dev | proc nr |       |          |         |
|_______________|___________|_________|_______|__________|_________|
|		|           |         |       |          |         |
| NW_IOCTL	| minor dev | proc nr |       |	NWIO..	 | address |
|_______________|___________|_________|_______|__________|_________|
|		|           |         |       |          |         |
| NW_READ	| minor dev | proc nr |	count |          | address |
|_______________|___________|_________|_______|__________|_________|
|		|           |         |       |          |         |
| NW_WRITE	| minor dev | proc nr |	count |          | address |
|_______________|___________|_________|_______|__________|_________|
|		|           |         |       |          |         |
| NW_CANCEL	| minor dev | proc nr |       |          |         |
|_______________|___________|_________|_______|__________|_________|

from the Ethernet task:
 _______________________________________________________________________
|		|           |         |          |            |         |
| m_type	|  DL_PORT  | DL_PROC |	DL_COUNT |  DL_STAT   | DL_TIME |
|_______________|___________|_________|__________|____________|_________|
|		|           |         |          |            |         |
| DL_TASK_INT 	| minor dev | proc nr | rd_count |  0  | stat |  time   |
|_______________|___________|_________|__________|____________|_________|
|		|           |         |          |            |         |
| DL_TASK_REPLY	| minor dev | proc nr | rd_count | err | stat |  time   |         |
|_______________|___________|_________|__________|____________|_________|
*/

#include "inet.h"

#define _MINIX	1

#include <unistd.h>
#include <sys/svrctl.h>

#include "mq.h"
#include "proto.h"
#include "generic/type.h"

#include "generic/assert.h"
#include "generic/buf.h"
#include "generic/clock.h"
#include "generic/eth.h"
#include "generic/event.h"
#if !CRAMPED
#include "generic/arp.h"
#include "generic/ip.h"
#include "generic/psip.h"
#include "generic/sr.h"
#include "generic/tcp.h"
#include "generic/udp.h"
#endif

THIS_FILE

int this_proc;		/* Process number of this server. */
int synal_tasknr;	/* Task number of the synchronous alarm task. */

#ifdef BUF_CONSISTENCY_CHECK
extern int inet_buf_debug;
#endif

_PROTOTYPE( void main, (void) );

FORWARD _PROTOTYPE( void nw_init, (void) );

PUBLIC void main()
{
	mq_t *mq;
	int r;
	int source;

	DBLOCK(1, printf("%s\n", version));

	nw_init();
	while (TRUE)
	{
#ifdef BUF_CONSISTENCY_CHECK
		if (inet_buf_debug)
		{
			static int buf_debug_count= 0;

			if (buf_debug_count++ > inet_buf_debug)
			{
				buf_debug_count= 0;
				if (!bf_consistency_check())
					break;
			}
		}
#endif
		if (ev_head)
		{
			ev_process();
			continue;
		}
		if (clck_call_expire)
		{
			clck_expire_timers();
			continue;
		}
		mq= mq_get();
		if (!mq)
			ip_panic(("out of messages"));

		r= receive (ANY, &mq->mq_mess);
		if (r<0)
		{
			ip_panic(("unable to receive: %d", r));
		}
		reset_time();
		source= mq->mq_mess.m_source;
		if (source == FS_PROC_NR)
		{
			sr_rec(mq);
		}
		else if (source == synal_tasknr)
		{
			clck_tick (&mq->mq_mess);
			mq_free(mq);
		}
#if ENABLE_ETH
		else
		{
compare(mq->mq_mess.m_type, ==, DL_TASK_REPLY);
			eth_rec(&mq->mq_mess);
			mq_free(mq);
		}
#endif /* ENABLE_ETH */
	}
	ip_panic(("task is not allowed to terminate"));
}

PRIVATE void nw_init()
{
	struct fssignon device;
	struct systaskinfo info;

	/* Read configuration. */
	read_conf();
#if ENABLE_ETH
	eth_prep();
#endif /* ENABLE_ETH */
#if ENABLE_ARP
	arp_prep();
#endif /* ENABLE_ARP */
	psip_prep();
	ip_prep();
	tcp_prep();
	udp_prep();

	/* Sign on as a server at all offices in the proper order. */
	if (svrctl(MMSIGNON, (void *) NULL) == -1) {
		printf("inet: server signon failed\n");
		exit(1);
	}
	if (svrctl(SYSSIGNON, (void *) &info) == -1) pause();

	/* Our new identity as a server. */
	this_proc = info.proc_nr;

	/* Register the device group. */
	device.dev= ip_dev;
	device.style= STYLE_CLONE;
	if (svrctl(FSSIGNON, (void *) &device) == -1) {
		printf("inet: error %d on registering ethernet devices\n",
			errno);
		pause();
	}

#ifdef BUF_CONSISTENCY_CHECK
	inet_buf_debug= 100;
	if (inet_buf_debug)
	{
		ip_warning(( "buffer consistency check enabled" ));
	}
#endif

	if (sys_findproc("SYN_AL", &synal_tasknr, 0) != OK)
		ip_panic(( "unable to find synchronous alarm task\n"));

	mq_init();
	bf_init();
	clck_init();
	sr_init();
#if ENABLE_ETH
	eth_init();
#endif /* ENABLE_ETH */
#if ENABLE_ARP
	arp_init();
#endif
#if ENABLE_PSIP
	psip_init();
#endif
#if ENABLE_IP
	ip_init();
#endif
#if ENABLE_TCP
	tcp_init();
#endif
#if ENABLE_UDP
	udp_init();
#endif
}

#if !CRAMPED
PUBLIC void panic0(file, line)
char *file;
int line;
{
	printf("panic at %s, %d: ", file, line);
}

PUBLIC void panic()
{
	printf("\ninet stacktrace: ");
	stacktrace();
	sys_abort(RBT_PANIC);
}

#else /* CRAMPED */

PUBLIC void panic(file, line)
char *file;
int line;
{
	printf("panic at %s, %d\n", file, line);
	sys_abort(RBT_PANIC);
}
#endif

#if !NDEBUG
PUBLIC void bad_assertion(file, line, what)
char *file;
int line;
char *what;
{
	panic0(file, line);
	printf("assertion \"%s\" failed", what);
	panic();
}


PUBLIC void bad_compare(file, line, lhs, what, rhs)
char *file;
int line;
int lhs;
char *what;
int rhs;
{
	panic0(file, line);
	printf("compare (%d) %s (%d) failed", lhs, what, rhs);
	panic();
}
#endif /* !NDEBUG */

/*
 * $PchId: inet.c,v 1.12 1996/12/17 07:58:19 philip Exp $
 */
