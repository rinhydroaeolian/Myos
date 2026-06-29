/**
 * commands.h — Command Implementations Header for myOS Shell
 *
 * 声明所有 19 个内置命令的处理函数。
 * 每个函数签名符合 cmd_handler_t: int cmd_xxx(int argc, char **argv)
 * 返回 0 表示成功，非 0 表示错误。
 */

#ifndef COMMANDS_H
#define COMMANDS_H

/* ── Process Observation Commands ────────────────────────────── */

/* ps: 观察系统所有进程数据 (读取 /proc 文件系统) */
int cmd_ps(int argc, char **argv);

/* ps axjf: 显示进程树状态
 * a=所有用户进程, x=无控制终端进程, j=作业格式, f=森林/树形显示 */
int cmd_ps_axjf(int argc, char **argv);

/* ps aux: 按 PID 排序显示进程信息 (BSD 风格) */
int cmd_ps_aux(int argc, char **argv);

/* top: 动态实时显示系统进程信息，按 CPU 使用率排序 */
int cmd_top(int argc, char **argv);

/* ── File & Directory Commands ───────────────────────────────── */

/* ls -l: 以长格式列出目录内容 (权限、链接数、所有者、大小、时间) */
int cmd_ls(int argc, char **argv);

/* ls -l|more: 以长格式列出目录内容并通过 more 分页显示 */
int cmd_ls_more(int argc, char **argv);

/* cat: 连接文件内容并输出到标准输出 */
int cmd_cat(int argc, char **argv);

/* cp: 复制文件 (保留权限) */
int cmd_cp(int argc, char **argv);

/* mv: 移动/重命名文件 (同文件系统用 rename，跨文件系统复制后删除) */
int cmd_mv(int argc, char **argv);

/* rm: 删除文件 (使用 unlink，带确认提示) */
int cmd_rm(int argc, char **argv);

/* file: 识别文件类型 (通过魔术字节和扩展名) */
int cmd_file(int argc, char **argv);

/* ── Utility Commands ────────────────────────────────────────── */

/* cal: 显示日历 (支持指定月份和年份) */
int cmd_cal(int argc, char **argv);

/* whoami: 显示当前有效用户名 */
int cmd_whoami(int argc, char **argv);

/* date: 显示或设置系统日期和时间 */
int cmd_date(int argc, char **argv);

/* pwd: 打印当前工作目录 */
int cmd_pwd(int argc, char **argv);

/* mkdir: 创建新目录 */
int cmd_mkdir(int argc, char **argv);

/* rmdir: 删除空目录 */
int cmd_rmdir(int argc, char **argv);

/* ── Shell Control Commands ──────────────────────────────────── */

/* exit: 退出 Shell */
int cmd_exit(int argc, char **argv);

/* more: 分页显示标准输入 (用于管道分页器) */
int cmd_more_pager(int argc, char **argv);

/* ── Editor Command ──────────────────────────────────────────── */

/* vi: 启动全屏幕文本编辑器 (类似 vi) */
int cmd_vi(int argc, char **argv);

/* ── Device Commands ─────────────────────────────────────────── */

/* print: 虚拟打印机命令 — 将文本文件打印为 PDF */
int cmd_print(int argc, char **argv);

/* readkbd: 读取虚拟键盘事件 */
int cmd_readkbd(int argc, char **argv);

/* startkbd: 启动虚拟键盘驱动 */
int cmd_startkbd(int argc, char **argv);

/* stopkbd: 停止虚拟键盘驱动 */
int cmd_stopkbd(int argc, char **argv);

#endif /* COMMANDS_H */
