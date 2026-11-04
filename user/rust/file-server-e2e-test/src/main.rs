//! 赫普OS - 文件服务跨进程端到端测试
//! 验证：fork子进程作为服务端 + 服务发现 + 跨进程IPC通信
//!
//! 流程：
//!   1. fork子进程
//!   2. 子进程：注册"e2e-file-server"服务，处理请求后退出
//!   3. 父进程：等待服务注册，通过服务名查找，执行OPEN/READ/CLOSE测试

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

fn read_u32(data: &[u8], offset: usize) -> u32 {
    if offset + 4 <= data.len() {
        u32::from_le_bytes([data[offset], data[offset+1], data[offset+2], data[offset+3]])
    } else {
        0
    }
}

fn read_i32(data: &[u8], offset: usize) -> i32 {
    read_u32(data, offset) as i32
}

fn extract_str(data: &[u8], max_len: usize) -> &str {
    let len = data.iter().take(max_len).position(|&b| b == 0).unwrap_or(max_len.min(data.len()));
    unsafe { core::str::from_utf8_unchecked(&data[..len]) }
}

/// 子进程：文件服务端
fn run_server() -> i32 {
    print_str("\n  [SERVER] starting, PID=");
    print_pid(hepu::getpid());
    print_str("\n");

    // 注册服务
    print_str("  [SERVER] registering service 'e2e-file-server'...\n");
    let ret = hepu::service_register("e2e-file-server", hepu::ServiceType::FILE, 1);
    if ret != 0 {
        print_str("  [SERVER] FAILED to register service: ");
        print_int(ret);
        print_str("\n");
        return -1;
    }
    print_str("  [SERVER] service registered, waiting for requests...\n");

    let mut request_count = 0u32;

    // 处理5个请求后退出
    loop {
        if let Some(msg) = hepu::ipc_recv() {
            request_count += 1;
            let client_pid = msg.sender_pid;
            let data = &msg.data[..msg.length as usize];

            print_str("  [SERVER] req#");
            print_int(request_count as i32);
            print_str(" type=");
            print_int(msg.msg_type as i32);
            print_str(" from PID=");
            print_pid(client_pid);
            print_str("\n");

            match msg.msg_type {
                hepu::IpcType::FILE_OPEN => {
                    let path = extract_str(data, 256);
                    print_str("  [SERVER] OPEN: ");
                    print_str(path);
                    print_str("\n");
                    let fd = hepu::open(path, hepu::flags::O_RDONLY, 0);
                    let mut resp = [0u8; 8];
                    resp[0..4].copy_from_slice(&(fd as u32).to_le_bytes());
                    hepu::ipc_send(client_pid, hepu::IpcType::RESPONSE, &resp);
                    print_str("  [SERVER] -> fd=");
                    print_int(fd);
                    print_str("\n");
                }
                hepu::IpcType::FILE_READ => {
                    let fd = read_i32(data, 0);
                    let count = read_u32(data, 4) as usize;
                    print_str("  [SERVER] READ: fd=");
                    print_int(fd);
                    print_str(" count=");
                    print_int(count as i32);
                    print_str("\n");
                    let mut buf = [0u8; 240];
                    let read_count = count.min(buf.len());
                    let n = hepu::read(fd, &mut buf[..read_count]);
                    let mut resp = [0u8; 256];
                    resp[0..4].copy_from_slice(&(n as u32).to_le_bytes());
                    if n > 0 {
                        resp[4..4+n as usize].copy_from_slice(&buf[..n as usize]);
                    }
                    hepu::ipc_send(client_pid, hepu::IpcType::RESPONSE, &resp[..4+n.max(0) as usize]);
                    print_str("  [SERVER] -> read ");
                    print_int(n as i32);
                    print_str(" bytes\n");
                }
                hepu::IpcType::FILE_CLOSE => {
                    let fd = read_i32(data, 0);
                    print_str("  [SERVER] CLOSE: fd=");
                    print_int(fd);
                    print_str("\n");
                    let ret = hepu::close(fd);
                    let mut resp = [0u8; 8];
                    resp[0..4].copy_from_slice(&(ret as u32).to_le_bytes());
                    hepu::ipc_send(client_pid, hepu::IpcType::RESPONSE, &resp);
                }
                _ => {
                    print_str("  [SERVER] unknown type, sending error\n");
                    let err = (-1i32).to_le_bytes();
                    hepu::ipc_send(client_pid, hepu::IpcType::ERROR, &err);
                }
            }

            if request_count >= 5 {
                print_str("  [SERVER] max requests reached, exiting\n");
                break;
            }
        } else {
            hepu::yield_cpu();
        }
    }

    print_str("  [SERVER] done\n");
    0
}

/// 父进程：文件服务客户端
fn run_client() -> i32 {
    print_str("\n  [CLIENT] starting, PID=");
    print_pid(hepu::getpid());
    print_str("\n");

    let mut passed = 0;
    let mut failed = 0;

    // 等待服务注册（轮询服务发现）
    print_str("  [CLIENT] waiting for service 'e2e-file-server'...\n");
    let mut server_pid = 0;
    for i in 0..200 {
        server_pid = hepu::service_lookup("e2e-file-server");
        if server_pid != 0 {
            break;
        }
        hepu::yield_cpu();
        if i % 50 == 49 {
            print_str("  [CLIENT] still waiting...\n");
        }
    }

    if server_pid == 0 {
        print_str("  [CLIENT] FAILED: service not found after timeout\n");
        return -1;
    }
    print_str("  [CLIENT] found service at PID=");
    print_pid(server_pid);
    print_str("\n");

    // Test 1: FILE_OPEN
    print_str("\n  [CLIENT] Test 1: FILE_OPEN /app/test_hepu.txt\n");
    let path = b"/app/test_hepu.txt\0";
    let send_ret = hepu::ipc_send(server_pid, hepu::IpcType::FILE_OPEN, path);
    if send_ret != 0 {
        print_str("  [CLIENT] FAILED: ipc_send returned ");
        print_int(send_ret as i32);
        print_str("\n");
        failed += 1;
    } else {
        // 等待响应
        let mut fd = -1;
        for _ in 0..100 {
            if let Some(msg) = hepu::ipc_recv() {
                if msg.msg_type == hepu::IpcType::RESPONSE {
                    fd = read_i32(&msg.data, 0);
                    break;
                }
            } else {
                hepu::yield_cpu();
            }
        }
        print_str("  [CLIENT] received fd=");
        print_int(fd);
        print_str("\n");
        if fd >= 0 {
            print_str("  [CLIENT] PASSED: FILE_OPEN\n");
            passed += 1;
        } else {
            print_str("  [CLIENT] FAILED: FILE_OPEN returned negative\n");
            failed += 1;
        }

        // Test 2: FILE_READ（仅当open成功）
        if fd >= 0 {
            print_str("\n  [CLIENT] Test 2: FILE_READ\n");
            let mut req = [0u8; 8];
            req[0..4].copy_from_slice(&(fd as u32).to_le_bytes());
            req[4..8].copy_from_slice(&64u32.to_le_bytes());
            hepu::ipc_send(server_pid, hepu::IpcType::FILE_READ, &req);

            let mut read_ok = false;
            for _ in 0..100 {
                if let Some(msg) = hepu::ipc_recv() {
                    if msg.msg_type == hepu::IpcType::RESPONSE {
                        let n = read_i32(&msg.data, 0);
                        print_str("  [CLIENT] read ");
                        print_int(n);
                        print_str(" bytes\n");
                        if n >= 0 {
                            print_str("  [CLIENT] PASSED: FILE_READ\n");
                            passed += 1;
                            read_ok = true;
                        } else {
                            print_str("  [CLIENT] FAILED: FILE_READ error\n");
                            failed += 1;
                        }
                        break;
                    }
                } else {
                    hepu::yield_cpu();
                }
            }
            if !read_ok {
                print_str("  [CLIENT] FAILED: FILE_READ timeout\n");
                failed += 1;
            }

            // Test 3: FILE_CLOSE
            print_str("\n  [CLIENT] Test 3: FILE_CLOSE\n");
            let close_req = (fd as u32).to_le_bytes();
            hepu::ipc_send(server_pid, hepu::IpcType::FILE_CLOSE, &close_req);

            let mut close_ok = false;
            for _ in 0..100 {
                if let Some(msg) = hepu::ipc_recv() {
                    if msg.msg_type == hepu::IpcType::RESPONSE {
                        let ret = read_i32(&msg.data, 0);
                        print_str("  [CLIENT] close returned ");
                        print_int(ret);
                        print_str("\n");
                        print_str("  [CLIENT] PASSED: FILE_CLOSE\n");
                        passed += 1;
                        close_ok = true;
                        break;
                    }
                } else {
                    hepu::yield_cpu();
                }
            }
            if !close_ok {
                print_str("  [CLIENT] FAILED: FILE_CLOSE timeout\n");
                failed += 1;
            }
        }
    }

    // Test 4: 服务发现验证（再次查找确认服务仍在）
    print_str("\n  [CLIENT] Test 4: Service lookup verification\n");
    let verify_pid = hepu::service_lookup("e2e-file-server");
    if verify_pid == server_pid {
        print_str("  [CLIENT] PASSED: service lookup returns same PID\n");
        passed += 1;
    } else {
        print_str("  [CLIENT] FAILED: service lookup returned PID=");
        print_pid(verify_pid);
        print_str(" (expected ");
        print_pid(server_pid);
        print_str(")\n");
        failed += 1;
    }

    print_str("\n  [CLIENT] Summary: passed=");
    print_int(passed);
    print_str(" failed=");
    print_int(failed);
    print_str("\n");

    if failed == 0 { 0 } else { -1 }
}

#[no_mangle]
pub extern "C" fn main() -> i32 {
    print_str("\n=== Hepu OS File Server E2E Test (cross-process) ===\n");
    print_str("Parent PID: ");
    print_pid(hepu::getpid());
    print_str("\n");

    let mut passed = 0;
    let mut failed = 0;

    // Test 0: fork
    print_str("\n[Test 0] fork\n");
    let pid = hepu::fork();
    print_str("  fork returned: ");
    print_int(pid as i32);
    print_str("\n");

    if pid < 0 {
        print_str("  FAILED: fork returned error\n");
        return -1;
    }

    if pid == 0 {
        // 子进程：运行文件服务端
        print_str("  [CHILD] I am the child, PID=");
        print_pid(hepu::getpid());
        print_str(", running as server\n");
        let server_ret = run_server();
        hepu::exit(server_ret);
    } else {
        // 父进程：运行客户端
        print_str("  [PARENT] child PID=");
        print_pid(pid as u32);
        print_str(", running as client\n");

        let client_ret = run_client();
        if client_ret == 0 {
            print_str("  [PARENT] PASSED: client tests\n");
            passed += 1;
        } else {
            print_str("  [PARENT] FAILED: client tests\n");
            failed += 1;
        }

        // 等待子进程退出
        print_str("\n[Test 5] waitpid\n");
        let mut status = 0;
        let wait_ret = hepu::waitpid(pid as u32, &mut status);
        print_str("  waitpid returned: ");
        print_int(wait_ret as i32);
        print_str(" status: ");
        print_int(status);
        print_str("\n");
        if wait_ret == pid as u32 {
            print_str("  PASSED: waitpid returned child PID\n");
            passed += 1;
        } else {
            print_str("  FAILED: waitpid returned unexpected value\n");
            failed += 1;
        }
    }

    print_str("\n=== E2E Test Summary ===\n");
    print_str("  Passed: ");
    print_int(passed);
    print_str("\n");
    print_str("  Failed: ");
    print_int(failed);
    print_str("\n");
    if failed == 0 {
        print_str("  ALL E2E TESTS PASSED!\n");
    } else {
        print_str("  SOME E2E TESTS FAILED\n");
    }
    print_str("\n");

    if failed == 0 { 0 } else { -1 }
}
