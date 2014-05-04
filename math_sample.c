#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/uaccess.h>

#include "math.h"

static int math_init(void)
{
	pr_info("Module math was loaded\n");
    return 0;
}

static void math_exit(void)
{
	pr_info("Module math was unloaded\n");
}

module_init(math_init);
module_exit(math_exit);
MODULE_AUTHOR("Ivan Ivanovich Ivanov IV <iii@ivanovo.su>");
MODULE_DESCRIPTION("Math module");
MODULE_LICENSE("GPL");
