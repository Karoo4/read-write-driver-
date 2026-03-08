/*
    Stealth kernel module
*/

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include "kmd.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("anonymous");
MODULE_DESCRIPTION("Input event helper");

static dev_t dev_num;
static struct cdev kmd_cdev;
static struct class *kmd_class;
static struct device *kmd_device;

static struct task_struct *target_task = NULL;
static struct pid *target_pid_struct = NULL;

static struct list_head *saved_mod_list;
static bool hidden = false;

static void hide_module(void) {
  if (hidden)
    return;

  // Remove from module list (hides from lsmod, /proc/modules)
  saved_mod_list = THIS_MODULE->list.prev;
  list_del(&THIS_MODULE->list);

  // Remove from sysfs (/sys/module/)
  kobject_del(&THIS_MODULE->mkobj.kobj);

  hidden = true;
  pr_info("Module hidden\n");
}

static void show_module(void) {
  if (!hidden)
    return;

  // Restore to module list
  list_add(&THIS_MODULE->list, saved_mod_list);
  hidden = false;
}

static bool is_authorized_caller(void) {
  struct file *exe;
  char *buf, *path;
  bool authorized = false;

  buf = kmalloc(PATH_MAX, GFP_KERNEL);
  if (!buf)
    return false;

  exe = current->mm->exe_file;
  if (exe) {
    path = d_path(&exe->f_path, buf, PATH_MAX);
    if (!IS_ERR(path)) {
      // Check if caller is one of our tools
      // Use a hash or signature in production
      if (strstr(path, "memscan") || strstr(path, "overlay") ||
          strstr(path, "uma")) {
        authorized = true;
      }
    }
  }

  kfree(buf);
  return authorized;
}

static void add_timing_jitter(void) {
  // Small random delay to mask access patterns
  unsigned int delay = get_random_u32() % 50;
  if (delay > 0) {
    udelay(delay);
  }
}

static int kmd_open(struct inode *inode, struct file *file) { return 0; }

static int kmd_release(struct inode *inode, struct file *file) { return 0; }

static long kmd_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
  int ret = 0;

  add_timing_jitter();

  switch (cmd) {
  case KMD_IOCTL_ATTACH: {
    struct kmd_attach_request req;

    if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
      return -EFAULT;

    if (target_pid_struct) {
      put_pid(target_pid_struct);
      target_pid_struct = NULL;
      target_task = NULL;
    }

    target_pid_struct = find_get_pid(req.pid);
    if (!target_pid_struct)
      return -ESRCH;

    rcu_read_lock();
    target_task = pid_task(target_pid_struct, PIDTYPE_PID);
    rcu_read_unlock();

    if (!target_task) {
      put_pid(target_pid_struct);
      target_pid_struct = NULL;
      return -ESRCH;
    }
    break;
  }

  case KMD_IOCTL_READ: {
    struct kmd_copy_request req;
    struct mm_struct *mm;
    int bytes_read;
    void *kbuf;

    if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
      return -EFAULT;

    if (!target_task)
      return -EINVAL;

    if (req.size > 4096)
      return -EINVAL;

    kbuf = kmalloc(req.size, GFP_KERNEL);
    if (!kbuf)
      return -ENOMEM;

    mm = get_task_mm(target_task);
    if (!mm) {
      kfree(kbuf);
      return -EINVAL;
    }

    bytes_read =
        access_process_vm(target_task, req.from, kbuf, req.size, FOLL_FORCE);
    mmput(mm);

    if (bytes_read != req.size) {
      kfree(kbuf);
      return -EIO;
    }

    if (copy_to_user((void __user *)req.to, kbuf, req.size)) {
      kfree(kbuf);
      return -EFAULT;
    }

    kfree(kbuf);
    break;
  }

  case KMD_IOCTL_WRITE: {
    struct kmd_copy_request req;
    struct mm_struct *mm;
    int bytes_written;
    void *kbuf;

    if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
      return -EFAULT;

    if (!target_task)
      return -EINVAL;

    if (req.size > 4096)
      return -EINVAL;

    kbuf = kmalloc(req.size, GFP_KERNEL);
    if (!kbuf)
      return -ENOMEM;

    if (copy_from_user(kbuf, (void __user *)req.to, req.size)) {
      kfree(kbuf);
      return -EFAULT;
    }

    mm = get_task_mm(target_task);
    if (!mm) {
      kfree(kbuf);
      return -EINVAL;
    }

    bytes_written = access_process_vm(target_task, req.from, kbuf, req.size,
                                      FOLL_FORCE | FOLL_WRITE);
    mmput(mm);
    kfree(kbuf);

    if (bytes_written != req.size)
      return -EIO;
    break;
  }

  // Hidden IOCTL to control module visibility
  case _IO(KMD_IOCTL_MAGIC, 0xFF):
    if (hidden)
      show_module();
    else
      hide_module();
    break;

  default:
    return -ENOTTY;
  }

  return ret;
}

static const struct file_operations kmd_fops = {
    .owner = THIS_MODULE,
    .open = kmd_open,
    .release = kmd_release,
    .unlocked_ioctl = kmd_ioctl,
};

static int __init kmd_init(void) {
  int ret;

  ret = alloc_chrdev_region(&dev_num, 0, 1, KMD_DEVICE_NAME);
  if (ret < 0)
    return ret;

  cdev_init(&kmd_cdev, &kmd_fops);
  kmd_cdev.owner = THIS_MODULE;

  ret = cdev_add(&kmd_cdev, dev_num, 1);
  if (ret < 0)
    goto err_cdev;

  kmd_class = class_create(KMD_DEVICE_NAME);
  if (IS_ERR(kmd_class)) {
    ret = PTR_ERR(kmd_class);
    goto err_class;
  }

  kmd_device = device_create(kmd_class, NULL, dev_num, NULL, KMD_DEVICE_NAME);
  if (IS_ERR(kmd_device)) {
    ret = PTR_ERR(kmd_device);
    goto err_device;
  }

  // Auto-hide after loading
  hide_module();

  return 0;

err_device:
  class_destroy(kmd_class);
err_class:
  cdev_del(&kmd_cdev);
err_cdev:
  unregister_chrdev_region(dev_num, 1);
  return ret;
}

static void __exit kmd_exit(void) {
  // Must unhide to unload properly
  show_module();

  if (target_pid_struct) {
    put_pid(target_pid_struct);
    target_pid_struct = NULL;
    target_task = NULL;
  }

  device_destroy(kmd_class, dev_num);
  class_destroy(kmd_class);
  cdev_del(&kmd_cdev);
  unregister_chrdev_region(dev_num, 1);
}

module_init(kmd_init);
module_exit(kmd_exit);
