#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/blk-mq.h>
#include <linux/blkdev.h>

struct loop_cmd {
	struct kthread_work	work;
	struct list_head	list;
	struct request	*rq;
	bool use_aio;
	struct kiocb	iocb;
};

struct loop_dev {
	struct file	*lo_backing_file;
	struct block_device	*bd;
	spinlock_t	lo_lock;
	struct blk_mq_tag_set	tag_set;
	struct request_queue	*lo_q;
	struct gendisk	*gd;
	atomic_t	lo_refcnt;
};
