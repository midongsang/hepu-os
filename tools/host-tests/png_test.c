/* 赫普OS 主机测试: PNG 解码器正确性校验
 * 用法: png_test <file.png> <expect.txt>
 * expect.txt 格式:
 *   line1: "<w> <h>"
 *   其余:  "<x> <y> <r> <g> <b> <a>"  (逐行, 与解码结果比对)
 * 输出 ALL PASS / FAIL 列表, 退出码 0/1
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "png64.h"

int main(int argc, char **argv)
{
    if (argc < 3) { printf("usage: png_test <png> <expect>\n"); return 2; }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { printf("FAIL: cannot open png\n"); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(sz);
    if (!buf || fread(buf, 1, sz, f) != (size_t)sz) { printf("FAIL: read\n"); return 1; }
    fclose(f);

    png64_image_t img;
    memset(&img, 0, sizeof(img));
    int r = png64_decode(buf, (int)sz, &img);
    free(buf);
    if (r != 0) { printf("FAIL: decode rc=%d\n", r); return 1; }

    FILE *e = fopen(argv[2], "r");
    if (!e) { printf("FAIL: no expect file\n"); return 1; }

    unsigned int ew = 0, eh = 0;
    if (fscanf(e, "%u %u", &ew, &eh) != 2) { printf("FAIL: expect header\n"); return 1; }
    if (ew != img.width || eh != img.height) {
        printf("FAIL: size got %ux%u expect %ux%u\n", img.width, img.height, ew, eh);
        return 1;
    }

    int fails = 0, checks = 0;
    unsigned int x, y, rr, gg, bb, aa;
    while (fscanf(e, "%u %u %u %u %u %u", &x, &y, &rr, &gg, &bb, &aa) == 6) {
        checks++;
        if (x >= img.width || y >= img.height) { fails++; continue; }
        unsigned char *p = img.rgba + ((size_t)y * img.width + x) * 4;
        if (p[0] != rr || p[1] != gg || p[2] != bb || p[3] != aa) {
            printf("FAIL: px(%u,%u) got %u,%u,%u,%u expect %u,%u,%u,%u\n",
                   x, y, p[0], p[1], p[2], p[3], rr, gg, bb, aa);
            fails++;
        }
    }
    fclose(e);
    png64_free(&img);

    if (fails == 0 && checks > 0) {
        printf("ALL PASS (%d 像素校验点, %ux%u)\n", checks, ew, eh);
        return 0;
    }
    printf("FAIL: %d/%d 不符\n", fails, checks);
    return 1;
}
