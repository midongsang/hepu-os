#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
void vga_puts(const char *s){ (void)s; }
extern int copy_from_user(void*,const void*,size_t); extern int copy_to_user(void*,const void*,size_t);
extern int copy_str_from_user(char*,const char*,size_t); extern int user_strlen(const char*,size_t);
extern int verify_user_read(const void*,size_t); extern int verify_user_write(void*,size_t);
extern int hepu_is_user_address(uintptr_t,size_t);
uint64_t hepu_test_cr3; static uint8_t *arena;
uint64_t *hepu_test_phys(uint64_t p){ return (uint64_t*)(arena + p); }
#define P 1ULL
#define W 2ULL
#define U 4ULL
#define PS 0x80ULL
int fails=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
int main(void){
  arena = aligned_alloc(4096, 0x20000); memset(arena,0,0x20000);
  /* real memory at the user window so successful copies can really happen */
  uint8_t *w = mmap((void*)0x40000000, 0x800000, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE, -1, 0);
  if (w != (void*)0x40000000) { printf("mmap fixed failed\n"); return 2; }
  uint64_t *pml4=(uint64_t*)(arena+0x1000), *pdpt=(uint64_t*)(arena+0x2000), *pd=(uint64_t*)(arena+0x3000), *pt=(uint64_t*)(arena+0x4000);
  pml4[0]=0x2000|P|W|U; pdpt[1]=0x3000|P|W|U;
  pd[0]=0x4000|P|W|U;                 /* 0x40000000..0x401FFFFF via PT */
  pd[1]=0x00200000|P|W|U|PS;          /* 0x40200000 2MB rw user */
  pd[2]=0x00400000|P|U|PS;            /* 0x40400000 2MB read-only user */
  pd[3]=0x00600000|P|W|PS;            /* 0x40600000 2MB supervisor only */
  pt[0]=0x10000|P|W|U;                /* page0 rw */
  pt[1]=0x11000|P|U;                  /* page1 read-only */
  /* pt[2] not present */
  pt[3]=0x13000|P|W|U;                /* page3 rw */
  hepu_test_cr3 = 0x1000;
  uint8_t kb[16384]; char s[300];
  /* ---- window ---- */
  CHECK(!hepu_is_user_address(0,1));  CHECK(!hepu_is_user_address(0x100000,16));
  CHECK(!hepu_is_user_address(0x3FFFFFFF,2)); CHECK(!hepu_is_user_address(0x7FFFFFFF,2));
  CHECK(hepu_is_user_address(0x7FFFFFFF,1));  CHECK(!hepu_is_user_address(0xFFFF800000100000ULL,8));
  CHECK(!hepu_is_user_address(~0ULL-3,16));   /* wraparound */
  CHECK(copy_from_user(kb,(void*)0x100000,16)!=0);            /* kernel image area via "user pointer" */
  CHECK(copy_to_user((void*)0x200000,kb,16)!=0);
  /* ---- 4KB pages ---- */
  memcpy(w+0x10, "hello", 6);
  CHECK(copy_from_user(kb,(void*)0x40000010,6)==0 && memcmp(kb,"hello",6)==0);
  CHECK(copy_to_user((void*)0x40000100,"abc",4)==0 && memcmp(w+0x100,"abc",4)==0);
  CHECK(copy_to_user((void*)0x40001000,"x",1)!=0);            /* read-only page */
  CHECK(copy_from_user(kb,(void*)0x40001000,16)==0);          /* but readable */
  CHECK(copy_to_user((void*)0x40000FFE,"abcd",4)!=0);         /* spans rw page -> read-only page */
  CHECK(copy_from_user(kb,(void*)0x40000FFE,4)==0);
  CHECK(copy_from_user(kb,(void*)0x40001FFE,4)!=0);           /* spans into unmapped page2 */
  CHECK(copy_from_user(kb,(void*)0x40002000,1)!=0);
  CHECK(copy_to_user((void*)0x40002000,"x",1)!=0);
  CHECK(copy_to_user((void*)0x40003000,"ok",3)==0);
  CHECK(copy_from_user(kb,(void*)0x40003000,0x1001)!=0);      /* runs past page3 into page4 (not in PT) */
  CHECK(copy_from_user(kb,(void*)0x40000000,8192)==0);        /* exactly pages 0..1, both readable */
  CHECK(copy_from_user(kb,(void*)0x40000000,8193)!=0);        /* one byte into unmapped page2 */
  /* ---- huge pages ---- */
  CHECK(copy_to_user((void*)0x40200010,"big",4)==0 && memcmp(w+0x200010,"big",4)==0);
  CHECK(copy_from_user(kb,(void*)0x403FFFF0,16)==0);
  CHECK(copy_to_user((void*)0x40400000,"x",1)!=0);            /* RO huge */
  CHECK(copy_from_user(kb,(void*)0x40400000,64)==0);
  CHECK(copy_from_user(kb,(void*)0x40600000,64)!=0);          /* supervisor-only huge */
  CHECK(copy_to_user((void*)0x40600000,"x",1)!=0);
  CHECK(copy_from_user(kb,(void*)0x40800000,8)!=0);           /* PD entry absent */
  /* ---- strings ---- */
  strcpy((char*)w+0x300,"/etc/cfg");
  CHECK(copy_str_from_user(s,(char*)0x40000300,sizeof s)==8 && strcmp(s,"/etc/cfg")==0);
  CHECK(copy_str_from_user(s,(char*)0x40000300,5)==4 && strcmp(s,"/etc")==0);   /* truncated to max-1 */
  CHECK(user_strlen((char*)0x40000300,64)==8);
  memset(w+0x1F00,'A',0x100);                                  /* page1 tail: no NUL until unmapped page2 */
  CHECK(copy_str_from_user(s,(char*)0x40001F00,sizeof s)==-1); /* must stop at the unmapped page */
  CHECK(user_strlen((char*)0x40001F00,4096)==-1);
  memset(w+0x1FF0,0,1); strcpy((char*)w+0x1FF0,"end");         /* NUL inside page1: fine */
  CHECK(copy_str_from_user(s,(char*)0x40001FF0,sizeof s)==3);
  CHECK(copy_str_from_user(s,(char*)0x40002000,sizeof s)==-1);
  CHECK(copy_str_from_user(s,(char*)0x100000,sizeof s)==-1);
  /* ---- verify_* ---- */
  CHECK(verify_user_read((void*)0x40000000,4096)); CHECK(!verify_user_write((void*)0x40001000,16));
  CHECK(verify_user_read((void*)0x40001000,4096));  CHECK(!verify_user_read((void*)0x40002000,1));
  /* ---- NULL / zero ---- */
  CHECK(copy_from_user(NULL,(void*)0x40000000,4)!=0); CHECK(copy_from_user(kb,(void*)0x40000000,0)!=0);
  /* ---- 1GB page under PDPT[1] ---- */
  uint64_t *pml4b=(uint64_t*)(arena+0x6000), *pdptb=(uint64_t*)(arena+0x7000);
  pml4b[0]=0x7000|P|W|U; pdptb[1]=0x40000000ULL|P|W|U|PS; hepu_test_cr3=0x6000;
  CHECK(copy_to_user((void*)0x40000500,"gb",3)==0); CHECK(copy_from_user(kb,(void*)0x407FF000,4096)==0);
  pdptb[1]=0x40000000ULL|P|W|PS;                              /* now supervisor-only 1GB page */
  CHECK(copy_from_user(kb,(void*)0x40000500,3)!=0);
  printf(fails?"UACCESS TESTS FAILED (%d)\n":"ALL USER-ACCESS TESTS PASSED\n",fails); return fails!=0;
}
