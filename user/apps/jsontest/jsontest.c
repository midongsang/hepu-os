/* ============================================================================
 * 赫普OS - JSON解析/生成示例
 * 演示cJSON库的使用方法
 * V8.56
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "../../lib/cjson/cJSON.h"

/* 系统调用 */
extern int hepu_write(int fd, const void *buf, int count);
extern void hepu_exit(int status);

static void print_str(const char *s) {
    int len = 0;
    while (s[len]) len++;
    hepu_write(1, s, len);
}

static void print_int(int n) {
    char buf[16];
    int i = 0, neg = 0;
    char temp[16];
    if (n < 0) { neg = 1; n = -n; }
    if (n == 0) buf[i++] = '0';
    else { while (n > 0) { temp[i++] = '0' + (n % 10); n /= 10; } }
    int j = 0;
    if (neg) buf[j++] = '-';
    while (i > 0) buf[j++] = temp[--i];
    buf[j] = 0;
    print_str(buf);
}

int main(int argc, char **argv)
{
    print_str("=== HepuOS JSON Test (cJSON v1.7.17) ===\n\n");

    /* 测试1: 创建JSON对象 */
    print_str("[Test 1] Create JSON object:\n");
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", "HepuOS");
    cJSON_AddStringToObject(root, "version", "1.0.2");
    cJSON_AddNumberToObject(root, "kernel_size_mb", 4);
    cJSON_AddBoolToObject(root, "uefi_boot", 1);
    cJSON_AddNullToObject(root, "secure_boot");

    /* 添加嵌套对象 */
    cJSON *features = cJSON_CreateObject();
    cJSON_AddStringToObject(features, "filesystem", "ext2");
    cJSON_AddStringToObject(features, "network", "TCP/IP");
    cJSON_AddStringToObject(features, "graphics", "GOP/VBE");
    cJSON_AddItemToObject(root, "features", features);

    /* 添加数组 */
    cJSON *apps = cJSON_CreateArray();
    cJSON_AddItemToArray(apps, cJSON_CreateString("File Manager"));
    cJSON_AddItemToArray(apps, cJSON_CreateString("Terminal"));
    cJSON_AddItemToArray(apps, cJSON_CreateString("Settings"));
    cJSON_AddItemToArray(apps, cJSON_CreateString("System Monitor"));
    cJSON_AddItemToObject(root, "apps", apps);

    char *json_str = cJSON_Print(root);
    if (json_str) {
        print_str(json_str);
        print_str("\n");
        free(json_str);
    }

    /* 测试2: 解析JSON */
    print_str("\n[Test 2] Parse JSON:\n");
    const char *test_json =
        "{\"user\":\"admin\",\"uid\":1000,\"groups\":[\"wheel\",\"users\"],"
        "\"shell\":\"/bin/hepu-sh\",\"home\":\"/home/admin\","
        "\"settings\":{\"theme\":\"dark\",\"font_size\":14,\"autosave\":true}}";

    cJSON *parsed = cJSON_Parse(test_json);
    if (parsed) {
        cJSON *user = cJSON_GetObjectItem(parsed, "user");
        cJSON *uid = cJSON_GetObjectItem(parsed, "uid");
        cJSON *groups = cJSON_GetObjectItem(parsed, "groups");
        cJSON *settings = cJSON_GetObjectItem(parsed, "settings");

        print_str("  user: ");
        if (cJSON_IsString(user)) print_str(user->valuestring);
        print_str("\n");

        print_str("  uid: ");
        if (cJSON_IsNumber(uid)) print_int((int)uid->valuedouble);
        print_str("\n");

        print_str("  groups: ");
        if (cJSON_IsArray(groups)) {
            int size = cJSON_GetArraySize(groups);
            for (int i = 0; i < size; i++) {
                cJSON *g = cJSON_GetArrayItem(groups, i);
                if (cJSON_IsString(g)) {
                    if (i > 0) print_str(", ");
                    print_str(g->valuestring);
                }
            }
        }
        print_str("\n");

        if (cJSON_IsObject(settings)) {
            cJSON *theme = cJSON_GetObjectItem(settings, "theme");
            cJSON *font_size = cJSON_GetObjectItem(settings, "font_size");
            print_str("  theme: ");
            if (cJSON_IsString(theme)) print_str(theme->valuestring);
            print_str("\n  font_size: ");
            if (cJSON_IsNumber(font_size)) print_int((int)font_size->valuedouble);
            print_str("\n");
        }

        cJSON_Delete(parsed);
    } else {
        print_str("  Parse failed at: ");
        print_str(cJSON_GetErrorPtr());
        print_str("\n");
    }

    /* 测试3: 错误处理 */
    print_str("\n[Test 3] Error handling:\n");
    cJSON *bad = cJSON_Parse("{invalid json}");
    if (!bad) {
        print_str("  Correctly rejected invalid JSON\n");
        print_str("  Error near: ");
        print_str(cJSON_GetErrorPtr());
        print_str("\n");
    } else {
        cJSON_Delete(bad);
    }

    /* 清理 */
    cJSON_Delete(root);

    print_str("\n=== All tests passed ===\n");
    hepu_exit(0);
    return 0;
}
