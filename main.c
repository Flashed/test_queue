/*
===============================================================================
Driver Name		:		test_queue
Author			:		Mikhail Z. <flashed@mail.ru>
License			:		GPL
===============================================================================
*/

#include<linux/init.h>
#include<linux/module.h>
#include<linux/fs.h>
#include<linux/cdev.h>
#include<asm/uaccess.h>
#include<linux/kfifo.h>
#include<linux/slab.h>
#include<linux/vmalloc.h>
#include<linux/time.h>
#include<linux/kthread.h>
#include<linux/sched.h>
#include<linux/delay.h>
#include <linux/semaphore.h>
#include"fileops.h"



MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mikhail Z. <flashed@mail.ru>");

#define DRIVER_NAME "test_queue"
#define PDEBUG(fmt,args...) printk(KERN_DEBUG "%s: "fmt,DRIVER_NAME, ##args)
#define PERR(fmt,args...) printk(KERN_ERR "%s : "fmt,DRIVER_NAME,##args)
#define PINFO(fmt,args...) printk(KERN_INFO "%s :"fmt,DRIVER_NAME, ##args)

#define FIRST_DEV_NUM 0
#define COUNT_DEV_NUM 1
#define MAX_MESS_SIZE (64 * 1024)

static dev_t dev_num_push = -1;
static struct cdev *dev_push = NULL;
static volatile unsigned int dev_push_busy = 0;
static char *mess_buff_push;

static dev_t dev_num_pop = -1;
static struct cdev *dev_pop = NULL;
static volatile unsigned int dev_pop_busy = 0;
static unsigned int dev_pop_end_mess = 0;
static struct limited_buff *read_item = NULL;

static struct kfifo queue;
static struct kfifo files_queue;
static unsigned int mess_size_count = 0;
static DEFINE_SPINLOCK(swap_lock);

//struct for keep queue item information
struct limited_buff{
	unsigned int len;
	char *buff;
};

//struct for keep swap file information
struct file_pointer{
	unsigned int len;
	char path[64];
};

//Thread for queue swap

static struct task_struct *task;

int thread_function(void *data)
{
	struct file *f;
	struct file_pointer fp;
	struct limited_buff lb;

	while(!kthread_should_stop()){

		spin_lock(&swap_lock);
		while (!kfifo_is_empty(&files_queue)
				&& kfifo_avail(&queue)){

			if (kfifo_out(&files_queue, &fp, sizeof(fp)) == 0){
				PERR("Failed to read item from files_queue.\n");
				spin_unlock(&swap_lock);
				break;
			}
			f = file_open(fp.path, O_RDONLY,0640);
			if (!f){
				PERR("Failed to open %s queue message file.\n", fp.path);
				spin_unlock(&swap_lock);
				return 0;
			}
			lb.buff = vmalloc(fp.len);
			if (!lb.buff){
				spin_unlock(&swap_lock);
				return -EFAULT;
			}
			lb.len = fp.len;
			file_read(f, 0, lb.buff, lb.len);
			file_close(f);
			kfifo_in(&queue, &lb, sizeof(lb));
			file_remove(fp.path);
			PINFO("Swap '%s' message to queue. Item size: %d\n", fp.path, lb.len);
		}
		spin_unlock(&swap_lock);
		msleep(1000);
	}
	return 0;

}


//DEV POP (device for pop items)------------------------------------------------------
static int dev_pop_open(struct inode *inode, struct file *file)
{
	if (dev_pop_busy){
		return -EBUSY;
	}
	dev_pop_busy = 1;

	spin_lock(&swap_lock);
	if (kfifo_is_empty(&queue)){
		dev_pop_end_mess = 1;
		spin_unlock(&swap_lock);
		return 0;
	}
	read_item = vmalloc(sizeof(struct limited_buff));
	if (kfifo_out(&queue, read_item, sizeof(struct limited_buff)) == 0){
		spin_unlock(&swap_lock);
		return -EFAULT;
	}
	spin_unlock(&swap_lock);

	dev_pop_end_mess = 0;
	return 0;
}

static ssize_t dev_pop_read(struct file *file, char __user *buff, size_t size, loff_t *offset)
{
	int read = 0;
	if (dev_pop_end_mess == 1 ){
		//End of message
		return 0;
	}
	read = size - *offset;
	if (read_item->len <= read){
		read = read_item->len;
		if (copy_to_user(buff, read_item->buff, read)){
			return -EFAULT;
		}
		dev_pop_end_mess = 1;
	} else {
		if (copy_to_user(buff, read_item->buff, read)){
			return -EFAULT;
		}
		read_item->buff += read;
		read_item->len -= read;
	}
	return read;
}

static int dev_pop_release(struct inode *inode, struct file *file)
{
	dev_pop_end_mess = 1;
	if (read_item != NULL){
		kvfree(read_item);
		read_item = NULL;
	}

	PINFO("Pop item from queue. Queue size: %ld\n", (kfifo_len(&queue)/(sizeof(struct limited_buff))));
	dev_pop_busy = 0;
	return 0;
}

struct file_operations dev_pop_fops = {
		.owner = THIS_MODULE,
		.open = dev_pop_open,
		.read = dev_pop_read,
		.release = dev_pop_release
};

//<<<< DEV POP----------------------------------------------------------------

//DEV PUSH (device for push items) -------------------------------------------------------------------

static int dev_push_open(struct inode *inode, struct file *file)
{
	if (dev_push_busy){
		return -EBUSY;
	}
	dev_push_busy = 1;
	mess_buff_push = vmalloc(MAX_MESS_SIZE);
	if (!mess_buff_push){
		PERR("Failed to alloc message buffer.\n");
		return -EFAULT;
	}
	return 0;
}

static ssize_t dev_push_write(struct file *file, const char __user *buff, size_t size, loff_t *offset)
{
	mess_size_count += (size -(*offset));
	if (mess_size_count > MAX_MESS_SIZE){
		mess_size_count = 0;
		PERR("Write too many data. Max message leingth is %d bytes\n", MAX_MESS_SIZE);
		return mess_size_count;
	}
	if (copy_from_user(mess_buff_push, buff, mess_size_count)){
		return -EFAULT;
	}
	return mess_size_count;
}

static int dev_push_release(struct inode *inode, struct file *file)
{
	struct limited_buff lb;
	struct file_pointer fp;
	struct file* f;
	struct timespec time;
	getnstimeofday(&time);


	if (mess_size_count == 0){
		goto exit;
	}
	lb.buff = mess_buff_push;
	lb.len = mess_size_count;

	spin_lock(&swap_lock);
	if (!kfifo_is_empty(&files_queue)
			|| !kfifo_avail(&queue)){
		fp.len = mess_size_count;
		sprintf(fp.path, "/tmp/%ld", time.tv_nsec);

		f = file_open(fp.path,  O_WRONLY | O_CREAT | O_TRUNC, 0666);
		file_write(f, 0, lb.buff, fp.len);
		file_sync(f);
		file_close(f);
		kfifo_in(&files_queue, &fp, sizeof(fp));
		PINFO("Pushed item to SWAP queue. File: %s \n", fp.path);
	} else {
		kfifo_in(&queue, &lb, sizeof(lb));
		PINFO("Pushed item to queue. Queue size: %ld\n", (kfifo_len(&queue)/(sizeof(lb))));
	}

	spin_unlock(&swap_lock);
exit:
	mess_size_count = 0;
	dev_push_busy = 0;

	return 0;
}

struct file_operations dev_push_fops = {
		.owner = THIS_MODULE,
		.open = dev_push_open,
		.write = dev_push_write,
		.release = dev_push_release
};

//<<<< DEV PUSH--------------------------------------------------------


static void free_if_alloc(void)
{
	if (task != NULL){
		kthread_stop(task);
	}

	if (dev_push != NULL){
		cdev_del(dev_push);
	}
	if (dev_pop != NULL){
			cdev_del(dev_pop);
	}
	if (dev_num_push != -1){
		unregister_chrdev_region(dev_num_push, COUNT_DEV_NUM);
	}
	if (dev_num_pop != -1){
		unregister_chrdev_region(dev_num_pop, COUNT_DEV_NUM);
	}

	if (mess_buff_push != NULL){
		kvfree(mess_buff_push);
	}
	if (read_item != NULL){
		kvfree(read_item);
	}
	spin_unlock(&swap_lock);
	kfifo_free(&queue);
	kfifo_free(&files_queue);

}

static int __init test_zaytsev_mod_init(void)
{

	int err = 0;

	//Init push device
	err = alloc_chrdev_region(&dev_num_push, FIRST_DEV_NUM, COUNT_DEV_NUM, "queue_push");
	if (err != 0){
		PERR("Failed to alloc chdev region. Error code: %d\n", err);
		return err;
	}

	dev_push = cdev_alloc();
	dev_push->ops = &dev_push_fops;

	err = cdev_add(dev_push, dev_num_push, COUNT_DEV_NUM);
	if (err != 0){
		PERR("Failed to add cdev. Error code: %d\n", err);
		free_if_alloc();
		return err;
	}

	//Init pop device
	err = alloc_chrdev_region(&dev_num_pop, FIRST_DEV_NUM, COUNT_DEV_NUM, "queue_pop");
	if (err != 0){
		PERR("Failed to alloc chdev region. Error code: %d\n", err);
		free_if_alloc();
		return err;
	}

	dev_pop = cdev_alloc();
	dev_pop->ops = &dev_pop_fops;

	err = cdev_add(dev_pop, dev_num_pop, COUNT_DEV_NUM);
	if (err != 0){
		PERR("Failed to add cdev. Error code: %d\n", err);
		free_if_alloc();
		return err;
	}

	//Init queue
	err = kfifo_alloc(&queue, (1024 * sizeof(struct limited_buff)), GFP_KERNEL);
	if (err != 0){
		PERR("Failed to allocate queue. Error code: %d\n", err);
		free_if_alloc();
		return err;
	}

	//Init files_queue (swap loading)
	err = kfifo_alloc(&files_queue, (1024 * sizeof(struct file_pointer)), GFP_KERNEL);
	if (err != 0){
		PERR("Failed to allocate files_queue. Error code: %d\n", err);
		free_if_alloc();
		return err;
	}

	task = kthread_run(&thread_function, 0,"queue_swap_thread");

	return 0;
}

static void __exit test_zaytsev_mod_exit(void)
{
	free_if_alloc();
}

module_init(test_zaytsev_mod_init);
module_exit(test_zaytsev_mod_exit);

