#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>

MODULE_LICENSE("Dual BSD/GPL");

static dev_t dev;
static struct class *scull_class = NULL;

struct scull_dev {
    struct cdev cdev;
};

struct scull_dev scull_devs[4];

loff_t scull_llseek(struct file *filp, loff_t off, int whence);
ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
long scull_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
int scull_open(struct inode *inode, struct file *filp);
int scull_release(struct inode *inode, struct file *filp);

loff_t scull_llseek(struct file *filp, loff_t off, int whence)
{
    printk(KERN_ALERT "scull_llseek\n");
    return 0;
}

ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    printk(KERN_ALERT "scull_read\n");
    return 0;
}

ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    printk(KERN_ALERT "scull_write\n");
    return 0;
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
    // need to implement trim on write only mode
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

    for (i = 0; i < 4; i++) {
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
    for (j = 0; j < 4; j++) {
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
        cdev_del(&scull_devs[i].cdev);
        device_destroy(scull_class, MKDEV(MAJOR(dev), i));
    }
    class_destroy(scull_class);
    unregister_chrdev_region(dev, 4);
    printk(KERN_ALERT "Goodbye, cruel world\n");
}

module_init(scull_init);
module_exit(scull_exit);