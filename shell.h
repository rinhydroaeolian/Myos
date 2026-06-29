/**
 * shell.h — Shell Module Header for myOS Shell
 *
 * 声明 Shell 命令解释器的所有公共接口，
 * 包括命令解析、调度和执行功能。
 */

#ifndef SHELL_H
#define SHELL_H

#include "datastruct.h"

/* ── Command Handler Type ────────────────────────────────────── */
/* 每个内置命令都是一个函数，接收 argc/argv，返回 0 表示成功 */
typedef int (*cmd_handler_t)(int argc, char **argv);

/* ── Command Table Entry ─────────────────────────────────────── */
/* 命令调度表中的条目：命令名称 + 对应的处理函数指针 */
typedef struct {
    const char     *name;      /* 命令名称 (如 "ls", "ps", "cat") */
    cmd_handler_t   handler;   /* 命令处理函数 */
    const char     *help;      /* 简短帮助文本 */
} cmd_entry_t;

/* ── Parsed Command Structure ────────────────────────────────── */
/* 表示一个解析后的简单命令 (管道中的一个片段) */
typedef struct {
    char  *args[MAX_ARGS];     /* 参数列表，args[0] 为命令名 */
    int    argc;               /* 参数个数 */
    int    background;         /* 是否为后台命令 (以 & 结尾) */
} parsed_cmd_t;

/* ── Pipeline Structure ──────────────────────────────────────── */
/* 表示一条可能包含管道的完整命令 */
typedef struct {
    parsed_cmd_t  cmds[MAX_CMDS];  /* 管道中的各段命令 */
    int           ncmds;           /* 管道命令数量 */
    int           background;      /* 整条管道是否后台运行 */
} pipeline_t;

/* ── Shell API ───────────────────────────────────────────────── */

/**
 * shell_init()
 * 初始化 Shell 环境：
 * - 注册所有内置命令
 * - 设置信号处理器
 * - 初始化进程管理器
 */
void shell_init(void);

/**
 * shell_run()
 * 启动 Shell 主循环：
 * - 显示提示符 myOSshell $
 * - 读取用户输入
 * - 解析命令
 * - 调度执行
 * - 循环直到收到 exit 命令
 */
void shell_run(void);

/**
 * shell_cleanup()
 * 清理 Shell 资源：
 * - 释放进程表
 * - 释放作业表
 * - 恢复终端设置
 */
void shell_cleanup(void);

/* ── Parser API ──────────────────────────────────────────────── */

/**
 * parse_command(line, pipeline)
 * 将输入行解析为管道结构。
 * @line:     用户输入的命令行字符串
 * @pipeline: 输出的管道结构
 * 返回: 0 成功, -1 解析错误
 */
int parse_command(char *line, pipeline_t *pipeline);

/* ── Executor API ────────────────────────────────────────────── */

/**
 * execute_pipeline(pipeline)
 * 执行解析后的管道命令。
 * - 如果是单个内置命令，直接在 shell 进程中执行
 * - 如果是管道或多个命令，fork 子进程执行
 * - 处理前台/后台执行
 * @pipeline: 要执行的管道结构
 * 返回: 0 成功, -1 错误
 */
int execute_pipeline(pipeline_t *pipeline);

/**
 * find_command(name)
 * 在内置命令表中查找命令。
 * @name: 命令名称
 * 返回: 找到的命令条目指针，未找到返回 NULL
 */
cmd_entry_t* find_command(const char *name);

/**
 * register_command(name, handler, help)
 * 动态注册一个内置命令到命令表。
 */
void register_command(const char *name, cmd_handler_t handler, const char *help);

#endif /* SHELL_H */
