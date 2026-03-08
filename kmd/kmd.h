#ifndef KMD_H
#define KMD_H

#include <linux/ioctl.h>
#include <linux/types.h>

// Use innocuous names that blend in
#define KMD_DEVICE_NAME "input_helper"
#define KMD_DEVICE_PATH "/dev/input_helper"

#define KMD_IOCTL_MAGIC 'k'

#define KMD_IOCTL_ATTACH _IOW(KMD_IOCTL_MAGIC, 1, struct kmd_attach_request)
#define KMD_IOCTL_READ _IOWR(KMD_IOCTL_MAGIC, 2, struct kmd_copy_request)
#define KMD_IOCTL_WRITE _IOW(KMD_IOCTL_MAGIC, 3, struct kmd_copy_request)

struct kmd_attach_request {
  pid_t pid;
};

struct kmd_copy_request {
  unsigned long from;
  unsigned long to;
  size_t size;
};

#endif /* !KMD_H */
