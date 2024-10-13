#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
MODULE_LICENSE("Dual BSD/GPL");

static dev_t dev;
static struct class* scull_class = NULL;

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

static int scull_init(void)
{
    int res;
    res = alloc_chrdev_region(&dev, 0, 4, "scull");
    if(res < 0)
    {
        printk(KERN_ALERT "Failed to allocate char device region\n");
        return res;
    }
    
    scull_class = class_create("scull");
    if(IS_ERR(scull_class))
    {
        printk(KERN_ALERT "Failed to create class\n");
        unregister_chrdev_region(dev, 4);
        return PTR_ERR(scull_class);
    }

    device_create(scull_class, NULL, MKDEV(MAJOR(dev), 0), NULL, "scull0");
    device_create(scull_class, NULL, MKDEV(MAJOR(dev), 1), NULL, "scull1");
    device_create(scull_class, NULL, MKDEV(MAJOR(dev), 2), NULL, "scull2");
    device_create(scull_class, NULL, MKDEV(MAJOR(dev), 3), NULL, "scull3");

	printk(KERN_ALERT "Hello, world\n");
	return 0;
}

static void scull_exit(void)
{
    device_destroy(scull_class, MKDEV(MAJOR(dev), 0));
    device_destroy(scull_class, MKDEV(MAJOR(dev), 1));
    device_destroy(scull_class, MKDEV(MAJOR(dev), 2));
    device_destroy(scull_class, MKDEV(MAJOR(dev), 3));

    class_destroy(scull_class);

    unregister_chrdev_region(dev, 4);
	printk(KERN_ALERT "Goodbye, cruel world\n");
}

module_init(scull_init);
module_exit(scull_exit);
