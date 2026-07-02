# myOS Shell — 小型操作系统原型

一个用 C 语言编写的小型操作系统原型，包含交互式 Shell、全屏幕编辑器和虚拟设备驱动程序。

## 项目架构

```
shell/
├── myos.c          # 主入口 + Shell 主循环
├── shell.c         # Shell 命令解析器 & 调度器
├── shell.h         # Shell 接口声明
├── commands.c      # 19 个内置命令实现
├── commands.h      # 命令声明
├── editor.c        # 全屏幕 vi-like 编辑器
├── editor.h        # 编辑器接口声明
├── printer.c       # 虚拟打印机驱动 + PDF 生成
├── printer.h       # 打印机接口声明
├── keyboard.c      # 虚拟键盘驱动
├── keyboard.h      # 键盘接口声明
├── process.c       # 进程管理 (PCB, 作业控制)
├── process.h       # 进程管理接口声明
├── datastruct.h    # 共享数据结构
├── Makefile        # 构建系统
└── README.md       # 本文件
```

## 构建 & 运行

### 环境要求
- Linux/UNIX 操作系统 (需要 /proc 文件系统、POSIX API)
- GCC 编译器
- GNU Make
- pthread 库

### 编译
```bash
cd shell
make
```

### 运行
```bash
./myos
```

### 清理
```bash
make clean
```

## 支持的命令

### 进程管理
| 命令 | 说明 |
|------|------|
| `ps` | 读取 /proc 显示所有进程 |
| `ps axjf` | 进程树显示 (a=所有用户, x=无终端, j=作业格式, f=森林) |
| `ps aux` | BSD 风格进程列表，按 PID 排序 |
| `top` | 动态进程监视器 (按 q 退出) |

### 文件操作
| 命令 | 说明 |
|------|------|
| `ls -l` | 长格式列出目录内容 |
| `ls -l \| more` | 分页显示 (管道示例) |
| `cat <file>` | 显示文件内容 |
| `cp <src> <dst>` | 复制文件 |
| `mv <src> <dst>` | 移动/重命名文件 |
| `rm <file>` | 删除文件 (带确认) |
| `file <path>` | 识别文件类型 (魔术字节) |

### 工具命令
| 命令 | 说明 |
|------|------|
| `cal` | 显示日历 |
| `whoami` | 显示当前用户 |
| `date` | 显示日期时间 |
| `pwd` | 打印工作目录 |
| `mkdir <dir>` | 创建目录 |
| `rmdir <dir>` | 删除空目录 |

### 编辑器 & 设备驱动
| 命令 | 说明 |
|------|------|
| `vi [file]` | 全屏幕编辑器 |
| `print <file> -o <pdf>` | 虚拟打印机 (文本→PDF) |
| `startkbd` | 启动虚拟键盘驱动 |
| `readkbd` | 读取键盘事件 |
| `stopkbd` | 停止虚拟键盘驱动 |

### Shell 控制
| 命令 | 说明 |
|------|------|
| `exit` | 退出 Shell |
| `help` | 显示帮助 |
| `<cmd> &` | 后台执行命令 |

## vi 编辑器使用

启动: `vi filename`

### 模式
- **NORMAL** (默认) — 按键作为编辑命令
- **INSERT** (按 i/a/o/O 进入) — 输入文本
- **COMMAND** (按 : 进入) — 保存/退出命令

### 常用按键
| 按键 | 功能 |
|------|------|
| h/j/k/l 或方向键 | 光标移动 |
| i | 光标前插入 |
| a | 光标后插入 |
| o | 下方新行插入 |
| x | 删除字符 |
| dd | 删除行 |
| :w | 保存文件 |
| :q | 退出 |
| :wq | 保存并退出 |
| :q! | 强制退出 |
| ESC | 返回 NORMAL 模式 |

## 虚拟键盘驱动

后台线程定期生成模拟按键事件到 `/tmp/myos_kbd_fifo`。

```bash
myOSshell $ startkbd          # 启动驱动
myOSshell $ readkbd           # 读取事件
myOSshell $ stopkbd           # 停止驱动
```

## 虚拟打印机

将文本文件转换为 PDF 文件。

```bash
myOSshell $ print source.c -o output.pdf
```

## 设计特点

1. **命令由 C 代码实现**: 不调用外部 `system()`，所有命令通过系统调用和文件操作编程实现
2. **进程管理**: 使用 PCB (Process Control Block) 数据结构追踪进程状态
3. **fork/exec 模型**: 外部命令通过 fork() 创建子进程执行
4. **管道支持**: 使用 pipe() + dup2() 实现命令间数据传递
5. **后台作业**: & 符号标记后台执行，SIGCHLD 自动回收
6. **全屏幕编辑器**: 使用 termios 原始模式实现逐键响应
7. **PDF 生成**: 从零构建符合 PDF 1.4 规范的文件
