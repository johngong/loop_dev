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

static int lo_queue_rq(struct blk_mq_hw_ctx *hctx,
		const struct blk_mq_queue_data *bd)
{
	struct lo_cmd *cmd = blk_mq_rq_to_pdu(bd->rq);
	struct loop_device *lo = cmd->rq->queue->queuedata;
	blk_mq_start_request(bd->rq);

	switch(req_op(cmd->rq)) {
	case REQ_OP_FLUSH:
	case REQ_OP_DISCARD:
	case REQ_OP_WRITE_ZEROS:
		cmd->use_aio = false;	
		break;
	default:
		cmd->use_aio = lo->use_aio;
		break;
	}

	kthread_queue_work(&lo->worker, &cmd->work);
	return BLK_MQ_RQ_QUEUE_OK;
}

static int lo_read_simple(struct loop_device *lo, struct request *rq,
                loff_t pos)
{
        struct bio_vec bvec;      
        struct req_iterator iter;
        struct iov_iter i;
        ssize_t len;
                
        rq_for_each_segment(bvec, rq, iter) {
                iov_iter_bvec(&i, ITER_BVEC, &bvec, 1, bvec.bv_len);
                len = vfs_iter_read(lo->lo_backing_file, &i, &pos);
                if (len < 0)
                        return len;
        
                flush_dcache_page(bvec.bv_page);
                                                                                                                                                               
                if (len != bvec.bv_len) {
                        struct bio *bio;
 
                        __rq_for_each_bio(bio, rq)
                                zero_fill_bio(bio);
                        break;
                }
                cond_resched();
        }
 
        return 0;
}

static int lo_read_transfer(struct loop_device *lo, struct request *rq,
                loff_t pos)
{               
        struct bio_vec bvec, b;
        struct req_iterator iter;
        struct iov_iter i;
        struct page *page;
        ssize_t len;
        int ret = 0;
                
        page = alloc_page(GFP_NOIO);
        if (unlikely(!page))
                return -ENOMEM;
                
        rq_for_each_segment(bvec, rq, iter) {
                loff_t offset = pos;
                
                b.bv_page = page;
                b.bv_offset = 0;
                b.bv_len = bvec.bv_len;
                
                iov_iter_bvec(&i, ITER_BVEC, &b, 1, b.bv_len);
                len = vfs_iter_read(lo->lo_backing_file, &i, &pos);                                                                                            
                if (len < 0) {
                        ret = len;
                        goto out_free_page;
                }

                ret = lo_do_transfer(lo, READ, page, 0, bvec.bv_page,
                        bvec.bv_offset, len, offset >> 9);
                if (ret)
                        goto out_free_page;
         
                flush_dcache_page(bvec.bv_page);
         
                if (len != bvec.bv_len) {
                        struct bio *bio;
         
                        __rq_for_each_bio(bio, rq)
                                zero_fill_bio(bio);
                        break;
                }
        }
         
        ret = 0;
out_free_page:
        __free_page(page);
        return ret;
}

static int lo_write_simple(struct loop_device *lo, struct request *rq,
                loff_t pos)
{      
        struct bio_vec bvec;
        struct req_iterator iter;
        int ret = 0;
       
        rq_for_each_segment(bvec, rq, iter) {
                ret = lo_write_bvec(lo->lo_backing_file, &bvec, &pos);
                if (ret < 0)
                        break;
                cond_resched();
        }
       
        return ret;
} 

static int lo_write_transfer(struct loop_device *lo, struct request *rq,
                loff_t pos)
{      
        struct bio_vec bvec, b;
        struct req_iterator iter;
        struct page *page;
        int ret = 0;
       
        page = alloc_page(GFP_NOIO);
        if (unlikely(!page))
                return -ENOMEM;
       
        rq_for_each_segment(bvec, rq, iter) {
                ret = lo_do_transfer(lo, WRITE, page, 0, bvec.bv_page,
                        bvec.bv_offset, bvec.bv_len, pos >> 9);
                if (unlikely(ret))
                        break;
       
                b.bv_page = page;
                b.bv_offset = 0;
                b.bv_len = bvec.bv_len;
                ret = lo_write_bvec(lo->lo_backing_file, &b, &pos);
                if (ret < 0)
                        break;
        }
                                                                                                                                                               
        __free_page(page);
        return ret;
} 

static int do_req_filebacked(struct loop_device *lo, struct request *rq)
{
        struct loop_cmd *cmd = blk_mq_rq_to_pdu(rq);
        loff_t pos = ((loff_t) blk_rq_pos(rq) << 9) + lo->lo_offset;

        switch (req_op(rq)) {
        case REQ_OP_FLUSH:
                return lo_req_flush(lo, rq);
        case REQ_OP_DISCARD:
        case REQ_OP_WRITE_ZEROES:
                return lo_discard(lo, rq, pos);
        case REQ_OP_WRITE:
                if (lo->transfer)
                        return lo_write_transfer(lo, rq, pos);
                else if (cmd->use_aio)
                        return lo_rw_aio(lo, cmd, pos, WRITE);
                else
                        return lo_write_simple(lo, rq, pos);
        case REQ_OP_READ:
                if (lo->transfer)
                        return lo_read_transfer(lo, rq, pos);
                else if (cmd->use_aio)
                        return lo_rw_aio(lo, cmd, pos, READ);
                else
                        return lo_read_simple(lo, rq, pos);
        default:
                WARN_ON_ONCE(1);
                return -EIO;
                break;
        } 
}

static void loop_handle_cmd(struct loop_cmd *cmd)
{
	const bool write = op_is_write(req_op(cmd->rq));
	struct loop_dev *lo = cmd->rq->q->queuedata;
	int ret = 0;

	ret = do_req_filebacked(lo, cmd->rq);

	if (!cmd->use_aio || ret) {
		cmd->ret = ret ? -EIO : 0;
		blk_mq_complete_request(cmd->rq);
	}
}

static void loop_queue_work(struct kthread_work *work)
{
	struct loop_cmd *cmd =
		container_of(work, struct loop_cmd, work);
	loop_handle_cmd(cmd);
}

static int lo_init_request(struct blk_mq_tag_set *set, struct request *rq,
		unsigned int hctx_idx, unsigned int numa_node)
{
	struct loop_cmd *cmd = blk_mq_rq_to_pdu(rq);

	cmd->rq = rq;
	kthread_init_work(&cmd->work, loop_queue_work);
	return 0;
}

static void lo_complete_request(struct request *rq)
{
        struct loop_cmd *cmd = blk_mq_rq_to_pdu(rq);

        if (unlikely(req_op(cmd->rq) == REQ_OP_READ && cmd->use_aio &&
                     cmd->ret >= 0 && cmd->ret < blk_rq_bytes(cmd->rq))) {
                struct bio *bio = cmd->rq->bio;

                bio_advance(bio, cmd->ret);
                zero_fill_bio(bio);
        }

        blk_mq_end_request(rq, cmd->ret < 0 ? -EIO : 0);
}

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
