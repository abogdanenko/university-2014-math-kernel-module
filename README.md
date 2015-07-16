# Math kernel module
Loadable kernel module that provides integer math functionality through ioctl
syscall interface.

## Build
To build kernel module and *math_ctl* executable run `make`.

## Setup
First, load the module. Run as root `make insert_module`. This command prints
the exact command needed for the next step. Create device node with *mknod*
command.

## Test
To test the module, run executable *math_ctl* as root.

## Usage
Userspace program should:

1. Open character device

 ```c
 fd  = open("/dev/math", O_RDWR);
 ```
2. Call *ioctl*

 ```c
 int x[3];
 x[0] = 7;
 x[1] = 8;
 ret = ioctl(fd, MATH_IOCTL_ADD, &x);
 /* x[2] = 15 */
 ```

Supported operations:
- Addition
- Negation
- Division
- Exponentiation
- Logarithm (any base)

The module does all kinds of error checking, e. g. arithmetic overflow.
Example system log messages:
```
Jul 11 17:13:29 acer kernel: math: requested operation 101 (MATH_IOCTL_ADD)
Jul 11 17:13:29 acer kernel: math: unable to compute requested mathematical operation
Jul 11 17:13:29 acer kernel: math: do_math returned error MATH_OVERFLOW
```

See *math.h* for operation codes and *math_ctl.c* for more usage examples.

Note: only up to `math_device.max_users = 6` users can open the device at the
same time.
