/**
 * printer.h — Virtual Printer Driver Header for myOS Shell
 *
 * 提供虚拟打印机接口，支持将文本文件打印为 PDF 文件。
 * 模拟 printer_open/printer_write/printer_close 的设备驱动模型。
 */

#ifndef PRINTER_H
#define PRINTER_H

/**
 * printer_print_file(input_file, output_pdf)
 * 将文本文件内容转换为 PDF 文件。
 *
 * @input_file:  输入的文本文件路径
 * @output_pdf:  输出的 PDF 文件路径
 * 返回: 0 成功, -1 错误
 *
 * PDF 生成说明:
 *   从零开始构建一个最小化的 PDF 1.4 文件，包含:
 *     - PDF 文件头
 *     - 文档目录 (Catalog)
 *     - 页面树 (Pages)
 *     - 单个页面对象，使用内置 Courier 字体
 *     - 文本内容通过 PDF 文本操作符渲染 (BT/ET/Tf/Td/Tj)
 *     - 交叉引用表 (xref)
 *     - 文件尾 (trailer)
 */
int printer_print_file(const char *input_file, const char *output_pdf);

#endif /* PRINTER_H */
