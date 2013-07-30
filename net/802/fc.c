/*
 * NET3:	Fibre Channel device handling subroutines
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *		Vineet Abraham <vma@iol.unh.edu>
 *		v 1.0 03/22/99
 */

#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/fcdevice.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/net.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <net/arp.h>

/*
 *	Put the headers on a Fibre Channel packet.
 */

static int fc_header(struct sk_buff *skb, struct net_device *dev,
		     unsigned short type,
		     const void *daddr,