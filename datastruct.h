/**
 * datastruct.h — Shared Data Structures for myOS Shell
 *
 * Contains:
 *   - Process Control Block (PCB) definition
 *   - Singly-linked list for process table and editor lines
 *   - Simple queue for keyboard event buffer
 *   - Job structure for background job tracking
 */

#ifndef DATASTRUCT_H
#define DATASTRUCT_H

#include <sys/types.h>

/* ── Process States ─────────────────────────────────────────── */
#define STATE_RUNNING    0
#define STATE_READY      1
#define STATE_BLOCKED    2
#define STATE_TERMINATED 3

/* ── Process Control Block (PCB) ─────────────────────────────── */
/* 每个 PCB 代表一个被追踪的进程，包含进程标识、名称、状态等信息 */
typedef struct pcb {
    pid_t pid;              /* 进程 ID */
    char name[256];         /* 进程名称/命令行 */
    int  state;             /* 进程状态: RUNNING/READY/BLOCKED/TERMINATED */
    struct pcb *next;       /* 链表指针，指向下一个 PCB */
} pcb_t;

/* ── Generic Singly-Linked List Node ─────────────────────────── */
/* 通用链表节点，用于编辑器行缓冲区和其它需要动态列表的地方 */
typedef struct line_node {
    char *text;                 /* 该行的文本内容 */
    int  len;                   /* 文本长度 */
    struct line_node *next;     /* 下一行 */
    struct line_node *prev;     /* 上一行 (双向链表，编辑器需要) */
} line_node_t;

/* ── Background Job ──────────────────────────────────────────── */
/* 后台作业结构，追踪通过 & 启动的后台进程 */
typedef struct job {
    pid_t pid;              /* 作业的进程 ID */
    char  command[512];     /* 启动该作业的命令行 */
    int   job_id;           /* 作业编号 (从 1 开始) */
    struct job *next;       /* 链表指针 */
} job_t;

/* ── Keyboard Event Queue ────────────────────────────────────── */
/* 键盘事件队列，存储虚拟键盘驱动产生的按键事件 */
#define KBD_QUEUE_SIZE 64

typedef struct {
    char events[KBD_QUEUE_SIZE][32];  /* 事件描述字符串 */
    int  front;                       /* 队首索引 */
    int  rear;                        /* 队尾索引 */
    int  count;                       /* 队列中当前元素个数 */
} kbd_queue_t;

/* ── Parser Token Limit ──────────────────────────────────────── */
#define MAX_ARGS    64      /* 一条命令最多支持的参数数量 */
#define MAX_CMDS    8       /* 管道中最多支持的命令数量 */
#define MAX_LINE    4096    /* 输入行最大长度 */

/* ── Function Declarations ───────────────────────────────────── */

/* PCB linked-list management */
pcb_t* pcb_create(pid_t pid, const char *name, int state);
void   pcb_add(pcb_t **head, pcb_t *new_pcb);
void   pcb_remove(pcb_t **head, pid_t pid);
pcb_t* pcb_find(pcb_t *head, pid_t pid);
void   pcb_free_all(pcb_t *head);

/* Line node management */
line_node_t* line_create(const char *text, int len);
void         line_free_all(line_node_t *head);

/* Keyboard queue operations */
void kbd_queue_init(kbd_queue_t *q);
int  kbd_queue_push(kbd_queue_t *q, const char *event);
int  kbd_queue_pop(kbd_queue_t *q, char *out, int maxlen);
int  kbd_queue_is_empty(kbd_queue_t *q);
int  kbd_queue_is_full(kbd_queue_t *q);

/* Job list management */
void  job_add(job_t **head, pid_t pid, const char *command);
void  job_remove(job_t **head, pid_t pid);
job_t* job_find(job_t *head, pid_t pid);
void  job_free_all(job_t *head);
int   job_next_id(job_t *head);

#endif /* DATASTRUCT_H */
