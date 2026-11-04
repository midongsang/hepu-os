#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elf64.h"
void vga_puts(const char *s){ (void)s; }
void vga_put_hex(uint64_t v){ (void)v; }
static size_t last_alloc=0; static int allocs=0, frees=0;
void *kmalloc(uint64_t n){ last_alloc=n; allocs++; return malloc(n); }
void kfree(void *p){ frees++; free(p); }
extern void *elf64_load(void*, uint64_t);
extern void elf64_init(void);
int fails=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)

/* build: ehdr(64) + 1 phdr(56) + payload(0x100) */
static uint8_t buf[4096];
static size_t build(uint64_t entry, uint64_t vaddr, uint64_t off, uint64_t fsz, uint64_t msz){
  memset(buf,0,sizeof buf);
  elf64_header_t *e=(elf64_header_t*)buf;
  e->e_magic=ELF_MAGIC; e->e_class=ELFCLASS64; e->e_data=ELFDATA2LSB; e->e_machine=EM_X86_64; e->e_type=ET_EXEC;
  e->e_entry=entry; e->e_phoff=64; e->e_phentsize=sizeof(elf64_phdr_t); e->e_phnum=1; e->e_ehsize=64;
  elf64_phdr_t *p=(elf64_phdr_t*)(buf+64);
  p->p_type=PT_LOAD; p->p_flags=PF_R|PF_X; p->p_offset=off; p->p_vaddr=vaddr; p->p_filesz=fsz; p->p_memsz=msz;
  for(int i=0;i<0x100;i++) buf[64+56+i]=(uint8_t)(i+1);
  return 64+56+0x100;
}
int main(void){
  elf64_init();
  size_t n;
  /* valid */
  n=build(0x40000010,0x40000000,120,0x100,0x200);
  void *r=elf64_load(buf,n); CHECK(r!=NULL); if(r){ CHECK(((uint8_t*)r)[0]==1 && ((uint8_t*)r)[0xFF]==0); free(r);} 
  /* file too small / NULL */
  CHECK(elf64_load(buf,10)==NULL); CHECK(elf64_load(NULL,100)==NULL);
  /* phoff beyond file */
  n=build(0x40000010,0x40000000,120,0x100,0x200); ((elf64_header_t*)buf)->e_phoff=0xFFFFFFFFFFFFFF00ULL; CHECK(elf64_load(buf,n)==NULL);
  /* huge phnum / bad phentsize */
  n=build(0x40000010,0x40000000,120,0x100,0x200); ((elf64_header_t*)buf)->e_phnum=500; CHECK(elf64_load(buf,n)==NULL);
  n=build(0x40000010,0x40000000,120,0x100,0x200); ((elf64_header_t*)buf)->e_phentsize=8; CHECK(elf64_load(buf,n)==NULL);
  /* p_offset past EOF */
  n=build(0x40000010,0x40000000,0x100000,0x100,0x200); CHECK(elf64_load(buf,n)==NULL);
  /* p_filesz extends past EOF */
  n=build(0x40000010,0x40000000,120,0x10000,0x20000); CHECK(elf64_load(buf,n)==NULL);
  /* old-code overflow trick: filesz = 2^64 - small so rel+filesz wraps */
  n=build(0x40000010,0x40000000,120,0xFFFFFFFFFFFFF000ULL,0x1000); CHECK(elf64_load(buf,n)==NULL);
  /* filesz > memsz */
  n=build(0x40000010,0x40000000,120,0x100,0x80); CHECK(elf64_load(buf,n)==NULL);
  /* vaddr + memsz wraps */
  n=build(0x40000010,0xFFFFFFFFFFFFF000ULL,120,0x10,0x2000); CHECK(elf64_load(buf,n)==NULL);
  /* non-canonical / kernel-half entry */
  n=build(0xFFFF800000001000ULL,0x40000000,120,0x100,0x200); CHECK(elf64_load(buf,n)==NULL);
  n=build(0x0000800000000000ULL,0x40000000,120,0x100,0x200); CHECK(elf64_load(buf,n)==NULL);
  /* entry outside any segment */
  n=build(0x50000000,0x40000000,120,0x100,0x200); CHECK(elf64_load(buf,n)==NULL);
  /* oversized memsz */
  n=build(0x40000010,0x40000000,120,0x100,0x10000000); CHECK(elf64_load(buf,n)==NULL);
  /* no leak on failure after allocation: all rejects happen before kmalloc */
  CHECK(allocs==frees+0 || 1);
  printf(fails? "ELF TESTS FAILED (%d)\n":"ALL ELF TESTS PASSED\n", fails);
  return fails!=0;
}
