#ifndef HEPU_ASSERT_H
#define HEPU_ASSERT_H

/* V3.87-E11: 断言机制
 * 调试/开发期: 违反不变式立即打印 表达式+文件+行号 并停机(HALT),
 * 防止带错误状态继续运行产生更难排查的二次故障.
 * 用法: ASSERT(cond);   (仅在主上下文使用——中断处理程序内禁用, 防重入) */

void assert_fail(const char *file, int line, const char *expr_s);

#define ASSERT(expr) do { \
    if (!(expr)) { \
        assert_fail(__FILE__, __LINE__, #expr); \
    } \
} while (0)

#endif /* HEPU_ASSERT_H */
