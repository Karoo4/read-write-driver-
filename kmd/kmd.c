#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "kmd.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WzrterFX");
MODULE_DESCRIPTION("Kernel-mode memory read/write driver");

static dev_t dev_num;
static struct cdev kmd_cdev;
static struct class *kmd_class;
static struct device *kmd_device;

static struct task_struct *target_task = NULL;
static struct pid *target_pid_struct = NULL;

static int kmd_open(struct inode *inode, struct file *file) { return 0; }

static int kmd_release(struct inode *inode, struct file *file) { return 0; }

static long kmd_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
  int ret = 0;

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
    if (!target_pid_struct) {
      pr_err("kmd: failed to find pid %d\n", req.pid);
      return -ESRCH;
    }

    rcu_read_lock();
    target_task = pid_task(target_pid_struct, PIDTYPE_PID);
    rcu_read_unlock();

    if (!target_task) {
      put_pid(target_pid_struct);
      target_pid_struct = NULL;
      pr_err("kmd: failed to get task for pid %d\n", req.pid);
      return -ESRCH;
    }

    pr_info("kmd: attached to pid %d\n", req.pid);
    break;
  }

  case KMD_IOCTL_READ: {
    struct kmd_copy_request req;
    struct mm_struct *mm;
    int bytes_read;
    void *kbuf;

    if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
      return -EFAULT;

    if (!target_task) {
      pr_err("kmd: no process attached\n");
      return -EINVAL;
    }

    if (req.size > 4096)
      return -EINVAL;

    kbuf = kmalloc(req.size, GFP_KERNEL);
    if (!kbuf)
      return -ENOMEM;

    mm = get_task_mm(target_task);
    if (!mm) {
      kfree(kbuf);
      pr_err("kmd: failed to get mm for target\n");
      return -EINVAL;
    }

    bytes_read =
        access_process_vm(target_task, req.from, kbuf, req.size, FOLL_FORCE);
    mmput(mm);

    if (bytes_read != req.size) {
      kfree(kbuf);
      pr_err("kmd: read failed, requested %zu, got %d\n", req.size, bytes_read);
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

    if (!target_task) {
      pr_err("kmd: no process attached\n");
      return -EINVAL;
    }

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
      pr_err("kmd: failed to get mm for target\n");
      return -EINVAL;
    }

    bytes_written = access_process_vm(target_task, req.from, kbuf, req.size,
                                      FOLL_FORCE | FOLL_WRITE);
    mmput(mm);
    kfree(kbuf);

    if (bytes_written != req.size) {
      pr_err("kmd: write failed, requested %zu, got %d\n", req.size,
             bytes_written);
      return -EIO;
    }
    break;
  }

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
  if (ret < 0) {
    pr_err("kmd: failed to allocate device number\n");
    return ret;
  }

  cdev_init(&kmd_cdev, &kmd_fops);
  kmd_cdev.owner = THIS_MODULE;

  ret = cdev_add(&kmd_cdev, dev_num, 1);
  if (ret < 0) {
    pr_err("kmd: failed to add cdev\n");
    goto err_cdev;
  }

  kmd_class = class_create(KMD_DEVICE_NAME);
  if (IS_ERR(kmd_class)) {
    pr_err("kmd: failed to create class\n");
    ret = PTR_ERR(kmd_class);
    goto err_class;
  }

  kmd_device = device_create(kmd_class, NULL, dev_num, NULL, KMD_DEVICE_NAME);
  if (IS_ERR(kmd_device)) {
    pr_err("kmd: failed to create device\n");
    ret = PTR_ERR(kmd_device);
    goto err_device;
  }

  pr_info("kmd: loaded successfully (major=%d, minor=%d)\n", MAJOR(dev_num),
          MINOR(dev_num));

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
  if (target_pid_struct) {
    put_pid(target_pid_struct);
    target_pid_struct = NULL;
    target_task = NULL;
  }

  device_destroy(kmd_class, dev_num);
  class_destroy(kmd_class);
  cdev_del(&kmd_cdev);
  unregister_chrdev_region(dev_num, 1);

  pr_info("kmd: unloaded\n");
}

module_init(kmd_init);
module_exit(kmd_exit);
