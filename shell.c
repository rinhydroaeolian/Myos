/**
 * shell.c — Shell Command Interpreter for myOS Shell
 *
 * 实现 Shell 核心功能:
 *   1. 命令解析器 — 将输入行解析为管道命令结构
 *      - 按 | 分割管道段
 *      - 按空白字符分割参数
 *      - 检测行末 & 标记后台命令
 *   2. 命令调度器 — 查表并分发命令
 *      - 内置命令在 shell 进程中直接执行
 *      - 外部命令通过 fork() + execvp() 执行
 *   3. 管道执行器 — 连接多个命令的输入输出
 *      - 使用 pipe() + dup2() 重定向标准输入输出
 *   4. 后台作业管理 — 追踪 & 启动的进程
 *
 * 提示符: myOSshell $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include "shell.h"
#include "commands.h"
#include "process.h"

/* ── Constants ───────────────────────────────────────────────── */
#define MAX_CMD_TABLE 64           /* 命令表最大容量 */
#define PROMPT        "myOSshell $ "  /* Shell 提示符 */

/* ── Global Command Table ────────────────────────────────────── */
static cmd_entry_t cmd_table[MAX_CMD_TABLE];
static int         cmd_count = 0;

/* 退出标志 — 由 cmd_exit() 设置，shell 主循环检测 */
volatile int g_shell_running = 1;

/* ── Command Table Management ────────────────────────────────── */

/**
 * register_command() — 注册命令到调度表
 * @name:    命令名称 (用户键入的命令)
 * @handler: 命令处理函数指针
 * @help:    帮助描述文本
 */
void register_command(const char *name, cmd_handler_t handler, const char *help) {
    if (cmd_count < MAX_CMD_TABLE) {
        cmd_table[cmd_count].name    = name;
        cmd_table[cmd_count].handler = handler;
        cmd_table[cmd_count].help    = help;
        cmd_count++;
    }
}

/**
 * find_command() — 在命令表中查找命令
 * 线性搜索命令表，按名称匹配。
 * @name: 要查找的命令名称
 * 返回: 命令条目指针，未找到返回 NULL
 */
cmd_entry_t* find_command(const char *name) {
    for (int i = 0; i < cmd_count; i++) {
        if (strcmp(cmd_table[i].name, name) == 0) {
            return &cmd_table[i];
        }
    }
    return NULL;
}

/* ── Input Reader ────────────────────────────────────────────── */

/**
 * read_input() — 读取用户输入行
 * 显示提示符，读取一行输入。
 * 返回: 动态分配的字符串 (调用者负责 free)，EOF 返回 NULL
 */
static char* read_input(void) {
    /* 使用 getline 的动态分配版本，手动实现 */
    char *line = (char*)malloc(MAX_LINE);
    if (!line) return NULL;

    /* 显示提示符并刷新 */
    printf("%s", PROMPT);
    fflush(stdout);

    /* 读取输入行 */
    if (fgets(line, MAX_LINE, stdin) == NULL) {
        free(line);
        return NULL;  /* EOF */
    }

    /* 去除末尾换行符 */
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
        line[len - 1] = '\0';
    }

    return line;
}

/* ── Command Parser ──────────────────────────────────────────── */

/**
 * parse_command() — 解析命令行输入
 *
 * 解析步骤:
 *   1. 跳过前导空白
 *   2. 按 | 字符分割管道段
 *   3. 每个管道段内按空白分割参数
 *   4. 检测 & 标记后台执行
 *
 * @line:     输入命令行字符串 (会被修改)
 * @pipeline: 输出的解析结果
 * 返回: 0 成功, -1 错误
 *
 * 示例:
 *   "ls -l | more"   -> cmds[0]={"ls","-l"} | cmds[1]={"more"}
 *   "sleep 10 &"     -> cmds[0]={"sleep","10"}, background=1
 *   "ps aux"         -> cmds[0]={"ps","aux"}
 */
int parse_command(char *line, pipeline_t *pipeline) {
    if (!line || !pipeline) return -1;

    memset(pipeline, 0, sizeof(pipeline_t));
    char *saveptr;        /* strtok_r 的上下文指针 */
    char *seg = strtok_r(line, "|", &saveptr);  /* 按管道分割 */

    /* 处理空输入 */
    if (!seg) return -1;

    while (seg && pipeline->ncmds < MAX_CMDS) {
        parsed_cmd_t *cmd = &pipeline->cmds[pipeline->ncmds];
        memset(cmd, 0, sizeof(parsed_cmd_t));

        /* 去除首尾空白 */
        while (*seg == ' ' || *seg == '\t') seg++;
        char *end = seg + strlen(seg) - 1;
        while (end > seg && (*end == ' ' || *end == '\t')) {
            *end = '\0';
            end--;
        }

        /* 手动分割参数 (避免嵌套 strtok 问题)
         * 不使用 strtok_r，因为内层 strtok_r 会破坏外层 saveptr */
        int arg_idx = 0;
        char *p = seg;
        while (*p && arg_idx < MAX_ARGS - 1) {
            /* 跳过空白 */
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '\0') break;

            cmd->args[arg_idx] = p;
            arg_idx++;

            /* 找到参数结尾 */
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) {
                *p = '\0';  /* 终止当前参数 */
                p++;
            }
        }
        cmd->argc = arg_idx;

        if (cmd->argc > 0) {
            pipeline->ncmds++;
        }

        seg = strtok_r(NULL, "|", &saveptr);  /* 获取下一个管道段 */
    }

    /* 检查最后一个命令是否以 & 结尾 (后台运行) */
    if (pipeline->ncmds > 0) {
        parsed_cmd_t *last = &pipeline->cmds[pipeline->ncmds - 1];
        if (last->argc > 0) {
            char *last_arg = last->args[last->argc - 1];
            if (strcmp(last_arg, "&") == 0) {
                last->background = 1;
                pipeline->background = 1;
                last->args[last->argc - 1] = NULL;  /* 移除 & */
                last->argc--;
            }
        }
    }

    return (pipeline->ncmds > 0) ? 0 : -1;
}

/* ── Command Executor ────────────────────────────────────────── */

/**
 * exec_single_command() — 执行单个简单命令 (无管道)
 *
 * @cmd: 要执行的解析后命令
 * 返回: 命令的退出码
 *
 * 处理逻辑:
 *   - 特殊处理 ps axjf / ps aux (通过检测参数路由到正确的处理器)
 *   - 特殊处理 ls -l|more (在 pipeline 层处理)
 *   - 内置命令: 直接调用 handler
 *   - 外部命令: fork() + execvp()
 */
static int exec_single_command(parsed_cmd_t *cmd) {
    if (cmd->argc == 0) return 0;

    const char *cmd_name = cmd->args[0];

    /* 特殊路由: ps 命令根据参数分派 */
    if (strcmp(cmd_name, "ps") == 0) {
        if (cmd->argc >= 2 && strcmp(cmd->args[1], "axjf") == 0) {
            return cmd_ps_axjf(cmd->argc - 1, cmd->args + 1);
        }
        if (cmd->argc >= 2 && strcmp(cmd->args[1], "aux") == 0) {
            return cmd_ps_aux(cmd->argc - 1, cmd->args + 1);
        }
        /* 普通 ps 不区分命令行，走正常流程 */
    }

    /* 在内置命令表中查找 */
    cmd_entry_t *entry = find_command(cmd_name);
    if (entry) {
        /* 内置命令 — 直接在当前进程执行 */
        return entry->handler(cmd->argc, cmd->args);
    }

    /* 外部命令 — fork 子进程执行 */
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        /* 子进程: 执行外部命令 */
        /* 构造以 NULL 结尾的参数数组 */
        char *argv[MAX_ARGS + 1];
        for (int i = 0; i < cmd->argc; i++) {
            argv[i] = cmd->args[i];
        }
        argv[cmd->argc] = NULL;

        execvp(cmd_name, argv);
        /* execvp 只在出错时返回 */
        fprintf(stderr, "myOSshell: %s: command not found\n", cmd_name);
        _exit(127);
    } else {
        /* 父进程: 追踪并等待 */
        track_process(pid, cmd_name, STATE_RUNNING);

        if (cmd->background) {
            /* 后台执行 — 不等待，注册作业 */
            int job_id = add_job(pid, cmd_name);
            printf("[%d] %d\n", job_id, pid);
        } else {
            /* 前台执行 — 等待子进程完成 */
            int status;
            waitpid(pid, &status, 0);
            untrack_process(pid);
        }
    }

    return 0;
}

/**
 * exec_pipeline_segment() — 在子进程中执行管道段
 *
 * @cmd:   要执行的命令
 * @in_fd:  标准输入重定向源 (或 STDIN_FILENO)
 * @out_fd: 标准输出重定向目标 (或 STDOUT_FILENO)
 *
 * 在 fork 后的子进程中调用，设置 stdin/stdout 重定向。
 * 不返回 (调用 _exit)。
 */
static void exec_pipeline_segment(parsed_cmd_t *cmd, int in_fd, int out_fd) {
    /* 重定向标准输入 */
    if (in_fd != STDIN_FILENO) {
        dup2(in_fd, STDIN_FILENO);
        close(in_fd);
    }

    /* 重定向标准输出 */
    if (out_fd != STDOUT_FILENO) {
        dup2(out_fd, STDOUT_FILENO);
        close(out_fd);
    }

    /* 检查是否为内置命令 */
    cmd_entry_t *entry = find_command(cmd->args[0]);
    if (entry) {
        int ret = entry->handler(cmd->argc, cmd->args);
        _exit(ret);
    }

    /* 外部命令 */
    char *argv[MAX_ARGS + 1];
    for (int i = 0; i < cmd->argc; i++) argv[i] = cmd->args[i];
    argv[cmd->argc] = NULL;
    execvp(cmd->args[0], argv);
    fprintf(stderr, "myOSshell: %s: command not found\n", cmd->args[0]);
    _exit(127);
}

/**
 * execute_pipeline() — 执行管道命令
 *
 * 对于单命令: 调用 exec_single_command()
 * 对于管道 (ncmds > 1): fork 多个子进程，用 pipe 连接:
 *
 *   cmd[0] | cmd[1] | ... | cmd[n-1]
 *
 *   为每对相邻命令创建 pipe:
 *     cmd[i].stdout -> pipe[i].write
 *     cmd[i+1].stdin  -> pipe[i].read
 *
 *   父进程关闭所有 pipe fd，等待所有子进程完成。
 */
int execute_pipeline(pipeline_t *pipeline) {
    if (!pipeline || pipeline->ncmds == 0) return -1;
    if (pipeline->ncmds > MAX_CMDS) {
        fprintf(stderr, "myOSshell: too many pipeline commands\n");
        return -1;
    }

    /* 空命令 (只有空白) */
    if (pipeline->cmds[0].argc == 0) return 0;

    /* 检查是否为 ls -l|more 的特殊组合 */
    if (pipeline->ncmds == 2 &&
        strcmp(pipeline->cmds[0].args[0], "ls") == 0 &&
        strcmp(pipeline->cmds[1].args[0], "more") == 0) {
        return cmd_ls_more(pipeline->cmds[0].argc, pipeline->cmds[0].args);
    }

    /* 单命令 (无管道) */
    if (pipeline->ncmds == 1) {
        return exec_single_command(&pipeline->cmds[0]);
    }

    /* ── 多命令管道 ────────────────────────────────────────── */
    int n = pipeline->ncmds;
    int pipe_fds[MAX_CMDS - 1][2];  /* n-1 个管道 */
    pid_t pids[MAX_CMDS];

    /* 创建所有管道 */
    for (int i = 0; i < n - 1; i++) {
        if (pipe(pipe_fds[i]) < 0) {
            perror("pipe");
            return -1;
        }
    }

    /* 为每个管道段 fork 子进程 */
    for (int i = 0; i < n; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return -1;
        }

        if (pid == 0) {
            /* 子进程 — 设置重定向并执行 */

            /* 对于非首命令，stdin 来自前一个管道 */
            int in_fd = (i > 0) ? pipe_fds[i - 1][0] : STDIN_FILENO;
            /* 对于非末命令，stdout 去往下一个管道 */
            int out_fd = (i < n - 1) ? pipe_fds[i][1] : STDOUT_FILENO;

            /* 关闭所有不需要的管道 fd */
            for (int j = 0; j < n - 1; j++) {
                if (j != i - 1) close(pipe_fds[j][0]);  /* 不是我需要的读端 */
                if (j != i)     close(pipe_fds[j][1]);  /* 不是我需要的写端 */
            }

            exec_pipeline_segment(&pipeline->cmds[i], in_fd, out_fd);
            /* exec_pipeline_segment 不返回 */
        }

        pids[i] = pid;
        track_process(pid, pipeline->cmds[i].args[0], STATE_RUNNING);
    }

    /* 父进程 — 关闭所有管道 fd */
    for (int i = 0; i < n - 1; i++) {
        close(pipe_fds[i][0]);
        close(pipe_fds[i][1]);
    }

    /* 前台/后台处理 */
    if (pipeline->background) {
        /* 后台执行 — 不等待 */
        for (int i = 0; i < n; i++) {
            int job_id = add_job(pids[i], pipeline->cmds[i].args[0]);
            printf("[%d] %d\n", job_id, pids[i]);
        }
    } else {
        /* 前台执行 — 等待所有子进程 */
        for (int i = 0; i < n; i++) {
            int status;
            waitpid(pids[i], &status, 0);
            untrack_process(pids[i]);
        }
    }

    return 0;
}

/* ── Shell Lifecycle ─────────────────────────────────────────── */

/**
 * shell_init() — 初始化 Shell 环境
 * 重置命令表和运行标志。
 */
void shell_init(void) {
    cmd_count = 0;
    g_shell_running = 1;
    memset(cmd_table, 0, sizeof(cmd_table));
}

/**
 * shell_cleanup() — 清理 Shell 资源
 * 释放进程表和作业表。
 */
void shell_cleanup(void) {
    pcb_free_all(process_table);
    process_table = NULL;
    job_free_all(job_list);
    job_list = NULL;
}

/**
 * shell_run() — Shell 主循环
 *
 * 循环:
 *   1. 显示提示符并读取输入
 *   2. 回收已终止的后台进程
 *   3. 解析命令行
 *   4. 执行管道命令
 *   5. 重复，直到 g_shell_running == 0
 */
void shell_run(void) {
    char *line;
    pipeline_t pipeline;

    while (g_shell_running) {
        /* 回收已完成的子进程 (非阻塞) */
        reap_children();

        /* 读取用户输入 */
        line = read_input();
        if (!line) {
            /* EOF (Ctrl+D) — 退出 Shell */
            printf("\n");
            break;
        }

        /* 跳过空行 */
        if (line[0] == '\0') {
            free(line);
            continue;
        }

        /* 解析命令 */
        if (parse_command(line, &pipeline) == 0) {
            /* 执行命令 */
            execute_pipeline(&pipeline);
        }

        free(line);
    }
}
