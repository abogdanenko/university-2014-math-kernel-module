#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>

#include "math.h"

const int max_users = 6;

struct math_device_t
{
    struct cdev cdev;
    dev_t num;
    atomic_t user_count;
} math_device;

int fop_open(struct inode* ip, struct file* fp)
{
    if(atomic_add_unless(&(math_device.user_count), 1, max_users))
    {

        pr_info("math: opened device\n");
        pr_info("math: %d user(s) total\n",
            atomic_read(&(math_device.user_count)));
        return 0;
    }
    else
    {
        pr_err("math: open command denied, "
            "max. users limit had been reached\n");
        return -EBUSY;
    }
}

int fop_release(struct inode* ip, struct file* fp)
{
    atomic_dec(&(math_device.user_count));
    pr_info("math: released device\n");
    pr_info("math: %d user(s) total\n",
        atomic_read(&(math_device.user_count)));
    return 0;
}

int arity(unsigned cmd, int* result)
{
    switch (cmd)
    {
        case MATH_IOCTL_NEG:
            *result = 1;
            return 0;
        case MATH_IOCTL_ADD:
        case MATH_IOCTL_DIV:
        case MATH_IOCTL_EXP:
        case MATH_IOCTL_LOG:
            *result = 2;
            return 0;
        default:
            return -1;
    }
}

int do_math(unsigned cmd, int* x)
{
    return 0;
}

long fop_unlocked_ioctl(struct file* fp, unsigned int cmd, unsigned long arg)
{
    int ret;
    int x[3];
    int* x_user = (int*) arg;
    int len;
    
    pr_info("math: ioctl begin\n");
    ret = arity(cmd, &len);
    if (ret)
    {
        pr_err("math: unknown operation code");
        return -EINVAL;
    }

    ret = copy_from_user(x, x_user, len * sizeof(int));
    ret = do_math(cmd, x);
    if (ret)
    {
        pr_err("math: unable to compute requested mathematical operation");
        return -EINVAL;
    }
    ret = copy_to_user(x_user + len, x + len, sizeof(int));

    pr_info("math: ioctl end\n");
    return 0;
}

struct file_operations fops = 
{
    .owner = THIS_MODULE,
    .open = fop_open,
    .release = fop_release,
    .unlocked_ioctl = fop_unlocked_ioctl
};

static int math_init(void)
{
    int ret;

    ret = alloc_chrdev_region(&(math_device.num), 0, 1, "math");

    if (ret < 0)
    {
        pr_err("math: failed to allocate device number\n");
        return ret;
    }

    cdev_init(&(math_device.cdev), &fops);
    ret = cdev_add(&(math_device.cdev), math_device.num, 1);
    
    if (ret < 0)
    {
        pr_err("math: unable to add character device\n");
        return ret;
    }

    atomic_set(&(math_device.user_count), 0);

    pr_info("math: loaded module\n");
    return 0;
}

static void math_exit(void)
{
    unregister_chrdev_region(math_device.num, 1);
    pr_info("math: unloaded module\n");
}

module_init(math_init);
module_exit(math_exit);

MODULE_AUTHOR("Alexey Bogdanenko <alexey@bogdanenko.com>");
MODULE_DESCRIPTION("Math module");
MODULE_LICENSE("GPL");
