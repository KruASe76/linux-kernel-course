#include <asm/io.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>


#define KBD_IRQ 1
#define BUF_SIZE 256


struct kbd_event {
    unsigned char scancode;
    bool pressed;
    const char* name;
};


static const char* keymap[128] = {
    [0x01] = "ESC", [0x02] = "1", [0x03] = "2", [0x04] = "3", [0x05] = "4",
    [0x06] = "5", [0x07] = "6", [0x08] = "7", [0x09] = "8", [0x0A] = "9",
    [0x0B] = "0", [0x0C] = "-", [0x0D] = "=", [0x0E] = "BACKSPACE",
    [0x0F] = "TAB", [0x10] = "Q", [0x11] = "W", [0x12] = "E", [0x13] = "R",
    [0x14] = "T", [0x15] = "Y", [0x16] = "U", [0x17] = "I", [0x18] = "O",
    [0x19] = "P", [0x1A] = "[", [0x1B] = "]", [0x1C] = "ENTER",
    [0x1D] = "LCTRL", [0x1E] = "A", [0x1F] = "S", [0x20] = "D", [0x21] = "F",
    [0x22] = "G", [0x23] = "H", [0x24] = "J", [0x25] = "K", [0x26] = "L",
    [0x27] = ";", [0x28] = "'", [0x29] = "`", [0x2A] = "LSHIFT", [0x2B] = "\\",
    [0x2C] = "Z", [0x2D] = "X", [0x2E] = "C", [0x2F] = "V", [0x30] = "B",
    [0x31] = "N", [0x32] = "M", [0x33] = ",", [0x34] = ".", [0x35] = "/",
    [0x36] = "RSHIFT", [0x37] = "KPASTERISK", [0x38] = "LALT", [0x39] = "SPACE",
    [0x3A] = "CAPSLOCK", [0x3B] = "F1", [0x3C] = "F2", [0x3D] = "F3", [0x3E] = "F4",
    [0x3F] = "F5", [0x40] = "F6", [0x41] = "F7", [0x42] = "F8", [0x43] = "F9",
    [0x44] = "F10", [0x45] = "NUMLOCK", [0x46] = "SCROLLLOCK",
};


static unsigned char raw_scancodes[BUF_SIZE];
static int raw_head = 0;
static int raw_tail = 0;
static DEFINE_SPINLOCK(raw_lock);

static struct kbd_event events[BUF_SIZE];
static int head = 0;
static int tail = 0;
static DEFINE_SPINLOCK(events_lock);

static struct work_struct kbd_work;
static int dev_id;


static void kbd_work_func(struct work_struct* work) {
    unsigned char scancode;
    bool pressed;
    const char* name;
    unsigned long flags;

    while (1) {
        spin_lock_irqsave(&raw_lock, flags);
        if (raw_head == raw_tail) {
            spin_unlock_irqrestore(&raw_lock, flags);
            break;
        }
        scancode = raw_scancodes[raw_tail];
        raw_tail = (raw_tail + 1) % BUF_SIZE;
        spin_unlock_irqrestore(&raw_lock, flags);

        pressed = !(scancode & 0x80);
        scancode &= 0x7F;
        name = keymap[scancode] ? keymap[scancode] : "UNKNOWN";

        spin_lock_irqsave(&events_lock, flags);
        events[head].scancode = scancode;
        events[head].pressed = pressed;
        events[head].name = name;
        head = (head + 1) % BUF_SIZE;
        if (head == tail) {
            tail = (tail + 1) % BUF_SIZE;
        }
        spin_unlock_irqrestore(&events_lock, flags);
    }
}

static irqreturn_t kb_irq(int irq, void* dev) {
    unsigned char scancode = inb(0x60);
    unsigned long flags;

    spin_lock_irqsave(&raw_lock, flags);
    raw_scancodes[raw_head] = scancode;
    raw_head = (raw_head + 1) % BUF_SIZE;
    spin_unlock_irqrestore(&raw_lock, flags);

    schedule_work(&kbd_work);

    return IRQ_HANDLED;
}


static int proc_show(struct seq_file* m, void* v) {
    int i;
    unsigned long flags;

    spin_lock_irqsave(&events_lock, flags);
    i = tail;
    while (i != head) {
        seq_printf(m, "[%-7s] %-12s (0x%02x)\n",
                   events[i].pressed ? "PRESS" : "RELEASE",
                   events[i].name,
                   events[i].scancode);
        i = (i + 1) % BUF_SIZE;
    }
    spin_unlock_irqrestore(&events_lock, flags);

    return 0;
}

static int proc_open(struct inode* inode, struct file* file) {
    return single_open(file, proc_show, NULL);
}

static const struct proc_ops proc_fops = {
    .proc_open = proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};


static int __init kb_init(void) {
    int ret;

    INIT_WORK(&kbd_work, kbd_work_func);

    proc_create("kbd_monitor", 0444, NULL, &proc_fops);

    ret = request_irq(KBD_IRQ, kb_irq, IRQF_SHARED, "kbd_monitor", &dev_id);
    if (ret) {
        remove_proc_entry("kbd_monitor", NULL);
        return ret;
    }

    return 0;
}

static void __exit kb_exit(void) {
    free_irq(KBD_IRQ, &dev_id);
    flush_work(&kbd_work);
    remove_proc_entry("kbd_monitor", NULL);
}


module_init(kb_init);
module_exit(kb_exit);


MODULE_LICENSE("GPL");
