#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");

struct pid_node {
    int pid;
    char comm[TASK_COMM_LEN];
    struct list_head list;
};

static struct list_head my_list;          // 配合 INIT_LIST_HEAD
static spinlock_t list_lock;
static struct task_struct *thread1;
static struct task_struct *thread2;

static int thread1_func(void *data)
{
    struct task_struct *p;

    for_each_process(p) {
        struct pid_node *node;

        if (kthread_should_stop())
            break;

        node = kmalloc(sizeof(*node), GFP_KERNEL);
        if (!node)
            continue;

        node->pid = p->pid;
        strscpy(node->comm, p->comm, TASK_COMM_LEN);

        spin_lock(&list_lock);
        list_add_tail(&node->list, &my_list); // 文档 5.2.3
        spin_unlock(&list_lock);
    }

    return 0;
}

static int thread2_func(void *data)
{
    while (!kthread_should_stop()) {
        struct pid_node *node = NULL;

        spin_lock(&list_lock);
        if (!list_empty(&my_list)) { // 文档 5.2.5
            node = list_first_entry(&my_list, struct pid_node, list); // 5.2.6
            list_del(&node->list); // 5.2.4
        }
        spin_unlock(&list_lock);

        if (node) {
            pr_info("pid=%d comm=%s\n", node->pid, node->comm);
            kfree(node);
        } else {
            msleep_interruptible(100); // 文档 5.5
        }
    }

    return 0;
}

static int __init kernel_module_init(void)
{
    // 1) 链表初始化（5.2.1）
    INIT_LIST_HEAD(&my_list);
    // 2) 自旋锁初始化（5.4.1）
    spin_lock_init(&list_lock);

    // 3) 创建线程（5.3.1）
    thread1 = kthread_create(thread1_func, NULL, "stu_t1");
    if (IS_ERR(thread1))
        return PTR_ERR(thread1);

    thread2 = kthread_create(thread2_func, NULL, "stu_t2");
    if (IS_ERR(thread2)) {
        kthread_stop(thread1);
        return PTR_ERR(thread2);
    }

    // 4) 唤醒线程（5.3.2）
    wake_up_process(thread1);
    wake_up_process(thread2);

    return 0;
}

static void __exit kernel_module_exit(void)
{
    struct list_head *pos, *n;

    // 1) 停线程（5.3.3）
    if (thread1)
        kthread_stop(thread1);
    if (thread2)
        kthread_stop(thread2);

    // 2) 用 list_for_each_safe 清理剩余节点（5.2.7）
    spin_lock(&list_lock);
    list_for_each_safe(pos, n, &my_list) {
        struct pid_node *node = list_entry(pos, struct pid_node, list);
        list_del(pos);
        kfree(node);
    }
    spin_unlock(&list_lock);
}

module_init(kernel_module_init);
module_exit(kernel_module_exit);
