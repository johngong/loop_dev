#include <linux/module.h>

struct loop_cmd {
	struct list_head	list;
	struct request	*rq;
	struct kthread_work	work;
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
