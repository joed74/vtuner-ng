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
#define VTUNER_DELSYS_LEN (sizeof(struct vtuner_delsys))

static ssize_t vtunerc_ctrldev_write(struct file *filp, const char *buff, size_t len, loff_t *off)
{
	struct vtunerc_ctx *ctx = filp->private_data;
	struct dvb_demux *demux = &ctx->demux;
	struct dmx_section_feed *sec;
	unsigned short pid;
	int tailsize, i, cc, cc_, idx, offs, pf, pesh, tune_id;
	bool sendfiller, pusi;
	struct vtunerc_feedinfo *fi;

	if (len < 188) {
		printk(KERN_ERR "vtunerc%d: Data is shorter then TS packet size (%lu < 188)\n", ctx->idx, len);
		return -EINVAL;
	}

	if (down_interruptible(&ctx->tswrite_sem)) {
		return -ERESTARTSYS;
	}

	tailsize = len % 188;
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
			up(&ctx->tswrite_sem);
			return -ENOMEM;
		}
		ctx->kernel_buf_size = len;
		printk(KERN_INFO "vtunerc%d: allocated buffer of %zu bytes\n", ctx->idx, len);
	}

	if (copy_from_user(ctx->kernel_buf, buff, len)) {
		printk(KERN_ERR "vtunerc%d: userdata passing error\n", ctx->idx);
		up(&ctx->tswrite_sem);
		return -EINVAL;
	}

	if (ctx->paused) {
		up(&ctx->tswrite_sem);
		return len;
	}

	for (i = 0; i < len; i += 188) {
		tune_id = (ctx->kernel_buf[i] & 0x38) >> 3;
		ctx->kernel_buf[i] &= 0xC7; // clear out optional tune_id
		if (ctx->kernel_buf[i] != 0x47) { /* start of TS packet */
			printk(KERN_ERR "vtunerc%d: Data not start on packet boundary: index=%d data=%02x %02x %02x %02x %02x ...\n",
					ctx->idx, i / 188, ctx->kernel_buf[i], ctx->kernel_buf[i + 1],
					ctx->kernel_buf[i + 2], ctx->kernel_buf[i + 3], ctx->kernel_buf[i + 4]);
			up(&ctx->tswrite_sem);
			return -EINVAL;
		}

		pusi = 0;
		idx = -1;
		pid = ((ctx->kernel_buf[i+1] & 0x1f)<<8)|ctx->kernel_buf[i+2];
		if (pid==0x1fff) {
			ctx->stat_fe_data += 188; // external filler
		} else {
			cc = ctx->kernel_buf[i+3] & 0x0f;
			sendfiller = 1;
			if (!tune_id || tune_id==ctx->tune_id) {
				if (!(ctx->status & FE_HAS_LOCK)) {
					dprintk(ctx, "set signal LOCK id=%i\n", tune_id);
					ctx->status = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK; // TS packets -> we habe a lock!
				}
				idx = feedtab_find_pid(ctx, pid);
				if (idx > -1) {
					fi = (struct vtunerc_feedinfo *) &ctx->feedinfo[idx];
					if (ctx->demux.feed[idx].pusi_seen) sendfiller = 0; // pusi already seen
					pusi = (ctx->kernel_buf[i+1] & 0x40);
					if (pusi) {
						offs = 4;
						if ((ctx->kernel_buf[i+3] & 0x30)==0x30) offs+=ctx->kernel_buf[i+4]+1;
					}

					if (((ctx->kernel_buf[i+3] & 0x20)==0x20) && (ctx->kernel_buf[i+4]==0xB7) && (fi->id == -1)) {
						fi->id = -2;
						sendfiller = 0; // packet is already a filler
					}

					if (ctx->demux.feed[idx].type == DMX_TYPE_TS)
					{
						if (pusi && (!ctx->demux.feed[idx].pusi_seen || fi->id == -2)) {
							dprintk(ctx,"found pusi for pid %it\n", pid);
							sendfiller = 0;
							// PES
							if (offs>0 && offs<182 && (ctx->kernel_buf[i+offs]==0) && (ctx->kernel_buf[i+offs+1]==0) && (ctx->kernel_buf[i+offs+2]==1)) {
								fi->id = ctx->kernel_buf[i+offs+3];
								if (fi->id == 0xbd) {
									pesh=0;
									if ((ctx->kernel_buf[i+offs+6] & 0xC0)==0x80) pesh = ctx->kernel_buf[i+offs+8]+3;
									if (ctx->kernel_buf[i+offs+pesh+6]==0x0B && ctx->kernel_buf[i+offs+pesh+7]==0x77) fi->subid=0x6a;
									if (ctx->kernel_buf[i+offs+pesh+6]==0x20) fi->subid=0x59;
								}
							}
						}
					}

					if (ctx->demux.feed[idx].type == DMX_TYPE_SEC)
					{
						if (pusi && (!ctx->demux.feed[idx].pusi_seen || fi->id == -2)) {
							dprintk(ctx,"found pusi for pid %is (cc=%i)\n", pid, cc);
							sendfiller = 0;
							// now start section feed
							sec = &ctx->demux.feed[idx].feed.sec;
							sec->is_filtering = 1;
							ctx->demux.feed[idx].state = DMX_STATE_GO;
							cc_ = cc - 1;
							if (cc_ == -1) cc_ = 15;
							ctx->demux.feed[idx].cc = cc_;
							ctx->demux.feed[idx].pusi_seen = 1;
							// PSI
							pf = ctx->kernel_buf[i+offs];
							if (offs+1+pf<188)
								fi->id = ctx->kernel_buf[i+offs+1+pf];
						}
					}
				}
			}

			if (sendfiller) {
				ctx->kernel_buf[i+1]=0x1F; // pid 0x1FFF
				ctx->kernel_buf[i+2]=0xFF;
				ctx->kernel_buf[i+3]=0x20+cc; // only adaption
				ctx->kernel_buf[i+4]=0xB7; // adaption field length (whole packet)
				ctx->kernel_buf[i+5]=0x00; // adaption fileds (none)
				memset(&ctx->kernel_buf[i+6],0xff,182);
				ctx->stat_fi_data += 188;
			}
		}
		dvb_dmx_swfilter_packets(demux, &ctx->kernel_buf[i], 1);
		if (idx > -1 && pusi && ctx->demux.feed[idx].type == DMX_TYPE_TS) ctx->demux.feed[idx].pusi_seen=1;
	}

	if (waitqueue_active(&ctx->rbuf.queue)) {
		dvb_ringbuffer_write(&ctx->rbuf, ctx->kernel_buf, len);
		wake_up(&ctx->rbuf.queue);
	}

	ctx->stat_wr_data += len;
	up(&ctx->tswrite_sem);

	return len;
}

static ssize_t vtunerc_ctrldev_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	struct vtunerc_ctx *ctx = filp->private_data;
	int left, avail;

	left = count;
	while (left) {
		if (wait_event_interruptible(
			    ctx->rbuf.queue,
			    dvb_ringbuffer_avail(&ctx->rbuf) > 0) < 0)
			return -EAGAIN;
		avail = dvb_ringbuffer_avail(&ctx->rbuf);
		if (avail > left)
			avail = left;
		dvb_ringbuffer_read_user(&ctx->rbuf, buf, avail);
		left -= avail;
		buf += avail;
	}
	return count;
}

static int vtunerc_ctrldev_open(struct inode *inode, struct file *filp)
{
	struct vtunerc_ctx *ctx;
	int minor;

	minor = MINOR(inode->i_rdev);
	ctx = filp->private_data = vtunerc_get_ctx(minor);
	if (ctx == NULL)
		return -ENOMEM;
	if (ctx->fd_opened == 0) dvb_proxyfe_set_delsys_info(ctx->fe);
	ctx->fd_opened++;
	return 0;
}

static int vtunerc_ctrldev_close(struct inode *inode, struct file *filp)
{
	struct vtunerc_ctx *ctx = filp->private_data;
	int minor;

	dprintk(ctx, "closing (fd_opened=%d)\n", ctx->fd_opened);

	ctx->fd_opened--;

	minor = MINOR(inode->i_rdev);

	/* set FAKE response, to allow finish any waiters
	   in vtunerc_ctrldev_xchange_message() */
	ctx->ctrldev_response.type = 0;
	wake_up_interruptible(&ctx->ctrldev_wait_response_wq);

	if (ctx->fd_opened == 0) {
		ctx->status = FE_NONE;
		ctx->stat_time = 0;
		ctx->stat_wr_data = 0;
		ctx->stat_fi_data = 0;
		ctx->stat_fe_data = 0;
		memset(&ctx->signal,0,sizeof(struct vtuner_signal));
		ctx->fe_params.delivery_system = 0; // now retune can happen
		dvb_proxyfe_clear_delsys_info(ctx->fe);
	}
	return 0;
}

static long vtunerc_ctrldev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct vtunerc_ctx *ctx = file->private_data;
	struct vtuner_delsys delsys;
	int ret = 0, i;

	if (down_interruptible(&ctx->ioctl_sem))
		return -ERESTARTSYS;

	switch (cmd) {
	case VTUNER_SET_SIGNAL:
		if (copy_from_user(&ctx->signal, (char *)arg, VTUNER_SIG_LEN)) {
			ret = -EFAULT;
		}
		dvb_proxyfe_set_signal(ctx);
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

	case VTUNER_SET_DELSYS:
		if (copy_from_user(&delsys, (char *) arg, VTUNER_DELSYS_LEN)) {
			ret = -EFAULT;
		}
		// sanity check, we dont allow all
		for (i=0; i<VTUNER_MAX_DELSYS; i++)
		{
			if (delsys.value[i]==4 || (delsys.value[i]>6 && delsys.value[i]<16) || delsys.value[i]>19)
				ret = -EINVAL;
		}
		if (ret==0 && !ctx->fe) ret=-EFAULT;
		if (ret==0) {
			memcpy(&ctx->fe->ops.delsys, &delsys.value, MAX_DELSYS*sizeof(u8));
			printk(KERN_INFO "vtunerc%d: setting delsys to", ctx->idx);
			for (i=0; i<VTUNER_MAX_DELSYS; i++) {
				switch (delsys.value[i])
				{
				    case 1:
				      printk(KERN_CONT " DVBC");
				      break;
				    case 2:
				      printk(KERN_CONT " DVBC_B");
				      break;
				    case 3:
				      printk(KERN_CONT " DVBT");
				      break;
				    case 5:
				      printk(KERN_CONT " DVBS");
				      break;
				    case 6:
				      printk(KERN_CONT " DVBS2");
				      break;
				    case 16:
				      printk(KERN_CONT " DVBT2");
				      break;
				    case 18:
				      printk(KERN_CONT " DVBC_C");
				      break;
				    case 19:
				      printk(KERN_CONT " DVBC2");
				      break;
				}
			}
			printk(KERN_CONT "\n");
		}
		break;

	default:
		printk(KERN_ERR "vtunerc%d: unknown IOCTL 0x%x\n", ctx->idx, cmd);
		ret = -ENOTTY; /* Linus: the only correct one return value for unsupported ioctl */
		break;
	}
	up(&ctx->ioctl_sem);

	return ret;
}

static long compat_vtunerc_ctrldev_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    return vtunerc_ctrldev_ioctl(file, cmd, arg);
}

static unsigned int vtunerc_ctrldev_poll(struct file *filp, poll_table *wait)
{
	struct vtunerc_ctx *ctx = filp->private_data;
	unsigned int mask = 0;

	poll_wait(filp, &ctx->ctrldev_wait_request_wq, wait);
	poll_wait(filp, &ctx->rbuf.queue, wait);

	if (!dvb_ringbuffer_empty(&ctx->rbuf))
		mask |= EPOLLIN | EPOLLRDNORM;

	if (ctx->ctrldev_request.type > -1) {
		mask |= POLLPRI;
	}

	return mask;
}

/* ------------------------------------------------ */

static const struct file_operations vtunerc_ctrldev_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = vtunerc_ctrldev_ioctl,
	.compat_ioctl = compat_vtunerc_ctrldev_ioctl,
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

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0)
	pclass = class_create(THIS_MODULE, "vtuner");
#else
	pclass = class_create("vtuner");
#endif
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
		up(&ctx->xchange_sem);
		return 0;
	}

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
		// no up here, results in orphan requests!
		return 0;
	}

	if (wait_event_interruptible(ctx->ctrldev_wait_response_wq, ctx->ctrldev_response.type != -1)) {
		ctx->ctrldev_request.type = -1;
		up(&ctx->xchange_sem);
		return -ERESTARTSYS;
	}

	BUG_ON(ctx->ctrldev_response.type == -1);

	memcpy(msg, &ctx->ctrldev_response, sizeof(struct vtuner_message));
	ctx->ctrldev_response.type = -1;

	up(&ctx->xchange_sem);

	return 0;
}
