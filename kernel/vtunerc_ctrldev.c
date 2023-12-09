/*
 * vtunerc: /dev/vtunerc device
 *
 * Copyright (C) 2010-11 Honza Petrous <jpetrous@smartimp.cz>
 * [Created 2010-03-23]
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/poll.h>
#include <linux/dvb/frontend.h>
#include "vtunerc_priv.h"

#define VTUNERC_CTRLDEV_MAJOR	266
#define VTUNERC_CTRLDEV_NAME	"vtunerc"

#define VTUNER_MSG_LEN (sizeof(struct vtuner_message))
#define VTUNER_SIG_LEN (sizeof(struct vtuner_signal))

static ssize_t vtunerc_ctrldev_write(struct file *filp, const char *buff, size_t len, loff_t *off)
{
	struct vtunerc_ctx *ctx = filp->private_data;
	struct dvb_demux *demux = &ctx->demux;
	int tailsize = len % 188;

	if (len < 188) {
		printk(KERN_ERR "vtunerc%d: Data are shorter then TS packet size (188B)\n",
				ctx->idx);
		return -EINVAL;
	}

	len -= tailsize;

	// new buffer need to be allocated ?
	if ((ctx->kernel_buf == NULL) || (len > ctx->kernel_buf_size)) {
		// free old buffer
		if (ctx->kernel_buf) {
			kfree(ctx->kernel_buf);
			ctx->kernel_buf = NULL;
			ctx->kernel_buf_size = 0;
		}
		// allocate a bigger buffer
		ctx->kernel_buf = kmalloc(len, GFP_KERNEL);
		if (!ctx->kernel_buf) {
			printk(KERN_ERR "vtunerc%d: unable to allocate buffer of %zu bytes\n", ctx->idx, len);
			return -ENOMEM;
		}
		ctx->kernel_buf_size = len;
		printk(KERN_INFO "vtunerc%d: allocated buffer of %zu bytes\n", ctx->idx, len);
	}

	if (down_interruptible(&ctx->tswrite_sem)) {
		return -ERESTARTSYS;
	}

	if (copy_from_user(ctx->kernel_buf, buff, len)) {
		printk(KERN_ERR "vtunerc%d: userdata passing error\n",
				ctx->idx);
		up(&ctx->tswrite_sem);
		return -EINVAL;
	}

	if (ctx->config->tscheck) {
		int i;

		for (i = 0; i < len; i += 188)
			if (ctx->kernel_buf[i] != 0x47) { /* start of TS packet */
				printk(KERN_ERR "vtunerc%d: Data not start on packet boundary: index=%d data=%02x %02x %02x %02x %02x ...\n",
						ctx->idx, i / 188, ctx->kernel_buf[i], ctx->kernel_buf[i + 1],
						ctx->kernel_buf[i + 2], ctx->kernel_buf[i + 3], ctx->kernel_buf[i + 4]);
				up(&ctx->tswrite_sem);
				return -EINVAL;
			}
	}

	if (ctx->kernel_buf[0]==0x47) ctx->signal.status=FE_HAS_LOCK;

	ctx->stat_wr_data += len;
	dvb_dmx_swfilter_packets(demux, ctx->kernel_buf, len / 188);

	ctx->nextpacket = 1;
	wake_up_interruptible(&ctx->ctrldev_wait_packet_wq);

	up(&ctx->tswrite_sem);

#ifdef CONFIG_PROC_FS
	/* TODO:  analyze injected data for statistics */
#endif

	return len;
}

static ssize_t vtunerc_ctrldev_read(struct file *filp, char __user *buff, size_t len, loff_t *off)
{
	struct vtunerc_ctx *ctx = filp->private_data;
	ssize_t ret = 0;

	if (!ctx->kernel_buf || ctx->kernel_buf_size==0 || len < ctx->kernel_buf_size)
		return -EINVAL;

	if (down_interruptible(&ctx->tsread_sem))
		return -ERESTARTSYS;

	if (wait_event_interruptible(ctx->ctrldev_wait_packet_wq, ctx->nextpacket != 0))
	{
		up(&ctx->tsread_sem);
		return -ERESTARTSYS;
	}

	if (ctx->nextpacket==1) {
		if (copy_to_user(buff, ctx->kernel_buf, ctx->kernel_buf_size)) {
			up(&ctx->tsread_sem);
			return -EINVAL;
		}
		ret = ctx->kernel_buf_size;
	}
	ctx->nextpacket = 0;
	up(&ctx->tsread_sem);
	return ret;
}

static int vtunerc_ctrldev_open(struct inode *inode, struct file *filp)
{
	struct vtunerc_ctx *ctx;
	int minor;

	minor = MINOR(inode->i_rdev);
	ctx = filp->private_data = vtunerc_get_ctx(minor);
	if (ctx == NULL)
		return -ENOMEM;

	ctx->fd_opened++;
	return 0;
}

static int vtunerc_ctrldev_close(struct inode *inode, struct file *filp)
{
	struct vtunerc_ctx *ctx = filp->private_data;
	int i, minor;
	//struct vtuner_message fakemsg;

	dprintk(ctx, "closing (fd_opened=%d)\n", ctx->fd_opened);

	ctx->fd_opened--;

	minor = MINOR(inode->i_rdev);

	/* set FAKE response, to allow finish any waiters
	   in vtunerc_ctrldev_xchange_message() */
	ctx->ctrldev_response.type = 0;
	dprintk(ctx, "faked response\n");
	wake_up_interruptible(&ctx->ctrldev_wait_response_wq);

	ctx->nextpacket=-1;
	wake_up_interruptible(&ctx->ctrldev_wait_packet_wq);

	if (ctx->fd_opened == 0) {
		ctx->stat_time = 0;
		ctx->stat_wr_data = 0;
		memset(&ctx->signal.status,0,sizeof(struct vtuner_signal));
		memset(&ctx->fe_params,0,sizeof(struct fe_params));
		memset(&ctx->pidtab,0,sizeof(unsigned short)*MAX_PIDTAB_LEN);
		for (i = 0; i < MAX_PIDTAB_LEN; i++)
			ctx->pidtab[i] = PID_UNKNOWN;
	}
	return 0;
}

static long vtunerc_ctrldev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct vtunerc_ctx *ctx = file->private_data;
	int ret = 0;

	if (down_interruptible(&ctx->ioctl_sem))
		return -ERESTARTSYS;

	switch (cmd) {
	case VTUNER_SET_SIGNAL:
		printk(KERN_INFO "vtunerc%d: set signal\n", ctx->idx);
		if (copy_from_user(&ctx->signal, (char *)arg, VTUNER_SIG_LEN)) {
			ret = -EFAULT;
		}
		break;

	case VTUNER_GET_MESSAGE:
		if (wait_event_interruptible(ctx->ctrldev_wait_request_wq, ctx->ctrldev_request.type != -1)) {
			ret = -ERESTARTSYS;
			break;
		}

		BUG_ON(ctx->ctrldev_request.type == -1);

		if (copy_to_user((char *)arg, &ctx->ctrldev_request, VTUNER_MSG_LEN)) {
			ret = -EFAULT;
			break;
		}

		ctx->ctrldev_request.type = -1;

		if (ctx->noresponse)
			up(&ctx->xchange_sem);
		break;

	case VTUNER_SET_RESPONSE:
		if (copy_from_user(&ctx->ctrldev_response, (char *)arg, VTUNER_MSG_LEN)) {
			ret = -EFAULT;
		}
		wake_up_interruptible(&ctx->ctrldev_wait_response_wq);
		break;

	default:
		printk(KERN_ERR "vtunerc%d: unknown IOCTL 0x%x\n", ctx->idx, cmd);
		ret = -ENOTTY; /* Linus: the only correct one return value for unsupported ioctl */
		break;
	}
	up(&ctx->ioctl_sem);

	return ret;
}

static unsigned int vtunerc_ctrldev_poll(struct file *filp, poll_table *wait)
{
	struct vtunerc_ctx *ctx = filp->private_data;
	unsigned int mask = 0;

	poll_wait(filp, &ctx->ctrldev_wait_request_wq, wait);

	if (ctx->ctrldev_request.type > -1) {
		mask = POLLPRI;
	}

	return mask;
}

/* ------------------------------------------------ */

static const struct file_operations vtunerc_ctrldev_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = vtunerc_ctrldev_ioctl,
	.write = vtunerc_ctrldev_write,
	.read  = vtunerc_ctrldev_read,
	.poll  = (void *) vtunerc_ctrldev_poll,
	.open  = vtunerc_ctrldev_open,
	.release  = vtunerc_ctrldev_close
};

static struct class *pclass;
static struct cdev cdev;
static dev_t chdev;

int vtunerc_register_ctrldev(struct vtunerc_ctx *ctx)
{
	int idx;

	chdev = MKDEV(VTUNERC_CTRLDEV_MAJOR, 0);

	if (register_chrdev_region(chdev, ctx->config->devices, VTUNERC_CTRLDEV_NAME)) {
		printk(KERN_ERR "vtunerc%d: unable to get major %d\n", ctx->idx, VTUNERC_CTRLDEV_MAJOR);
		return -EINVAL;
	}

	cdev_init(&cdev, &vtunerc_ctrldev_fops);

	cdev.owner = THIS_MODULE;
	cdev.ops = &vtunerc_ctrldev_fops;

	if (cdev_add(&cdev, chdev, ctx->config->devices) < 0)
		printk(KERN_WARNING "vtunerc%d: unable to create dev\n", ctx->idx);

	pclass = class_create(THIS_MODULE, "vtuner");
	if (IS_ERR(pclass)) {
		printk(KERN_ERR "vtunerc%d: unable to register major %d\n", ctx->idx, VTUNERC_CTRLDEV_MAJOR);
		return PTR_ERR(pclass);
	}

	for (idx = 0; idx < ctx->config->devices; idx++) {
		struct device *clsdev;

		clsdev = device_create(pclass, NULL, MKDEV(VTUNERC_CTRLDEV_MAJOR, idx), /*ctx*/ NULL, "vtunerc%d", idx);

		printk(KERN_NOTICE "vtunerc: registered /dev/vtunerc%d\n", idx);
	}

	return 0;
}

void vtunerc_unregister_ctrldev(struct vtunerc_config *config)
{
	int idx;

	printk(KERN_NOTICE "vtunerc: unregistering\n");

	unregister_chrdev_region(chdev, config->devices);

	for (idx = 0; idx < config->devices; idx++)
		device_destroy(pclass, MKDEV(VTUNERC_CTRLDEV_MAJOR, idx));

	cdev_del(&cdev);

	class_destroy(pclass);
}


int vtunerc_ctrldev_xchange_message(struct vtunerc_ctx *ctx, struct vtuner_message *msg, int wait4response)
{
	if (down_interruptible(&ctx->xchange_sem))
		return -ERESTARTSYS;

	if (ctx->fd_opened < 1) {
		//dprintk(ctx, "XCH_MSG: %d: no fd\n", msg->type);
		up(&ctx->xchange_sem);
		return 0;
	}
	dprintk(ctx, "XCH_MSG: %d: entered\n", msg->type);

#if 0
	BUG_ON(ctx->ctrldev_request.type != -1);
#else
	if(ctx->ctrldev_request.type != -1)
		printk(KERN_WARNING "vtunerc%d: orphan request detected, type %d\n", ctx->idx, ctx->ctrldev_request.type);

#endif

	memcpy(&ctx->ctrldev_request, msg, sizeof(struct vtuner_message));
	ctx->ctrldev_response.type = -1;
	ctx->noresponse = !wait4response;
	wake_up_interruptible(&ctx->ctrldev_wait_request_wq);

	if (!wait4response) {
		dprintk(ctx, "XCH_MSG: %d (DONE)\n", msg->type);
		// no up here, results in orphan requests!
		return 0;
	}

	if (wait_event_interruptible(ctx->ctrldev_wait_response_wq, ctx->ctrldev_response.type != -1)) {
		dprintk(ctx, "XCH_MSG: %d: wait_event interrupted\n", msg->type);
		ctx->ctrldev_request.type = -1;
		up(&ctx->xchange_sem);
		return -ERESTARTSYS;
	}

	BUG_ON(ctx->ctrldev_response.type == -1);

	dprintk(ctx, "XCH_MSG: %d -> %d (DONE)\n", msg->type, ctx->ctrldev_response.type);
	memcpy(msg, &ctx->ctrldev_response, sizeof(struct vtuner_message));
	ctx->ctrldev_response.type = -1;

	up(&ctx->xchange_sem);

	return 0;
}
