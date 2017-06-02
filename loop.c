#include <linux/module.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include "loop.h"

static DEFINE_MUTEX(loop_index_mutex);


static int lo_ioctl(struct block_device *bd, fmode_t mode,
		unsigned int cmd, usigned long arg)
{
	return 0;
}

static int lo_release(struct gendisk *bd, fmode mode)
{
	return 0;
}

static int lo_open(struct block_device *bd, fmode mode)
{
	struct loop_dev *lo;
	int err = 0;

	mutex_lock(loop_index_mutex);
	lo = bd->gd->private_data;
	if (!lo) {
		err = -ENXIO;
		goto out;
	}

	atomic_increase(&lo->lo_refcnt);
out:
	mutex_unlock(loop_index_mutex);
	return err;
}

static const block_device_operations lo_fops = {
	.owner	=	THIS_MODULE,
	.open	=	lo_open,
	.release	=	lo_release,
	.ioctl	=	lo_ioctl,
};

static struct blk_mq_ops lo_mq_ops = {
	.queue_rq	=	lo_queue_rq,
	.init_request	=	lo_init_request,
	.complete	=       lo_complete_request,
}

static add_loop_dev(struct loop_dev **ld)
{
	int err, i;
	struct loop_dev	*l;
	struct gendisk	*gd;

	l = kzalloc(sizeof(*l), GFP_KERNEL);
	l->lo_state = 0;
	err = idr_alloc(&loop_idr, l, 0, 0, GFP_KERNEL);

	i = err;

	l->tag_set.ops = &lo_mq_ops;
	l->tag_set.nr_hw_queues = 1;
	l->tag_set.queue_depth = 128;
	l->tag_set.numa_node = NUMA_NO_NODE;
	l->tag_set.cmd_size = sizeof(struct loop_cmd);
	l->tag_set.flags = BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_SG_MERGE;
	l->tag_set.driver_data = l;

	err = blk_mq_alloc_tag_set(&l->tag_set);
	l->lo_q = blk_mq_init_queue(&l->tag_set);

	l->lo_q->queuedata = l;

	__set_bit(QUEUE_FLAG_NOMERGES, &l->lo_q->queue_flags);

	gd = l->lo_disk = alloc_disk(0);
	gd->major = major;
	gd->first_minor = 0;
	gd->fops = &lo_fops;
	gd->private_date = l;
	gd->queue = l->lo_q;

	add_disk(gd);

	*ld = l;
	return 0;
}


static int major;
static int __init loop_init(void)
{
	struct loop_dev *ld;

	major = register_blkdev(0, "jgloop")
	if (major < 0)
		return -ENOMEM;

	kobj_map(bdev_map, MKDEV(major, 0), (1UL << 20), THIS_MODULE, loop_probe, NULL, NULL);
	add_loop_dev(&ld);
	return 0;
}

static void __exit loop_exit(void)
{
	kobject_unmap(bdev_map, MKDEV(major, 0), (1UL << 20));
}

module_init(loop_init);
module_exit(loop_exit);
