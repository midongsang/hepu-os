//! 赫普OS 日志服务 (log-service)
//!
//! 收集、存储和查询系统日志
//! V7.94: 初始实现

#![no_std]
#![no_main]

use hepu_sys::*;

/// 日志条目
#[derive(Clone, Copy)]
struct LogEntry {
    timestamp: u32,
    level: u8,       // 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR
    source: [u8; 16],
    message: [u8; 96],
}

/// 日志缓冲区
const LOG_BUFFER_SIZE: usize = 128;
static mut LOG_BUFFER: [LogEntry; LOG_BUFFER_SIZE] = [LogEntry {
    timestamp: 0,
    level: 0,
    source: [0; 16],
    message: [0; 96],
}; LOG_BUFFER_SIZE];
static mut LOG_HEAD: usize = 0;
static mut LOG_COUNT: usize = 0;

/// 日志级别字符串
const LEVEL_STR: [&str; 4] = ["DEBUG", "INFO ", "WARN ", "ERROR"];

#[no_mangle]
pub extern "C" fn _start() -> ! {
    print("[log-service] starting...\n");

    // 初始化日志缓冲区
    unsafe {
        LOG_HEAD = 0;
        LOG_COUNT = 0;
    }

    // 添加启动日志
    log_add(1, "log-service", "Log service initialized");

    print("[log-service] ready\n");

    // 主循环
    loop {
        sleep_ms(1000);
    }
}

/// 添加日志条目
fn log_add(level: u8, source: &str, message: &str) {
    unsafe {
        let entry = &mut LOG_BUFFER[LOG_HEAD];
        entry.timestamp = time();
        entry.level = level;

        // 复制source
        let src_bytes = source.as_bytes();
        let src_len = core::cmp::min(src_bytes.len(), 15);
        entry.source[..src_len].copy_from_slice(&src_bytes[..src_len]);
        entry.source[src_len] = 0;

        // 复制message
        let msg_bytes = message.as_bytes();
        let msg_len = core::cmp::min(msg_bytes.len(), 95);
        entry.message[..msg_len].copy_from_slice(&msg_bytes[..msg_len]);
        entry.message[msg_len] = 0;

        LOG_HEAD = (LOG_HEAD + 1) % LOG_BUFFER_SIZE;
        if LOG_COUNT < LOG_BUFFER_SIZE {
            LOG_COUNT += 1;
        }
    }
}
