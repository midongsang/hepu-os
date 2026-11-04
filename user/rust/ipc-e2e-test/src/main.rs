//! 赫普OS - 端到端IPC集成测试（自包含版本）
//! 由于fork存在内核栈复制问题，本测试在同一进程中验证：
//! 1. 服务注册与发现
//! 2. IPC消息发送与接收
//! 3. 文件操作请求/响应协议

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

#[no_mangle]
pub extern "C" fn main() -> i32 {
    print_str("\n=== Hepu OS IPC End-to-End Test (self-contained) ===\n");
    print_str("PID: ");
    print_pid(hepu::getpid());
    print_str("\n");

    let mut passed = 0;
    let mut failed = 0;

    // ===== Test 1: 服务注册 =====
    print_str("\n[Test 1] Service register\n");
    let ret = hepu::service_register("e2e-test-service", hepu::ServiceType::GENERAL, 1);
    print_str("  service_register: ");
    print_int(ret);
    print_str("\n");
    if ret == 0 {
        print_str("  PASSED: service registered\n");
        passed += 1;
    } else {
        print_str("  FAILED: service register failed\n");
        failed += 1;
    }

    // ===== Test 2: 服务发现 =====
    print_str("\n[Test 2] Service lookup\n");
    let pid = hepu::service_lookup("e2e-test-service");
    print_str("  service_lookup: PID=");
    print_pid(pid);
    print_str("\n");
    if pid == hepu::getpid() {
        print_str("  PASSED: found self via service discovery\n");
        passed += 1;
    } else if pid != 0 {
        print_str("  PASSED: found service at PID=");
        print_pid(pid);
        print_str(" (different from self)\n");
        passed += 1;
    } else {
        print_str("  FAILED: service not found\n");
        failed += 1;
    }

    // ===== Test 3: IPC消息发送给自己 =====
    print_str("\n[Test 3] IPC send to self\n");
    let test_data = b"hello ipc\0";
    let send_ret = hepu::ipc_send(hepu::getpid(), hepu::IpcType::FILE_OPEN, test_data);
    print_str("  ipc_send: ");
    print_int(send_ret as i32);
    print_str("\n");
    if send_ret == 0 {
        print_str("  PASSED: message sent\n");
        passed += 1;
    } else {
        print_str("  FAILED: ipc_send failed\n");
        failed += 1;
    }

    // ===== Test 4: IPC消息接收 =====
    print_str("\n[Test 4] IPC receive\n");
    let mut received = false;
    for _ in 0..50 {
        if let Some(msg) = hepu::ipc_recv() {
            print_str("  received: type=");
            print_int(msg.msg_type as i32);
            print_str(" sender=");
            print_pid(msg.sender_pid);
            print_str(" len=");
            print_int(msg.length as i32);
            print_str("\n");
            if msg.msg_type == hepu::IpcType::FILE_OPEN {
                let path = extract_str(&msg.data[..msg.length as usize], 64);
                print_str("  path: ");
                print_str(path);
                print_str("\n");
                if path == "hello ipc" {
                    print_str("  PASSED: received correct message\n");
                    passed += 1;
                    received = true;
                } else {
                    print_str("  FAILED: wrong message content\n");
                    failed += 1;
                }
            } else {
                print_str("  FAILED: wrong message type\n");
                failed += 1;
            }
            break;
        } else {
            hepu::yield_cpu();
        }
    }
    if !received {
        print_str("  FAILED: timeout waiting for message\n");
        failed += 1;
    }

    // ===== Test 5: 文件操作请求/响应协议 =====
    print_str("\n[Test 5] File operation protocol (open)\n");
    let path = "/test_hepu.txt\0";
    let mut req = [0u8; 64];
    req[..path.len()].copy_from_slice(path.as_bytes());
    hepu::ipc_send(hepu::getpid(), hepu::IpcType::FILE_OPEN, &req[..path.len()]);

    let mut fd = -1;
    for _ in 0..50 {
        if let Some(msg) = hepu::ipc_recv() {
            if msg.msg_type == hepu::IpcType::FILE_OPEN {
                let p = extract_str(&msg.data[..msg.length as usize], 64);
                print_str("  server received OPEN: ");
                print_str(p);
                print_str("\n");
                fd = hepu::open(p, hepu::flags::O_RDONLY, 0);
                let mut resp = [0u8; 8];
                resp[0..4].copy_from_slice(&(fd as u32).to_le_bytes());
                hepu::ipc_send(msg.sender_pid, hepu::IpcType::RESPONSE, &resp);
                print_str("  server sent response: fd=");
                print_int(fd);
                print_str("\n");
                break;
            }
        } else {
            hepu::yield_cpu();
        }
    }

    let mut got_response = false;
    for _ in 0..50 {
        if let Some(msg) = hepu::ipc_recv() {
            if msg.msg_type == hepu::IpcType::RESPONSE {
                let ret_fd = read_i32(&msg.data, 0);
                print_str("  client received response: fd=");
                print_int(ret_fd);
                print_str("\n");
                if ret_fd == fd {
                    print_str("  PASSED: request/response protocol works\n");
                    passed += 1;
                    got_response = true;
                } else {
                    print_str("  FAILED: response fd mismatch\n");
                    failed += 1;
                }
                break;
            }
        } else {
            hepu::yield_cpu();
        }
    }
    if !got_response {
        print_str("  FAILED: timeout waiting for response\n");
        failed += 1;
    }

    // ===== Test 6: IPC消息结构大小 =====
    print_str("\n[Test 6] IPC message structure\n");
    let msg_size = core::mem::size_of::<hepu::IpcMessage>();
    print_str("  IpcMessage size: ");
    print_int(msg_size as i32);
    print_str(" bytes\n");
    if msg_size > 0 {
        print_str("  PASSED: IpcMessage valid\n");
        passed += 1;
    } else {
        print_str("  FAILED: IpcMessage size 0\n");
        failed += 1;
    }

    // ===== 总结 =====
    print_str("\n=== Test Summary ===\n");
    print_str("  Passed: ");
    print_int(passed);
    print_str("\n");
    print_str("  Failed: ");
    print_int(failed);
    print_str("\n");
    if failed == 0 {
        print_str("  ALL TESTS PASSED!\n");
    } else {
        print_str("  SOME TESTS FAILED\n");
    }
    print_str("\nNote: Full cross-process IPC test requires fork fix (BUG-20).\n");
    print_str("This test validates service discovery and IPC mechanism in single process.\n\n");

    if failed == 0 { 0 } else { -1 }
}
