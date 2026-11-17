# 4. 定义但未调用的关键函数（安全/权限）：死代码 vs 预留接口

基线：`5f93c2d`。

## 4.1 方法与口径

- 扫描范围：`kernel/` 下 auth、security、capability、domain、mac、seccomp、permission、secure_boot、biometric、keystore 命名模块。
- 引用计数口径：函数名按词法 grep 全 `kernel/`，排除定义所在文件；计数为 0 = 仓库内无其他调用方。
- **重要限定**：32 位 Makefile 用 `--gc-sections`（`Makefile:27`），未引用的目标段会在链接期丢弃；静态 0 引用 ≠ 已编译进最终镜像。是否真被链接进内核，属 **待验证**（需符号表/编译产物）。
- 分类：
  - **预留接口**：有框架初始化路径接入、或存在 syscall/应用层接入意图，但个别子函数未被走到。
  - **死代码倾向**：无初始化接入、无 syscall 登记、无测试调用，仓库内完全不可达。
  - 未在任何 syscall 分发表查到这些 `cap_*`/`sec_*`/`mac_*`/`prm_*`/`keystore_*`/`biometric_*` 名称的表项（`syscall64_full.c`、`syscall.c` grep 无命中）。

## 4.2 64 位：认证与沙箱

### 4.2.1 `auth64_verify_password` —— 部分接线，登录路径未接（已确认）

| 项 | 证据 |
|----|------|
| 定义 | `kernel/arch/x86_64/auth64.c:321` `int auth64_verify_password(const char *user, const char *password)`：取持久化 hash，sha256 计算后比较，`:343-352` |
| 唯一内部调用方 | `auth64_elevate()` `:357`（提权"su"路径） |
| 外部调用方 | 0（全 `kernel/` grep 除 `auth64.c` 无命中） |
| 会话/登录路径 | 无。开机后只调 `auth64_is_configured()` 判是否首启（`kernel/arch/x86_64/desktop64.c:242-246`）；已配置账号直接进入桌面，不弹密码；`g_role` 由 `auth64_init()` 从配置直接加载（`auth64.c:255-274`） |
| 判定 | **预留接口（提权路径已接，登录/锁屏路径未接）**。密码只在提权时校验，开机进入桌面不校验 |
| 影响 | 拿到会话即可使用持久化账号角色；`deelevate`（`:372`）无密码回退保护 |
| main 复核 | `3d0bd23` 上仍未接登录/锁屏路径：`auth64_verify_password` 唯一调用方仍是 `auth64_elevate`（`auth64.c:357`）；函数签名在 main 上为 `(const char *pass)`（基线为 `(user, password)`）。状态未修，见报告 05 §5.9 第 1 项 |

其余 auth64 可达性：`auth64_create_account`（`:311`）由 OOBE 调用（`oobe64.c:137` 写配置）；`auth64_write_config/read_config`（`:124,173`）OOBE/l10n 使用；`auth64_deelevate`（`:372`）、`auth64_current_user/current_role`（`:380`）有桌面/提权面板调用。

### 4.2.2 seccomp 公开包装未接 syscall 路径（已确认）

定义文件：`kernel/arch/x86_64/seccomp64.c`；接入路径：`seccomp64_init`（`:116`，`kernel_main_64.c:443`）、`seccomp64_set_filter`（`:143`，syscall `:987-1017`）、`seccomp64_guard_current`（`:438`，syscall 分发 `:3012`）、`seccomp64_get_mode`（`:152`）、`seccomp64_reset`（`:160`，进程复用 `user_process.c:2415-2418`）。

| 函数 | 行号 | 外部引用 | 判定 |
|------|------|----------|------|
| `seccomp64_allow` | `:125` | 0 | 预留包装：与 `deny` 配对的白名单 API，未从任何路径调用 |
| `seccomp64_deny` | `:130` | 0 | 同上 |
| `seccomp64_check` | `:419` | 0 | 预留：按过滤器查某号是否被拒，`guard_current` 走的是内部 `check_filter` 而非此包装 |
| `seccomp64_check_current` | `:426` | 0 | 预留：当前进程过滤器检查，未接分发路径 |

### 4.2.3 安全规则组合查询未接线（已确认）

定义文件：`kernel/arch/x86_64/security64.c`。

| 函数 | 行号 | 外部引用 | 判定 |
|------|------|----------|------|
| `sec_cap_check` | `:146` | 0 | 仅内部被 `sec_check_both:174`、`sec_cap_only:193` 调用 |
| `sec_check_both` | `:161` | 0 | 预留组合策略（域+能力双满足），无调用方 |
| `sec_rule_only` | `:183` | 0 | 预留域规则单查 |
| `sec_cap_only` | `:189` | 0 | 预留能力单查 |
| `sec64_audit` | `:212` | 0 | 预留审计记录写入；当前路径不产生审计事件 |

已接入对照：`sec_register_domain/type/add_rule`、`sec_check_access`、`sec_add_rule`、`sec_rule` 读取类（`:85,92,106,113` 等）有内核/设备层调用。

### 4.2.4 能力/域框架：回收与列举接口不可达（已确认）

64 位实现：`kernel/arch/x86_64/capability64.c`、`domain64.c`，经 `capability64.h:20-29` 宏映射到统一 `cap_*` 名称。

| 统一名（64 实现） | 行号 | 外部引用 | 判定 |
|-------------------|------|----------|------|
| `cap_mint`（`cap64_mint`） | `capability64.c:102` | 1（`hepu_dev.c:29`） | 已接入设备分配路径 |
| `cap_derive`（`cap64_derive`） | `:110` | 1 | 同上 |
| `cap_release`（`cap64_release`） | `:138` | 1 | 同上 |
| `cap_check`（`cap64_check`） | `:124` | 10（设备/权限层） | 已接入 |
| `cap_revoke`（`cap64_revoke`） | `:117` | 0 | **死代码倾向**：无撤销路径，能力句柄只增不减 |
| `cap_count`/`cap_list` | `:83/:88` | 2（`shell64.c:681` 列表显示） | 部分接入（只读列举） |
| `cap_list_domain`（`cap64_list_domain`） | `:146` | 0 | 死代码倾向 |
| `domain64_create` | `domain64.c:27` | 0 | 死代码倾向：域只能由注册表预置，无运行时创建 |
| `domain64_get_by_type` | `:42` | 0 | 死代码倾向 |
| `domain64_count` | `:15` | 0（`domain64_list:20` 同） | 列举接口未接 |

## 4.3 32 位 / 共享：未接线的安全框架

下列文件经顶层 Makefile `find kernel -name "*.c"` 编入 32 位构建（`Makefile:71`）；64 位显式清单未包含它们（`x86_64/Makefile` C_SOURCES）。

### 4.3.1 密钥库与生物识别（`kernel/security/security2.c`）

| 函数 | 行号 | 外部引用 | 判定 |
|------|------|----------|------|
| `keystore_generate_key` | `:61` | 内部 `:51`（init 自动生成 default） | 框架初始化自调用 |
| `keystore_get_key` | `:95` | 0 | 死代码倾向：取钥路径不存在，密钥库写入后无人读取 |
| `keystore_delete_key` | `:107` | 0 | 死代码倾向 |
| `keystore_list_keys` | `:121` | 0 | 死代码倾向 |
| `keystore_has_key` | `:131` | 0 | 死代码倾向 |
| `biometric_enroll` | `:282` | 0 | 死代码倾向：无录入入口 |
| `biometric_verify` | `:304` | 0 | **安全关键未接线**：生物特征校验函数写好但登录/解锁路径不调用 |
| `biometric_remove` | `:319` | 0 | 死代码倾向 |
| `biometric_list` | `:328` | 0 | 死代码倾向 |
| `biometric_get_status` | `:338` | 0 | 死代码倾向 |
| `biometric_cancel` | `:348` | 0 | 死代码倾向 |

### 4.3.2 策略 / MAC / 域 / 能力（`kernel/security/`）

| 函数 | 行号 | 外部引用 | 判定 |
|------|------|----------|------|
| `sec_load_policy` | `security.c:277` | 0 | 预留：策略文件加载未接启动路径 |
| `sec_get_rule` | `security.c:347` | 0 | 死代码倾向 |
| `sec_check_pid_file` | `security.c:355` | 0 | 预留：按 pid+文件的访问校验，未接 syscall |
| `mac_rule_get` | `mac.c:276` | 0 | 死代码倾向 |
| `mac_rule_clear` | `mac.c:285` | 0 | 死代码倾向 |
| `domain_create` | `domain.c:61` | 0 | 死代码倾向（同 64 位侧） |
| `domain_list` | `domain.c:126` | 0 | 死代码倾向 |
| `domain_refresh_stats` | `domain.c:167` | 0 | 死代码倾向 |
| `cap_revoke` | `capability.c:111` | 0 | 死代码倾向 |
| `cap_count`/`cap_list` | `:171/:181` | 0 | 死代码倾向 |
| `cap_list_domain` | `:193` | 0 | 死代码倾向 |

### 4.3.3 权限管理器（`kernel/arch/x86/permission_manager.c`）

已被 boot 自测子集走到：`prm_app_register`、`prm_request_permission`、`prm_grant_permission`、`prm_check_permission`、`prm_grant_onetime`（自测见 `permission_manager_ext.c:137-148`）。下列接口外部引用 0，且未在 32 位 syscall 表登记：

| 函数 | 行号 | 判定 |
|------|------|------|
| `prm_app_unregister` | `:165` | 预留：应用注销路径 |
| `prm_get_permission_status` | `:282` | 预留：状态查询未接 |
| `prm_onetime_expired` | `:298` | 死代码倾向 |
| `prm_extend_onetime` | `:310` | 死代码倾向 |
| `prm_set_fuzzy_location` | `:317` | 预留：模糊定位策略 |
| `prm_get_fuzzy_location` | `:325` | 死代码倾向 |
| `prm_fuzzy_location_coords` | `:332` | 死代码倾向 |
| `prm_set_empty_auth` | `:342` | 预留：空授权开关 |
| `prm_get_empty_data` | `:350` | 死代码倾向 |
| `prm_log_access` | `:358` | 死代码倾向：权限使用日志未产生 |
| `prm_get_logs` | `:375` | 死代码倾向 |
| `prm_get_app_logs` | `:386` | 死代码倾向 |

## 4.4 待验证项

1. **链接期是否存活**：上述 0 引用函数经 `--gc-sections` 后是否仍在内核镜像中；验证方法：32 位 `build_v250/hepu-kernel.bin`、64 位 `build_x86_64/hepu-kernel64.bin` 的符号表 `nm`/`objdump -t` 查符号是否保留。
2. **预留接口是否有用户态调用**：当前只查了 `kernel/`；`user/apps/` 经 syscall 号而非函数名调用，需对照 `tools/gen_syscall_doc.py` 产出的 syscall 表确认是否有未被 C 符号 grep 到的号通道。
3. `kernel/security/*.c` 是否真的被 64 位构建排除：64 位 Makefile 为显式清单，未见到 `find kernel/security` 通配；需一次 64 位编译确认对象列表。
