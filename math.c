#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>

#include "math.h"

enum math_err {
    MATH_BAD_CMD = 1,
    MATH_OVERFLOW,
    MATH_ZERO_DIV,
    MATH_BAD_EXP
};

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
            return MATH_BAD_CMD;
    }
}

int math_neg(int a, int* c)
{
    if (a == INT_MIN)
    {
        return MATH_OVERFLOW;
    }
    *c = 0 - a;
    return 0;
}

int math_add(int a, int b, int* c)
{
    if (a > 0 && b > 0)
    {
        if ((a - INT_MAX) + b > 0)
        {
            return MATH_OVERFLOW;
        }
    }

    if (a < 0 && b < 0)
    {
        if ((a - INT_MIN) + b < 0)
        {
            return MATH_OVERFLOW;
        }
    }

    *c = a + b;
    return 0;
}

int math_div(int a, int b, int* c)
{
    if (b == 0)
    {
        return MATH_ZERO_DIV;
    }
    *c = a / b;
    return 0;
}

// a >= 2, b >= 2
int math_exp_pos_pow2(int a, int b, int* c)
{
    return 0;
}

int math_exp_neg_pow(int a, int b, int* c)
{
    if (a < -1 || a > 1)
    {
        return MATH_UNDERFLOW;
    }

    if (a == 0)
    {
        return MATH_BAD_EXP;
    }

    if (a == 1)
    {
        *c = 1;
        return 0;
    }

    // a == -1:
    if (b % 2)
    {
        *c = -1;
        return 0;
    }

    // a == -1 and b is even:
    *c = 1;
    return 0;
}

int math_exp_pos_pow(int a, int b, int* c)
{
    int abs_a;
    int ret;

    if (b == 1)
    {
        *c = a;
        return 0;
    }

    // b > 1:

    if (a == 0)
    {
        *c = 0;
        return 0;
    }

    if (a == -1)
    {
        if (b % 2)
        {
            *c = -1;
            return 0;
        }
        else
        {
            *c = 1;
            return 0;
        }
    }

    if (a == 1)
    {
        *c = 1;
        return 0;
    }

    if (a > 0)
    {
        return math_exp_pos_pow2(a, b, c);
    }
    
    // a < 0, a != -1:

    ret = neg(a, &abs_a);
    if (ret)
    {
        return ret;
    }
    ret = math_exp_pos_pow2(abs_a, b, c);
    if (ret)
    {
        return ret;
    }
    *c = 0 - *c;
    return 0;
}

int math_exp(int a, int b, int* c)
{
    if (b > 0)
    {
        return math_exp_pos_pow(a, b, c);
    }

    if (b < 0)
    {
        return math_exp_neg_pow(a, b, c);
    }

    // b == 0:

    if (a == 0)
    {
        return MATH_BAD_EXP;
    }

    // b == 0 and a != 0:
    *c = 1;
    return 0;
}

int math_log(int a, int b, int* c)
{
    return 0;
}

int do_math(unsigned cmd, int* x)
{
    switch (cmd)
    {
        case MATH_IOCTL_NEG:
            return math_neg(x[0], &(x[1]));
        case MATH_IOCTL_ADD:
	    return math_add(x[0], x[1], &(x[2]));
        case MATH_IOCTL_DIV:
	    return math_div(x[0], x[1], &(x[2]));
        case MATH_IOCTL_EXP:
	    return math_exp(x[0], x[1], &(x[2]));
        case MATH_IOCTL_LOG:
	    return math_log(x[0], x[1], &(x[2]));
        default:
            return MATH_BAD_CMD;
    }
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
        pr_err("math: unknown operation code\n");
        return -EINVAL;
    }

    pr_info("math: requested operation %x\n", cmd);
    ret = copy_from_user(x, x_user, len * sizeof(int));
    ret = do_math(cmd, x);
    if (ret)
    {
        pr_err("math: unable to compute requested mathematical operation\n");
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
