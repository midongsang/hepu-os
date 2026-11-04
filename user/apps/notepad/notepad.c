/* ============================================================================
 * 赫普OS - 记事本应用
 * 支持创建、查看、编辑文本笔记
 * ========================================================================== */

#include <hepu/syscall.h>
#include <hepu/stdio.h>
#include <hepu/string.h>

#define MAX_NOTES 10
#define MAX_TITLE 32
#define MAX_CONTENT 256

typedef struct {
    char title[MAX_TITLE];
    char content[MAX_CONTENT];
    int created;  /* 时间戳(秒) */
    int used;
} note_t;

static note_t notes[MAX_NOTES];
static int note_count = 0;

static void note_create(const char *title, const char *content)
{
    if (note_count >= MAX_NOTES) {
        printf("  [错误] 笔记已满(%d条)\n", MAX_NOTES);
        return;
    }
    note_t *n = &notes[note_count];
    strncpy(n->title, title, MAX_TITLE - 1);
    strncpy(n->content, content, MAX_CONTENT - 1);
    n->created = note_count * 3600;  /* 模拟时间戳 */
    n->used = 1;
    note_count++;
    printf("  [已创建] #%d %s\n", note_count, title);
}

static void note_list(void)
{
    if (note_count == 0) {
        printf("  (暂无笔记)\n");
        return;
    }
    for (int i = 0; i < note_count; i++) {
        printf("  #%d [%s] %s\n", i + 1, notes[i].title,
               notes[i].content[0] ? notes[i].content : "(空)");
    }
}

static void note_view(int idx)
{
    if (idx < 1 || idx > note_count) {
        printf("  [错误] 笔记不存在\n");
        return;
    }
    note_t *n = &notes[idx - 1];
    printf("\n  ========================================\n");
    printf("  标题: %s\n", n->title);
    printf("  创建: 系统启动后+%d秒\n", n->created);
    printf("  ========================================\n");
    printf("  %s\n", n->content);
    printf("  ========================================\n");
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    printf("========================================\n");
    printf("      赫普OS 记事本\n");
    printf("========================================\n\n");

    printf("[创建示例笔记]\n");
    note_create("欢迎使用赫普OS",
        "这是你的第一条笔记。赫普OS记事本支持创建、查看、列表功能。\n"
        "系统归人，数据归己，自由可控，极致纯净。");
    note_create("待办事项",
        "1. 体验创变自由模式\n2. 配置多语言输入法\n3. 探索.hug应用生态\n4. 设置多用户隐私空间");
    note_create("快捷键备忘",
        "系统调用通过int 0x80触发\n应用格式为.hug\n内核版本可通过hepu_info()获取");
    note_create("系统特色",
        "微内核架构\n内核级后台冻结\n设备标识随机化\n弹性自由桌面\nAI通知分级\nGPLv3开源");

    printf("\n[笔记列表] 共%d条\n", note_count);
    note_list();

    printf("\n[查看笔记 #2]\n");
    note_view(2);

    printf("\n[查看笔记 #1]\n");
    note_view(1);

    printf("\n========================================\n");
    printf("  记事本运行完毕\n");
    printf("========================================\n");

    return 0;
}
