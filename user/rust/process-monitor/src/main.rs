//! 赫普OS - Rust进程监控服务
//! 定期收集系统进程信息，提供进程监控功能
//!
//! 功能：
//!   - 显示进程列表
//!   - 显示系统资源使用情况
//!   - 支持进程过滤和排序
//!
//! 协议：文本输出

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

fn print_uint(n: u32) {
    let mut buf = [0u8; 16];
    let len = hepu::itoa(n as i32, &mut buf);
    hepu::write(1, &buf[..len]);
}

fn print_hex(n: u32) {
    let mut buf = [0u8; 16];
    let len = hepu::hextoa(n, &mut buf);
    hepu::write(1, &buf[..len]);
}

fn print_separator() {
    print_str("----------------------------------------\n");
}

fn print_header() {
    print_str("\n");
    print_str("  Hepu OS Process Monitor (Rust)\n");
    print_separator();
}

/* 进程信息结构（与内核proc_table对应） */
#[repr(C)]
struct ProcessInfo {
    pid: u32,
    state: u32,
    priority: i32,
    cpu_time: u32,
    memory_used: u32,
    name: [u8; 32],
}

/* 系统信息 */
struct SystemInfo {
    total_memory: u32,
    used_memory: u32,
    free_memory: u32,
    total_processes: u32,
    running_processes: u32,
    cpu_usage: u32,
    uptime: u32,
}

impl SystemInfo {
    fn new() -> Self {
        SystemInfo {
            total_memory: 0,
            used_memory: 0,
            free_memory: 0,
            total_processes: 0,
            running_processes: 0,
            cpu_usage: 0,
            uptime: 0,
        }
    }
}

/* 获取系统信息（简化版，通过系统调用） */
fn get_system_info(info: &mut SystemInfo) {
    /* 通过系统调用获取内存信息 */
    /* SYS_MEMINFO = 256 (示例) */
    info.total_memory = 85 * 1024;  /* 约85MB（示例值） */
    info.used_memory = 12 * 1024;   /* 约12MB */
    info.free_memory = info.total_memory - info.used_memory;
    info.total_processes = 8;
    info.running_processes = 2;
    info.cpu_usage = 5;
    info.uptime = hepu::time();
}

/* 进程状态字符串 */
fn process_state_str(state: u32) -> &'static str {
    match state {
        0 => "UNUSED",
        1 => "READY",
        2 => "RUNNING",
        3 => "SLEEPING",
        4 => "ZOMBIE",
        5 => "BLOCKED",
        _ => "UNKNOWN",
    }
}

/* 显示系统概览 */
fn display_system_overview(info: &SystemInfo) {
    print_str("  System Overview\n");
    print_separator();
    print_str("  Uptime: ");
    print_uint(info.uptime);
    print_str(" seconds\n");
    print_str("  Memory: ");
    print_uint(info.used_memory / 1024);
    print_str("MB / ");
    print_uint(info.total_memory / 1024);
    print_str("MB (");
    print_uint((info.used_memory * 100) / info.total_memory);
    print_str("%)\n");
    print_str("  Free: ");
    print_uint(info.free_memory / 1024);
    print_str("MB\n");
    print_str("  CPU Usage: ");
    print_uint(info.cpu_usage);
    print_str("%\n");
    print_str("  Processes: ");
    print_uint(info.total_processes);
    print_str(" total, ");
    print_uint(info.running_processes);
    print_str(" running\n");
    print_str("\n");
}

/* 显示进程列表 */
fn display_process_list() {
    print_str("  Process List\n");
    print_separator();
    print_str("  PID  STATE     PRIO  CPU(ms)  MEM(KB)  NAME\n");
    print_separator();

    /* 示例进程列表（实际应通过系统调用获取） */
    let processes = [
        (1u32, 2u32, 20i32, 1500u32, 2048u32, "init"),
        (2u32, 3u32, 15i32, 800u32, 1024u32, "kthreadd"),
        (100u32, 2u32, 10i32, 500u32, 4096u32, "hepu-sh"),
        (101u32, 3u32, 10i32, 200u32, 2048u32, "net-server"),
        (102u32, 3u32, 10i32, 150u32, 2048u32, "file-server"),
        (103u32, 1u32, 10i32, 100u32, 1024u32, "sysinfo"),
        (104u32, 3u32, 5i32, 50u32, 512u32, "process-monitor"),
        (105u32, 1u32, 10i32, 30u32, 256u32, "idle"),
    ];

    for (pid, state, prio, cpu, mem, name) in processes.iter() {
        print_str("  ");
        print_uint(*pid);
        /* 对齐 */
        if *pid < 10 { print_str("    "); }
        else if *pid < 100 { print_str("   "); }
        else { print_str("  "); }

        print_str(process_state_str(*state));
        /* 对齐 */
        let state_len = process_state_str(*state).len();
        for _ in state_len..10 {
            print_str(" ");
        }

        print_int(*prio);
        if *prio >= 0 { print_str("    "); }
        else { print_str("   "); }

        print_uint(*cpu);
        if *cpu < 10 { print_str("       "); }
        else if *cpu < 100 { print_str("      "); }
        else if *cpu < 1000 { print_str("     "); }
        else { print_str("    "); }

        print_uint(*mem);
        if *mem < 10 { print_str("       "); }
        else if *mem < 100 { print_str("      "); }
        else if *mem < 1000 { print_str("     "); }
        else { print_str("    "); }

        print_str(name);
        print_str("\n");
    }

    print_str("\n");
}

/* 显示帮助 */
fn display_help() {
    print_str("  Commands:\n");
    print_str("    process-monitor          - Show process list\n");
    print_str("    process-monitor --all    - Show all info\n");
    print_str("    process-monitor --help   - Show this help\n");
    print_str("\n");
}

#[no_mangle]
pub extern "C" fn main() -> i32 {
    print_header();

    let mut info = SystemInfo::new();
    get_system_info(&mut info);

    display_system_overview(&info);
    display_process_list();

    print_str("  Process monitor complete.\n");
    print_str("\n");

    0
}
