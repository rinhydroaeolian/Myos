/**
 * printer.c — Virtual Printer Driver Implementation for myOS Shell
 *
 * 实现虚拟打印机，将文本内容输出为 PDF 格式文件。
 * 使用标准的 PDF 1.4 格式从零构建 PDF 文档。
 *
 * 设备驱动模型:
 *   本模块模拟了打印机驱动程序的标准接口模式:
 *     - printer_open():  打开/初始化设备
 *     - printer_write(): 写入数据到设备
 *     - printer_close(): 关闭设备并完成输出
 *
 * PDF 结构说明:
 *   一个最小 PDF 文件由以下部分组成:
 *     1. Header:         %PDF-1.4\n
 *     2. Body:           包含编号的 PDF 对象
 *     3. Cross-reference: 对象偏移量表 (xref)
 *     4. Trailer:        文件尾，指向根对象
 *
 *   PDF 使用 Courier 等宽字体渲染文本，适合打印源代码。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "printer.h"

/* ── Constants ───────────────────────────────────────────────── */
#define PDF_FONT_SIZE   10          /* 字体大小 (pt) */
#define PDF_MARGIN      50          /* 页边距 (pt) */
#define PDF_LINE_SPACING 14         /* 行间距 (pt) */
#define PDF_CHARS_PER_LINE 90       /* 每行最大字符数 */
#define PDF_LINES_PER_PAGE 55       /* 每页最大行数 */
#define PDF_PAGE_WIDTH  612         /* Letter 页面宽度 (pt) */
#define PDF_PAGE_HEIGHT 792         /* Letter 页面高度 (pt) */

/* ── Internal State ─────────────────────────────────────────── */

typedef struct {
    FILE   *file;              /* 输出文件指针 */
    long    offset;            /* 当前写入位置 */
    long    obj_offsets[256];  /* 各对象的偏移量 */
    int     obj_count;         /* 对象计数器 */
    int     current_page;      /* 当前页码 */
    int     lines_on_page;     /* 当前页已写入的行数 */
    char   *page_content;      /* 当前页的内容流缓冲区 */
    size_t  content_len;       /* 缓冲区长度 */
    size_t  content_cap;       /* 缓冲区容量 */
} printer_t;

static printer_t g_printer;

/* ── PDF String Escaping ────────────────────────────────────── */

/**
 * pdf_escape_string() — 转义 PDF 字符串中的特殊字符
 *
 * PDF 字符串中需要转义的字符:
 *   \  -> \\
 *   (  -> \(
 *   )  -> \)
 *   以及不可打印字符
 *
 * @input:  原始字符串
 * @output: 转义后的字符串 (调用者负责提供足够空间)
 * @maxlen: 输出缓冲区大小
 * 返回: 写入的字节数 (不含结尾 null)
 */
static int pdf_escape_string(const char *input, char *output, int maxlen) {
    int out_pos = 0;
    for (const char *p = input; *p && out_pos < maxlen - 2; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch == '\\' || ch == '(' || ch == ')') {
            output[out_pos++] = '\\';
            output[out_pos++] = ch;
        } else if (ch < 32 || ch > 126) {
            /* 不可打印字符替换为空格 */
            output[out_pos++] = ' ';
        } else {
            output[out_pos++] = ch;
        }
    }
    output[out_pos] = '\0';
    return out_pos;
}

/* ── Low-Level PDF Writing ──────────────────────────────────── */

/**
 * pdf_write_header() — 写入 PDF 文件头
 * 格式: %PDF-1.4\n
 * 注意: 文件头后的 % 注释用于标记文件为二进制 (某些工具需要)
 */
static void pdf_write_header(void) {
    fprintf(g_printer.file, "%%PDF-1.4\n");
    /* 添加二进制标记 (4 个非 ASCII 字节，传统上用于标识二进制 PDF) */
    fprintf(g_printer.file, "%%%c%c%c%c\n", 0xE2, 0xE3, 0xCF, 0xD3);
    g_printer.offset = ftell(g_printer.file);
}

/**
 * pdf_begin_object() — 开始一个 PDF 对象
 * 记录对象的文件偏移量 (用于交叉引用表)。
 */
static void pdf_begin_object(int obj_id) {
    g_printer.obj_offsets[obj_id] = ftell(g_printer.file);
    fprintf(g_printer.file, "%d 0 obj\n", obj_id);
    g_printer.obj_count = obj_id + 1;
}

/**
 * pdf_end_object() — 结束当前对象
 */
static void pdf_end_object(void) {
    fprintf(g_printer.file, "endobj\n");
}

/**
 * pdf_write_object_stream() — 写入一个流对象 (如页面内容)
 * @obj_id: 对象编号
 * @data:   流数据
 * @len:    数据长度
 * @dict:   流字典 (如 "/Length %d\n/Filter /ASCIIHexDecode\n")
 */
static void pdf_write_object_stream(int obj_id, const char *data, int len,
                                     const char *dict) {
    pdf_begin_object(obj_id);
    fprintf(g_printer.file, "<< /Length %d\n%s>>\nstream\n", len, dict);
    fwrite(data, 1, len, g_printer.file);
    fprintf(g_printer.file, "\nendstream\n");
    pdf_end_object();
}

/**
 * pdf_write_catalog() — 写入文档目录对象 (Object 1)
 * 目录是 PDF 文档的根对象，指向页面树。
 */
static void pdf_write_catalog(int pages_obj_id) {
    pdf_begin_object(1);
    fprintf(g_printer.file, "<< /Type /Catalog\n"
                            "   /Pages %d 0 R\n"
                            ">>\n", pages_obj_id);
    pdf_end_object();
}

/**
 * pdf_write_pages() — 写入页面树对象
 * @page_ids:  页面对象 ID 数组
 * @num_pages: 页面数量
 */
static void pdf_write_pages(int *page_ids, int num_pages) {
    pdf_begin_object(2);
    fprintf(g_printer.file, "<< /Type /Pages\n"
                            "   /Kids [");
    for (int i = 0; i < num_pages; i++) {
        fprintf(g_printer.file, "%d 0 R ", page_ids[i]);
    }
    fprintf(g_printer.file, "]\n"
                            "   /Count %d\n"
                            ">>\n", num_pages);
    pdf_end_object();
}

/**
 * pdf_write_page() — 写入一个页面对象
 * @page_obj_id:     页面对象的 ID
 * @content_obj_id:  内容流对象的 ID
 * @parent_obj_id:   父页面树的 ID (通常为 2)
 */
static void pdf_write_page(int page_obj_id, int content_obj_id, int parent_obj_id) {
    pdf_begin_object(page_obj_id);
    fprintf(g_printer.file, "<< /Type /Page\n"
                            "   /Parent %d 0 R\n"
                            "   /MediaBox [0 0 %d %d]\n"
                            "   /Contents %d 0 R\n"
                            "   /Resources << /Font << /F1 << /Type /Font\n"
                            "                              /Subtype /Type1\n"
                            "                              /BaseFont /Courier\n"
                            "                           >>\n"
                            "                >>\n"
                            ">>\n",
                            parent_obj_id,
                            PDF_PAGE_WIDTH, PDF_PAGE_HEIGHT,
                            content_obj_id);
    pdf_end_object();
}

/**
 * pdf_write_content_stream() — 写入页面内容流
 * 使用 PDF 文本操作符:
 *   BT              — 开始文本对象 (Begin Text)
 *   /F1 10 Tf       — 选择字体和大小
 *   x y Td          — 定位文本
 *   (text) Tj       — 显示文本字符串
 *   ET              — 结束文本对象 (End Text)
 */
static void pdf_write_content_stream(int obj_id, const char *content) {
    /* 查找实际的换行符数量以计算正确的流长度 */
    pdf_begin_object(obj_id);
    int len = (int)strlen(content);
    fprintf(g_printer.file, "<< /Length %d >>\nstream\n", len);
    fprintf(g_printer.file, "%s", content);
    fprintf(g_printer.file, "endstream\n");
    pdf_end_object();
}

/**
 * pdf_write_xref() — 写入交叉引用表 (xref)
 *
 * xref 表记录了每个对象的文件偏移量，使 PDF 阅读器可以
 * 快速定位任意对象。
 *
 * 格式:
 *   xref
 *   0 <num_objects>
 *   0000000000 65535 f\r\n    (对象 0 永远为空且不可用)
 *   <offset> 00000 n\r\n     (对象 1..n)
 */
static void pdf_write_xref(int num_objects) {
    long xref_offset = ftell(g_printer.file);
    fprintf(g_printer.file, "xref\n");
    fprintf(g_printer.file, "0 %d\n", num_objects + 1);
    /* 对象 0 */
    fprintf(g_printer.file, "0000000000 65535 f \r\n");
    /* 对象 1..n */
    for (int i = 1; i < num_objects; i++) {
        fprintf(g_printer.file, "%010ld 00000 n \r\n", g_printer.obj_offsets[i]);
    }
    /* trailer */
    fprintf(g_printer.file, "trailer\n");
    fprintf(g_printer.file, "<< /Size %d\n"
                            "   /Root 1 0 R\n"
                            ">>\n", num_objects + 1);
    fprintf(g_printer.file, "startxref\n");
    fprintf(g_printer.file, "%ld\n", xref_offset);
    fprintf(g_printer.file, "%%%%EOF\n");
}

/* ── Page Content Buffer ────────────────────────────────────── */

/**
 * printer_init_page_buffer() — 初始化新页面的内容缓冲区
 */
static void printer_init_page_buffer(void) {
    if (g_printer.page_content) free(g_printer.page_content);
    g_printer.page_content = (char*)malloc(4096);
    g_printer.content_len = 0;
    g_printer.content_cap = 4096;
    g_printer.lines_on_page = 0;

    if (g_printer.page_content) {
        g_printer.page_content[0] = '\0';
    }
}

/**
 * printer_append_to_page() — 向页面内容缓冲区追加文本行
 * 将文本行转换为 PDF 定位 + 显示指令。
 */
static void printer_append_to_page(const char *line) {
    if (!g_printer.page_content) return;

    /* 转义文本中的特殊字符 */
    char escaped[512];
    pdf_escape_string(line, escaped, sizeof(escaped));

    /* 计算 Y 坐标 (从页面上方开始，向下移动) */
    int y = PDF_PAGE_HEIGHT - PDF_MARGIN
            - g_printer.lines_on_page * PDF_LINE_SPACING;

    /* 构建 PDF 文本指令 */
    char instruction[1024];
    int instr_len = snprintf(instruction, sizeof(instruction),
                             "BT /F1 %d Tf %d %d Td (%s) Tj ET\n",
                             PDF_FONT_SIZE, PDF_MARGIN, y, escaped);

    /* 扩展缓冲区 */
    size_t needed = g_printer.content_len + instr_len + 1;
    if (needed >= g_printer.content_cap) {
        size_t new_cap = g_printer.content_cap * 2;
        if (new_cap < needed) new_cap = needed + 4096;
        char *new_buf = (char*)realloc(g_printer.page_content, new_cap);
        if (!new_buf) return;
        g_printer.page_content = new_buf;
        g_printer.content_cap = new_cap;
    }

    /* 追加到页面缓冲区 */
    memcpy(g_printer.page_content + g_printer.content_len, instruction, instr_len);
    g_printer.content_len += instr_len;
    g_printer.page_content[g_printer.content_len] = '\0';
    g_printer.lines_on_page++;
}

/**
 * printer_flush_page() — 将当前页面写入 PDF
 * @page_obj_id: 当前页面的对象 ID
 * @content_obj_id: 内容流的对象 ID
 *
 * 写入页面对象和内容流对象。
 */
static void printer_flush_page(int page_obj_id, int content_obj_id) {
    if (!g_printer.page_content || g_printer.content_len == 0) {
        /* 空页 — 写一个空流 */
        pdf_write_content_stream(content_obj_id, " ");
    } else {
        pdf_write_content_stream(content_obj_id, g_printer.page_content);
    }
    pdf_write_page(page_obj_id, content_obj_id, 2 /* parent */);

    /* 重置页面缓冲区 */
    free(g_printer.page_content);
    g_printer.page_content = NULL;
    g_printer.content_len = 0;
    g_printer.content_cap = 0;
    g_printer.lines_on_page = 0;
}

/* ── Public API ─────────────────────────────────────────────── */

/**
 * printer_print_file() — 将文本文件打印为 PDF
 *
 * 处理流程:
 *   1. 打开输出 PDF 文件
 *   2. 写入 PDF 文件头
 *   3. 逐行读取输入文本文件
 *   4. 每行转换为 PDF 文本指令
 *   5. 每满一页 (PDF_LINES_PER_PAGE 行) 写入一个页面对象
 *   6. 最后写入目录、页面树、交叉引用表和文件尾
 *   7. 关闭文件
 *
 * @input_file: 输入文本文件路径
 * @output_pdf: 输出 PDF 路径
 * 返回: 0 成功, 1 失败
 */
int printer_print_file(const char *input_file, const char *output_pdf) {
    /* 打开输入文件 */
    FILE *in = fopen(input_file, "r");
    if (!in) {
        fprintf(stderr, "printer: cannot open '%s': %s\n",
                input_file, strerror(errno));
        return 1;
    }

    /* 打开输出 PDF 文件 */
    memset(&g_printer, 0, sizeof(g_printer));
    g_printer.file = fopen(output_pdf, "wb");
    if (!g_printer.file) {
        fprintf(stderr, "printer: cannot create '%s': %s\n",
                output_pdf, strerror(errno));
        fclose(in);
        return 1;
    }

    printf("Printer: printing '%s' -> '%s' ...\n", input_file, output_pdf);

    /* 写入 PDF 文件头 */
    pdf_write_header();

    /* 预留对象偏移槽位 (最多 512 个页面，每页 2 个对象 + 目录 + 页面树) */
    /* 对象分配:
     *   1: Catalog
     *   2: Pages
     *   3,4: Page 1  (3=content stream, 4=page object)
     *   5,6: Page 2
     *   ...
     *   (content_obj_id, page_obj_id) 对
     */

    int page_ids[512];  /* 最多 512 页 */
    int num_pages = 0;
    int next_obj_id = 3;  /* 从对象 3 开始分配 */

    /* 读取并处理输入文件 */
    char line_buf[1024];
    printer_init_page_buffer();

    while (fgets(line_buf, sizeof(line_buf), in)) {
        /* 去除换行符 */
        size_t len = strlen(line_buf);
        if (len > 0 && line_buf[len - 1] == '\n') {
            line_buf[len - 1] = '\0';
            len--;
        }
        if (len > 0 && line_buf[len - 1] == '\r') {
            line_buf[len - 1] = '\0';
            len--;
        }

        /* 截断过长行 */
        if ((int)len > PDF_CHARS_PER_LINE) {
            line_buf[PDF_CHARS_PER_LINE] = '\0';
        }

        printer_append_to_page(line_buf);

        /* 页面满了，写入 PDF */
        if (g_printer.lines_on_page >= PDF_LINES_PER_PAGE) {
            if (num_pages < 512) {
                int content_obj = next_obj_id++;
                int page_obj = next_obj_id++;
                printer_flush_page(page_obj, content_obj);
                page_ids[num_pages++] = page_obj;
            }
            printer_init_page_buffer();
        }
    }

    /* 写入最后一页 (如果有内容) */
    if (g_printer.lines_on_page > 0 || num_pages == 0) {
        if (num_pages < 512) {
            int content_obj = next_obj_id++;
            int page_obj = next_obj_id++;
            printer_flush_page(page_obj, content_obj);
            page_ids[num_pages++] = page_obj;
        }
    }

    /* 如果没有内容，创建空页 */
    if (num_pages == 0) {
        int content_obj = next_obj_id++;
        int page_obj = next_obj_id++;
        pdf_write_content_stream(content_obj,
            "BT /F1 10 Tf 50 700 Td (Empty document) Tj ET\n");
        pdf_write_page(page_obj, content_obj, 2);
        page_ids[num_pages++] = page_obj;
    }

    /* 写入文档结构 */
    pdf_write_catalog(2);          /* Catalog -> Pages(2) */
    pdf_write_pages(page_ids, num_pages);  /* Pages tree */

    /* 写入交叉引用表和文件尾 */
    pdf_write_xref(next_obj_id - 1);

    fclose(in);
    fclose(g_printer.file);

    printf("Printer: PDF generated successfully (%d pages)\n", num_pages);
    return 0;
}
