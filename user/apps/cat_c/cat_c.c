/* 最简单的C版本cat测试 */
int main() {
    const char *msg = "C cat: hello\n";
    /* write(1, msg, 14) */
    asm volatile(
        "int $0x80"
        : : "a"(4), "b"(1), "c"(msg), "d"(14)
    );
    /* exit(0) */
    asm volatile(
        "int $0x80"
        : : "a"(1), "b"(0)
    );
    return 0;
}

void _start() {
    main();
    while (1);
}
