/**
 * editor.h — Full-Screen Editor Header for myOS Shell
 *
 * 提供类似 vi 的全屏幕文本编辑器接口。
 * 使用原始终端模式 (raw mode) 实现全屏幕控制。
 */

#ifndef EDITOR_H
#define EDITOR_H

/**
 * editor_run(filename)
 * 启动全屏幕编辑器。
 *
 * @filename: 要编辑的文件路径 (可为 NULL，表示新建文件)
 * 返回: 0 成功, -1 错误
 *
 * 功能:
 *   - 将终端切换为原始模式 (禁用行缓冲和回显)
 *   - 加载文件内容到行缓冲区
 *   - 进入编辑主循环，处理按键:
 *     * NORMAL 模式: h/j/k/l 移动光标, i/a/o/O 插入, x 删除, dd 删除行,
 *                    :w 保存, :q 退出, :wq 保存退出, :q! 强制退出
 *     * INSERT 模式: 输入文本, Backspace 删除, ESC 返回 NORMAL
 *     * COMMAND 模式: 处理 : 开头的命令
 *   - 退出时恢复终端设置
 */
int editor_run(const char *filename);

#endif /* EDITOR_H */
