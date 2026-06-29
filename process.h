/**
 * process.h — Process Management Header for myOS Shell
 *
 * 提供进程控制块 (PCB) 的管理接口，包括进程追踪、
 * 后台作业管理和信号处理。
 */

#ifndef PROCESS_H
#define PROCESS_H

#include "datastruct.h"

/* ── Global Process Table ────────────────────────────────────── */
extern pcb_t *process_table;    /* 全局进程链表头 */
extern job_t *job_list;         /* 全局后台作业链表头 */

/* ── Process Management API ──────────────────────────────────── */

/**
 * init_process_manager()
 * 初始化进程管理器，设置信号处理函数。
 * - 注册 SIGCHLD 处理函数以自动回收子进程
 * - 忽略 SIGTTOU 以允许后台进程输出
 */
void init_process_manager(void);

/**
 * track_process(pid, name, state)
 * 将一个新进程添加到全局进程表中。
 * 在 fork() 之后由 shell 调用以追踪子进程。
 */
void track_process(pid_t pid, const char *name, int state);

/**
 * untrack_process(pid)
 * 从进程表中移除指定 PID 的条目。
 * 在 SIGCHLD 处理中回收已终止进程时调用。
 */
void untrack_process(pid_t pid);

/**
 * update_process_state(pid, state)
 * 更新指定进程的状态 (RUNNING/READY/BLOCKED/TERMINATED)。
 */
void update_process_state(pid_t pid, int state);

/**
 * print_process_table()
 * 打印当前所有被追踪的进程信息 (供 ps 命令使用)。
 */
void print_process_table(void);

/**
 * reap_children()
 * 非阻塞地回收所有已终止的子进程 (WNOHANG)。
 * 由 SIGCHLD 处理函数或 shell 主循环调用。
 */
void reap_children(void);

/* ── Background Job API ──────────────────────────────────────── */

/**
 * add_job(pid, command)
 * 将一个新后台作业添加到作业列表。
 * 返回分配的作业编号。
 */
int add_job(pid_t pid, const char *command);

/**
 * remove_job(pid)
 * 从作业列表中移除指定 PID 的作业。
 */
void remove_job(pid_t pid);

/**
 * print_jobs()
 * 打印所有活跃的后台作业 (类似 bash 的 jobs 命令)。
 */
void print_jobs(void);

#endif /* PROCESS_H */
