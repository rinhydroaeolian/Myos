/**
 * process.c — Process Management Implementation for myOS Shell
 *
 * 实现进程控制块 (PCB) 管理、后台作业追踪、
 * 信号处理 (SIGCHLD) 和子进程回收。
 * 所有函数均附有详细注释说明功能。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include "process.h"

/* ── Global State ────────────────────────────────────────────── */
pcb_t *process_table = NULL;   /* 进程表头指针 */
job_t *job_list = NULL;        /* 后台作业表头指针 */

/* ── PCB Linked-List Operations ──────────────────────────────── */

/**
 * pcb_create() — 创建一个新的 PCB 节点
 * @pid:   进程 ID
 * @name:  进程名称/命令行字符串
 * @state: 初始状态
 * 返回:   新分配的 PCB 指针，失败返回 NULL
 */
pcb_t* pcb_create(pid_t pid, const char *name, int state) {
    pcb_t *p = (pcb_t*)malloc(sizeof(pcb_t));
    if (!p) return NULL;
    p->pid = pid;
    strncpy(p->name, name, sizeof(p->name) - 1);
    p->name[sizeof(p->name) - 1] = '\0';
    p->state = state;
    p->next = NULL;
    return p;
}

/**
 * pcb_add() — 将 PCB 添加到链表头部
 */
void pcb_add(pcb_t **head, pcb_t *new_pcb) {
    if (!new_pcb) return;
    new_pcb->next = *head;
    *head = new_pcb;
}

/**
 * pcb_remove() — 从链表中移除指定 PID 的 PCB
 */
void pcb_remove(pcb_t **head, pid_t pid) {
    pcb_t *curr = *head, *prev = NULL;
    while (curr) {
        if (curr->pid == pid) {
            if (prev) prev->next = curr->next;
            else      *head = curr->next;
            free(curr);
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

/**
 * pcb_find() — 按 PID 查找 PCB
 * 返回: 找到的 PCB 指针，未找到返回 NULL
 */
pcb_t* pcb_find(pcb_t *head, pid_t pid) {
    while (head) {
        if (head->pid == pid) return head;
        head = head->next;
    }
    return NULL;
}

/**
 * pcb_free_all() — 释放整个 PCB 链表
 */
void pcb_free_all(pcb_t *head) {
    while (head) {
        pcb_t *tmp = head;
        head = head->next;
        free(tmp);
    }
}

/* ── Line Node Operations ────────────────────────────────────── */

/**
 * line_create() — 创建新行节点 (用于编辑器)
 * @text: 行文本内容
 * @len:  文本长度
 */
line_node_t* line_create(const char *text, int len) {
    line_node_t *node = (line_node_t*)malloc(sizeof(line_node_t));
    if (!node) return NULL;
    node->text = (char*)malloc(len + 1);
    if (!node->text) { free(node); return NULL; }
    memcpy(node->text, text, len);
    node->text[len] = '\0';
    node->len = len;
    node->next = NULL;
    node->prev = NULL;
    return node;
}

/**
 * line_free_all() — 释放整个行链表
 */
void line_free_all(line_node_t *head) {
    while (head) {
        line_node_t *tmp = head;
        head = head->next;
        free(tmp->text);
        free(tmp);
    }
}

/* ── Keyboard Queue Operations ───────────────────────────────── */

/**
 * kbd_queue_init() — 初始化键盘事件循环队列
 */
void kbd_queue_init(kbd_queue_t *q) {
    q->front = 0;
    q->rear = 0;
    q->count = 0;
}

/**
 * kbd_queue_push() — 向队列添加一个事件
 * 返回: 0 成功, -1 队列满
 */
int kbd_queue_push(kbd_queue_t *q, const char *event) {
    if (q->count >= KBD_QUEUE_SIZE) return -1;
    strncpy(q->events[q->rear], event, 31);
    q->events[q->rear][31] = '\0';
    q->rear = (q->rear + 1) % KBD_QUEUE_SIZE;
    q->count++;
    return 0;
}

/**
 * kbd_queue_pop() — 从队列取出一个事件
 * @out:    输出缓冲区
 * @maxlen: 缓冲区大小
 * 返回: 0 成功, -1 队列空
 */
int kbd_queue_pop(kbd_queue_t *q, char *out, int maxlen) {
    if (q->count <= 0) return -1;
    strncpy(out, q->events[q->front], maxlen - 1);
    out[maxlen - 1] = '\0';
    q->front = (q->front + 1) % KBD_QUEUE_SIZE;
    q->count--;
    return 0;
}

int kbd_queue_is_empty(kbd_queue_t *q) { return q->count == 0; }
int kbd_queue_is_full(kbd_queue_t *q)  { return q->count >= KBD_QUEUE_SIZE; }

/* ── Job List Operations ─────────────────────────────────────── */

/**
 * job_add() — 添加后台作业到链表
 */
void job_add(job_t **head, pid_t pid, const char *command) {
    job_t *j = (job_t*)malloc(sizeof(job_t));
    if (!j) return;
    j->pid = pid;
    strncpy(j->command, command, sizeof(j->command) - 1);
    j->command[sizeof(j->command) - 1] = '\0';
    j->job_id = job_next_id(*head);
    j->next = *head;
    *head = j;
}

/**
 * job_remove() — 从链表中移除作业
 */
void job_remove(job_t **head, pid_t pid) {
    job_t *curr = *head, *prev = NULL;
    while (curr) {
        if (curr->pid == pid) {
            if (prev) prev->next = curr->next;
            else      *head = curr->next;
            free(curr);
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

/**
 * job_find() — 按 PID 查找作业
 */
job_t* job_find(job_t *head, pid_t pid) {
    while (head) {
        if (head->pid == pid) return head;
        head = head->next;
    }
    return NULL;
}

/**
 * job_free_all() — 释放整个作业链表
 */
void job_free_all(job_t *head) {
    while (head) {
        job_t *tmp = head;
        head = head->next;
        free(tmp);
    }
}

/**
 * job_next_id() — 分配下一个作业编号
 * 遍历链表找出最大 ID + 1
 */
int job_next_id(job_t *head) {
    int max_id = 0;
    while (head) {
        if (head->job_id > max_id) max_id = head->job_id;
        head = head->next;
    }
    return max_id + 1;
}

/* ── Process Manager ─────────────────────────────────────────── */

/**
 * reap_children() — 非阻塞回收所有已终止子进程
 * 使用 waitpid(..., WNOHANG) 遍历回收。
 * 对于每个回收的进程，从进程表和作业表中移除，
 * 如果是后台作业则打印完成信息。
 */
void reap_children(void) {
    pid_t pid;
    int status;
    /* 循环回收直到没有更多已终止的子进程 */
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        /* 检查是否有对应的后台作业 */
        job_t *j = job_find(job_list, pid);
        if (j) {
            /* 后台作业完成，打印提示 */
            fprintf(stderr, "\n[%d]  Done    %s\n", j->job_id, j->command);
            remove_job(pid);
        }
        /* 从进程表中移除 */
        pcb_remove(&process_table, pid);
    }
}

/**
 * sigchld_handler() — SIGCHLD 信号处理函数
 * 当子进程状态改变时 (终止/停止/继续) 被调用。
 * 调用 reap_children() 回收僵尸进程。
 */
static void sigchld_handler(int sig) {
    (void)sig;  /* 未使用的参数 */
    /* 保存并恢复 errno，避免影响主程序 */
    int saved_errno = errno;
    reap_children();
    errno = saved_errno;
}

/**
 * init_process_manager() — 初始化进程管理器
 * 设置信号处理器:
 *   - SIGCHLD: 自动回收子进程
 *   - SIGTTOU: 忽略，允许后台进程写入终端
 */
void init_process_manager(void) {
    struct sigaction sa;

    /* 设置 SIGCHLD 处理器 */
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;  /* 重启被中断的系统调用，不报告停止的子进程 */
    sigaction(SIGCHLD, &sa, NULL);

    /* 忽略 SIGTTOU，允许后台进程输出到终端 */
    signal(SIGTTOU, SIG_IGN);
}

/**
 * track_process() — 追踪新进程
 * 创建一个新 PCB 并添加到全局进程表。
 */
void track_process(pid_t pid, const char *name, int state) {
    pcb_t *p = pcb_create(pid, name, state);
    if (p) pcb_add(&process_table, p);
}

/**
 * untrack_process() — 停止追踪进程
 */
void untrack_process(pid_t pid) {
    pcb_remove(&process_table, pid);
}

/**
 * update_process_state() — 更新进程状态
 */
void update_process_state(pid_t pid, int state) {
    pcb_t *p = pcb_find(process_table, pid);
    if (p) p->state = state;
}

/**
 * print_process_table() — 打印进程表
 * 用于内置 ps 命令，显示所有追踪的进程。
 */
void print_process_table(void) {
    printf("  PID STATE   COMMAND\n");
    printf("----- ------- --------\n");
    pcb_t *p = process_table;
    while (p) {
        const char *state_str;
        switch (p->state) {
            case STATE_RUNNING:    state_str = "RUNNING"; break;
            case STATE_READY:      state_str = "READY  "; break;
            case STATE_BLOCKED:    state_str = "BLOCKED"; break;
            case STATE_TERMINATED: state_str = "TERM   "; break;
            default:               state_str = "UNKNOWN"; break;
        }
        printf("%5d %-7s %s\n", p->pid, state_str, p->name);
        p = p->next;
    }
}

/* ── Job Management ──────────────────────────────────────────── */

/**
 * add_job() — 添加后台作业
 * 返回分配的作业编号。
 */
int add_job(pid_t pid, const char *command) {
    job_add(&job_list, pid, command);
    job_t *j = job_find(job_list, pid);
    return j ? j->job_id : 0;
}

/**
 * remove_job() — 移除后台作业
 */
void remove_job(pid_t pid) {
    job_remove(&job_list, pid);
}

/**
 * print_jobs() — 打印所有活跃的后台作业
 */
void print_jobs(void) {
    if (!job_list) {
        printf("No background jobs.\n");
        return;
    }
    printf("Job ID   PID   Command\n");
    printf("------  -----  -------\n");
    job_t *j = job_list;
    while (j) {
        printf("  %-4d   %-5d  %s\n", j->job_id, j->pid, j->command);
        j = j->next;
    }
}
