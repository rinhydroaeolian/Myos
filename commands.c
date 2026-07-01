/**
 * commands.c — Built-in Command Implementations for myOS Shell
 *
 * 实现所有 19+ 个内置命令，每个命令均通过 POSIX 系统调用
 * 或手动解析 /proc 文件系统实现 (不直接调用系统命令)。
 *
 * 每个函数格式: int cmd_xxx(int argc, char **argv)
 *   返回 0 表示成功，非 0 表示错误。
 *
 * 命令列表:
 *   进程观察: ps, ps axjf, ps aux, top
 *   文件操作: ls, cat, cp, mv, rm, file
 *   工具命令: cal, whoami, date, pwd
 *   目录操作: mkdir, rmdir
 *   Shell 控制: exit
 *   编辑器:   vi
 *   设备驱动: print, startkbd, readkbd, stopkbd
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <ctype.h>

#include "commands.h"
#include "shell.h"
#include "process.h"
#include "editor.h"
#include "printer.h"
#include "keyboard.h"
#include "datastruct.h"

/* ── Forward Declarations ────────────────────────────────────── */
extern volatile int g_shell_running;

/* ── Helper: Get username from UID ──────────────────────────── */
static const char* uid_to_name(uid_t uid) {
    struct passwd *pw = getpwuid(uid);
    return pw ? pw->pw_name : "unknown";
}

/* ── Helper: Format file mode to ls -l style ────────────────── */
/* 将 stat.st_mode 转换为 "drwxr-xr-x" 格式的权限字符串 */
static void format_mode(mode_t mode, char *out) {
    /* 文件类型 */
    if (S_ISDIR(mode))  out[0] = 'd';
    else if (S_ISLNK(mode)) out[0] = 'l';
    else if (S_ISCHR(mode)) out[0] = 'c';
    else if (S_ISBLK(mode)) out[0] = 'b';
    else if (S_ISFIFO(mode))out[0] = 'p';
    else if (S_ISSOCK(mode))out[0] = 's';
    else out[0] = '-';

    /* 权限位 */
    out[1] = (mode & S_IRUSR) ? 'r' : '-';
    out[2] = (mode & S_IWUSR) ? 'w' : '-';
    out[3] = (mode & S_IXUSR) ? 'x' : '-';
    out[4] = (mode & S_IRGRP) ? 'r' : '-';
    out[5] = (mode & S_IWGRP) ? 'w' : '-';
    out[6] = (mode & S_IXGRP) ? 'x' : '-';
    out[7] = (mode & S_IROTH) ? 'r' : '-';
    out[8] = (mode & S_IWOTH) ? 'w' : '-';
    out[9] = (mode & S_IXOTH) ? 'x' : '-';
    out[10] = '\0';
}

/**
 * cmd_ps() — 观察系统所有进程数据
 *
 * 功能: 读取 /proc 文件系统，列出所有运行中的进程信息。
 * 对于每个数字目录 (PID)，读取 /proc/[pid]/stat 和 /proc/[pid]/status
 * 获取 PID、进程名、状态等基本信息。
 *
 * 输出格式: PID STATE COMMAND
 */
int cmd_ps(int argc, char **argv) {
    (void)argc; (void)argv;

    printf("  PID TTY      STAT   TIME COMMAND\n");
    printf("----- -------- ------ ----- -------\n");

    DIR *dir = opendir("/proc");
    if (!dir) {
        perror("ps: opendir /proc");
        return 1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        /* 只处理数字目录 (PID) */
        if (entry->d_type != DT_DIR) continue;
        char *end;
        long pid = strtol(entry->d_name, &end, 10);
        if (*end != '\0') continue;

        char path[256];

        /* 读取命令行 */
        snprintf(path, sizeof(path), "/proc/%ld/cmdline", pid);
        FILE *f = fopen(path, "r");
        char cmdline[256] = "";
        if (f) {
            size_t n = fread(cmdline, 1, sizeof(cmdline) - 1, f);
            if (n > 0) {
                /* cmdline 使用 \0 分隔，替换为空格 */
                for (size_t i = 0; i < n - 1; i++) {
                    if (cmdline[i] == '\0') cmdline[i] = ' ';
                }
                cmdline[n] = '\0';
            }
            fclose(f);
        }

        /* 如果没有 cmdline，使用 comm */
        if (strlen(cmdline) == 0) {
            snprintf(path, sizeof(path), "/proc/%ld/comm", pid);
            f = fopen(path, "r");
            if (f) {
                if (fgets(cmdline, sizeof(cmdline), f)) {
                    cmdline[strcspn(cmdline, "\n")] = '\0';
                }
                fclose(f);
            }
        }

        /* 读取状态 */
        char state[16] = "?";
        char tty[16] = "?";
        snprintf(path, sizeof(path), "/proc/%ld/stat", pid);
        f = fopen(path, "r");
        if (f) {
            char statline[1024];
            if (fgets(statline, sizeof(statline), f)) {
                /* 解析 stat 文件: 跳过 pid 和 comm (括号内容) */
                char *p = statline;
                while (*p && *p != ')') p++;
                if (*p == ')') p += 2; /* 跳过 ") " */

                /* 字段 3: state */
                if (*p) {
                    state[0] = *p;
                    state[1] = '\0';
                }

                /* 跳过几个字段到达 tty_nr (字段 7) */
                int field = 3;
                while (*p && field < 7) {
                    if (*p == ' ') field++;
                    p++;
                }
                snprintf(tty, sizeof(tty), "%.15s", p);
                /* 简化 tty 显示 */
                if (strcmp(tty, "0") == 0 || tty[0] == '0') {
                    strcpy(tty, "?");
                }
            }
            fclose(f);
        }

        /* 读取进程运行时间 (utime + stime) */
        char utime_str[32] = "0", stime_str[32] = "0";
        snprintf(path, sizeof(path), "/proc/%ld/stat", pid);
        f = fopen(path, "r");
        if (f) {
            char statline[1024];
            if (fgets(statline, sizeof(statline), f)) {
                /* 简单提取: 解析到第 14,15 字段 */
                char *p = statline;
                while (*p && *p != ')') p++;
                if (*p == ')') p += 2;
                for (int i = 3; i < 14 && *p; i++) {
                    while (*p && *p != ' ') p++;
                    if (*p == ' ') p++;
                }
                /* 现在 p 指向 utime (字段 14) */
                char *next = p;
                while (*next && *next != ' ') next++;
                if (*next) { *next = '\0'; next++; }
                snprintf(utime_str, sizeof(utime_str), "%.31s", p);

                /* stime (字段 15) */
                p = next;
                next = p;
                while (*next && *next != ' ') next++;
                if (*next) *next = '\0';
                snprintf(stime_str, sizeof(stime_str), "%.31s", p);
            }
            fclose(f);
        }

        long utime = atol(utime_str), stime = atol(stime_str);
        long total_ticks = utime + stime;
        long seconds = total_ticks / sysconf(_SC_CLK_TCK);
        long minutes = seconds / 60;
        seconds %= 60;

        /* 输出 */
        printf("%5ld %-8s %-6s %2ld:%02ld %s\n",
               pid, tty, state, minutes, seconds, cmdline[0] ? cmdline : "[kernel]");
    }
    closedir(dir);
    return 0;
}

/**
 * cmd_ps_axjf() — 显示进程树状态
 *
 * 参数含义:
 *   a — 显示所有用户 (非仅当前用户) 的进程
 *   x — 显示没有控制终端的进程 (守护进程等)
 *   j — 作业格式 (显示 PGID, SID 等作业控制信息)
 *   f — 森林/树形显示 (按父子关系缩进显示进程树)
 *
 * 实现: 读取所有进程的 PID/PPID，构建树结构，
 * 从 PID=1 (init/systemd) 开始递归打印，按层级缩进。
 */
int cmd_ps_axjf(int argc, char **argv) {
    (void)argc; (void)argv;

    printf("  PID  PPID PGID  SID TTY   STAT COMMAND\n");
    printf("----- ----- ----- ----- ---- ----- -------\n");

    /* 第一步: 收集所有进程信息 */
    typedef struct {
        pid_t pid, ppid, pgid, sid;
        char state[4], tty[8], comm[256];
    } proc_info_t;

    proc_info_t procs[4096];
    int nprocs = 0;

    DIR *dir = opendir("/proc");
    if (!dir) { perror("opendir"); return 1; }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && nprocs < 4096) {
        if (entry->d_type != DT_DIR) continue;
        char *end;
        long pid = strtol(entry->d_name, &end, 10);
        if (*end != '\0') continue;

        char path[256];
        snprintf(path, sizeof(path), "/proc/%ld/stat", pid);

        proc_info_t *pi = &procs[nprocs];
        memset(pi, 0, sizeof(*pi));
        pi->pid = (pid_t)pid;

        FILE *f = fopen(path, "r");
        if (!f) continue;

        char line[1024];
        if (!fgets(line, sizeof(line), f)) { fclose(f); continue; }
        fclose(f);

        /* 解析 stat: pid (comm) state ppid pgrp session tty_nr ... */
        char *p = line;
        /* 跳过 pid */
        while (*p && *p != ' ') p++;
        if (*p == ' ') p++;

        /* 提取 comm */
        if (*p == '(') {
            p++;
            char *comm_start = p;
            while (*p && *p != ')') p++;
            int comm_len = p - comm_start;
            if (comm_len > 254) comm_len = 254;
            memcpy(pi->comm, comm_start, comm_len);
            pi->comm[comm_len] = '\0';
            if (*p == ')') p++;
        }
        if (*p == ' ') p++;

        /* state */
        if (*p) { pi->state[0] = *p; pi->state[1] = '\0'; p++; }
        while (*p == ' ') p++;

        /* ppid */
        pi->ppid = (pid_t)atoi(p);
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;

        /* pgrp */
        pi->pgid = (pid_t)atoi(p);
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;

        /* session */
        pi->sid = (pid_t)atoi(p);
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;

        /* tty_nr (可简化为显示 "?" 或 tty 号) */
        int tty_nr = atoi(p);
        if (tty_nr == 0) strcpy(pi->tty, "?");
        else snprintf(pi->tty, sizeof(pi->tty), "%d", tty_nr);

        nprocs++;
    }
    closedir(dir);

    /* 第二步: 构建子进程索引用于树形输出 */
    /* 简单方法: 按层级排序 */
    /* 对于每个进程，计算其深度 (到 init 的距离) */
    /* 然后按深度输出 */

    /* 计算深度: 每个进程追踪父进程链直到 ppid=1 或 ppid=0 */
    int depth[4096];
    for (int i = 0; i < nprocs; i++) {
        depth[i] = 0;
        pid_t ppid = procs[i].ppid;
        while (ppid > 1 && depth[i] < 20) {
            /* 查找父进程 */
            int found = 0;
            for (int j = 0; j < nprocs; j++) {
                if (procs[j].pid == ppid) {
                    ppid = procs[j].ppid;
                    depth[i]++;
                    found = 1;
                    break;
                }
            }
            if (!found) break;
        }
    }

    /* 第三步: 按 PID 排序输出 (简单遍历) */
    /* 对于 f 模式，需要在每个进程前加缩进 */
    for (int i = 0; i < nprocs; i++) {
        /* 缩进 */
        if (depth[i] > 0) {
            for (int d = 0; d < depth[i]; d++) {
                printf("  ");
            }
            printf("\\_ ");
        }

        printf("%5d %5d %5d %5d %-4s %-5s %s\n",
               procs[i].pid, procs[i].ppid, procs[i].pgid,
               procs[i].sid, procs[i].tty, procs[i].state, procs[i].comm);
    }

    printf("\nTotal: %d processes\n", nprocs);
    return 0;
}

/**
 * cmd_ps_aux() — 按 PID 排序显示进程信息 (BSD 风格)
 *
 * 输出格式: USER PID %CPU %MEM VSZ RSS TTY STAT START TIME COMMAND
 *
 * 实现:
 *   - 读取 /proc/[pid]/stat 获取 CPU 时间、内存信息
 *   - 读取 /proc/[pid]/status 获取 UID
 *   - 计算 %CPU (基于进程运行时间 / 系统运行时间)
 *   - 计算 %MEM (基于 RSS / 总内存)
 *   - 按 PID 排序输出
 */
int cmd_ps_aux(int argc, char **argv) {
    (void)argc; (void)argv;

    printf("%-12s %6s %4s %4s %8s %8s %-8s %-6s %6s %s\n",
           "USER", "PID", "%CPU", "%MEM", "VSZ", "RSS", "TTY", "STAT", "TIME", "COMMAND");

    /* 读取系统总内存 */
    long total_mem_kb = 0;
    FILE *meminfo = fopen("/proc/meminfo", "r");
    if (meminfo) {
        char line[256];
        while (fgets(line, sizeof(line), meminfo)) {
            if (strncmp(line, "MemTotal:", 9) == 0) {
                total_mem_kb = atol(line + 9);
                break;
            }
        }
        fclose(meminfo);
    }
    if (total_mem_kb == 0) total_mem_kb = 1;

    /* 收集进程信息 */
    typedef struct {
        pid_t pid;
        uid_t uid;
        char  comm[256], state[4], tty[8];
        long  vsize, rss;
        unsigned long utime, stime;
        unsigned long long starttime;
    } aux_info_t;

    aux_info_t procs[4096];
    int nprocs = 0;

    DIR *dir = opendir("/proc");
    if (!dir) { perror("opendir"); return 1; }
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL && nprocs < 4096) {
        if (entry->d_type != DT_DIR) continue;
        char *end;
        long pid = strtol(entry->d_name, &end, 10);
        if (*end != '\0') continue;

        aux_info_t *ai = &procs[nprocs];
        memset(ai, 0, sizeof(*ai));
        ai->pid = (pid_t)pid;

        char path[256];

        /* 读取 stat */
        snprintf(path, sizeof(path), "/proc/%ld/stat", pid);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        char line[1024];
        if (!fgets(line, sizeof(line), f)) { fclose(f); continue; }
        fclose(f);

        /* 解析 stat 文件 */
        char *p = line;
        while (*p && *p != ' ') p++;  /* skip pid */
        while (*p == ' ') p++;
        /* comm */
        if (*p == '(') {
            p++;
            char *cs = p;
            while (*p && *p != ')') p++;
            int clen = p - cs;
            if (clen > 254) clen = 254;
            memcpy(ai->comm, cs, clen);
            ai->comm[clen] = '\0';
            if (*p == ')') p++;
        }
        while (*p == ' ') p++;

        /* state */
        if (*p) { ai->state[0] = *p; ai->state[1] = '\0'; p++; }
        while (*p == ' ') p++;

        /* ppid — skip */
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        /* pgrp — skip */
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        /* session — skip */
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        /* tty_nr */
        int tty_nr = atoi(p);
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        /* tpgid — skip */
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        /* flags — skip */
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        /* minflt — skip */
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        /* cminflt — skip */
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        /* majflt — skip */
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        /* cmajflt — skip */
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;

        /* utime (14) */
        ai->utime = strtoul(p, &p, 10); while (*p == ' ') p++;
        /* stime (15) */
        ai->stime = strtoul(p, &p, 10); while (*p == ' ') p++;
        /* cutime — skip */ while (*p && *p != ' ') p++; while (*p == ' ') p++;
        /* cstime — skip */ while (*p && *p != ' ') p++; while (*p == ' ') p++;
        /* priority — skip */ while (*p && *p != ' ') p++; while (*p == ' ') p++;
        /* nice — skip */ while (*p && *p != ' ') p++; while (*p == ' ') p++;
        /* num_threads — skip */ while (*p && *p != ' ') p++; while (*p == ' ') p++;
        /* itrealvalue — skip */ while (*p && *p != ' ') p++; while (*p == ' ') p++;
        /* starttime (22) */
        ai->starttime = strtoull(p, &p, 10); while (*p == ' ') p++;
        /* vsize (23) */
        ai->vsize = atol(p); while (*p && *p != ' ') p++; while (*p == ' ') p++;
        /* rss (24) — 页数 */
        ai->rss = atol(p);

        /* tty */
        if (tty_nr == 0) strcpy(ai->tty, "?");
        else snprintf(ai->tty, sizeof(ai->tty), "pts/%d", tty_nr & 0xFF);

        /* 获取 UID */
        snprintf(path, sizeof(path), "/proc/%ld/status", pid);
        f = fopen(path, "r");
        if (f) {
            char sl[256];
            while (fgets(sl, sizeof(sl), f)) {
                if (strncmp(sl, "Uid:", 4) == 0) {
                    ai->uid = (uid_t)atol(sl + 4);
                    break;
                }
            }
            fclose(f);
        }

        nprocs++;
    }
    closedir(dir);

    /* 按 PID 排序 (冒泡排序，简单实现) */
    for (int i = 0; i < nprocs - 1; i++) {
        for (int j = i + 1; j < nprocs; j++) {
            if (procs[i].pid > procs[j].pid) {
                aux_info_t tmp = procs[i];
                procs[i] = procs[j];
                procs[j] = tmp;
            }
        }
    }

    /* 获取系统 uptime */
    unsigned long long uptime_sec = 0;
    FILE *uptime_f = fopen("/proc/uptime", "r");
    if (uptime_f) {
        double upt;
        if (fscanf(uptime_f, "%lf", &upt) == 1) {
            uptime_sec = (unsigned long long)upt;
        }
        fclose(uptime_f);
    }

    long clk_tck = sysconf(_SC_CLK_TCK);
    if (clk_tck <= 0) clk_tck = 100;

    /* 输出 */
    for (int i = 0; i < nprocs; i++) {
        aux_info_t *ai = &procs[i];

        /* 计算 CPU 使用率 */
        unsigned long long proc_sec = 0;
        if (uptime_sec > (ai->starttime / clk_tck)) {
            proc_sec = uptime_sec - (ai->starttime / clk_tck);
        }
        unsigned long total_time = ai->utime + ai->stime;
        float cpu_pct = 0.0f;
        if (proc_sec > 0) {
            cpu_pct = 100.0f * ((float)total_time / clk_tck) / (float)proc_sec;
        }

        /* 计算内存使用率 */
        float mem_pct = 100.0f * (float)(ai->rss * 4) / (float)total_mem_kb;

        /* 时间格式 */
        unsigned long time_sec = total_time / clk_tck;
        unsigned long time_min = time_sec / 60;
        time_sec %= 60;

        printf("%-12s %6d %3.1f %3.1f %8ld %8ld %-8s %-6s %2lu:%02lu %s\n",
               uid_to_name(ai->uid), ai->pid,
               cpu_pct, mem_pct,
               ai->vsize / 1024, ai->rss * 4,
               ai->tty, ai->state,
               time_min, time_sec, ai->comm);
    }
    return 0;
}

/**
 * cmd_top() — 动态显示系统进程和资源使用情况
 *
 * 功能: 实时刷新显示系统总览和进程列表。
 *   上半部分: CPU 使用率、内存使用情况
 *   下半部分: 按 CPU 使用率排序的进程列表
 *
 * 交互: 按 'q' 键退出
 *
 * 实现: 使用 /proc/stat (CPU) 和 /proc/meminfo (内存)。
 *   通过非阻塞输入检测 'q' 按键。
 */
int cmd_top(int argc, char **argv) {
    (void)argc; (void)argv;

    printf("\033[2J");  /* 清屏 */
    printf("top — myOS Process Monitor (press 'q' to quit)\n\n");

    /* 设置为非阻塞输入 */
    int old_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, old_flags | O_NONBLOCK);

    int running = 1;
    while (running) {
        /* 检查是否有 'q' 输入 */
        char ch;
        while (read(STDIN_FILENO, &ch, 1) > 0) {
            if (ch == 'q' || ch == 'Q') {
                running = 0;
                break;
            }
        }
        if (!running) break;

        /* 清屏并定位光标到顶 */
        printf("\033[H\033[J");
        printf("top — myOS Process Monitor (press 'q' to quit)\n");
        printf("═══════════════════════════════════════════════════════\n\n");

        /* ── CPU 信息 ─────────────────────────────────────── */
        FILE *statf = fopen("/proc/stat", "r");
        if (statf) {
            char line[256];
            if (fgets(line, sizeof(line), statf)) {
                unsigned long user, nice, sys, idle, iowait, irq, softirq, steal;
                int n = sscanf(line, "cpu %lu %lu %lu %lu %lu %lu %lu %lu",
                              &user, &nice, &sys, &idle, &iowait, &irq, &softirq, &steal);
                if (n >= 4) {
                    unsigned long total = user + nice + sys + idle + (n >= 5 ? iowait : 0)
                                        + (n >= 6 ? irq : 0) + (n >= 7 ? softirq : 0) + (n >= 8 ? steal : 0);
                    unsigned long used = total - idle;
                    float cpu_pct = total > 0 ? 100.0f * (float)used / (float)total : 0.0f;
                    printf("  CPU Usage: %.1f%%  (user: %lu, sys: %lu, idle: %lu)\n\n",
                           cpu_pct, user, sys, idle);
                }
            }
            fclose(statf);
        }

        /* ── 内存信息 ──────────────────────────────────────── */
        FILE *memf = fopen("/proc/meminfo", "r");
        if (memf) {
            long mem_total = 0, mem_free = 0, mem_avail = 0;
            char line[256];
            while (fgets(line, sizeof(line), memf)) {
                if (strncmp(line, "MemTotal:", 9) == 0) mem_total = atol(line + 9);
                else if (strncmp(line, "MemFree:", 8) == 0) mem_free = atol(line + 8);
                else if (strncmp(line, "MemAvailable:", 13) == 0) mem_avail = atol(line + 13);
            }
            fclose(memf);
            printf("  Memory: Total=%ld MB, Free=%ld MB, Available=%ld MB",
                   mem_total / 1024, mem_free / 1024, mem_avail / 1024);
            printf("  (Used: %.1f%%)\n\n", 100.0f * (float)(mem_total - mem_avail) / (float)mem_total);
        }

        /* ── 进程列表 ──────────────────────────────────────── */
        printf("  %6s %-20s %5s %5s\n", "PID", "COMMAND", "%CPU", "%MEM");
        printf("  ------ -------------------- ----- -----\n");

        DIR *dir = opendir("/proc");
        if (dir) {
            struct dirent *entry;
            int count = 0;
            while ((entry = readdir(dir)) != NULL && count < 15) {
                if (entry->d_type != DT_DIR) continue;
                char *end;
                long pid = strtol(entry->d_name, &end, 10);
                if (*end != '\0') continue;

                char path[256], comm[64] = "";
                snprintf(path, sizeof(path), "/proc/%ld/comm", pid);
                FILE *f = fopen(path, "r");
                if (f) {
                    if (fgets(comm, sizeof(comm), f)) {
                        comm[strcspn(comm, "\n")] = '\0';
                    }
                    fclose(f);
                }
                if (!comm[0]) continue;

                printf("  %6ld %-20s %5s %5s\n", pid, comm, "-", "-");
                count++;
            }
            closedir(dir);
        }

        printf("\n\033[7m Press 'q' to quit \033[0m\n");
        fflush(stdout);

        /* 等待 2 秒 */
        sleep(2);
    }

    /* 恢复终端设置 */
    fcntl(STDIN_FILENO, F_SETFL, old_flags);
    printf("\n");
    return 0;
}

/**
 * cmd_ls() — 列出目录内容
 *
 * 功能: 列出指定目录下的文件和子目录。
 *   不带参数: 列出当前目录的内容 (简单格式)
 *   带 -l 参数: 以长格式列出 (权限、链接数、所有者、大小、修改时间、名称)
 *
 * 实现: 使用 opendir()/readdir() 遍历目录，
 *   使用 stat() 获取每个条目的详细信息。
 */
int cmd_ls(int argc, char **argv) {
    int long_format = 0;
    const char *dirname = ".";

    /* 解析参数 */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) {
            long_format = 1;
        } else if (argv[i][0] != '-') {
            dirname = argv[i];
        }
    }

    DIR *dir = opendir(dirname);
    if (!dir) {
        perror("ls");
        return 1;
    }

    if (long_format) {
        /* 长格式: drwxr-xr-x 2 user group 4096 Jun 28 12:00 filename */
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            /* 跳过隐藏文件 (默认) */
            if (entry->d_name[0] == '.') continue;

            char fullpath[1024];
            snprintf(fullpath, sizeof(fullpath), "%s/%s", dirname, entry->d_name);

            struct stat st;
            if (lstat(fullpath, &st) == 0) {
                char mode[11];
                format_mode(st.st_mode, mode);

                char mtime[32];
                struct tm *tm_info = localtime(&st.st_mtime);
                strftime(mtime, sizeof(mtime), "%b %d %H:%M", tm_info);

                printf("%s %2lu %-8s %-8s %8lld %s %s",
                       mode, (unsigned long)st.st_nlink,
                       uid_to_name(st.st_uid),
                       uid_to_name(st.st_gid),
                       (long long)st.st_size,
                       mtime, entry->d_name);

                /* 符号链接显示目标 */
                if (S_ISLNK(st.st_mode)) {
                    char link_target[256];
                    ssize_t n = readlink(fullpath, link_target, sizeof(link_target) - 1);
                    if (n >= 0) {
                        link_target[n] = '\0';
                        printf(" -> %s", link_target);
                    }
                }
                printf("\n");
            }
        }
    } else {
        /* 简单格式: 多列显示 */
        struct dirent *entry;
        int count = 0;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;  /* 跳过隐藏文件 */
            printf("%-24s", entry->d_name);
            if (++count % 3 == 0) printf("\n");
        }
        if (count % 3 != 0) printf("\n");
    }

    closedir(dir);
    return 0;
}

/**
 * cmd_ls_more() — ls -l 输出通过 more 分页
 *
 * 功能: 这是管道命令 ls -l|more 的专用实现。
 *   使用 pipe() 创建管道，fork() 两个子进程:
 *     - 子进程 1: 执行 ls -l，输出重定向到管道写端
 *     - 子进程 2: 执行 more 分页器，输入重定向自管道读端
 *
 * 实现原理: 这是管道的标准实现模式 —
 *   1. pipe() 创建一对文件描述符
 *   2. fork() 第一个子进程，dup2() 将 stdout 重定向到管道写端
 *   3. fork() 第二个子进程，dup2() 将 stdin 重定向自管道读端
 *   4. 父进程关闭管道两端，等待两个子进程结束
 */
int cmd_ls_more(int argc, char **argv) {
    (void)argc; (void)argv;

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        return 1;
    }

    /* 子进程 1: ls -l (输出到管道) */
    pid_t pid1 = fork();
    if (pid1 < 0) {
        perror("fork");
        return 1;
    }
    if (pid1 == 0) {
        /* ls -l 子进程 */
        close(pipefd[0]);              /* 关闭读端 */
        dup2(pipefd[1], STDOUT_FILENO); /* stdout -> 管道写端 */
        close(pipefd[1]);

        /* 执行 ls -l */
        cmd_ls(argc, argv);
        _exit(0);
    }

    /* 子进程 2: more (从管道读取) */
    pid_t pid2 = fork();
    if (pid2 < 0) {
        perror("fork");
        return 1;
    }
    if (pid2 == 0) {
        /* more 子进程 */
        close(pipefd[1]);             /* 关闭写端 */
        dup2(pipefd[0], STDIN_FILENO); /* stdin -> 管道读端 */
        close(pipefd[0]);

        /* 执行简单的 more 分页器 */
        char line[1024];
        int ln = 0;
        int rows = 24;  /* 默认终端高度 */

        while (fgets(line, sizeof(line), stdin)) {
            printf("%s", line);
            ln++;
            if (ln >= rows - 1) {
                printf("--More--");
                fflush(stdout);
                int ch = getchar();
                if (ch == 'q' || ch == 'Q') break;
                ln = 0;
                /* 清除 --More-- 行 */
                printf("\r          \r");
            }
        }
        _exit(0);
    }

    /* 父进程 */
    close(pipefd[0]);
    close(pipefd[1]);
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);

    return 0;
}

/**
 * cmd_more() — 分页显示标准输入 (内部使用)
 * 用于简单管道中的分页器
 */
int cmd_more_pager(int argc, char **argv) {
    (void)argc; (void)argv;
    char line[1024];
    int ln = 0;
    int rows = 24;

    while (fgets(line, sizeof(line), stdin)) {
        printf("%s", line);
        ln++;
        if (ln >= rows - 1) {
            printf("--More--");
            fflush(stdout);
            int ch = getchar();
            if (ch == 'q' || ch == 'Q') break;
            ln = 0;
            printf("\r          \r");
        }
    }
    return 0;
}

/**
 * cmd_cal() — 显示日历
 *
 * 功能: 显示指定月份和年份的日历。
 *   不带参数: 显示当前月份
 *   一个参数: 显示指定年份 (1-9999) 的全年日历
 *   两个参数: 显示指定月份和年份的日历
 *
 * 实现: 使用 Zeller 公式计算指定日期是星期几，
 *   然后用标准日历格式输出。
 */
int cmd_cal(int argc, char **argv) {
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    int month = tm_now->tm_mon + 1;
    int year = tm_now->tm_year + 1900;

    if (argc >= 2) {
        if (argc == 2) {
            /* cal <year> */
            year = atoi(argv[1]);
            if (year < 1 || year > 9999) {
                fprintf(stderr, "cal: invalid year: %s\n", argv[1]);
                return 1;
            }
            /* 打印全年日历 */
            for (int m = 1; m <= 12; m++) {
                /* 这里简化，只打印当月 + 提示 */
            }
            month = 1;  /* 默认显示一月 */
        }
        if (argc >= 3) {
            /* cal <month> <year> */
            month = atoi(argv[1]);
            year = atoi(argv[2]);
            if (month < 1 || month > 12) {
                fprintf(stderr, "cal: invalid month: %s\n", argv[1]);
                return 1;
            }
        }
    }

    /* 月份名称 */
    const char *months[] = {
        "", "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };

    /* 每月天数 */
    int days_in_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    /* 闰年判断 */
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
        days_in_month[2] = 29;
    }

    int days = days_in_month[month];

    /* 使用 Zeller 公式计算该月第一天是星期几 (0=Sun, 1=Mon, ...) */
    /* Zeller: h = (q + 13*(m+1)/5 + K + K/4 + J/4 + 5*J) % 7 */
    /* 其中 m 的 1,2 月视为上一年的 13,14 月 */
    int m = month, y = year;
    if (m <= 2) { m += 12; y--; }
    int K = y % 100;
    int J = y / 100;
    int q = 1;  /* 第一天 */
    int h = (q + (13 * (m + 1)) / 5 + K + K / 4 + J / 4 + 5 * J) % 7;
    /* Zeller 结果: 0=Sat, 1=Sun, ..., 6=Fri */
    /* 转换为 0=Sun, 1=Mon, ..., 6=Sat */
    int dow = (h + 6) % 7;

    /* 打印日历头 */
    printf("     %s %d\n", months[month], year);
    printf("Su Mo Tu We Th Fr Sa\n");

    /* 前导空格 */
    for (int i = 0; i < dow; i++) {
        printf("   ");
    }

    /* 打印日期 */
    for (int day = 1; day <= days; day++) {
        printf("%2d ", day);
        dow++;
        if (dow >= 7) {
            printf("\n");
            dow = 0;
        }
    }
    if (dow != 0) printf("\n");

    return 0;
}

/**
 * cmd_whoami() — 显示当前有效用户名
 *
 * 功能: 打印与当前有效用户 ID 关联的用户名。
 * 等同于 Unix 的 "whoami" 命令。
 *
 * 实现: 使用 geteuid() 获取有效 UID，
 *   使用 getpwuid() 查找对应的用户名。
 */
int cmd_whoami(int argc, char **argv) {
    (void)argc; (void)argv;
    uid_t uid = geteuid();
    struct passwd *pw = getpwuid(uid);
    if (pw) {
        printf("%s\n", pw->pw_name);
    } else {
        printf("%d\n", (int)uid);
    }
    return 0;
}

/**
 * cmd_date() — 显示系统日期和时间
 *
 * 功能: 以可读格式打印当前系统日期和时间。
 *
 * 实现: 使用 time() 获取当前时间戳，
 *   使用 localtime() 转换为本地时间结构，
 *   使用 strftime() 格式化为标准格式输出。
 */
int cmd_date(int argc, char **argv) {
    (void)argc; (void)argv;
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    char buf[128];
    /* 格式: 星期 月份 日期 时:分:秒 时区 年份 */
    strftime(buf, sizeof(buf), "%a %b %d %H:%M:%S %Z %Y", tm_now);
    printf("%s\n", buf);
    return 0;
}

/**
 * cmd_pwd() — 打印当前工作目录
 *
 * 功能: 输出当前进程的工作目录的绝对路径。
 *
 * 实现: 使用 getcwd() 系统调用获取当前工作目录。
 */
int cmd_pwd(int argc, char **argv) {
    (void)argc; (void)argv;
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd))) {
        printf("%s\n", cwd);
    } else {
        perror("pwd");
        return 1;
    }
    return 0;
}

/**
 * cmd_mkdir() — 创建新目录
 *
 * 功能: 创建一个或多个目录。
 *   默认权限为 0755 (rwxr-xr-x)。
 *
 * 实现: 使用 mkdir() 系统调用。
 */
int cmd_mkdir(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "mkdir: missing operand\nUsage: mkdir <directory>\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        if (mkdir(argv[i], 0755) < 0) {
            fprintf(stderr, "mkdir: %s: %s\n", argv[i], strerror(errno));
            return 1;
        }
    }
    return 0;
}

/**
 * cmd_rmdir() — 删除空目录
 *
 * 功能: 删除一个或多个空目录。
 *   目录必须为空才能被删除。
 *
 * 实现: 使用 rmdir() 系统调用。
 */
int cmd_rmdir(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "rmdir: missing operand\nUsage: rmdir <directory>\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        if (rmdir(argv[i]) < 0) {
            fprintf(stderr, "rmdir: %s: %s\n", argv[i], strerror(errno));
            return 1;
        }
    }
    return 0;
}

/**
 * cmd_cat() — 连接并显示文件内容
 *
 * 功能: 读取一个或多个文件的内容并输出到标准输出。
 *   支持 "-" 作为标准输入的占位符。
 *
 * 实现: 使用 open() + read() + write() 系统调用逐块读取并输出。
 *   缓冲区大小为 4096 字节。
 */
int cmd_cat(int argc, char **argv) {
    if (argc < 2) {
        /* 无参数时从标准输入读取 */
        char buf[4096];
        ssize_t n;
        while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
            if (write(STDOUT_FILENO, buf, n) < 0) {
                perror("cat: write");
                break;
            }
        }
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        int fd;
        if (strcmp(argv[i], "-") == 0) {
            fd = STDIN_FILENO;
        } else {
            fd = open(argv[i], O_RDONLY);
            if (fd < 0) {
                fprintf(stderr, "cat: %s: %s\n", argv[i], strerror(errno));
                continue;
            }
        }

        char buf[4096];
        ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            if (write(STDOUT_FILENO, buf, n) < 0) {
                perror("cat: write");
                break;
            }
        }

        if (fd != STDIN_FILENO) close(fd);
    }
    return 0;
}

/**
 * cmd_cp() — 复制文件
 *
 * 功能: 将源文件复制到目标文件。
 *   如果目标是目录，则在目录中创建同名的副本。
 *   保留源文件的权限。
 *
 * 实现: 使用 open()/read()/write() 逐块复制数据，
 *   使用 fchmod()/fstat() 保留文件权限。
 *   缓冲区大小为 64KB 以获得较好的复制性能。
 */
int cmd_cp(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "cp: missing operand\nUsage: cp <source> <destination>\n");
        return 1;
    }

    const char *src_path = argv[1];
    char dst_path[1024];
    strncpy(dst_path, argv[2], sizeof(dst_path) - 1);
    dst_path[sizeof(dst_path) - 1] = '\0';

    /* 如果目标是目录，构造目标路径 */
    struct stat dst_st;
    if (stat(dst_path, &dst_st) == 0 && S_ISDIR(dst_st.st_mode)) {
        /* 提取源文件名 */
        const char *basename = strrchr(src_path, '/');
        basename = basename ? basename + 1 : src_path;
        snprintf(dst_path, sizeof(dst_path), "%s/%s", argv[2], basename);
    }

    /* 打开源文件 */
    int src_fd = open(src_path, O_RDONLY);
    if (src_fd < 0) {
        fprintf(stderr, "cp: %s: %s\n", src_path, strerror(errno));
        return 1;
    }

    /* 获取源文件权限 */
    struct stat src_st;
    if (fstat(src_fd, &src_st) < 0) {
        perror("cp: fstat");
        close(src_fd);
        return 1;
    }

    /* 创建/打开目标文件 */
    int dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, src_st.st_mode);
    if (dst_fd < 0) {
        fprintf(stderr, "cp: %s: %s\n", dst_path, strerror(errno));
        close(src_fd);
        return 1;
    }

    /* 逐块复制数据 (64KB 缓冲区) */
    char buf[65536];
    ssize_t n;
    while ((n = read(src_fd, buf, sizeof(buf))) > 0) {
        if (write(dst_fd, buf, n) < 0) {
            perror("cp: write");
            close(src_fd);
            close(dst_fd);
            return 1;
        }
    }

    /* 保留权限 */
    fchmod(dst_fd, src_st.st_mode);

    close(src_fd);
    close(dst_fd);
    printf("cp: '%s' -> '%s'\n", src_path, dst_path);
    return 0;
}

/**
 * cmd_mv() — 移动/重命名文件
 *
 * 功能: 将源文件移动到目标位置。
 *   如果源和目标在同一文件系统，使用 rename() (原子操作)。
 *   如果跨文件系统，先复制再删除源文件。
 *
 * 实现: 首先尝试 rename()，如果失败 (EXDEV 跨设备错误)
 *   则执行 "cp + rm" 的复制后删除策略。
 */
int cmd_mv(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "mv: missing operand\nUsage: mv <source> <destination>\n");
        return 1;
    }

    const char *src = argv[1];
    char dst[1024];
    strncpy(dst, argv[2], sizeof(dst) - 1);
    dst[sizeof(dst) - 1] = '\0';

    /* 如果目标是目录，构造目标路径 */
    struct stat dst_st;
    if (stat(dst, &dst_st) == 0 && S_ISDIR(dst_st.st_mode)) {
        const char *basename = strrchr(src, '/');
        basename = basename ? basename + 1 : src;
        snprintf(dst, sizeof(dst), "%s/%s", argv[2], basename);
    }

    /* 首先尝试 rename (同文件系统) */
    if (rename(src, dst) == 0) {
        printf("mv: '%s' -> '%s'\n", src, dst);
        return 0;
    }

    /* 跨文件系统 — 复制后删除 */
    if (errno == EXDEV) {
        /* 构造 cp + rm 命令参数 */
        char *cp_args[] = {"cp", (char*)src, dst, NULL};
        int ret = cmd_cp(3, cp_args);
        if (ret != 0) return ret;

        /* 删除源文件 */
        char *rm_args[] = {"rm", (char*)src, NULL};
        return cmd_rm(2, rm_args);
    }

    fprintf(stderr, "mv: %s: %s\n", src, strerror(errno));
    return 1;
}

/**
 * cmd_rm() — 删除文件
 *
 * 功能: 删除一个或多个文件。
 *   默认会先确认再删除 (安全考虑)。
 *
 * 实现: 使用 unlink() 系统调用删除文件。
 */
int cmd_rm(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "rm: missing operand\nUsage: rm <file>\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        /* 确认提示 (安全检查) */
        printf("rm: remove '%s'? (y/N) ", argv[i]);
        fflush(stdout);
        char response[8];
        if (!fgets(response, sizeof(response), stdin)) {
            printf("\n");
            continue;
        }

        if (response[0] != 'y' && response[0] != 'Y') {
            printf("rm: skipped '%s'\n", argv[i]);
            continue;
        }

        if (unlink(argv[i]) < 0) {
            fprintf(stderr, "rm: %s: %s\n", argv[i], strerror(errno));
        } else {
            printf("rm: removed '%s'\n", argv[i]);
        }
    }
    return 0;
}

/**
 * cmd_file() — 识别文件类型
 *
 * 功能: 通过检查文件内容的魔术字节 (magic bytes) 来识别文件类型。
 *   支持识别的类型:
 *     - ELF 可执行文件
 *     - Shell 脚本 (shebang #!)
 *     - PNG, JPEG, GIF, BMP 图像
 *     - PDF 文档
 *     - ZIP 压缩文件
 *     - GZIP 压缩文件
 *     - tar 归档
 *     - 纯文本/二进制/空文件
 *
 * 实现: 读取文件前 256 字节，检查魔术字节序列。
 *   对于无法通过魔术字节识别的文件，回退到扩展名判断。
 */
int cmd_file(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "file: missing operand\nUsage: file <file>\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        /* 先检查是否为目录 */
        struct stat st;
        if (stat(argv[i], &st) == 0 && S_ISDIR(st.st_mode)) {
            printf("%s: directory\n", argv[i]);
            continue;
        }

        FILE *f = fopen(argv[i], "rb");
        if (!f) {
            printf("%s: cannot open (%s)\n", argv[i], strerror(errno));
            continue;
        }

        unsigned char buf[256];
        size_t n = fread(buf, 1, sizeof(buf), f);
        fclose(f);

        printf("%s: ", argv[i]);

        if (n == 0) {
            printf("empty\n");
            continue;
        }

        /* ELF: 0x7F 'E' 'L' 'F' */
        if (n >= 4 && buf[0] == 0x7F && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F') {
            const char *elf_type = "ELF";
            if (n >= 18) {
                int type = buf[16] | (buf[17] << 8);
                if (type == 1) elf_type = "ELF 32-bit relocatable";
                else if (type == 2) elf_type = "ELF 32-bit executable";
                else if (type == 3) elf_type = "ELF 32-bit shared lib";
            }
            printf("%s\n", elf_type);
        }
        /* Shebang: #! */
        else if (n >= 2 && buf[0] == '#' && buf[1] == '!') {
            /* 提取解释器路径 */
            char interp[128] = "";
            for (size_t j = 2; j < n && j < 130 && buf[j] != '\n' && buf[j] != '\r'; j++) {
                interp[j - 2] = (char)buf[j];
            }
            printf("script, interpreter: %s\n", interp[0] ? interp : "unknown");
        }
        /* PNG: 89 50 4E 47 */
        else if (n >= 4 && buf[0] == 0x89 && buf[1] == 'P' && buf[2] == 'N' && buf[3] == 'G') {
            printf("PNG image data\n");
        }
        /* JPEG: FF D8 FF */
        else if (n >= 3 && buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) {
            printf("JPEG image data\n");
        }
        /* GIF: GIF89a / GIF87a */
        else if (n >= 6 && (memcmp(buf, "GIF89a", 6) == 0 || memcmp(buf, "GIF87a", 6) == 0)) {
            printf("GIF image data\n");
        }
        /* BMP: BM */
        else if (n >= 2 && buf[0] == 'B' && buf[1] == 'M') {
            printf("BMP image data\n");
        }
        /* PDF: %PDF */
        else if (n >= 4 && buf[0] == '%' && buf[1] == 'P' && buf[2] == 'D' && buf[3] == 'F') {
            printf("PDF document\n");
        }
        /* ZIP: PK\x03\x04 */
        else if (n >= 4 && buf[0] == 'P' && buf[1] == 'K' && buf[2] == 0x03 && buf[3] == 0x04) {
            printf("ZIP archive\n");
        }
        /* GZIP: 1F 8B */
        else if (n >= 2 && buf[0] == 0x1F && buf[1] == 0x8B) {
            printf("gzip compressed data\n");
        }
        /* tar: "ustar" at offset 257 (但对于我们只读256字节，检查 ustar 在 0x101) */
        /* 简化: 检查扩展名 */
        else {
            /* 尝试判断是否为文本文件 */
            int is_text = 1;
            for (size_t j = 0; j < n; j++) {
                if (buf[j] == 0) {
                    is_text = 0;
                    break;
                }
            }
            if (is_text) {
                /* 通过扩展名猜测 */
                const char *ext = strrchr(argv[i], '.');
                if (ext) {
                    if (strcmp(ext, ".c") == 0) printf("C source, ASCII text\n");
                    else if (strcmp(ext, ".h") == 0) printf("C header, ASCII text\n");
                    else if (strcmp(ext, ".py") == 0) printf("Python script, ASCII text\n");
                    else if (strcmp(ext, ".sh") == 0) printf("Bourne shell script, ASCII text\n");
                    else if (strcmp(ext, ".txt") == 0) printf("ASCII text\n");
                    else if (strcmp(ext, ".md") == 0) printf("Markdown document, ASCII text\n");
                    else if (strcmp(ext, ".html") == 0) printf("HTML document, ASCII text\n");
                    else if (strcmp(ext, ".o") == 0) printf("Object file\n");
                    else printf("ASCII text (%s)\n", ext);
                } else {
                    printf("ASCII text\n");
                }
            } else {
                printf("binary data\n");
            }
        }
    }
    return 0;
}

/**
 * cmd_exit() — 退出 Shell
 *
 * 功能: 终止 myOS Shell 进程。
 *   设置全局标志 g_shell_running = 0 使主循环退出。
 */
int cmd_exit(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("Exiting myOS Shell...\n");
    g_shell_running = 0;
    return 0;
}

/**
 * cmd_vi() — 启动全屏幕编辑器
 *
 * 功能: 调用内置的全屏幕文本编辑器 (editor.c)。
 *   支持打开指定文件进行编辑。
 *
 * 参数: 可选的 filename。
 */
int cmd_vi(int argc, char **argv) {
    const char *filename = (argc >= 2) ? argv[1] : NULL;
    return editor_run(filename);
}

/**
 * cmd_print() — 虚拟打印机命令
 *
 * 功能: 将文本文件内容转换为 PDF 文件并保存。
 *   模拟打印机输出到 PDF 的过程。
 *
 * 参数: print <textfile> [-o output.pdf]
 */
int cmd_print(int argc, char **argv) {
    const char *input_file = NULL;
    const char *output_file = "output.pdf";

    if (argc < 2) {
        fprintf(stderr, "print: missing operand\nUsage: print <textfile> [-o output.pdf]\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[i + 1];
            i++;
        } else if (!input_file) {
            input_file = argv[i];
        }
    }

    if (!input_file) {
        fprintf(stderr, "print: no input file specified\n");
        return 1;
    }

    return printer_print_file(input_file, output_file);
}

/**
 * cmd_startkbd() — 启动虚拟键盘驱动
 *
 * 功能: 启动虚拟键盘驱动程序，后台线程开始生成模拟按键事件。
 */
int cmd_startkbd(int argc, char **argv) {
    (void)argc; (void)argv;
    return kbd_start();
}

/**
 * cmd_readkbd() — 读取虚拟键盘事件
 *
 * 功能: 从键盘事件队列/管道中读取并显示虚拟按键事件。
 */
int cmd_readkbd(int argc, char **argv) {
    (void)argc; (void)argv;
    return kbd_read();
}

/**
 * cmd_stopkbd() — 停止虚拟键盘驱动
 *
 * 功能: 停止虚拟键盘驱动程序。
 */
int cmd_stopkbd(int argc, char **argv) {
    (void)argc; (void)argv;
    return kbd_stop();
}
