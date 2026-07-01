/**
 * keyboard.c — Virtual Keyboard Driver Implementation for myOS Shell
 *
 * 实现虚拟键盘设备驱动，模拟键盘输入事件。
 *
 * 设计原理:
 *   真实键盘驱动在操作系统中通过中断处理程序接收硬件扫描码，
 *   将其转换为键码并放入输入缓冲区供上层读取。
 *
 *   本虚拟驱动模拟这一过程:
 *   - 使用后台线程 (模拟硬件中断) 定期生成"按键事件"
 *   - 通过命名管道 (FIFO) 传递事件 (模拟内核缓冲区)
 *   - 上层 Shell 通过 readkbd 命令读取事件 (模拟 read() 系统调用)
 *
 * 模拟的按键类型:
 *   - 普通字符: a-z, A-Z, 0-9
 *   - 功能键: <F1>-<F12>, <ENTER>, <ESC>, <TAB>
 *   - 控制键: <CTRL+C>, <CTRL+D>, <BACKSPACE>
 *   - 方向键: <UP>, <DOWN>, <LEFT>, <RIGHT>
 *
 * 技术实现:
 *   - pthread 创建后台线程
 *   - mkfifo() 创建命名管道 /tmp/myos_kbd_fifo
 *   - 线程每隔随机间隔 (1-5 秒) 写入一个按键事件
 *   - 使用互斥锁保护共享状态
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <time.h>
#include "keyboard.h"
#include "datastruct.h"

/* ── Constants ───────────────────────────────────────────────── */
#define KBD_FIFO_PATH  "/tmp/myos_kbd_fifo"  /* 键盘事件管道路径 */
#define KBD_MAX_EVENTS 64                     /* 最大队列事件数 */

/* ── Global State ────────────────────────────────────────────── */
static pthread_t    kbd_thread;        /* 后台线程 ID */
static int          kbd_running = 0;   /* 驱动运行标志 */
static int          kbd_fifo_fd = -1;  /* FIFO 写端文件描述符 */
static pthread_mutex_t kbd_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Simulated Key Events ────────────────────────────────────── */

/* 模拟按键事件表 — 虚拟键盘可以生成的事件类型 */
static const char *simulated_keys[] = {
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j",
    "k", "l", "m", "n", "o", "p", "q", "r", "s", "t",
    "u", "v", "w", "x", "y", "z",
    "A", "B", "C", "D", "E",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "<ENTER>", "<ESC>", "<TAB>", "<SPACE>",
    "<F1>", "<F2>", "<F3>", "<F4>",
    "<UP>", "<DOWN>", "<LEFT>", "<RIGHT>",
    "<BACKSPACE>", "<DELETE>",
    "<CTRL+C>", "<CTRL+D>",
    "hello", "world", "test",
    NULL
};

static int num_simulated_keys = 0;

/**
 * kbd_get_random_key() — 随机选择一个模拟按键
 * 返回: 按键事件字符串指针
 */
static const char* kbd_get_random_key(void) {
    if (num_simulated_keys == 0) {
        /* 首次调用时计数 */
        for (num_simulated_keys = 0;
             simulated_keys[num_simulated_keys] != NULL;
             num_simulated_keys++);
    }
    int idx = rand() % num_simulated_keys;
    return simulated_keys[idx];
}

/**
 * kbd_thread_func() — 虚拟键盘驱动线程主函数
 *
 * 循环执行:
 *   1. 随机睡眠 1-5 秒 (模拟按键间隔)
 *   2. 生成一个随机按键事件
 *   3. 将事件写入 FIFO 管道
 *   4. 打印日志信息
 *   5. 检查退出标志
 *
 * 线程安全: 使用互斥锁保护 kbd_running 标志。
 */
static void* kbd_thread_func(void *arg) {
    (void)arg;
    srand((unsigned int)time(NULL) ^ (unsigned int)pthread_self());

    printf("[kbd] Virtual keyboard driver started.\n");
    printf("[kbd] Events are written to %s\n", KBD_FIFO_PATH);
    printf("[kbd] Use 'readkbd' command to read events.\n");

    while (1) {
        /* 检查是否应该退出 */
        pthread_mutex_lock(&kbd_mutex);
        int running = kbd_running;
        pthread_mutex_unlock(&kbd_mutex);
        if (!running) break;

        /* 按需打开 FIFO 写端（阻塞直到读者连接） */
        if (kbd_fifo_fd < 0) {
            printf("[kbd] Waiting for reader on %s ...\n", KBD_FIFO_PATH);
            kbd_fifo_fd = open(KBD_FIFO_PATH, O_WRONLY);
            if (kbd_fifo_fd < 0) {
                /* 被停止信号中断或 FIFO 被删除 */
                usleep(500 * 1000);
                continue;
            }
            printf("[kbd] Reader connected, generating events.\n");
        }

        /* 随机睡眠 1-5 秒 */
        int sleep_ms = 1000 + (rand() % 4000);
        usleep(sleep_ms * 1000);

        /* 再次检查退出标志 */
        pthread_mutex_lock(&kbd_mutex);
        running = kbd_running;
        if (!running) { pthread_mutex_unlock(&kbd_mutex); break; }

        /* 生成随机按键事件 */
        const char *key = kbd_get_random_key();

        /* 将事件写入 FIFO */
        char event[64];
        time_t now = time(NULL);
        struct tm *tm_now = localtime(&now);
        char timestamp[16];
        strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_now);
        int len = snprintf(event, sizeof(event), "[%s] Key: %s\n", timestamp, key);

        ssize_t written = write(kbd_fifo_fd, event, len);
        if (written < 0) {
            /* 读者断开连接 — 关闭并等待下次连接 */
            close(kbd_fifo_fd);
            kbd_fifo_fd = -1;
        }
        pthread_mutex_unlock(&kbd_mutex);
    }

    /* 清理 */
    if (kbd_fifo_fd >= 0) {
        close(kbd_fifo_fd);
        kbd_fifo_fd = -1;
    }
    printf("[kbd] Virtual keyboard driver stopped.\n");
    return NULL;
}

/* ── Public API ─────────────────────────────────────────────── */

/**
 * kbd_start() — 启动虚拟键盘驱动
 *
 * 启动步骤:
 *   1. 检查驱动是否已在运行 (避免重复启动)
 *   2. 创建命名管道 (FIFO) — 如果已存在则复用
 *   3. 以非阻塞模式打开 FIFO 写端
 *   4. 创建后台线程开始生成事件
 *
 * 返回: 0 成功, -1 已运行或出错
 */
int kbd_start(void) {
    pthread_mutex_lock(&kbd_mutex);

    if (kbd_running) {
        printf("[kbd] Virtual keyboard driver is already running.\n");
        pthread_mutex_unlock(&kbd_mutex);
        return -1;
    }

    /* 创建命名管道 (FIFO) */
    /* 如果已存在则删除重建 */
    unlink(KBD_FIFO_PATH);
    if (mkfifo(KBD_FIFO_PATH, 0666) < 0) {
        fprintf(stderr, "[kbd] Failed to create FIFO: %s\n", strerror(errno));
        pthread_mutex_unlock(&kbd_mutex);
        return -1;
    }

    /* 启动后台线程（FIFO 写端由线程按需打开，避免 O_NONBLOCK + O_WRONLY 的 ENXIO） */
    kbd_fifo_fd = -1;
    kbd_running = 1;
    if (pthread_create(&kbd_thread, NULL, kbd_thread_func, NULL) != 0) {
        fprintf(stderr, "[kbd] Failed to create driver thread.\n");
        kbd_running = 0;
        unlink(KBD_FIFO_PATH);
        pthread_mutex_unlock(&kbd_mutex);
        return -1;
    }
    pthread_detach(kbd_thread);  /* 分离线程，退出时自动清理 */

    pthread_mutex_unlock(&kbd_mutex);

    printf("[kbd] Virtual keyboard driver started successfully.\n");
    return 0;
}

/**
 * kbd_read() — 读取虚拟键盘事件
 *
 * 从 FIFO 读取并显示所有待处理的按键事件。
 *
 * 读取原理: 模拟操作系统从键盘缓冲区读取数据。
 *   以非阻塞模式打开 FIFO 读端，读取并显示所有可用数据。
 *
 * 返回: 0 成功 (可能无数据), -1 驱动未运行或 FIFO 不存在
 */
int kbd_read(void) {
    pthread_mutex_lock(&kbd_mutex);
    int running = kbd_running;
    pthread_mutex_unlock(&kbd_mutex);

    if (!running) {
        printf("[kbd] Keyboard driver is not running. Use 'startkbd' first.\n");
        return -1;
    }

    /* 以非阻塞模式打开 FIFO 读端 */
    int read_fd = open(KBD_FIFO_PATH, O_RDONLY | O_NONBLOCK);
    if (read_fd < 0) {
        fprintf(stderr, "[kbd] Cannot read events: %s\n", strerror(errno));
        return -1;
    }

    printf("┌─ Virtual Keyboard Events ─────────────────────────────┐\n");

    char buf[4096];
    ssize_t n = read(read_fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        /* 逐行打印事件 */
        char *line = strtok(buf, "\n");
        while (line) {
            printf("│ %-54s │\n", line);
            line = strtok(NULL, "\n");
        }
    } else {
        printf("│ %-54s │\n", "No new events in buffer.");
    }

    printf("└──────────────────────────────────────────────────────┘\n");
    close(read_fd);
    return 0;
}

/**
 * kbd_stop() — 停止虚拟键盘驱动
 *
 * 停止步骤:
 *   1. 设置运行标志为 0 (通知线程退出)
 *   2. 关闭 FIFO 写端
 *   3. 删除命名管道文件
 *   4. 等待线程退出
 *
 * 返回: 0 成功, -1 未运行
 */
int kbd_stop(void) {
    pthread_mutex_lock(&kbd_mutex);

    if (!kbd_running) {
        printf("[kbd] Keyboard driver is not running.\n");
        pthread_mutex_unlock(&kbd_mutex);
        return -1;
    }

    /* 通知线程停止 */
    kbd_running = 0;

    /* 关闭并删除 FIFO */
    if (kbd_fifo_fd >= 0) {
        close(kbd_fifo_fd);
        kbd_fifo_fd = -1;
    }
    unlink(KBD_FIFO_PATH);

    pthread_mutex_unlock(&kbd_mutex);

    printf("[kbd] Keyboard driver stopped.\n");
    return 0;
}
