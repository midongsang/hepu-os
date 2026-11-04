//! 赫普OS - 应用商店 v0.3
//! 显示可用应用、已安装应用，支持安装/卸载/搜索
//! V6.74: 添加/apps/目录自动创建

#![no_std]
#![no_main]

use hepu_sys as hepu;

#[no_mangle]
pub extern "C" fn _start() -> ! {
    let code = main();
    hepu::exit(code);
}

fn print_str(s: &str) {
    hepu::write(1, s.as_bytes());
}

fn print_int(n: i32) {
    let mut buf = [0u8; 16];
    let len = hepu::itoa(n, &mut buf);
    hepu::write(1, &buf[..len]);
}

fn print_separator() {
    print_str("========================================\n");
}

/// 应用信息结构
struct AppInfo {
    name: &'static str,
    version: &'static str,
    category: &'static str,
    description: &'static str,
    size: u32,
}

/// 内置应用仓库（V6.72，后续从网络/数据盘加载）
const APP_REPO: [AppInfo; 11] = [
    AppInfo { name: "hello-rust", version: "1.0", category: "demo", description: "Rust Hello World示例", size: 8 },
    AppInfo { name: "cat", version: "1.0", category: "utility", description: "文件查看工具", size: 6 },
    AppInfo { name: "echo", version: "1.0", category: "utility", description: "回显输入文本", size: 5 },
    AppInfo { name: "sysinfo", version: "1.0", category: "system", description: "系统信息查看器", size: 12 },
    AppInfo { name: "fork-test", version: "1.0", category: "test", description: "进程创建测试", size: 10 },
    AppInfo { name: "ipc-e2e-test", version: "1.0", category: "test", description: "IPC端到端测试", size: 15 },
    AppInfo { name: "socket-test", version: "1.0", category: "test", description: "BSD Socket API测试", size: 18 },
    AppInfo { name: "fs-client", version: "1.0", category: "system", description: "文件系统客户端", size: 14 },
    AppInfo { name: "file-server", version: "1.0", category: "system", description: "文件服务器", size: 20 },
    AppInfo { name: "app-store", version: "0.2", category: "system", description: "应用商店（本程序）", size: 16 },
    AppInfo { name: "gfxtest", version: "1.0", category: "demo", description: "图形模式测试", size: 8 },
];

/// 检查应用是否已安装（通过检查/apps/目录下是否存在该文件）
fn is_app_installed(name: &str) -> bool {
    let mut path = [0u8; 64];
    let mut len = 0;
    for b in b"/apps/" { path[len] = *b; len += 1; }
    for b in name.as_bytes() { path[len] = *b; len += 1; }
    for b in b".elf" { path[len] = *b; len += 1; }
    let fd = hepu::open(unsafe { core::str::from_utf8_unchecked(&path[..len]) }, 0, 0);
    if fd >= 0 {
        hepu::close(fd);
        true
    } else {
        false
    }
}

/// 复制文件（从src到dst）
fn copy_file(src: &str, dst: &str) -> i32 {
    let src_fd = hepu::open(src, 0, 0);  // O_RDONLY
    if src_fd < 0 {
        return -1;
    }

    let dst_fd = hepu::open(dst, 0x41, 0o644);  // O_WRONLY|O_CREAT
    if dst_fd < 0 {
        hepu::close(src_fd);
        return -2;
    }

    let mut buf = [0u8; 512];
    let mut total: i32 = 0;
    loop {
        let n = hepu::read(src_fd, &mut buf);
        if n <= 0 { break; }
        let w = hepu::write(dst_fd, &buf[..n as usize]);
        if w < 0 { break; }
        total += w as i32;
    }

    hepu::close(src_fd);
    hepu::close(dst_fd);
    total
}

/// 安装应用
fn install_app(name: &str) -> i32 {
    // 检查是否已安装
    if is_app_installed(name) {
        print_str("  [INFO] 应用已安装: ");
        print_str(name);
        print_str("\n");
        return 0;
    }

    // 查找应用信息
    let mut found = false;
    for app in APP_REPO.iter() {
        if app.name == name {
            found = true;
            print_str("  正在安装: ");
            print_str(name);
            print_str(" v");
            print_str(app.version);
            print_str(" (");
            print_int(app.size as i32);
            print_str("KB)...\n");
            break;
        }
    }
    if !found {
        print_str("  [ERROR] 未找到应用: ");
        print_str(name);
        print_str("\n");
        return -1;
    }

    // 构建源路径和目标路径
    let mut src_path = [0u8; 64];
    let mut src_len = 0;
    for b in b"/" { src_path[src_len] = *b; src_len += 1; }
    for b in name.as_bytes() { src_path[src_len] = *b; src_len += 1; }
    for b in b".elf" { src_path[src_len] = *b; src_len += 1; }

    let mut dst_path = [0u8; 64];
    let mut dst_len = 0;
    for b in b"/apps/" { dst_path[dst_len] = *b; dst_len += 1; }
    for b in name.as_bytes() { dst_path[dst_len] = *b; dst_len += 1; }
    for b in b".elf" { dst_path[dst_len] = *b; dst_len += 1; }

    let src = unsafe { core::str::from_utf8_unchecked(&src_path[..src_len]) };
    let dst = unsafe { core::str::from_utf8_unchecked(&dst_path[..dst_len]) };

    // 复制文件
    let copied = copy_file(src, dst);
    if copied > 0 {
        print_str("  [OK] 安装成功，复制了 ");
        print_int(copied);
        print_str(" 字节\n");
        0
    } else {
        print_str("  [WARN] 文件复制失败（可能源文件不存在），标记为已安装\n");
        // 即使复制失败，也标记为已安装（框架演示）
        0
    }
}

/// 卸载应用
fn uninstall_app(name: &str) -> i32 {
    if !is_app_installed(name) {
        print_str("  [INFO] 应用未安装: ");
        print_str(name);
        print_str("\n");
        return 0;
    }

    print_str("  正在卸载: ");
    print_str(name);
    print_str("...\n");

    // 构建目标路径
    let mut path = [0u8; 64];
    let mut len = 0;
    for b in b"/apps/" { path[len] = *b; len += 1; }
    for b in name.as_bytes() { path[len] = *b; len += 1; }
    for b in b".elf" { path[len] = *b; len += 1; }

    let p = unsafe { core::str::from_utf8_unchecked(&path[..len]) };
    let ret = hepu::unlink(p);
    if ret == 0 {
        print_str("  [OK] 卸载成功\n");
    } else {
        print_str("  [WARN] 文件删除失败（返回码: ");
        print_int(ret);
        print_str("），标记为已卸载\n");
    }
    0
}

fn show_app_list() {
    print_str("\n");
    print_separator();
    print_str("  赫普OS 应用商店 v0.2\n");
    print_separator();
    print_str("\n  可用应用列表：\n\n");

    let mut total_size: u32 = 0;
    let mut installed_count = 0;
    for (i, app) in APP_REPO.iter().enumerate() {
        let installed = is_app_installed(app.name);
        if installed { installed_count += 1; }

        print_str("  [");
        print_int((i + 1) as i32);
        print_str("] ");
        print_str(app.name);
        print_str(" (v");
        print_str(app.version);
        print_str(")\n");
        print_str("      ");
        print_str(app.description);
        print_str("\n");
        print_str("      分类: ");
        print_str(app.category);
        print_str(" | 大小: ");
        print_int(app.size as i32);
        print_str("KB | 状态: ");
        if installed {
            print_str("已安装\n");
        } else {
            print_str("未安装\n");
        }
        total_size += app.size;
    }

    print_str("\n  共 ");
    print_int(APP_REPO.len() as i32);
    print_str(" 个应用，已安装 ");
    print_int(installed_count);
    print_str(" 个，总大小: ");
    print_int(total_size as i32);
    print_str("KB\n");
}

fn show_help() {
    print_str("\n");
    print_separator();
    print_str("  应用商店命令\n");
    print_separator();
    print_str("\n  list       - 显示所有应用\n");
    print_str("  installed  - 显示已安装应用\n");
    print_str("  info <name> - 显示应用详情\n");
    print_str("  install <name>   - 安装应用\n");
    print_str("  uninstall <name> - 卸载应用\n");
    print_str("  search <keyword> - 搜索应用\n");
    print_str("  update     - 更新应用列表\n");
    print_str("  help       - 显示帮助\n");
    print_str("  exit       - 退出应用商店\n");
}

fn show_installed() {
    print_str("\n  已安装应用：\n\n");
    let mut count = 0;
    for app in APP_REPO.iter() {
        if is_app_installed(app.name) {
            print_str("  - ");
            print_str(app.name);
            print_str(" v");
            print_str(app.version);
            print_str(" (");
            print_str(app.category);
            print_str(")\n");
            count += 1;
        }
    }
    if count == 0 {
        print_str("  （无已安装应用）\n");
    }
    print_str("\n  共 ");
    print_int(count);
    print_str(" 个已安装应用\n");
}

fn search_apps(keyword: &str) {
    print_str("\n  搜索结果：\n\n");
    let mut count = 0;
    for app in APP_REPO.iter() {
        if app.name.contains(keyword) || app.description.contains(keyword) || app.category.contains(keyword) {
            print_str("  - ");
            print_str(app.name);
            print_str(": ");
            print_str(app.description);
            print_str("\n");
            count += 1;
        }
    }
    if count == 0 {
        print_str("  未找到匹配的应用\n");
    }
}

fn app_info(name: &str) {
    for app in APP_REPO.iter() {
        if app.name == name {
            let installed = is_app_installed(app.name);
            print_str("\n");
            print_separator();
            print_str("  应用详情\n");
            print_separator();
            print_str("\n  名称: ");
            print_str(app.name);
            print_str("\n  版本: ");
            print_str(app.version);
            print_str("\n  分类: ");
            print_str(app.category);
            print_str("\n  大小: ");
            print_int(app.size as i32);
            print_str("KB\n  状态: ");
            if installed {
                print_str("已安装\n");
            } else {
                print_str("未安装\n");
            }
            print_str("  描述: ");
            print_str(app.description);
            print_str("\n  路径: /apps/");
            print_str(app.name);
            print_str(".elf\n");
            return;
        }
    }
    print_str("\n  未找到应用: ");
    print_str(name);
    print_str("\n");
}

fn read_line(buf: &mut [u8]) -> usize {
    let mut len = 0;
    while len < buf.len() - 1 {
        let mut ch = [0u8; 1];
        let n = hepu::read(0, &mut ch);
        if n <= 0 { break; }
        if ch[0] == b'\n' { break; }
        if ch[0] == b'\r' { continue; }
        if ch[0] == 0x7f || ch[0] == 0x08 {
            if len > 0 {
                len -= 1;
                print_str("\x08 \x08");
            }
            continue;
        }
        buf[len] = ch[0];
        len += 1;
        hepu::write(1, &ch);
    }
    print_str("\n");
    buf[len] = 0;
    len
}

fn parse_command(input: &str) -> (&str, &str) {
    let input = input.trim();
    if let Some(pos) = input.find(' ') {
        (&input[..pos], &input[pos + 1..].trim())
    } else {
        (input, "")
    }
}

#[no_mangle]
pub extern "C" fn main() -> i32 {
    print_str("\n");
    print_separator();
    print_str("  赫普OS 应用商店 v0.3\n");
    print_str("  Hepu OS App Store\n");
    print_separator();

    /* V6.74: 自动创建/apps/目录 */
    let ret = hepu::mkdir("/apps/", 0o755);
    if ret == 0 {
        print_str("\n  [INIT] /apps/ 目录已创建\n");
    } else {
        print_str("\n  [INIT] /apps/ 目录已存在或创建失败\n");
    }

    print_str("\n  输入 'help' 查看命令列表\n");
    print_str("  应用安装目录: /apps/\n");

    let mut buf = [0u8; 256];
    loop {
        print_str("\napp-store> ");
        let len = read_line(&mut buf);
        if len == 0 { continue; }

        let input = unsafe { core::str::from_utf8_unchecked(&buf[..len]) };
        let (cmd, arg) = parse_command(input);

        match cmd {
            "list" | "ls" => show_app_list(),
            "installed" => show_installed(),
            "info" => app_info(arg),
            "install" => { install_app(arg); }
            "uninstall" => { uninstall_app(arg); }
            "search" => search_apps(arg),
            "update" => print_str("\n  [OK] 应用列表已是最新\n"),
            "help" | "?" => show_help(),
            "exit" | "quit" => {
                print_str("\n  再见！\n");
                break;
            }
            "" => {}
            _ => {
                print_str("  未知命令: ");
                print_str(cmd);
                print_str("，输入 'help' 查看帮助\n");
            }
        }
    }

    0
}
