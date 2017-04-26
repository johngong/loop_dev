#include <linux/module.h>
#include <linux/sched.h>
#include <linux/fs.h>

struct loop_dev {
	struct block_device	*lo_device
	struct file	*lo_backing_file;
	spinlock_t	lo_lock;
	struct request_queue	*lo_q;
	struct blk_mq_tag_set	tag_set;
	struct gendisk	*lo_disk;
}

static add_loop_dev(struct loop_dev **ld)
{
	
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
