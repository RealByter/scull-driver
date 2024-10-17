#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <asm/uaccess.h>

#include "scull.h"

struct scull_pipe
{
    wait_queue_head_t inq, outq;
    char *buffer, *end;
    int buffersize;
    char *rp, *wp;
    int nreaders, nwriters;
    struct fasync_struct *async_queue;
    struct semaphore sem;
    struct cdev cdev;
};

static struct class *scull_pipe_class = NULL;
static int scull_p_nr_devs = SCULL_P_NR_DEVS;
int scull_p_buffer = SCULL_P_BUFFER;
dev_t scull_p_devno;

module_param(scull_p_nr_devs, int, 0);
module_param(scull_p_buffer, int, 0);

static struct scull_pipe *scull_p_devices;

static int scull_p_fasync(int fd, struct file *filp, int mode);
static int spacefree(struct scull_pipe *dev);

static int scull_p_open(struct inode *inode, struct file *filp)
{
    struct scull_pipe *dev;

    dev = container_of(inode->i_cdev, struct scull_pipe, cdev);
    filp->private_data = dev;

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;
    if (!dev->buffer)
    {
        dev->buffer = kmalloc(scull_p_buffer, GFP_KERNEL);
        if (!dev->buffer)
        {
            up(&dev->sem);
            return -ENOMEM;
        }
    }
    dev->buffersize = scull_p_buffer;
    dev->end = dev->buffer + dev->buffersize;
    dev->rp = dev->wp = dev->buffer;

    if (filp->f_mode & FMODE_READ)
        dev->nreaders++;
    if (filp->f_mode & FMODE_WRITE)
        dev->nwriters++;
    up(&dev->sem);

    return nonseekable_open(inode, filp);
}

static int scull_p_release(struct inode *inode, struct file *filp)
{
    struct scull_pipe *dev = filp->private_data;

    // scull_p_fasync(-1, filp, 0);
    down(&dev->sem);
    if (filp->f_mode & FMODE_READ)
        dev->nreaders--;
    if (filp->f_mode & FMODE_WRITE)
        dev->nwriters--;
    if (dev->nreaders + dev->nwriters == 0)
    {
        kfree(dev->buffer);
        dev->buffer = NULL;
    }
    up(&dev->sem);
    return 0;
}

static ssize_t scull_p_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct scull_pipe *dev = filp->private_data;

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    while (dev->rp == dev->wp)
    {
        up(&dev->sem);
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN;
        PDEBUG("\"%s\" reading: going to sleep\n", current->comm);
        if (wait_event_interruptible(dev->inq, (dev->rp != dev->wp)))
            return -ERESTARTSYS;
        if (down_interruptible(&dev->sem))
            return -ERESTARTSYS;
    }

    if (dev->wp > dev->rp)
        count = min(count, (size_t)(dev->wp - dev->rp));
    else
        count = min(count, (size_t)(dev->end - dev->rp));

    if (copy_to_user(buf, dev->rp, count))
    {
        up(&dev->sem);
        return -EFAULT;
    }

    dev->rp += count;
    if (dev->rp == dev->end)
        dev->rp = dev->buffer;
    up(&dev->sem);

    wake_up_interruptible(&dev->outq);
    PDEBUG("\"%s\" did read %li bytes\n", current->comm, (long)count);
    return count;
}

static int spacefree(struct scull_pipe *dev)
{
    if (dev->rp == dev->wp)
        return dev->buffersize - 1;
    return ((dev->rp + dev->buffersize - dev->wp) % dev->buffersize) - 1;
}

static int scull_getwritespace(struct scull_pipe *dev, struct file *filp)
{
    while (spacefree(dev) == 0)
    {
        DEFINE_WAIT(wait);

        up(&dev->sem);
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN;
        PDEBUG("\"%s\" writing: going to sleep\n", current->comm);
        prepare_to_wait(&dev->outq, &wait, TASK_INTERRUPTIBLE);
        if (spacefree(dev) == 0)
            schedule();
        finish_wait(&dev->outq, &wait);
        if (signal_pending(current))
            return -ERESTARTSYS;
        if (down_interruptible(&dev->sem))
            return -ERESTARTSYS;
    }
    return 0;
}

static ssize_t scull_p_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct scull_pipe *dev = filp->private_data;
    int result;

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    result = scull_getwritespace(dev, filp);
    if(result)
        return result;

    count = min(count, (size_t)spacefree(dev));
    if(dev->wp >= dev->rp)
        count = min(count, (size_t)(dev->end - dev->wp));
    else
        count = min(count, (size_t)(dev->rp - dev->wp - 1));
    
    PDEBUG("Going to accept %li bytes to %p from %p\n", (long)count, dev->wp, buf);
    if(copy_from_user(dev->wp, buf, count))
    {
        up(&dev->sem);
        return -EFAULT;
    }

    dev->wp += count;
    if(dev->wp == dev->end)
        dev->wp = dev->buffer;
    up(&dev->sem);

    wake_up_interruptible(&dev->inq);

    if(dev->async_queue)
        kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
    PDEBUG("\"%s\" did write %li bytes\n", current->comm, (long)count);
    return count;
}

struct file_operations scull_pipe_fops = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,
	.read =		scull_p_read,
	.write =	scull_p_write,
	// .poll =		scull_p_poll,
	.unlocked_ioctl = scull_ioctl,
	.open =		scull_p_open,
	.release =	scull_p_release,
	// .fasync =	scull_p_fasync,
};


static void scull_p_setup_cdev(struct scull_pipe *dev, int index)
{
	int err, devno = scull_p_devno + index;
	cdev_init(&dev->cdev, &scull_pipe_fops);
	dev->cdev.owner = THIS_MODULE;
	err = cdev_add (&dev->cdev, devno, 1);
	if (err)
		printk(KERN_NOTICE "Error %d adding scullpipe%d", err, index);
}

 
int scull_p_init(dev_t firstdev)
{
	int i, result;

	result = register_chrdev_region(firstdev, scull_p_nr_devs, "scullp");
	if (result < 0) {
		printk(KERN_NOTICE "Unable to get scullp region, error %d\n", result);
		return 0;
	}
	scull_p_devno = firstdev;
	scull_p_devices = kmalloc(scull_p_nr_devs * sizeof(struct scull_pipe), GFP_KERNEL);
	if (scull_p_devices == NULL) {
		unregister_chrdev_region(firstdev, scull_p_nr_devs);
		return 0;
	}

    scull_pipe_class = class_create("scullp"); 
    if(IS_ERR(scull_pipe_class))
    {
        printk(KERN_ALERT "Failed to create class\n");
        result = PTR_ERR(scull_pipe_class);
        kfree(scull_p_devices);
        unregister_chrdev_region(firstdev, scull_p_nr_devs);
        return 0;
    }

	memset(scull_p_devices, 0, scull_p_nr_devs * sizeof(struct scull_pipe));
	for (i = 0; i < scull_p_nr_devs; i++) {
		init_waitqueue_head(&(scull_p_devices[i].inq));
		init_waitqueue_head(&(scull_p_devices[i].outq));
		sema_init(&scull_p_devices[i].sem, 1);
        device_create(scull_pipe_class, NULL, MKDEV(MAJOR(firstdev), i), NULL, "scullp%d", i);
		scull_p_setup_cdev(scull_p_devices + i, i);
	}

	return scull_p_nr_devs;
}

void scull_p_cleanup(void)
{
	int i;

	if (!scull_p_devices)
		return; 

	for (i = 0; i < scull_p_nr_devs; i++) {
		cdev_del(&scull_p_devices[i].cdev);
		kfree(scull_p_devices[i].buffer);
	}
	kfree(scull_p_devices);
	unregister_chrdev_region(scull_p_devno, scull_p_nr_devs);
	scull_p_devices = NULL; 
}