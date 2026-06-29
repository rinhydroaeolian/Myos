/**
 * myos.c — Main Entry Point for myOS Shell Prototype
 *
 * 小型操作系统原型的主入口。
 * 功能:
 *   1. 初始化进程管理器 (信号处理、PCB 表)
 *   2. 注册所有内置命令到命令表
 *   3. 启动 Shell 主循环 (显示 myOSshell 提示符)
 *   4. 退出时清理所有资源
 *
 * 使用方法:
 *   make && ./myos
 *
 * 架构说明:
 *   本程序模拟一个微型操作系统的核心功能:
 *   - Shell 命令解释器 (shell.c)
 *   - 19 个内置命令 (commands.c)
 *   - 全屏幕编辑器 (editor.c)
 *   - 虚拟键盘驱动 (keyboard.c)
 *   - 虚拟打印机驱动 (printer.c)
 *   - 进程管理 (process.c)
 *
 *   所有命令通过 fork() + exec() 模型执行，后台命令
 *   使用 & 符号标记，进程通过 SIGCHLD 自动回收。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shell.h"
#include "commands.h"
#include "process.h"
#include "keyboard.h"
#include "printer.h"

/**
 * print_banner() — 打印欢迎横幅
 * 在 Shell 启动时显示系统信息。
 */
static void print_banner(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║         Welcome to myOS Shell v1.0           ║\n");
    printf("║   Small Operating System Prototype           ║\n");
    printf("║                                              ║\n");
    printf("║   Type 'help' for available commands         ║\n");
    printf("║   Type 'exit' to quit                        ║\n");
    printf("╚══════════════════════════════════════════════╝\n");
    printf("\n");
}

/**
 * help_command() — 内置 help 命令的实现
 * 列出所有可用命令及其功能说明。
 */
static int help_command(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("\n┌─────────────────────────────────────────────────────────────┐\n");
    printf("│ myOS Shell — Available Commands                            │\n");
    printf("├──────┬──────────────────────────────────────────────────────┤\n");
    printf("│ %-4s │ %-52s │\n", "Cmd", "Description");
    printf("├──────┼──────────────────────────────────────────────────────┤\n");
    printf("│ %-4s │ %-52s │\n", "ps",  "观察系统所有进程数据");
    printf("│ %-4s │ %-52s │\n", "ps",  "axjf — 显示进程树状态");
    printf("│ %-4s │ %-52s │\n", "ps",  "aux — 按PID排序进程信息");
    printf("│ %-4s │ %-52s │\n", "ls",  "列出目录内容, -l 长格式");
    printf("│ %-4s │ %-52s │\n", "top", "动态显示系统进程和资源使用");
    printf("│ %-4s │ %-52s │\n", "cal", "显示日历");
    printf("│ %-4s │ %-52s │\n", "whoami","显示当前用户名");
    printf("│ %-4s │ %-52s │\n", "date","显示系统日期和时间");
    printf("│ %-4s │ %-52s │\n", "pwd", "打印当前工作目录");
    printf("│ %-4s │ %-52s │\n", "mv",  "移动/重命名文件");
    printf("│ %-4s │ %-52s │\n", "cp",  "复制文件");
    printf("│ %-4s │ %-52s │\n", "file","识别文件类型");
    printf("│ %-4s │ %-52s │\n", "cat", "连接并显示文件内容");
    printf("│ %-4s │ %-52s │\n", "rm",  "删除文件");
    printf("│ %-4s │ %-52s │\n", "vi",  "全屏幕文本编辑器");
    printf("│ %-4s │ %-52s │\n", "mkdir","创建目录");
    printf("│ %-4s │ %-52s │\n", "rmdir","删除空目录");
    printf("│ %-4s │ %-52s │\n", "print","打印文件为 PDF (虚拟打印机)");
    printf("│ %-4s │ %-52s │\n", "startkbd","启动虚拟键盘驱动");
    printf("│ %-4s │ %-52s │\n", "readkbd","读取虚拟键盘事件");
    printf("│ %-4s │ %-52s │\n", "stopkbd","停止虚拟键盘驱动");
    printf("│ %-4s │ %-52s │\n", "exit","退出 Shell");
    printf("│ %-4s │ %-52s │\n", "help","显示此帮助信息");
    printf("└──────┴──────────────────────────────────────────────────────┘\n");
    printf("\n提示: 在命令末尾添加 & 可后台运行 (如: sleep 10 &)\n");
    printf("      管道支持: 使用 | 连接多个命令 (如: ls -l|more)\n\n");
    return 0;
}

/**
 * register_all_commands() — 注册所有内置命令
 * 将命令名称映射到对应的处理函数。
 * 这是命令调度表的核心 — shell 解析输入后通过此表查找执行函数。
 */
static void register_all_commands(void) {
    /* 进程观察命令 */
    register_command("ps",    cmd_ps,     "观察系统所有进程数据");
    register_command("top",   cmd_top,    "动态显示系统进程信息");
    register_command("cal",   cmd_cal,    "显示日历");
    register_command("whoami",cmd_whoami, "显示当前用户名");
    register_command("date",  cmd_date,   "显示系统日期和时间");
    register_command("pwd",   cmd_pwd,    "打印当前工作目录");

    /* 文件操作命令 */
    register_command("ls",   cmd_ls,   "列出目录内容");
    register_command("cat",  cmd_cat,  "连接并显示文件内容");
    register_command("cp",   cmd_cp,   "复制文件");
    register_command("mv",   cmd_mv,   "移动/重命名文件");
    register_command("rm",   cmd_rm,   "删除文件");
    register_command("file", cmd_file, "识别文件类型");

    /* 目录操作命令 */
    register_command("mkdir", cmd_mkdir, "创建目录");
    register_command("rmdir", cmd_rmdir, "删除空目录");

    /* 编辑器命令 */
    register_command("vi", cmd_vi, "全屏幕文本编辑器");

    /* 分页器 (用于管道) */
    register_command("more", cmd_more_pager, "分页显示输出");

    /* 设备驱动命令 */
    register_command("print",    cmd_print,    "虚拟打印机 — 打印为 PDF");
    register_command("startkbd", cmd_startkbd,"启动虚拟键盘驱动");
    register_command("readkbd",  cmd_readkbd, "读取虚拟键盘事件");
    register_command("stopkbd",  cmd_stopkbd, "停止虚拟键盘驱动");

    /* Shell 控制命令 */
    register_command("exit", cmd_exit, "退出 myOS Shell");
    register_command("help", help_command, "显示帮助信息");
}

/**
 * main() — 程序入口点
 * 1. 初始化所有子系统
 * 2. 打印欢迎信息
 * 3. 启动 Shell 主循环
 * 4. 退出前清理资源
 */
int main(void) {
    /* 第一步: 初始化进程管理器 (信号处理器 + 进程表) */
    init_process_manager();

    /* 第二步: 初始化 Shell (命令表 + 环境) */
    shell_init();

    /* 第三步: 注册所有内置命令 */
    register_all_commands();

    /* 第四步: 打印欢迎横幅 */
    print_banner();

    /* 第五步: 进入 Shell 主循环 (阻塞直到用户输入 exit) */
    shell_run();

    /* 第六步: 清理资源 */
    shell_cleanup();

    /* 确保虚拟键盘驱动已停止 */
    kbd_stop();

    printf("Goodbye!\n");
    return 0;
}
