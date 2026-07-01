/**
 * editor.c — Full-Screen vi-like Editor for myOS Shell
 *
 * 实现类似 vi 的全屏幕文本编辑器，支持:
 *   - 三种模式: NORMAL (默认), INSERT (编辑), COMMAND (命令行)
 *   - 光标移动: h(左) j(下) k(上) l(右), 也支持方向键
 *   - 基本编辑: 插入文本, 删除字符/行, 退格
 *   - 文件操作: 打开, 保存 (:w), 保存退出 (:wq), 强制退出 (:q!)
 *   - 行号显示
 *   - 状态栏 (模式, 文件名, 修改标志, 光标位置)
 *   - 原始终端模式 (使用 termios)
 *
 * 设计要点:
 *   - 使用双向链表存储文件行 (便于插入和删除)
 *   - 使用 gap buffer 的简化版 (行级编辑)
 *   - 通过 termios 设置 raw mode 获取每个按键
 *   - ANSI escape sequences 控制光标和清屏
 *   - 支持终端窗口大小自适应
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include "editor.h"
#include <time.h>
#include "datastruct.h"

/* ── Editor State ────────────────────────────────────────────── */

/* 编辑器模式枚举 */
typedef enum {
    MODE_NORMAL,    /* 默认模式 — 按键作为命令 */
    MODE_INSERT,    /* 插入模式 — 输入文本 */
    MODE_COMMAND    /* 命令行模式 — :w, :q 等 */
} editor_mode_t;

/* 编辑器全局状态结构 */
typedef struct {
    /* 终端 */
    struct termios orig_termios;  /* 原始终端设置，用于恢复 */
    int screen_rows;              /* 终端行数 */
    int screen_cols;              /* 终端列数 */

    /* 文件 */
    line_node_t *lines;           /* 文件内容行链表 */
    int num_lines;                /* 总行数 */
    char filename[256];           /* 编辑的文件名 */
    int modified;                 /* 是否有未保存的修改 */

    /* 光标 */
    int cx, cy;                   /* 光标在屏幕上的位置 (列, 行) */
    int scroll_row;               /* 垂直滚动偏移 */

    /* 模式 */
    editor_mode_t mode;           /* 当前模式 */

    /* 命令行缓冲区 */
    char cmdline[256];            /* : 命令的输入缓冲区 */
    int cmdlen;                   /* 命令缓冲区长度 */

    /* 消息 */
    char status_msg[256];         /* 状态栏消息 */
    time_t status_msg_time;       /* 消息显示时间 */
} editor_t;

static editor_t E;  /* 全局编辑器状态 */

/* ── Terminal Control ────────────────────────────────────────── */

/**
 * die() — 紧急退出时的清理函数
 * 在发生致命错误时恢复终端并退出。
 */
static void die(const char *msg) {
    /* 清屏并恢复光标 */
    (void)write(STDOUT_FILENO, "\033[2J\033[H", 7);
    perror(msg);
    /* 尝试恢复终端 */
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios);
    exit(1);
}

/**
 * editor_enable_raw_mode() — 启用原始终端模式
 *
 * 关闭以下终端特性:
 *   - ECHO:   不回显输入字符
 *   - ICANON: 禁用规范模式 (行缓冲)，使输入逐字符可用
 *   - ISIG:   禁用信号字符 (Ctrl+C, Ctrl+Z)
 *   - IEXTEN: 禁用扩展字符 (Ctrl+V)
 *
 * 同时设置:
 *   - VMIN=0, VTIME=1: 非阻塞读取，1ms 超时
 *   - 禁用 \r\n 转换 (ICRNL, INLCR)
 *   - 禁用输出处理 (OPOST)
 */
static void editor_enable_raw_mode(void) {
    /* 保存原始设置 */
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) < 0) {
        die("tcgetattr");
    }

    struct termios raw = E.orig_termios;

    /* 输入标志: 禁用各种处理和转换 */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* 输出标志: 禁用输出处理 */
    raw.c_oflag &= ~(OPOST);
    /* 控制标志: 8位字符，禁用流控 */
    raw.c_cflag |= (CS8);
    /* 本地标志: 禁用回显、规范模式、信号字符 */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    /* 超时: 100ms 超时，每次读取尽可能多 */
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) {
        die("tcsetattr");
    }
}

/**
 * editor_disable_raw_mode() — 恢复终端到原始设置
 */
static void editor_disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios);
}

/**
 * editor_get_window_size() — 获取终端窗口大小
 * 使用 ioctl(TIOCGWINSZ) 获取行列数。
 * 如果失败，回退到 24x80。
 */
static void editor_get_window_size(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0 || ws.ws_col == 0) {
        /* 回退方法: 移动光标到右下角 */
        (void)write(STDOUT_FILENO, "\033[999C\033[999B", 12);
        /* 这里可以进一步通过 ANSI 查询，但简化处理 */
        E.screen_rows = 24;
        E.screen_cols = 80;
    } else {
        E.screen_cols = ws.ws_col;
        E.screen_rows = ws.ws_col > 0 ? ws.ws_row : 24;
    }
    /* 为状态栏和命令行保留 2 行 */
    if (E.screen_rows > 2) E.screen_rows -= 2;
}

/* ── Line Buffer Operations ─────────────────────────────────── */

/**
 * editor_insert_line() — 在指定位置后插入一行
 * @after: 在此行之后插入 (NULL 表示在开头插入)
 * @text:  行文本
 * @len:   文本长度
 */
static line_node_t* editor_insert_line(line_node_t *after, const char *text, int len) {
    line_node_t *node = line_create(text, len);
    if (!node) return NULL;

    if (!after) {
        /* 插入到链表开头 */
        node->next = E.lines;
        if (E.lines) E.lines->prev = node;
        E.lines = node;
    } else {
        node->next = after->next;
        node->prev = after;
        if (after->next) after->next->prev = node;
        after->next = node;
    }
    E.num_lines++;
    E.modified = 1;
    return node;
}

/**
 * editor_delete_line() — 删除指定行
 * @node: 要删除的行节点
 * 返回: 被删除行的下一行 (或上一行)
 */
static line_node_t* editor_delete_line(line_node_t *node) {
    if (!node) return NULL;

    line_node_t *next = node->next ? node->next : node->prev;

    if (node->prev) node->prev->next = node->next;
    else E.lines = node->next;
    if (node->next) node->next->prev = node->prev;

    free(node->text);
    free(node);
    E.num_lines--;
    E.modified = 1;
    return next;
}

/**
 * editor_get_line() — 获取指定索引的行
 * @index: 行索引 (从 0 开始)
 */
static line_node_t* editor_get_line(int index) {
    line_node_t *node = E.lines;
    for (int i = 0; i < index && node; i++) {
        node = node->next;
    }
    return node;
}

/**
 * editor_load_file() — 加载文件到行缓冲区
 * 逐行读取文件，每行创建链表节点。
 */
static void editor_load_file(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return;

    char *line_buf = NULL;
    size_t line_cap = 0;
    ssize_t len;

    line_node_t *last = NULL;
    while ((len = getline(&line_buf, &line_cap, f)) > 0) {
        /* 去除末尾换行符 */
        if (len > 0 && line_buf[len - 1] == '\n') {
            line_buf[len - 1] = '\0';
            len--;
        }
        if (len > 0 && len > 0 && line_buf[len - 1] == '\r') {
            line_buf[len - 1] = '\0';
            len--;
        }
        last = editor_insert_line(last, line_buf, len);
    }
    free(line_buf);
    fclose(f);

    /* 如果文件为空，至少保留一行 */
    if (!E.lines) {
        editor_insert_line(NULL, "", 0);
        E.modified = 0;  /* 空文件不算修改 */
    }
}

/**
 * editor_save_file() — 保存行缓冲区到文件
 * 遍历行链表，逐行写入文件。
 */
static int editor_save_file(void) {
    FILE *f = fopen(E.filename, "w");
    if (!f) {
        snprintf(E.status_msg, sizeof(E.status_msg),
                 "Cannot save: %s", strerror(errno));
        return -1;
    }

    line_node_t *node = E.lines;
    while (node) {
        fprintf(f, "%s\n", node->text);
        node = node->next;
    }
    fclose(f);
    E.modified = 0;
    snprintf(E.status_msg, sizeof(E.status_msg),
             "\"%s\" %d lines written", E.filename, E.num_lines);
    return 0;
}

/* ── Rendering ──────────────────────────────────────────────── */

/**
 * editor_scroll() — 调整滚动以确保光标可见
 */
static void editor_scroll(void) {
    if (E.cy < E.scroll_row) {
        E.scroll_row = E.cy;
    }
    if (E.cy >= E.scroll_row + E.screen_rows) {
        E.scroll_row = E.cy - E.screen_rows + 1;
    }
}

/**
 * editor_draw_rows() — 绘制文本行
 * 遍历可见范围内的所有行并显示。
 */
static void editor_draw_rows(void) {
    for (int i = 0; i < E.screen_rows; i++) {
        int file_row = i + E.scroll_row;
        line_node_t *line = editor_get_line(file_row);

        if (line) {
            /* 行号 (蓝色/灰色) */
            char line_num[16];
            snprintf(line_num, sizeof(line_num), "\033[90m%4d \033[0m", file_row + 1);
            (void)write(STDOUT_FILENO, line_num, strlen(line_num));

            /* 行内容 (截断到屏幕宽度) */
            int max_chars = E.screen_cols - 6;  /* 减去行号宽度 */
            if (max_chars < 10) max_chars = 10;

            int len = line->len;
            if (len > max_chars) len = max_chars;
            (void)write(STDOUT_FILENO, line->text, len);
        } else if (file_row >= E.num_lines) {
            /* 超出文件范围，显示 ~ */
            char tilde[16];
            snprintf(tilde, sizeof(tilde), "\033[90m%4s \033[0m~", "");
            (void)write(STDOUT_FILENO, tilde, strlen(tilde));
        }

        /* 清除行尾并换行 */
        (void)write(STDOUT_FILENO, "\033[K\r\n", 5);
    }
}

/**
 * editor_draw_status_bar() — 绘制状态栏
 * 显示: 模式, 文件名, 修改标志, 行号/总行数
 */
static void editor_draw_status_bar(void) {
    /* 反色显示状态栏 */
    (void)write(STDOUT_FILENO, "\033[7m", 4);

    char status[256];
    int len = 0;

    /* 左侧: 模式 */
    const char *mode_str;
    switch (E.mode) {
        case MODE_NORMAL:  mode_str = "NORMAL";  break;
        case MODE_INSERT:  mode_str = "INSERT";  break;
        case MODE_COMMAND: mode_str = "COMMAND"; break;
        default:           mode_str = "?";       break;
    }
    len += snprintf(status + len, sizeof(status) - len,
                    " %s ", mode_str);

    /* 文件名和修改标志 */
    const char *fname = E.filename[0] ? E.filename : "[No Name]";
    char modified_mark = E.modified ? '+' : ' ';
    len += snprintf(status + len, sizeof(status) - len,
                    "| %s %c", fname, modified_mark);

    /* 右侧: 光标位置 */
    char right[64];
    int rlen = snprintf(right, sizeof(right), "| %d/%d ",
                        E.cy + 1, E.num_lines);

    /* 填充空格 */
    while (len < E.screen_cols - rlen && len < (int)sizeof(status) - 1) {
        status[len++] = ' ';
    }
    status[len] = '\0';

    (void)write(STDOUT_FILENO, status, len);
    (void)write(STDOUT_FILENO, right, rlen);
    (void)write(STDOUT_FILENO, "\033[0m\r\n", 6);
}

/**
 * editor_draw_message_bar() — 绘制消息/命令行
 */
static void editor_draw_message_bar(void) {
    (void)write(STDOUT_FILENO, "\033[K", 3);  /* 清除行 */

    if (E.mode == MODE_COMMAND) {
        /* 显示命令行 */
        (void)write(STDOUT_FILENO, ":", 1);
        (void)write(STDOUT_FILENO, E.cmdline, E.cmdlen);
    } else if (E.status_msg[0]) {
        /* 显示状态消息 (5秒后清除) */
        (void)write(STDOUT_FILENO, E.status_msg, strlen(E.status_msg));
        if (time(NULL) - E.status_msg_time > 5) {
            E.status_msg[0] = '\0';
        }
    }
}

/**
 * editor_refresh_screen() — 完整刷新屏幕
 *
 * 绘制顺序:
 *   1. 隐藏光标
 *   2. 定位到屏幕左上角
 *   3. 绘制所有文本行
 *   4. 绘制状态栏
 *   5. 绘制消息栏
 *   6. 定位光标到正确位置
 *   7. 显示光标
 */
static void editor_refresh_screen(void) {
    editor_scroll();

    char buf[32];
    /* 隐藏光标 */
    (void)write(STDOUT_FILENO, "\033[?25l", 6);
    /* 定位到左上角 */
    (void)write(STDOUT_FILENO, "\033[H", 3);

    editor_draw_rows();
    editor_draw_status_bar();
    editor_draw_message_bar();

    /* 定位光标 */
    int cursor_x = (E.cx - 0) + 5;  /* 5 = line number width */
    int cursor_y = (E.cy - E.scroll_row) + 1;
    if (cursor_x < 1) cursor_x = 1;
    if (cursor_y < 1) cursor_y = 1;

    snprintf(buf, sizeof(buf), "\033[%d;%dH", cursor_y, cursor_x);
    (void)write(STDOUT_FILENO, buf, strlen(buf));

    /* 显示光标 */
    (void)write(STDOUT_FILENO, "\033[?25h", 6);
}

/* ── Insert Operations ──────────────────────────────────────── */

/**
 * editor_insert_char() — 在光标位置插入字符
 */
static void editor_insert_char(int ch) {
    line_node_t *line = editor_get_line(E.cy);
    if (!line) {
        /* 当前行不存在，先创建 */
        line_node_t *prev = editor_get_line(E.cy - 1);
        line = editor_insert_line(prev, "", 0);
        if (!line) return;
    }

    /* 在文本中插入字符 */
    if (E.cx > line->len) E.cx = line->len;
    char *new_text = (char*)malloc(line->len + 2);
    if (!new_text) return;

    memcpy(new_text, line->text, E.cx);
    new_text[E.cx] = (char)ch;
    memcpy(new_text + E.cx + 1, line->text + E.cx, line->len - E.cx);
    new_text[line->len + 1] = '\0';

    free(line->text);
    line->text = new_text;
    line->len++;
    E.cx++;
    E.modified = 1;
}

/**
 * editor_insert_newline() — 在光标位置插入换行 (分裂当前行)
 */
static void editor_insert_newline(void) {
    line_node_t *line = editor_get_line(E.cy);
    if (!line) return;

    /* 光标后的文本移到新行 */
    int rest_len = line->len - E.cx;
    char *rest_text = line->text + E.cx;

    line_node_t *new_line = editor_insert_line(line, rest_text, rest_len);
    if (!new_line) return;

    /* 截断当前行 */
    line->text[E.cx] = '\0';
    line->len = E.cx;

    E.cy++;
    E.cx = 0;
    E.modified = 1;
}

/**
 * editor_delete_char() — 删除光标位置的字符
 */
static void editor_delete_char(void) {
    line_node_t *line = editor_get_line(E.cy);
    if (!line) return;

    if (E.cx < line->len) {
        /* 删除当前行的一个字符 */
        memmove(line->text + E.cx, line->text + E.cx + 1, line->len - E.cx);
        line->len--;
        E.modified = 1;
    } else {
        /* 在行尾 — 连接下一行 */
        line_node_t *next = line->next;
        if (next) {
            /* 合并两行 */
            char *new_text = (char*)malloc(line->len + next->len + 1);
            if (!new_text) return;
            memcpy(new_text, line->text, line->len);
            memcpy(new_text + line->len, next->text, next->len);
            new_text[line->len + next->len] = '\0';
            free(line->text);
            line->text = new_text;
            line->len = line->len + next->len;
            editor_delete_line(next);
            E.modified = 1;
        }
    }
}

/**
 * editor_backspace() — 删除光标前的字符
 */
static void editor_backspace(void) {
    if (E.cx > 0) {
        E.cx--;
        editor_delete_char();
    } else if (E.cy > 0) {
        /* 在行首 — 移动到上一行末尾并连接 */
        line_node_t *prev = editor_get_line(E.cy - 1);
        line_node_t *curr = editor_get_line(E.cy);
        if (prev && curr) {
            E.cx = prev->len;
            char *new_text = (char*)malloc(prev->len + curr->len + 1);
            if (!new_text) return;
            memcpy(new_text, prev->text, prev->len);
            memcpy(new_text + prev->len, curr->text, curr->len);
            new_text[prev->len + curr->len] = '\0';
            free(prev->text);
            prev->text = new_text;
            prev->len = prev->len + curr->len;
            editor_delete_line(curr);
            E.cy--;
            E.modified = 1;
        }
    }
}

/* ── Input Processing ───────────────────────────────────────── */

/**
 * editor_read_key() — 读取一个按键
 * 处理 escape sequences (方向键等)
 */
static int editor_read_key(void) {
    char seq[4];
    ssize_t n;

    while ((n = read(STDIN_FILENO, seq, 1)) == 0);

    if (n < 0) return -1;
    if (seq[0] == '\033') {
        /* Escape sequence — 检查是否为方向键 */
        if (read(STDIN_FILENO, seq + 1, 1) <= 0) return '\033';
        if (seq[1] != '[') return '\033';

        if (read(STDIN_FILENO, seq + 2, 1) <= 0) return '\033';
        switch (seq[2]) {
            case 'A': return 1000;  /* 上箭头 */
            case 'B': return 1001;  /* 下箭头 */
            case 'C': return 1002;  /* 右箭头 */
            case 'D': return 1003;  /* 左箭头 */
            case 'H': return 1004;  /* Home */
            case 'F': return 1005;  /* End */
            /* 处理 3 序列方向键 */
            case '1': case '2': case '3': case '4': case '5': case '6':
                if (read(STDIN_FILENO, seq + 3, 1) > 0 && seq[3] == '~') {
                    switch (seq[2]) {
                        case '1': return 1004;  /* Home */
                        case '3': return 1006;  /* Delete */
                        case '4': return 1005;  /* End */
                        case '5': return 1007;  /* Page Up */
                        case '6': return 1008;  /* Page Down */
                    }
                }
                break;
        }
        return '\033';
    }
    return (int)(unsigned char)seq[0];
}

/* ── Command Mode Processing ────────────────────────────────── */

/**
 * editor_execute_command() — 执行 : 命令
 * 支持的命令:
 *   :w   — 保存文件
 *   :q   — 退出 (如果有修改则拒绝)
 *   :wq  — 保存并退出
 *   :q!  — 强制退出 (丢弃修改)
 *   :w <filename> — 另存为
 * 返回: 1 表示应退出编辑器, 0 继续
 */
static int editor_execute_command(void) {
    if (E.cmdlen == 0) return 0;

    char *cmd = E.cmdline;
    if (cmd[0] == 'w' && cmd[1] == 'q') {
        /* :wq — 保存并退出 */
        if (!E.filename[0]) {
            snprintf(E.status_msg, sizeof(E.status_msg), "No filename");
            return 0;
        }
        editor_save_file();
        return 1;
    } else if (cmd[0] == 'q' && cmd[1] == '!') {
        /* :q! — 强制退出 */
        return 1;
    } else if (cmd[0] == 'q') {
        /* :q — 退出 (需未修改) */
        if (E.modified) {
            snprintf(E.status_msg, sizeof(E.status_msg),
                     "No write since last change (use :q! to override)");
            return 0;
        }
        return 1;
    } else if (cmd[0] == 'w' && cmd[1] == ' ') {
        /* :w <filename> — 另存为 */
        strncpy(E.filename, cmd + 2, sizeof(E.filename) - 1);
        E.filename[sizeof(E.filename) - 1] = '\0';
        editor_save_file();
    } else if (cmd[0] == 'w') {
        /* :w — 保存 */
        if (!E.filename[0]) {
            snprintf(E.status_msg, sizeof(E.status_msg),
                     "No filename (use :w <filename>)");
            return 0;
        }
        editor_save_file();
    } else {
        snprintf(E.status_msg, sizeof(E.status_msg),
                 "Unknown command: %s", cmd);
    }

    return 0;
}

/* ── Main Editor Process ────────────────────────────────────── */

/**
 * editor_process_keypress() — 处理单个按键
 * 返回: 1 表示应退出编辑器, 0 继续
 */
static int editor_process_keypress(void) {
    int key = editor_read_key();

    /* ── COMMAND 模式 ──────────────────────────────────────── */
    if (E.mode == MODE_COMMAND) {
        switch (key) {
            case '\r':  /* Enter */
            case '\n':
                return editor_execute_command();
            case '\033':  /* ESC — 取消命令 */
                E.mode = MODE_NORMAL;
                E.cmdlen = 0;
                E.cmdline[0] = '\0';
                break;
            case 127:     /* Backspace */
            case '\b':
                if (E.cmdlen > 0) E.cmdlen--;
                E.cmdline[E.cmdlen] = '\0';
                break;
            default:
                if (key >= 32 && key < 127 && E.cmdlen < (int)sizeof(E.cmdline) - 1) {
                    E.cmdline[E.cmdlen++] = (char)key;
                    E.cmdline[E.cmdlen] = '\0';
                }
                break;
        }
        return 0;
    }

    /* ── NORMAL 模式 ────────────────────────────────────────── */
    if (E.mode == MODE_NORMAL) {
        switch (key) {
            case 'h': case 1003:  /* 左 */
                if (E.cx > 0) E.cx--;
                break;
            case 'j': case 1001:  /* 下 */
                if (E.cy < E.num_lines - 1) E.cy++;
                break;
            case 'k': case 1000:  /* 上 */
                if (E.cy > 0) E.cy--;
                break;
            case 'l': case 1002:  /* 右 */
                {
                    line_node_t *line = editor_get_line(E.cy);
                    if (line && E.cx < line->len) E.cx++;
                }
                break;
            case '0':  /* 行首 */
                E.cx = 0;
                break;
            case '$':  /* 行尾 */
                {
                    line_node_t *line = editor_get_line(E.cy);
                    if (line) E.cx = line->len;
                }
                break;
            case 'w':  /* 下一个单词 */
                {
                    line_node_t *line = editor_get_line(E.cy);
                    if (line) {
                        /* 跳过当前单词 */
                        while (E.cx < line->len && line->text[E.cx] != ' ') E.cx++;
                        /* 跳过空格 */
                        while (E.cx < line->len && line->text[E.cx] == ' ') E.cx++;
                    }
                }
                break;
            case 'b':  /* 上一个单词 */
                if (E.cx > 0) {
                    line_node_t *line;
                    E.cx--;
                    line = editor_get_line(E.cy);
                    if (line) {
                        while (E.cx > 0 && line->text[E.cx] == ' ') E.cx--;
                        while (E.cx > 0 && line->text[E.cx - 1] != ' ') E.cx--;
                    }
                }
                break;
            case 'g':  /* gg — 文件开头 (需要连按两次 g) */
                /* 简化: 单次 g 不移动 */
                break;
            case 'G':  /* 文件末尾 */
                E.cy = E.num_lines > 0 ? E.num_lines - 1 : 0;
                E.cx = 0;
                break;
            case 'x':  /* 删除字符 */
                editor_delete_char();
                break;
            case 'd':  /* dd — 删除行 (需要连按两次 d) */
                /* 简化: 单次 d 不执行 */
                break;
            case 'i':  /* 进入插入模式 (光标前) */
                E.mode = MODE_INSERT;
                break;
            case 'a':  /* 追加模式 (光标后) */
                if (E.cx < editor_get_line(E.cy)->len) E.cx++;
                E.mode = MODE_INSERT;
                break;
            case 'o':  /* 下方打开新行 */
                {
                    line_node_t *line = editor_get_line(E.cy);
                    if (line) {
                        editor_insert_line(line, "", 0);
                    } else {
                        editor_insert_line(NULL, "", 0);
                    }
                    E.cy++;
                    E.cx = 0;
                    E.mode = MODE_INSERT;
                }
                break;
            case 'O':  /* 上方打开新行 */
                {
                    line_node_t *prev = editor_get_line(E.cy - 1);
                    editor_insert_line(prev, "", 0);
                    E.cx = 0;
                    E.mode = MODE_INSERT;
                }
                break;
            case ':':  /* 进入命令行模式 */
                E.mode = MODE_COMMAND;
                E.cmdlen = 0;
                E.cmdline[0] = '\0';
                break;
            case 'J':  /* 连接下一行到当前行 */
                {
                    line_node_t *line = editor_get_line(E.cy);
                    line_node_t *next = line ? line->next : NULL;
                    if (line && next) {
                        char *new_text = (char*)malloc(line->len + next->len + 1);
                        if (new_text) {
                            memcpy(new_text, line->text, line->len);
                            memcpy(new_text + line->len, next->text, next->len);
                            new_text[line->len + next->len] = '\0';
                            free(line->text);
                            line->text = new_text;
                            line->len = line->len + next->len;
                            editor_delete_line(next);
                        }
                    }
                }
                break;
            /* Ctrl+L — 刷新屏幕 */
            case 12:
                break;
        }

        /* 确保 cx 不越界 */
        line_node_t *cur_line = editor_get_line(E.cy);
        if (cur_line && E.cx > cur_line->len) {
            E.cx = cur_line->len;
        }
        if (E.cx < 0) E.cx = 0;

        return 0;
    }

    /* ── INSERT 模式 ────────────────────────────────────────── */
    if (E.mode == MODE_INSERT) {
        switch (key) {
            case '\033':  /* ESC — 退出插入模式 */
                E.mode = MODE_NORMAL;
                if (E.cx > 0) E.cx--;  /* 光标左移一位 (vi 行为) */
                break;
            case 127:     /* Backspace */
            case '\b':
                editor_backspace();
                break;
            case '\r':    /* Enter — 换行 */
            case '\n':
                editor_insert_newline();
                break;
            case 12:      /* Ctrl+L */
                break;
            default:
                if (key >= 32 && key < 127) {
                    editor_insert_char(key);
                }
                break;
        }
        return 0;
    }

    return 0;
}

/**
 * editor_init() — 初始化编辑器状态
 */
static void editor_init(const char *filename) {
    memset(&E, 0, sizeof(E));
    E.mode = MODE_NORMAL;
    E.screen_rows = 24;
    E.screen_cols = 80;

    if (filename) {
        strncpy(E.filename, filename, sizeof(E.filename) - 1);
        E.filename[sizeof(E.filename) - 1] = '\0';
        editor_load_file(filename);
    }

    /* 如果文件为空或不存在，创建空的第一行 */
    if (!E.lines) {
        editor_insert_line(NULL, "", 0);
        E.modified = 0;  /* 新建空文件不算修改 */
    }
}

/**
 * editor_run() — 启动全屏幕编辑器 (外部入口)
 *
 * 编辑流程:
 *   1. 保存原始终端设置
 *   2. 切换到原始模式
 *   3. 加载文件
 *   4. 主循环: 读取按键 -> 处理 -> 刷新屏幕
 *   5. 退出时恢复终端
 */
int editor_run(const char *filename) {
    /* 清屏并进入 */
    (void)write(STDOUT_FILENO, "\033[2J\033[H", 7);
    printf("myOS Editor v1.0 — vi-like full-screen editor\r\n");
    printf("Loading...\r\n");

    /* 初始化终端 */
    editor_enable_raw_mode();
    editor_get_window_size();
    editor_init(filename);

    /* 设置状态消息 */
    snprintf(E.status_msg, sizeof(E.status_msg),
             "HELP: :w save | :q quit | :wq save+quit | :q! force quit");
    E.status_msg_time = time(NULL);

    /* ── 主编辑循环 ──────────────────────────────────────────── */
    int quit = 0;
    while (!quit) {
        editor_refresh_screen();
        quit = editor_process_keypress();
    }

    /* 恢复终端 */
    editor_disable_raw_mode();
    (void)write(STDOUT_FILENO, "\033[2J\033[H", 7);

    printf("Editor closed.\n");
    if (E.modified) {
        printf("Warning: unsaved changes were lost.\n");
    }

    /* 清理 */
    line_free_all(E.lines);
    E.lines = NULL;

    return 0;
}
