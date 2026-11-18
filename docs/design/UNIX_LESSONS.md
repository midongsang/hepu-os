# 从 Unix 借来的三件事：可落地的接口方案

本文不是综述，是三条**能直接改代码**的设计。每条都给：对外契约、数据结构、落地步骤、验收标准。
原则：**抽象层先定死，功能慢慢加**。

---

## 一、VFS ops 表冻结为对外契约

### 问题
驱动/文件系统各自实现读写，语义不一致（谁负责边界检查、谁负责拷贝、短读写怎么算），
新增一种设备就要改调用方。

### 对外契约（v1，冻结）
```c
struct vfs_file_ops {
    int  (*open)(struct vnode *v, struct file *f, uint32_t flags);
    int  (*close)(struct file *f);
    long (*read)(struct file *f, void *ubuf, uint64_t len, uint64_t *done);
    long (*write)(struct file *f, const void *ubuf, uint64_t len, uint64_t *done);
    long (*ioctl)(struct file *f, uint64_t cmd, void *uarg);
    int  (*stat)(struct vnode *v, struct vfs_stat *out);
};
```

**四条铁律**
1. **ops 里的 `ubuf` 永远是用户态指针，内核负责校验与拷贝**；ops 实现不得直接解引用（不得出现 `*(uint32_t*)ubuf`）。
2. **短读写必须通过 `*done` 回报**，返回值只表示"成功/失败/被信号打断"。
3. **字段只增不改**：v1 之后新增能力走 `ioctl` 或新增函数指针并追加在结构体尾部。
4. **ops 表在编译期常量**（`const struct vfs_file_ops`），不允许运行时替换函数指针。

### 落地步骤
1. 现状盘点：列出仓库里所有"直接读写用户指针"的设备/文件系统代码路径。
2. 先给 VFS 加一层 `copy_from_user/copy_to_user`（已有则复用），ops 实现统一改走它。
3. 加一个静态检查：`grep` 出 `vfs_file_ops` 实现里出现 `ubuf` 直接解引用的位置，作为 CI 门禁。
4. 驱动侧只注册 ops 表，**不允许驱动自己注册 syscall**。

### 验收
- 新增一个"只读设备"驱动，只需要实现 `open/read/close` 三个函数，不改任何调用方代码；
- CI 静态检查通过（0 处直接解引用 `ubuf`）。

---

## 二、权限校验收口到一个点 + 每次拒绝留痕

### 问题
权限校验散落在 syscall 入口、IPC 服务、文件系统各处，每处各写一套判断；
拒绝时没有任何记录，出事无法追。

### 对外契约
```c
/* 唯一授权入口。返回 0 = 允许；-EPERM = 拒绝（并写审计） */
int cap_check(struct cap_handle *h,
              uint32_t required_rights,
              const struct cap_object *obj,
              const char *op_name);
```

**规则**
1. **所有"能不能做"的判断，只能调用它**。syscall 入口、IPC 消息分发、服务内部敏感操作，一律走它。
2. **拒绝即审计**：拒绝时写一条定长记录到审计环（见下），**允许时不写**（避免刷爆）。
3. **能力句柄一次一用、可撤销**：句柄本身带对象引用与权限位，服务持有句柄才能操作对象，不持有就无权。

### 审计环（定长、无锁、可被 diag 读取）
```c
struct audit_rec {
    uint64_t ts;          /* 单调时钟 */
    uint32_t pid;         /* 主体 */
    uint32_t rights;      /* 请求的权限位 */
    uint32_t obj_id;      /* 目标对象 */
    uint16_t op_id;       /* 操作编号（编译期枚举） */
    uint8_t  verdict;     /* 0=允许 1=拒绝 */
    uint8_t  reason;      /* 拒绝原因码 */
};
/* 256 条环形缓冲；写满覆盖最旧；读取由 diag 服务提供 */
```

### 落地步骤
1. 定义 `cap_rights` 枚举（READ/WRITE/EXEC/NET/MOUNT…），先只做实际用到的。
2. 把现有分散校验点逐个替换成 `cap_check`，每替换一处就在 `op_id` 枚举里登记。
3. 加 CI 检查：`grep` 出"直接比较 uid/flag 做授权"的位置，要求清零。
4. diag 服务加一个查询接口：按 pid 或按对象拉最近 N 条拒绝记录。

### 验收
- 故意用无权句柄调用敏感操作 → 返回 `-EPERM`，且 diag 里能看到这条拒绝记录（含 pid/op/对象）；
- 全仓库 0 处绕过 `cap_check` 的授权判断。

---

## 三、服务崩溃现场 + 结构化状态

### 问题
watchdog 重启服务，但"为什么重启"没有任何信息；只看到"服务又挂了"。

### 数据结构（崩溃时由服务/内核写入，固定共享页）
```c
struct crash_ctx {
    uint32_t magic;        /* 0x43524348 'CRCH' */
    uint32_t pid;
    uint64_t pc;           /* 崩溃点 */
    uint64_t sp;
    uint64_t last_syscall; /* 系统调用号 */
    uint64_t last_arg0;
    uint64_t last_ipc_tag; /* 最后一次 IPC 的 tag */
    uint64_t regs[16];     /* 关键寄存器快照 */
    char     msg[96];      /* 人类可读的一行 */
};
```

### 结构化状态查询（不用文本解析）
```
diag.query(service_name) -> {
   name, state(0=run,1=degraded,2=restarting,3=dead),
   restarts, uptime_ms, last_crash{ pc, last_syscall, msg, ts }
}
```
返回**二进制结构**（或严格键值），**不做文本 grep** —— Unix 的 `/proc` 文本接口是历史包袱，别学。

### 落地步骤
1. 内核在创建服务时分配一页 `crash_ctx`，映射到服务只写、diag 只读。
2. 服务在 `_exit`/异常入口填 `last_syscall`/`last_ipc_tag`/`msg`（每进一次 syscall 更新一次，开销极低）。
3. watchdog 重启前先读 `crash_ctx`，把 `last_crash` 存进服务表；重启次数自增。
4. diag 暴露查询接口；桌面"系统状态"面板可直接消费。

### 验收
- 人为让某服务空指针崩溃 → 重启后 `diag.query` 能给出 `pc`、`last_syscall`、`msg`；
- 重复崩溃 3 次后，`restarts=3` 且 `last_crash` 为最近一次。

---

## 四条通用纪律（贯穿以上三项）

1. **接口冻结，实现自由**：对外契约一旦发布，只增不改。
2. **校验集中**：所有"能不能"的判断只有一个函数。
3. **失败可观测**：任何拒绝/崩溃都必须留下机器可读的痕迹。
4. **不做提权二进制**：需要更高权限时，走显式能力授予，而不是"给这个程序多一点权利"。
