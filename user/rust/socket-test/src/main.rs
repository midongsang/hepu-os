//! 赫普OS - BSD Socket API 测试
//! 验证：socket/bind/listen/connect/send/recv 基本功能（loopback）

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

fn print_hex(n: u32) {
    let mut buf = [0u8; 16];
    let mut i = 0;
    if n == 0 {
        buf[i] = b'0';
        i += 1;
    } else {
        let digits = b"0123456789ABCDEF";
        let mut tmp = n;
        while tmp > 0 {
            buf[i] = digits[(tmp & 0xF) as usize];
            tmp >>= 4;
            i += 1;
        }
        buf[..i].reverse();
    }
    hepu::write(1, &buf[..i]);
}

#[no_mangle]
pub extern "C" fn main() -> i32 {
    print_str("\n=== Hepu OS BSD Socket API Test ===\n");
    print_str("PID: ");
    print_int(hepu::getpid() as i32);
    print_str("\n");

    let mut passed = 0;
    let mut failed = 0;

    // Test 1: 创建TCP socket
    print_str("\n[Test 1] socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)\n");
    let server_fd = hepu::socket(
        hepu::AddressFamily::AF_INET,
        hepu::SocketType::SOCK_STREAM,
        hepu::Protocol::IPPROTO_TCP,
    );
    if server_fd >= 0 {
        print_str("  PASSED: socket created, fd=");
        print_int(server_fd);
        print_str("\n");
        passed += 1;
    } else {
        print_str("  FAILED: socket() returned ");
        print_int(server_fd);
        print_str("\n");
        failed += 1;
    }

    // Test 2: bind
    print_str("\n[Test 2] bind(127.0.0.1:8080)\n");
    let addr = hepu::SockAddrIn::loopback(8080);
    let ret = hepu::bind(server_fd, &addr);
    if ret == 0 {
        print_str("  PASSED: bind successful\n");
        passed += 1;
    } else {
        print_str("  FAILED: bind() returned ");
        print_int(ret);
        print_str("\n");
        failed += 1;
    }

    // Test 3: listen
    print_str("\n[Test 3] listen(backlog=5)\n");
    let ret = hepu::listen(server_fd, 5);
    if ret == 0 {
        print_str("  PASSED: listen successful\n");
        passed += 1;
    } else {
        print_str("  FAILED: listen() returned ");
        print_int(ret);
        print_str("\n");
        failed += 1;
    }

    // Test 4: 创建客户端socket并connect
    print_str("\n[Test 4] connect to 127.0.0.1:8080\n");
    let client_fd = hepu::socket(
        hepu::AddressFamily::AF_INET,
        hepu::SocketType::SOCK_STREAM,
        hepu::Protocol::IPPROTO_TCP,
    );
    if client_fd < 0 {
        print_str("  FAILED: client socket() returned ");
        print_int(client_fd);
        print_str("\n");
        failed += 1;
    } else {
        let server_addr = hepu::SockAddrIn::loopback(8080);
        let ret = hepu::connect(client_fd, &server_addr);
        if ret == 0 {
            print_str("  PASSED: connect successful (loopback)\n");
            passed += 1;
        } else {
            print_str("  FAILED: connect() returned ");
            print_int(ret);
            print_str("\n");
            failed += 1;
        }
    }

    // Test 5: accept (loopback模式下connect会自动创建对端)
    print_str("\n[Test 5] accept (loopback auto-accept)\n");
    let conn_fd = hepu::accept(server_fd, None);
    if conn_fd >= 0 {
        print_str("  PASSED: accept returned fd=");
        print_int(conn_fd);
        print_str("\n");
        passed += 1;
    } else {
        print_str("  FAILED: accept() returned ");
        print_int(conn_fd);
        print_str("\n");
        failed += 1;
    }

    // Test 6: send数据
    print_str("\n[Test 6] send 'Hello, Socket!'\n");
    let msg = b"Hello, Socket!";
    let sent = hepu::send(client_fd, msg, 0);
    if sent == msg.len() as isize {
        print_str("  PASSED: sent ");
        print_int(sent as i32);
        print_str(" bytes\n");
        passed += 1;
    } else {
        print_str("  FAILED: send() returned ");
        print_int(sent as i32);
        print_str(" (expected ");
        print_int(msg.len() as i32);
        print_str(")\n");
        failed += 1;
    }

    // Test 7: recv数据
    print_str("\n[Test 7] recv data\n");
    let mut buf = [0u8; 64];
    let received = hepu::recv(conn_fd, &mut buf, 0);
    if received > 0 {
        print_str("  PASSED: received ");
        print_int(received as i32);
        print_str(" bytes: ");
        hepu::write(1, &buf[..received as usize]);
        print_str("\n");
        // 验证数据
        if &buf[..received as usize] == msg {
            print_str("  PASSED: data matches\n");
            passed += 1;
        } else {
            print_str("  FAILED: data mismatch\n");
            failed += 1;
        }
    } else {
        print_str("  FAILED: recv() returned ");
        print_int(received as i32);
        print_str("\n");
        failed += 1;
    }

    // Test 8: 双向通信（服务器回复）
    print_str("\n[Test 8] bidirectional communication\n");
    let reply = b"ACK: received";
    let sent = hepu::send(conn_fd, reply, 0);
    if sent > 0 {
        let mut rbuf = [0u8; 64];
        let received = hepu::recv(client_fd, &mut rbuf, 0);
        if received > 0 && &rbuf[..received as usize] == reply {
            print_str("  PASSED: server reply received correctly\n");
            passed += 1;
        } else {
            print_str("  FAILED: reply mismatch (received ");
            print_int(received as i32);
            print_str(" bytes)\n");
            failed += 1;
        }
    } else {
        print_str("  FAILED: send reply failed\n");
        failed += 1;
    }

    // 关闭所有socket
    hepu::close_socket(client_fd);
    hepu::close_socket(conn_fd);
    hepu::close_socket(server_fd);

    // 总结
    print_str("\n=== Socket Test Summary ===\n");
    print_str("  Passed: ");
    print_int(passed);
    print_str("\n");
    print_str("  Failed: ");
    print_int(failed);
    print_str("\n");
    if failed == 0 {
        print_str("  ALL SOCKET TESTS PASSED!\n");
    } else {
        print_str("  SOME TESTS FAILED\n");
    }
    print_str("\n");

    if failed == 0 { 0 } else { -1 }
}
