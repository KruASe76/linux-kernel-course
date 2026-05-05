#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/timekeeping.h>


#define MAX_CHATS 5
#define MAX_MSG_LEN 1024
#define MAX_USERNAME_LEN 32
#define KB_16 (16 * 1024)
#define TIMEZONE_OFFSET (3 * 3600)  // UTC+3


struct tg_message {
    struct list_head list;
    struct timespec64 timespec;

    char username[MAX_USERNAME_LEN];
    char text[MAX_MSG_LEN];
};

struct tg_chat {
    char name[16];
    char device_name[32];

    unsigned int message_count;
    struct list_head messages;

    struct mutex lock;
    struct miscdevice device;
};


static struct tg_chat chats[MAX_CHATS];


static int add_message(struct tg_chat* chat, const char* username, const char* text) {
    struct tg_message* msg;

    msg = kmalloc(sizeof(*msg), GFP_KERNEL);
    if (!msg) {
        return -ENOMEM;
    }

    ktime_get_real_ts64(&msg->timespec);
    strscpy(msg->username, username, sizeof(msg->username));
    strscpy(msg->text, text, sizeof(msg->text));

    mutex_lock(&chat->lock);
    list_add_tail(&msg->list, &chat->messages);
    chat->message_count++;
    mutex_unlock(&chat->lock);

    return 0;
}

static void free_chat_messages(struct tg_chat* chat) {
    struct tg_message* msg;
    struct tg_message* tmp;

    mutex_lock(&chat->lock);
    list_for_each_entry_safe(msg, tmp, &chat->messages, list) {
        list_del(&msg->list);
        kfree(msg);
    }
    chat->message_count = 0;
    mutex_unlock(&chat->lock);
}


static int tg_open(struct inode* inode, struct file* file) {
    struct miscdevice* device = file->private_data;
    struct tg_chat* chat = container_of(device, struct tg_chat, device);

    file->private_data = chat;
    return 0;
}

static int tg_release(struct inode* inode, struct file* file) {
    return 0;
}

static ssize_t tg_read(struct file* file, char __user* buf, size_t count, loff_t* ppos) {
    struct tg_chat* chat = file->private_data;
    struct tg_message* message;
    char* kbuf;
    size_t total_len = 0;
    struct tm tm;
    ssize_t result;

    kbuf = kmalloc(KB_16, GFP_KERNEL);  // 16KB > 10 messages * ~1KB
    if (!kbuf) {
        return -ENOMEM;
    }

    mutex_lock(&chat->lock);

    int skipped = 0;
    if (chat->message_count > 10) {
        skipped = chat->message_count - 10;
        total_len += snprintf(
            kbuf + total_len,
            KB_16 - total_len,
            "<%d messages not shown>\n",
            skipped
        );
    }

    int i = 0;
    list_for_each_entry(message, &chat->messages, list) {
        if (i < skipped) {
            i++;
            continue;
        }

        time64_to_tm(message->timespec.tv_sec + TIMEZONE_OFFSET, 0, &tm);

        total_len += snprintf(
            kbuf + total_len,
            KB_16 - total_len,
            "[%02d:%02d:%02d | %s] %s\n",
            tm.tm_hour,
            tm.tm_min,
            tm.tm_sec,
            message->username,
            message->text
        );
    }

    mutex_unlock(&chat->lock);

    result = simple_read_from_buffer(buf, count, ppos, kbuf, total_len);

    kfree(kbuf);
    return result;
}

static ssize_t tg_write(struct file* file, const char __user* buf, size_t count, loff_t* ppos) {
    struct tg_chat* chat = file->private_data;
    char* text;
    char username[MAX_USERNAME_LEN];

    if (count > MAX_MSG_LEN - 1) {
        return -EMSGSIZE;
    }

    text = kmalloc(count + 1, GFP_KERNEL);
    if (!text) {
        return -ENOMEM;
    }

    if (copy_from_user(text, buf, count)) {
        kfree(text);
        return -EFAULT;
    }
    text[count] = '\0';

    if (count > 0 && text[count - 1] == '\n') {
        text[count - 1] = '\0';
    }

    if (current_uid().val == 0) {  // there are no usernames in kernel :(
        strscpy(username, "root", sizeof(username));
    } else {
        snprintf(username, sizeof(username), "user_%u", current_uid().val);
    }

    add_message(chat, username, text);

    kfree(text);
    return count;
}

static const struct file_operations tg_fops = {
    .owner = THIS_MODULE,
    .open = tg_open,
    .release = tg_release,
    .read = tg_read,
    .write = tg_write,
};


static void init_mock_data_and_devices(void) {
    for (int i = 0; i < MAX_CHATS; i++) {
        snprintf(chats[i].name, sizeof(chats[i].name), "chat_%d", i + 1);
        snprintf(chats[i].device_name, sizeof(chats[i].device_name), "telegram/chat_%d", i + 1);

        mutex_init(&chats[i].lock);
        INIT_LIST_HEAD(&chats[i].messages);
        chats[i].message_count = 0;

        chats[i].device.minor = MISC_DYNAMIC_MINOR;
        chats[i].device.name = chats[i].device_name;
        chats[i].device.fops = &tg_fops;
        chats[i].device.mode = 0666;
    }

    add_message(&chats[0], "Maria", "Hello!");
    add_message(&chats[0], "Vladimir", "Good day!");
    add_message(&chats[0], "Maria", "How are you?");

    add_message(&chats[1], "sysadmin", "Server crashed again :(");
    add_message(&chats[1], "devops", "Restarting already...");

    for (int i = 1; i <= 15; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Message number %d", i);
        add_message(&chats[2], "Spammer", buf);
    }

    add_message(&chats[3], "kernel_panic", "RIP");

    // leave chat_5 empty
}


static int __init tg_init(void) {
    init_mock_data_and_devices();

    for (int i = 0; i < MAX_CHATS; i++) {
        int result = misc_register(&chats[i].device);
        if (result) {
            pr_err("fs_telegram: Error registering %s\n", chats[i].device_name);

            while (--i >= 0) {
                misc_deregister(&chats[i].device);
            }

            for (int j = 0; j < MAX_CHATS; j++) {
                free_chat_messages(&chats[j]);
            }

            return result;
        }
    }

    pr_info("fs_telegram: Module loaded\n");
    return 0;
}

static void __exit tg_exit(void) {
    for (int i = 0; i < MAX_CHATS; i++) {
        misc_deregister(&chats[i].device);
        free_chat_messages(&chats[i]);
    }

    pr_info("fs_telegram: Module unloaded\n");
}


module_init(tg_init);
module_exit(tg_exit);


MODULE_LICENSE("GPL");
