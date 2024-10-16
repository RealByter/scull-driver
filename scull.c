#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include "scull.h"

MODULE_LICENSE("Dual BSD/GPL");

static int scull_quantum = SCULL_QUANTUM;
static int scull_qset = SCULL_QSET;
module_param(scull_quantum, int, S_IRUGO);
module_param(scull_qset, int, S_IRUGO);

static dev_t dev;
static struct class *scull_class = NULL;

struct scull_qset
{
    void **data;
    struct scull_qset *next;
};

struct scull_dev
{
    struct scull_qset *data;
    int quantum;
    int qset;
    unsigned int size;
    struct cdev cdev;
};

struct scull_dev scull_devs[4];

loff_t scull_llseek(struct file *filp, loff_t off, int whence);
ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
long scull_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
int scull_open(struct inode *inode, struct file *filp);
int scull_release(struct inode *inode, struct file *filp);

int scull_trim(struct scull_dev *dev)
{
    struct scull_qset *next, *dptr;
    int qset = dev->qset;
    int i = 0;

    for (dptr = dev->data; dptr; dptr = next)
    {
        if (dptr->data)
        {
            for (i = 0; i < qset; i++)
            {
                kfree(dptr->data[i]);
            }
            kfree(dptr->data);
            dptr->data = NULL;
        }
        next = dptr->next;
        kfree(dptr);
    }
    dev->size = 0;
    dev->quantum = scull_quantum;
    dev->qset = scull_qset;
    dev->data = NULL;
    return 0;
}

struct scull_qset *scull_follow(struct scull_dev *dev, int n)
{
    struct scull_qset *qs = dev->data;

    if (!qs)
    {
        qs = dev->data = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
        if (qs == NULL)
            return NULL;
        memset(qs, 0, sizeof(struct scull_qset));
    }

    while (n--)
    {
        if (!qs->next)
        {
            qs->next = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
            if (qs->next == NULL)
                return NULL;
            memset(qs->next, 0, sizeof(struct scull_qset));
        }
        qs = qs->next;
        continue;
    }
    return qs;
}

loff_t scull_llseek(struct file *filp, loff_t off, int whence)
{
    printk(KERN_ALERT "scull_llseek\n");
    return 0;
}

ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    printk(KERN_ALERT "scull_read\n");

    struct scull_dev *dev = filp->private_data;
    struct scull_qset *dptr;
    int quantum = dev->quantum;
    int itemsize = quantum * dev->qset;
    int qset = dev->qset;
    int item, s_pos, q_pos, rest;
    ssize_t retval = 0;

    if (*f_pos >= dev->size)
        goto out;
    if (*f_pos + count > dev->size)
        count = dev->size - *f_pos;

    item = (long)*f_pos / itemsize;
    rest = (long)*f_pos % itemsize;
    s_pos = rest / quantum;
    q_pos = rest % quantum;

    dptr = scull_follow(dev, item);

    if (dptr == NULL || !dptr->data || !dptr->data[s_pos])
        goto out;

    if (count > quantum - q_pos)
        count = quantum - q_pos;

    if (copy_to_user(buf, dptr->data[s_pos] + q_pos, count))
    {
        retval = -EFAULT;
        goto out;
    }

    *f_pos += count;
    retval = count;

out:
    return retval;
}

ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    printk(KERN_ALERT "scull_write\n");

    struct scull_dev *dev = filp->private_data;
    struct scull_qset *dptr;
    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset;
    int item, s_pos, q_pos, rest;
    ssize_t retval = -ENOMEM;

    item = (long)*f_pos / itemsize;
    rest = (long)*f_pos % itemsize;
    s_pos = rest / quantum;
    q_pos = rest % quantum;

    dptr = scull_follow(dev, item);
    if(dptr == NULL)
        goto out;
    if(!dptr->data)
    {
        dptr->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
        if(!dptr->data)
            goto out;
        memset(dptr->data, 0, qset * sizeof(char *));
    }
    if(!dptr->data[s_pos])
    {
        dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
        if(!dptr->data[s_pos])
            goto out;
    }

    if(count > quantum - q_pos)
        count = quantum - q_pos;

    if(copy_from_user(dptr->data[s_pos] + q_pos, buf, count))
    {
        retval = -EFAULT;
        goto out;
    }
    *f_pos += count;
    retval = count;

    if(dev->size < *f_pos)
        dev->size = *f_pos;

out:
    return retval;
}

long scull_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    printk(KERN_ALERT "scull_ioctl\n");
    return 0;
}

int scull_open(struct inode *inode, struct file *filp)
{
    printk(KERN_ALERT "scull_open\n");

    struct scull_dev *dev = container_of(inode->i_cdev, struct scull_dev, cdev);
    filp->private_data = dev;

    if ((filp->f_flags & O_ACCMODE) == O_WRONLY)
    {
        scull_trim(dev);
    }

    return 0;
}

int scull_release(struct inode *inode, struct file *filp)
{
    printk(KERN_ALERT "scull_release\n");
    return 0;
}

struct file_operations scull_fops = {
    .owner = THIS_MODULE,
    .llseek = scull_llseek,
    .read = scull_read,
    .write = scull_write,
    .unlocked_ioctl = scull_ioctl,
    .open = scull_open,
    .release = scull_release,
};

static int __init scull_init(void)
{
    int res;
    int i, j;

    res = alloc_chrdev_region(&dev, 0, 4, "scull");
    if (res < 0)
    {
        printk(KERN_ALERT "Failed to allocate char device region\n");
        goto fail_alloc_chrdev;
    }

    scull_class = class_create("scull"); // no need to include the owner in v6
    if (IS_ERR(scull_class))
    {
        printk(KERN_ALERT "Failed to create class\n");
        res = PTR_ERR(scull_class);
        goto fail_class_create;
    }

    for (i = 0; i < 4; i++)
    {
        device_create(scull_class, NULL, MKDEV(MAJOR(dev), i), NULL, "scull%d", i);
    }

    for (i = 0; i < 4; i++)
    {
        cdev_init(&scull_devs[i].cdev, &scull_fops);
        scull_devs[i].cdev.owner = THIS_MODULE;
        res = cdev_add(&scull_devs[i].cdev, MKDEV(MAJOR(dev), i), 1);
        if (res < 0)
        {
            printk(KERN_ALERT "Failed to add cdev\n");
            for (j = 0; j < i; j++)
            {
                cdev_del(&scull_devs[j].cdev);
            }
            goto fail_cdev_add;
        }
    }

    printk(KERN_ALERT "Hello, world\n");
    return 0;

fail_cdev_add:
    for (j = 0; j < 4; j++)
    {
        device_destroy(scull_class, MKDEV(MAJOR(dev), j));
    }
    class_destroy(scull_class);
fail_class_create:
    unregister_chrdev_region(dev, 4);
fail_alloc_chrdev:
    return res;
}

static void __exit scull_exit(void)
{
    int i;
    for (i = 0; i < 4; i++)
    {
        scull_trim(&scull_devs[i]); // Free all allocated memory
        cdev_del(&scull_devs[i].cdev);
        device_destroy(scull_class, MKDEV(MAJOR(dev), i));
    }
    class_destroy(scull_class);
    unregister_chrdev_region(dev, 4);
    printk(KERN_ALERT "Goodbye, cruel world\n");
}

module_init(scull_init);
module_exit(scull_exit);