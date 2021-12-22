include/linux/broadcom/bcm2835_smi.h
drivers/char/broadcom/bcm2835_smi_dev.c
drivers/misc/bcm2835_smi.c


the final files are in
/lib/modules/5.10.63-v8+/kernel/drivers/misc/bcm2835_smi.ko
/lib/modules/5.10.63-v8+/kernel/drivers/char/broadcom/bcm2835_smi_dev.ko

we need to change the smi_dev and call it "smi_stream_dev"

----

There is a much simpler version here, tested on jessie and stretch.
```
sudo apt-get install raspberrypi-kernel-headers
```
and then when your files are in place :
```
make -C /lib/modules/$(uname -r)/build M=$(PWD) modules
```

Example
Create the hello directory, go inside and create the following files : hello.c and Makefile.
I recommend working as your normal user, not root, only insmod, rmmod, and make modules_install commands require root permissions, and the necessary sudo is shown in the following commands.


hello.c (unchanged, your file)
```
#include <linux/init.h>  
#include <linux/kernel.h> 
#include <linux/module.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Do-nothing test driver");
MODULE_VERSION("0.1");

static int __init hello_init(void){
   printk(KERN_INFO "Hello, world.\n");
   return 0;
}

static void __exit hello_exit(void){
   printk(KERN_INFO "Goodbye, world.\n");
}

module_init(hello_init);
module_exit(hello_exit);
Makefile (changed)
```

```
obj-m+=hello.o

all:
    make -C /lib/modules/$(shell uname -r)/build M=$(pwd) modules

clean:
    make -C /lib/modules/$(shell uname -r)/build M=$(pwd) clean

modules_install: all
    $(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install
    $(DEPMOD)   
Usage
```


Build : make (in the same directory as the Makefile)
Test
Insert the module with sudo insmod hello.ko
Find Hello World :) in the output of dmesg
Remove the module with sudo rmmod hello
Find Goodbye, world. int the output of dmesg
Install, when your module is working, sudo make modules_install will install the module where it belongs, so modprobe will work.


## References

```
insmod
modprobe
rmmod
```

User-space utilities that load modules into the running kernels and remove them.

```
#include <linux/init.h>
module_init(init_function);
module_exit(cleanup_function);
```

Macros that designate a module’s initialization and cleanup functions.
```
_ _init
_ _initdata
_ _exit
_ _exitdata
```
Markers for functions (`_ _init` and `_ _exit`) and data (`_ _initdata` and `_ _exitdata`) that are only used at module initialization or cleanup time. Items marked for initialization may be discarded once initialization completes; the exit items may be discarded if module unloading has not been configured into the kernel. These markers work by causing the relevant objects to be placed in a special ELF section in the executable file.

```
#include <linux/sched.h>
```

One of the most important header files. This file contains definitions of much of the kernel API used by the driver, including functions for sleeping and numerous variable declarations.
```
struct task_struct *current;
current->pid
current->comm
```
The current process.
The process ID and command name for the current process.

`obj-m` A makefile symbol used by the kernel build system to determine which modules should be built in the current directory.

```
/sys/module
/proc/modules
```
`/sys/module` is a sysfs directory hierarchy containing information on currently-loaded modules. 
`/proc/modules` is the older, single-file version of that information. Entries contain the module name, the amount of memory each module occupies, and the usage count. Extra strings are appended to each line to specify flags that are currently active for the module.

```
vermagic.o
```
An object file from the kernel source directory that describes the environment a module was built for.

```
#include <linux/module.h>
```
Required header. It must be included by a module source.

```
#include <linux/version.h>
```
A header file containing information on the version of the kernel being built.

```
LINUX_VERSION_CODE
```
Integer macro, useful to `#ifdef` version dependencies.

```
EXPORT_SYMBOL (symbol);
EXPORT_SYMBOL_GPL (symbol);
```
Macro used to export a symbol to the kernel. The second form limits use of the exported symbol to GPL-licensed modules.

```
MODULE_AUTHOR(author);
MODULE_DESCRIPTION(description);
MODULE_VERSION(version_string);
MODULE_DEVICE_TABLE(table_info);
MODULE_ALIAS(alternate_name);
```
Place documentation on the module in the object file.

```
MODULE_LICENSE(license);
```
Declare the license governing this module.

```
#include <linux/moduleparam.h>
module_param(variable, type, perm);
```

Macro that creates a module parameter that can be adjusted by the user when the module is loaded (or at boot time for built-in code). The type can be one of bool, charp, int, invbool, long, short, ushort, uint, ulong, or intarray.

```
#include <linux/kernel.h>
int printk(const char * fmt, ...);
```
The analogue of printf for kernel code.
