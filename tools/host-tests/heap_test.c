#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void vga_puts(const char *s){ (void)s; }
void vga_put_hex(uint64_t v){ (void)v; }
void kheap64_init(void*, uint64_t);
void *kmalloc(size_t); void kfree(void*); void *kcalloc(size_t,size_t);
void *kmalloc_aligned(size_t,size_t); void *krealloc(void*,size_t);
void kheap64_get_stats(uint64_t*,uint64_t*,uint64_t*,uint64_t*,uint64_t*);
static uint64_t used(void){uint64_t t,u,f,a,c;kheap64_get_stats(&t,&u,&f,&a,&c);return u;}
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
int fails=0;
int main(void){
  kheap64_init(0,0);
  uint64_t base = used();
  void *a=kmalloc(64), *b=kmalloc(64), *c=kmalloc(64);
  CHECK(((uintptr_t)a%16)==0 && ((uintptr_t)b%16)==0 && ((uintptr_t)c%16)==0);
  kfree(b);  CHECK(used() < base + 3*(64+48));          /* middle block now freed */
  kfree(a);  kfree(c);
  CHECK(used()==base);                                    /* everything returned, coalesced */
  void *big = kmalloc(7*1024*1024);  CHECK(big!=NULL);    /* coalescing really works */
  kfree(big); CHECK(used()==base);
  /* overflow / bad sizes */
  CHECK(kmalloc((size_t)-4)==NULL);
  CHECK(kmalloc((size_t)-1)==NULL);
  CHECK(kcalloc((size_t)1<<62, 8)==NULL);
  CHECK(kcalloc((size_t)(1ULL<<61)+1, 8)==NULL);          /* wraps to small value in old code */
  CHECK(kmalloc_aligned((size_t)-8, 64)==NULL);
  CHECK(kmalloc_aligned(64, 24)==NULL);                   /* not power of two */
  CHECK(kmalloc(0)==NULL);
  /* aligned alloc + free */
  size_t als[] = {16,32,64,256,4096,65536};
  for (int i=0;i<6;i++){
    void *p = kmalloc_aligned(100, als[i]);
    CHECK(p && ((uintptr_t)p % als[i])==0);
    memset(p, 0xAB, 100);
    kfree(p);
    CHECK(used()==base);
  }
  /* aligned double free */
  void *p = kmalloc_aligned(100, 64); kfree(p); kfree(p); CHECK(used()==base);
  /* normal double free, free of garbage pointers */
  void *q = kmalloc(40); kfree(q); kfree(q); CHECK(used()==base);
  int local; kfree(&local); kfree((void*)0x1234); kfree(NULL); CHECK(used()==base);
  /* realloc */
  char *r = kmalloc(32); memset(r, 'x', 32);
  r = krealloc(r, 5000); CHECK(r && r[0]=='x' && r[31]=='x');
  char *ra = kmalloc_aligned(32, 256); memset(ra,'y',32);
  ra = krealloc(ra, 3000); CHECK(ra && ra[0]=='y' && ra[31]=='y');
  kfree(r); kfree(ra); CHECK(used()==base);
  /* random stress with canary overlap detection */
  enum {N=400}; unsigned char *ptr[N]; size_t sz[N]; memset(ptr,0,sizeof ptr);
  srand(1);
  for (int it=0; it<200000; it++){
    int i = rand()%N;
    if (ptr[i]) {
      for(size_t k=0;k<sz[i];k++) if(ptr[i][k]!=(unsigned char)(i+1)){ printf("CORRUPT at it=%d\n",it); fails++; return 1; }
      kfree(ptr[i]); ptr[i]=NULL;
    } else {
      sz[i] = 1 + rand()%3000;
      ptr[i] = (rand()%4==0) ? kmalloc_aligned(sz[i], 16u<<(rand()%5)) : kmalloc(sz[i]);
      if (ptr[i]) memset(ptr[i], i+1, sz[i]);
    }
  }
  for (int i=0;i<N;i++) if(ptr[i]) kfree(ptr[i]);
  CHECK(used()==base);
  printf(fails? "TESTS FAILED (%d)\n":"ALL HEAP TESTS PASSED\n", fails);
  return fails!=0;
}
