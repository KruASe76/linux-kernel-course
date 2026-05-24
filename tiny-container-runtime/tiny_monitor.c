#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pid.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>


#define TINY_IOC_MAGIC 'T'
#define TINY_IOC_REGISTER_CONTAINER _IOW(TINY_IOC_MAGIC, 1, pid_t)


struct container_node {
    pid_t pid;
    struct list_head list;
};

static LIST_HEAD(container_list);
static DEFINE_MUTEX(container_mutex);


static long monitor_ioctl(struct file *file, unsigned int command, unsigned long arg) {
    if (command == TINY_IOC_REGISTER_CONTAINER) {
        pid_t pid;
        struct container_node *node;

        if (copy_from_user(&pid, (pid_t __user *)arg, sizeof(pid_t))) {
            return -EFAULT;
        }

        node = kmalloc(sizeof(*node), GFP_KERNEL);
        if (!node) {
            return -ENOMEM;
        }

        node->pid = pid;

        mutex_lock(&container_mutex);
        list_add_tail(&node->list, &container_list);
        mutex_unlock(&container_mutex);

        pr_info("tiny_monitor: started tracking container with pid: %d\n", pid);
        return 0;
    }

    return -ENOTTY;
}


static const struct file_operations monitor_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = monitor_ioctl,
};

static struct miscdevice monitor_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "tiny_monitor",
    .fops = &monitor_fops,
};


static int stats_show(struct seq_file *file, void *_) {
    struct container_node *node;
    struct task_struct *task;

    seq_printf(file, "%-10s %-15s %-15s %-15s %-15s\n",
                    "PID", "MIN_FLT", "MAJ_FLT", "UTIME", "STIME");
    seq_printf(file, "----------------------------------------------------------------------\n");

    mutex_lock(&container_mutex);

    list_for_each_entry(node, &container_list, list) {
        rcu_read_lock();

        task = pid_task(find_vpid(node->pid), PIDTYPE_PID);
        if (task && task->signal) {
            // aggregate the whole process tree
            unsigned long min_flt = task->min_flt + task->signal->cmin_flt;
            unsigned long maj_flt = task->maj_flt + task->signal->cmaj_flt;
            unsigned long long utime = task->utime + task->signal->cutime;
            unsigned long long stime = task->stime + task->signal->cstime;

            seq_printf(file, "%-10d %-15lu %-15lu %-15llu %-15llu\n",
                            node->pid, min_flt, maj_flt, utime, stime);
        } else {
            seq_printf(file, "%-10d [STOPPED]\n", node->pid);
        }

        rcu_read_unlock();
    }

    mutex_unlock(&container_mutex);
    return 0;
}

static int stats_open(struct inode *inode, struct file *file) {
    return single_open(file, stats_show, NULL);
}


static const struct proc_ops stats_pops = {
    .proc_open = stats_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};


static int __init tiny_monitor_init(void) {
    int ret = misc_register(&monitor_dev);
    if (ret) {
        pr_err("tiny_monitor: failed to register misc device\n");
        return ret;
    }

    if (!proc_create("tiny_stats", 0444, NULL, &stats_pops)) {
        misc_deregister(&monitor_dev);
        pr_err("tiny_monitor: failed to create proc entry\n");
        return -ENOMEM;
    }

    pr_info("tiny_monitor: loaded\n");
    return 0;
}

static void __exit tiny_monitor_exit(void) {
    struct container_node *node, *tmp;

    remove_proc_entry("tiny_stats", NULL);
    misc_deregister(&monitor_dev);

    // graph cleanup
    mutex_lock(&container_mutex);
    list_for_each_entry_safe(node, tmp, &container_list, list) {
        list_del(&node->list);
        kfree(node);
    }
    mutex_unlock(&container_mutex);

    pr_info("tiny_monitor: unloaded\n");
}


module_init(tiny_monitor_init);
module_exit(tiny_monitor_exit);


MODULE_LICENSE("GPL");
