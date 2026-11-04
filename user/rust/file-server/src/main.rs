//! 赫普OS - Rust文件服务（完善版）
//! 第一个Rust用户态服务，通过IPC提供完整文件操作
//!
//! 请求协议：
//!   FILE_OPEN:    data = path字符串(\0结尾)
//!   FILE_READ:    data = [fd:u32][count:u32]
//!   FILE_WRITE:   data = [fd:u32][count:u32][data...]
//!   FILE_CLOSE:   data = [fd:u32]
//!   FILE_STAT:    data = path字符串(\0结尾)
//!   FILE_MKDIR:   data = path字符串(\0结尾)
//!   FILE_UNLINK:  data = path字符串(\0结尾)
//!   FILE_LSEEK:   data = [fd:u32][offset:i32][whence:u32]
//!
//! 响应协议：
//!   RESPONSE: data = [ret_code:i32][...额外数据]
//!   ERROR:    data = [error_code:i32]

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

fn print_pid(pid: u32) {
    print_int(pid as i32);
}

/// 从消息data中读取u32
fn read_u32(data: &[u8], offset: usize) -> u32 {
    if offset + 4 <= data.len() {
        u32::from_le_bytes([data[offset], data[offset+1], data[offset+2], data[offset+3]])
    } else {
        0
    }
}

/// 从消息data中读取i32
fn read_i32(data: &[u8], offset: usize) -> i32 {
    read_u32(data, offset) as i32
}

/// 从消息data中提取字符串（到\0或末尾）
fn extract_str(data: &[u8], max_len: usize) -> &str {
    let len = data.iter().take(max_len).position(|&b| b == 0).unwrap_or(max_len.min(data.len()));
    unsafe { core::str::from_utf8_unchecked(&data[..len]) }
}

/// 发送响应（带返回码）
fn send_response(client_pid: u32, ret_code: i32, extra: &[u8]) {
    let mut resp = [0u8; 256];
    resp[0..4].copy_from_slice(&ret_code.to_le_bytes());
    let extra_len = extra.len().min(252);
    resp[4..4+extra_len].copy_from_slice(&extra[..extra_len]);
    hepu::ipc_send(client_pid, hepu::IpcType::RESPONSE, &resp[..4+extra_len]);
}

/// 发送错误响应
fn send_error(client_pid: u32, error_code: i32) {
    let resp = error_code.to_le_bytes();
    hepu::ipc_send(client_pid, hepu::IpcType::ERROR, &resp);
}

#[no_mangle]
pub extern "C" fn main() -> i32 {
    print_str("\n=== Hepu OS File Server v3 (Rust) ===\n");
    print_str("PID: ");
    print_pid(hepu::getpid());
    print_str("\n");
    print_str("Supported: OPEN/READ/WRITE/CLOSE/STAT/MKDIR/UNLINK/LSEEK\n");

    // 注册服务到服务注册表
    print_str("Registering service 'file-server'...\n");
    let ret = hepu::service_register("file-server", hepu::ServiceType::FILE, 2);
    if ret == 0 {
        print_str("Service registered successfully.\n");
    } else {
        print_str("Service registration failed: ");
        print_int(ret);
        print_str("\n");
    }

    print_str("Waiting for IPC requests...\n");

    let mut request_count = 0u32;
    let mut error_count = 0u32;

    loop {
        if let Some(msg) = hepu::ipc_recv() {
            request_count += 1;
            let client_pid = msg.sender_pid;

            print_str("\n[REQ #");
            print_int(request_count as i32);
            print_str("] PID=");
            print_pid(client_pid);
            print_str(" type=");
            print_int(msg.msg_type as i32);
            print_str(" len=");
            print_int(msg.length as i32);
            print_str("\n");

            let data = &msg.data[..msg.length as usize];

            match msg.msg_type {
                // FILE_OPEN: data = path字符串
                hepu::IpcType::FILE_OPEN => {
                    let path = extract_str(data, 256);
                    print_str("  OPEN: ");
                    print_str(path);
                    print_str("\n");
                    let fd = hepu::open(path, hepu::flags::O_RDONLY, 0);
                    print_str("  -> fd=");
                    print_int(fd);
                    print_str("\n");
                    send_response(client_pid, fd, &[]);
                }

                // FILE_READ: data = [fd:u32][count:u32]
                hepu::IpcType::FILE_READ => {
                    let fd = read_i32(data, 0);
                    let count = read_u32(data, 4) as usize;
                    print_str("  READ: fd=");
                    print_int(fd);
                    print_str(" count=");
                    print_int(count as i32);
                    print_str("\n");
                    let mut buf = [0u8; 240]; // 留16字节给响应头
                    let read_count = count.min(buf.len());
                    let n = hepu::read(fd, &mut buf[..read_count]);
                    print_str("  -> read ");
                    print_int(n as i32);
                    print_str(" bytes\n");
                    if n >= 0 {
                        send_response(client_pid, n as i32, &buf[..n as usize]);
                    } else {
                        send_error(client_pid, n as i32);
                        error_count += 1;
                    }
                }

                // FILE_WRITE: data = [fd:u32][count:u32][data...]
                hepu::IpcType::FILE_WRITE => {
                    let fd = read_i32(data, 0);
                    let count = read_u32(data, 4) as usize;
                    let write_data = if 8 + count <= data.len() {
                        &data[8..8+count]
                    } else {
                        &data[8..]
                    };
                    print_str("  WRITE: fd=");
                    print_int(fd);
                    print_str(" count=");
                    print_int(write_data.len() as i32);
                    print_str("\n");
                    let n = hepu::write(fd, write_data);
                    print_str("  -> wrote ");
                    print_int(n as i32);
                    print_str(" bytes\n");
                    send_response(client_pid, n as i32, &[]);
                }

                // FILE_CLOSE: data = [fd:u32]
                hepu::IpcType::FILE_CLOSE => {
                    let fd = read_i32(data, 0);
                    print_str("  CLOSE: fd=");
                    print_int(fd);
                    print_str("\n");
                    let ret = hepu::close(fd);
                    send_response(client_pid, ret, &[]);
                }

                // FILE_STAT: data = path字符串
                hepu::IpcType::FILE_STAT => {
                    let path = extract_str(data, 256);
                    print_str("  STAT: ");
                    print_str(path);
                    print_str("\n");
                    // 简化：用access检查文件是否存在
                    let ret = hepu::access(path, 0);
                    send_response(client_pid, ret, &[]);
                }

                // FILE_MKDIR: data = path字符串
                hepu::IpcType::FILE_MKDIR => {
                    let path = extract_str(data, 256);
                    print_str("  MKDIR: ");
                    print_str(path);
                    print_str("\n");
                    let ret = hepu::mkdir(path, 0o755);
                    print_str("  -> ret=");
                    print_int(ret);
                    print_str("\n");
                    send_response(client_pid, ret, &[]);
                }

                // FILE_UNLINK: data = path字符串
                hepu::IpcType::FILE_UNLINK => {
                    let path = extract_str(data, 256);
                    print_str("  UNLINK: ");
                    print_str(path);
                    print_str("\n");
                    let ret = hepu::unlink(path);
                    send_response(client_pid, ret, &[]);
                }

                // FILE_LSEEK: data = [fd:u32][offset:i32][whence:u32]
                hepu::IpcType::FILE_LSEEK => {
                    let fd = read_i32(data, 0);
                    let offset = read_i32(data, 4);
                    let whence = read_u32(data, 8);
                    print_str("  LSEEK: fd=");
                    print_int(fd);
                    print_str(" offset=");
                    print_int(offset);
                    print_str(" whence=");
                    print_int(whence as i32);
                    print_str("\n");
                    let ret = hepu::lseek(fd, offset, whence);
                    send_response(client_pid, ret, &[]);
                }

                _ => {
                    print_str("  Unknown type\n");
                    send_error(client_pid, -1);
                    error_count += 1;
                }
            }

            // 处理20个请求后退出（测试模式）
            if request_count >= 20 {
                print_str("\n=== Server Summary ===\n");
                print_str("Requests: ");
                print_int(request_count as i32);
                print_str("\nErrors: ");
                print_int(error_count as i32);
                print_str("\nServer exiting.\n");
                break;
            }
        } else {
            hepu::yield_cpu();
        }
    }

    0
}
