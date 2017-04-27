#include <linux/module.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include "loop.h"


static int lo_open(block_device *bdev, fmode_t mode)
{
	int err = 0;
	struct loop_dev *ld;
	ld = bdev->bd_disk->private_date;
	if (!ld) {
		err = -ENXIO;
		goto out;
	}
	atomic_inc(&ld->lo_refcnt);
out:
	return err;
}

static int lo_release()
{

}
static const block_device_operations lo_mq_ops = {
	.owner	=	THIS_MODULE,
	.open	=	lo_open,
	.release	=	lo_release,
	.ioctl	=	lo_ioctl,
	.compat_ioctl	=	lo_compat_ioctl,	
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
	
	err = blk_mq_alloc_yag_set(&l->tag_set);
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
