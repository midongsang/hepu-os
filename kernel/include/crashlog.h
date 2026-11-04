#ifndef CRASHLOG_H
#define CRASHLOG_H
#include <stdint.h>
int crashlog_append(const char *name, uint32_t pid,
                    uint32_t isr, uint32_t err,
                    uint32_t eip, uint32_t cs, uint32_t esp);
int crashlog_dump(void);
#endif
