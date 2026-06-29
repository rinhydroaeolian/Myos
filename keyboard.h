/**
 * keyboard.h — Virtual Keyboard Driver Header for myOS Shell
 *
 * 提供虚拟键盘驱动接口，模拟键盘输入事件。
 * 使用后台线程生成模拟按键，通过命名管道 (FIFO) 传递。
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

/**
 * kbd_start()
 * 启动虚拟键盘驱动程序。
 * 创建一个后台线程，定期生成模拟按键事件并写入 FIFO。
 * 返回: 0 成功, -1 已运行或启动失败
 */
int kbd_start(void);

/**
 * kbd_read()
 * 从键盘事件队列中读取并显示待处理的事件。
 * 返回: 0 成功, -1 键盘未启动或无数据
 */
int kbd_read(void);

/**
 * kbd_stop()
 * 停止虚拟键盘驱动程序。
 * 终止后台线程并清理 FIFO 资源。
 * 返回: 0 成功, -1 未运行
 */
int kbd_stop(void);

#endif /* KEYBOARD_H */
