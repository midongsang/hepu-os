//! 赫普OS - Rust网络服务（net-server）
//! 首个Rust用户态网络服务，提供HTTP服务器功能
//!
//! 功能：
//!   - TCP监听端口8080
//!   - 处理HTTP GET请求
//!   - 返回简单HTML响应
//!   - 支持多连接循环
//!
//! 协议：HTTP/1.0

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

/// HTTP响应
const HTTP_RESPONSE: &str = "\
HTTP/1.0 200 OK\r\n\
Content-Type: text/html\r\n\
Content-Length: 128\r\n\
Connection: close\r\n\
\r\n\
<html><head><title>Hepu OS</title></head>\
<body><h1>Welcome to Hepu OS!</h1>\
<p>Rust net-server HTTP service is running.</p>\
</body></html>";

/// 404响应
const HTTP_404: &str = "\
HTTP/1.0 404 Not Found\r\n\
Content-Type: text/plain\r\n\
Content-Length: 13\r\n\
Connection: close\r\n\
\r\n\
404 Not Found";

/// 处理单个HTTP连接
fn handle_connection(client_fd: i32) {
    let mut buf = [0u8; 1024];
    let n = hepu::recv(client_fd, &mut buf, 0);
    if n <= 0 {
        return;
    }

    print_str("  Request: ");
    if n > 80 {
        hepu::write(1, &buf[..80]);
        print_str("...\n");
    } else {
        hepu::write(1, &buf[..n as usize]);
        print_str("\n");
    }

    // 简单解析：GET / -> 200, 其他 -> 404
    let is_get = buf.len() >= 3 && &buf[..3] == b"GET";
    let response = if is_get { HTTP_RESPONSE } else { HTTP_404 };
    hepu::send(client_fd, response.as_bytes(), 0);
}

#[no_mangle]
pub extern "C" fn main() -> i32 {
    print_str("\n=== Hepu OS Net Server v1 (Rust) ===\n");
    print_str("PID: ");
    print_pid(hepu::getpid());
    print_str("\n");
    print_str("HTTP server on port 8080\n");

    // 注册服务到服务注册表
    print_str("Registering service 'net-server'...\n");
    let ret = hepu::service_register("net-server", hepu::ServiceType::NETWORK, 2);
    if ret == 0 {
        print_str("Service registered successfully.\n");
    } else {
        print_str("Service registration failed: ");
        print_int(ret);
        print_str("\n");
    }

    // 创建TCP socket
    let listen_fd = hepu::socket(
        hepu::AddressFamily::AF_INET,
        hepu::SocketType::SOCK_STREAM,
        hepu::Protocol::IPPROTO_TCP,
    );
    if listen_fd < 0 {
        print_str("Error: socket() failed: ");
        print_int(listen_fd);
        print_str("\n");
        return 1;
    }
    print_str("Socket created: fd=");
    print_int(listen_fd);
    print_str("\n");

    // 绑定到0.0.0.0:8080
    let addr = hepu::SockAddrIn::new(0, 8080); // 0 = INADDR_ANY
    let ret = hepu::bind(listen_fd, &addr);
    if ret < 0 {
        print_str("Error: bind() failed: ");
        print_int(ret);
        print_str("\n");
        return 1;
    }
    print_str("Bound to 0.0.0.0:8080\n");

    // 监听
    let ret = hepu::listen(listen_fd, 5);
    if ret < 0 {
        print_str("Error: listen() failed: ");
        print_int(ret);
        print_str("\n");
        return 1;
    }
    print_str("Listening for connections...\n");

    // 主循环：接受连接
    let mut connection_count = 0u32;
    loop {
        print_str("\nWaiting for connection...\n");
        let client_fd = hepu::accept(listen_fd, None);
        if client_fd < 0 {
            print_str("Error: accept() failed: ");
            print_int(client_fd);
            print_str("\n");
            continue;
        }

        connection_count += 1;
        print_str("\n[CONN #");
        print_int(connection_count as i32);
        print_str("] New client fd=");
        print_int(client_fd);
        print_str("\n");

        handle_connection(client_fd);

        // 关闭客户端连接
        // hepu::close(client_fd); // 待hepu-sys添加close封装
        print_str("[CONN #");
        print_int(connection_count as i32);
        print_str("] Done\n");

        // 限制最大连接数，避免无限循环
        if connection_count >= 100 {
            print_str("\nMax connections reached, exiting.\n");
            break;
        }
    }

    print_str("\nNet server exiting.\n");
    0
}
