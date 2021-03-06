/*
 * math character device by Alexey Bogdanenko <alexey@bogdanenko.com>
 *
 * Loadable kernel module that provides simple integer math functionality
 * through ioctl syscall interface.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>

#include "math.h"

/*****************************************************************************/
/* TYPES */

enum math_err
{
    MATH_BAD_CMD = 1,
    MATH_OVERFLOW,
    MATH_UNDERFLOW,
    MATH_BAD_EXP,
    MATH_BAD_LOG,
    MATH_ZERO_DIV
};

struct math_device_t
{
    struct cdev* cdev;
    dev_t num;
    atomic_t user_count;
    int max_users;
};

/*****************************************************************************/
/* FUNCTION PROTOTYPES */

/* initialises module */
static int math_init(void);

/* frees resources */
static void math_exit(void);

/* enforces user limit when user attempts to open the device */
static int fop_open(struct inode*, struct file*);

/* decrements user counter */
static int fop_release(struct inode*, struct file*);

/* moves data between kernel and user space, calls do_math to do its job */
static long fop_unlocked_ioctl(
    struct file*,
    unsigned int,
    unsigned long);

/* calls specific math function depending on command */
static int do_math(unsigned, int*);

/* gives number of arguments of specific mathematical operation */
static int arity(unsigned, int*);

/* negates the first argument, checks for overflows */
static int math_neg(int, int*);

/* computes the sum of the first two arguments, checks for overflow */
static int math_add(int, int, int*);

/* divides first argument by second, checks for zero division */
static int math_div(int, int, int*);

/* computes the product of the first two arguments, checks for overflow  */
static int math_mul(int, int, int*);

/*
 * Raises the first argument to the power of the second argument.
 * Works only in special case (base < 0). Checks for overflow
 * and bad second argument
 */
static int math_exp_negative_power(int, int, int*);

/*
 * Raises the first argument to the power of the second argument.
 * Works only in special case (base > 0). Checks for overflow
 */
static int math_exp_positive_power(int, int, int*);

/*
 * Raises the first argument to the power of the second argument.
 * Works only in special case (base > 1, exp > 1). Checks for overflow
 */
static int math_exp2(int, int, int*);

/*
 * Raises the first argument to the power of the second argument, checks for
 * bad input, overflows etc.
 *
 */
static int math_exp(int, int, int*);

/* computes the logarithm of the first argument, base = second argument */
static int math_log(int, int, int*);

/* returns the name of error, error number should be from enum math_err */
static const char* math_err_name(int);

/* returns ioctl command name */
static const char* cmd_name(int);
/*****************************************************************************/
/* GLOBAL VARIABLES */

static struct math_device_t math_device;
static struct file_operations fops =
{
    .owner = THIS_MODULE,
    .open = fop_open,
    .release = fop_release,
    .unlocked_ioctl = fop_unlocked_ioctl
};

/*****************************************************************************/
/* MODULE MACROS */

module_init(math_init);
module_exit(math_exit);

MODULE_AUTHOR("Alexey Bogdanenko <alexey@bogdanenko.com>");
MODULE_DESCRIPTION("Math module");
MODULE_LICENSE("GPL");

/*****************************************************************************/
/* IMPLEMENTATION */

static int math_init(void)
{
    int ret;

    math_device.max_users = 6;
    atomic_set(&(math_device.user_count), 0);

    ret = alloc_chrdev_region(&(math_device.num), 0, 1, "math");

    if (ret < 0)
    {
        pr_err("math: failed to allocate device number\n");
        return ret;
    }

    math_device.cdev = cdev_alloc();
    if (math_device.cdev == NULL)
    {
        pr_err("math: failed to allocate a cdev\n");
        unregister_chrdev_region(math_device.num, 1);
        return -1;
    }

    cdev_init(math_device.cdev, &fops);
    math_device.cdev->owner = THIS_MODULE;
    ret = cdev_add(math_device.cdev, math_device.num, 1);

    if (ret < 0)
    {
        pr_err("math: unable to add character device\n");
        cdev_del(math_device.cdev);
        unregister_chrdev_region(math_device.num, 1);
        return ret;
    }

    pr_info("math: loaded module\n");
    return 0;
}

static void math_exit(void)
{
    cdev_del(math_device.cdev);
    unregister_chrdev_region(math_device.num, 1);
    pr_info("math: unloaded module\n");
}

static int fop_open(struct inode* ip, struct file* fp)
{
    if (atomic_add_unless(&(math_device.user_count), 1, math_device.max_users))
    {

        pr_info("math: opened device\n");
        pr_info("math: %d user(s) total\n",
            atomic_read(&(math_device.user_count)));
        return 0;
    }

    pr_err("math: open command denied, "
        "max. users limit had been reached\n");
    return -EBUSY;
}

static int fop_release(struct inode* ip, struct file* fp)
{
    atomic_dec(&(math_device.user_count));
    pr_info("math: released device\n");
    pr_info("math: %d user(s) total\n",
        atomic_read(&(math_device.user_count)));
    return 0;
}

static long fop_unlocked_ioctl(
    struct file* fp,
    unsigned int cmd,
    unsigned long arg)
{
    int ret;
    int x[3];
    int __user* x_user;
    int len;

    x_user = (int*) arg;
    ret = arity(cmd, &len);
    if (ret)
    {
        pr_err("math: unknown operation code\n");
        return -EINVAL;
    }

    pr_info("math: requested operation %x (%s)\n", cmd, cmd_name(cmd));

    ret = copy_from_user(x, x_user, len * sizeof(int));
    if (ret)
    {
        pr_err("math: unable to copy %d bytes from userspace\n", ret);
        return -EFAULT;
    }

    ret = do_math(cmd, x);
    if (ret)
    {
        pr_err("math: unable to compute requested mathematical operation\n");
        pr_info("math: do_math returned error %s\n", math_err_name(ret));
        return -EINVAL;
    }

    ret = copy_to_user(x_user + len, x + len, sizeof(int));
    if (ret)
    {
        pr_err("math: unable to copy %d bytes to userspace\n", ret);
        return -EFAULT;
    }

    return 0;
}

static int do_math(unsigned cmd, int* x)
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

static int arity(unsigned cmd, int* result)
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

static int math_neg(int a, int* c)
{
    if (a == INT_MIN)
    {
        return MATH_OVERFLOW;
    }
    *c = 0 - a;
    return 0;
}

static int math_add(int a, int b, int* c)
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

static int math_div(int a, int b, int* c)
{
    if (b == 0)
    {
        return MATH_ZERO_DIV;
    }
    *c = a / b;
    return 0;
}

/* a > 1, b > 1 */
static int math_mul(int a, int b, int*c)
{
    if (b > INT_MAX / a)
    {
        return MATH_OVERFLOW;
    }
    *c = a * b;
    return 0;
}

static int math_exp_negative_power(int a, int b, int* c)
{
    if (a < -1 || a > 1)
    {
        /* error: the result is less then 1 in absolute value, not int. */
        return MATH_UNDERFLOW;
    }

    if (a == 0)
    {
        /* error: zero to negative power */
        return MATH_BAD_EXP;
    }

    if (a == 1)
    {
        /* one to negative power is one */
        *c = 1;
        return 0;
    }

    /* case a == -1: */
    /* if the power is odd, the res. is -1, otherwise the res. is 1 */
    if (b % 2)
    {
        *c = -1;
        return 0;
    }
    *c = 1;
    return 0;
}

static int math_exp_positive_power(int a, int b, int* c)
{
    int ret;

    if (b == 1)
    {
        /* the result equals the base */
        *c = a;
        return 0;
    }

    /* case b > 1: */

    if (a > 1)
    {
        return math_exp2(a, b, c);
    }

    if (a == 0 || a == 1)
    {
        *c = a;
        return 0;
    }

    if (a == -1)
    {
        /* if the power is odd, the res. is -1, otherwise the res. is 1 */
        if (b % 2)
        {
            *c = -1;
            return 0;
        }
        *c = 1;
        return 0;
    }

    /* case a < -1: */
    /*
     * reduce it to case a > 1
     * negate the base, then, if the power is odd, negate the result
     */

    if (a == INT_MIN)
    {
        return MATH_OVERFLOW;
    }

    ret = math_exp2(0 - a, b, c);

    if (ret)
    {
        return ret;
    }

    if (b % 2)
    {
        *c = 0 - *c;
    }

    return 0;
}

/* a > 1, b > 1 */
static int math_exp2(int a, int b, int* c)
{
    /*
     * use simple multiplication
     * result = a * a * ... * a (b times)
     */
    int i;
    int p = a; /* product */
    int ret;

    for (i = 0; i < b - 1; i++)
    {
        /* compute p *= a, check for overflow */
        ret = math_mul(a, p, &p);
        if (ret)
        {
            return ret;
        }
    }

    *c = p;
    return 0;
}

static int math_exp(int a, int b, int* c)
{
    if (b < 0)
    {
        return math_exp_negative_power(a, b, c);
    }

    if (b > 0)
    {
        return math_exp_positive_power(a, b, c);
    }

    // case b == 0:
    if (a == 0)
    {
        /* error: zero to the power of zero */
        return MATH_BAD_EXP;
    }

    /* case a != 0: */
    /* non-zero to the power of zero */
    *c = 1;
    return 0;
}

static int math_log(int a, int b, int* c)
{
    int p;
    int k;
    int ret;

    if (a <= 0)
    {
        return MATH_BAD_LOG;
    }

    if (b <= 1)
    {
        return MATH_BAD_LOG;
    }

    if (a == 1)
    {
        *c = 0;
        return 0;
    }

    /* we now have a > 1 and b > 1 */

    if (a < b)
    {
        *c = 0;
        return 0;
    }

    if (a == b)
    {
        *c = 1;
        return 0;
    }

    /* we now have 1 < b < a (<= INT_MAX) */

    /*
     * algorithm: compute p == b ** k, (k = 2, 3, ...) using multiplication
     * the logarithm is the largest k such that p <= a
     */

    p = b; /* product */
    k = 1; /* power (result) */
    /* loop invariant: p == b ** k */
    do
    {
        ret = math_mul(p, b, &p);
        if (ret == 0)
        {
            k++;
        }
    }
    while (ret == 0 && p <= a);

    if (ret)
    {
        /*
         * b ** k == p <= a (previous loop condition or before cycle condition)
         * b ** (k + 1) > INT_MAX >= a
         */
        *c = k;
    }
    else
    {
        /*
         * b ** (k - 1) <= a (prev. loop condition or before cycle condition)
         * b ** k == p > a
         */
        *c = k - 1;
    }

    return 0;
}

static const char* math_err_name(int code)
{
    switch (code)
    {
        case MATH_BAD_CMD:
            return "MATH_BAD_CMD";
        case MATH_OVERFLOW:
            return "MATH_OVERFLOW";
        case MATH_UNDERFLOW:
            return "MATH_UNDERFLOW";
        case MATH_BAD_EXP:
            return "MATH_BAD_EXP";
        case MATH_BAD_LOG:
            return "MATH_BAD_LOG";
        case MATH_ZERO_DIV:
            return "MATH_ZERO_DIV";
        default:
            return "UNKNOWN ERROR CODE"; /* never happens */
    }
    return "UNKNOWN ERROR CODE"; /* never happens */
}

static const char* cmd_name(int cmd)
{
    switch (cmd)
    {
        case MATH_IOCTL_ADD:
            return "MATH_IOCTL_ADD";
        case MATH_IOCTL_NEG:
            return "MATH_IOCTL_NEG";
        case MATH_IOCTL_DIV:
            return "MATH_IOCTL_DIV";
        case MATH_IOCTL_EXP:
            return "MATH_IOCTL_EXP";
        case MATH_IOCTL_LOG:
            return "MATH_IOCTL_LOG";
        default:
            return "UNKNOWN IOCTL";
    }

    return "UNKNOWN IOCTL"; /* never happens */
}
