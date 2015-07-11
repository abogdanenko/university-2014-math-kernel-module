# math-kernel-module
Kernel module that performs integer arithmetic

1. Build kernel module and `math_ctl` executable

 ```
 make
 ```
2. Insert module. Run as root:

 ```
 make insert_module
 ```
This command also prints the exact command needed for the next step.
3. Create device node with `mknod` command
4. To test the module, run executable `math_ctl` as root
