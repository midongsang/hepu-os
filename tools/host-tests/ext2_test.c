#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ext2_algo.h"
void vga_puts(const char*s){(void)s;} void vga_put_hex(uint64_t v){(void)v;}
static FILE *fp; static uint32_t g_bs=1024;
static int rd(uint32_t b, uint8_t *buf){ if(fseek(fp,(long)b*g_bs,SEEK_SET)) return -1; return fread(buf,1,g_bs,fp)==g_bs?0:-1; }
static int wr(uint32_t b, const uint8_t *buf){ (void)b;(void)buf; return -1; }
int fails=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)

static int mount(const char *img, ext2_context_t *ctx, uint8_t **bufp){
  fp=fopen(img,"rb"); if(!fp) return -9;
  ext2_superblock_t sb; fseek(fp,1024,SEEK_SET); fread(&sb,1,sizeof sb,fp);
  int v=ext2_algo_validate_sb(&sb); if(v!=0) return v;
  g_bs=1024u<<sb.s_log_block_size;
  *bufp = malloc(4096);                      /* same as kernel EXT2_MAX_BLOCK_SIZE; ASan redzone catches overruns past it */
  memset(ctx,0,sizeof *ctx);
  ext2_algo_init(ctx,rd,wr,*bufp);
  ctx->sb=sb; ctx->block_size=g_bs; ctx->group_count=(sb.s_blocks_count+sb.s_blocks_per_group-1)/sb.s_blocks_per_group;
  return 0;
}
static int cmpfile(ext2_context_t *ctx,const char *path,const char *host){
  FILE *h=fopen(host,"rb"); if(!h) return -1; fseek(h,0,SEEK_END); long n=ftell(h); fseek(h,0,SEEK_SET);
  uint8_t *a=malloc(n+16), *b=malloc(n+16); fread(a,1,n,h); fclose(h);
  int r=ext2_algo_read_file(ctx,path,b,n+16); int ok=(r==n && memcmp(a,b,n)==0); free(a); free(b); return ok?0:-2;
}
static void run_valid(const char *img){
  ext2_context_t c; uint8_t *buf; CHECK(mount(img,&c,&buf)==0);
  CHECK(cmpfile(&c,"/hello.txt","fsroot/hello.txt")==0);
  CHECK(cmpfile(&c,"/big.bin","fsroot/big.bin")==0);
  CHECK(cmpfile(&c,"/etc/cfg","fsroot/etc/cfg")==0);
  CHECK(cmpfile(&c,"/bin/file40","fsroot/bin/file40")==0);
  CHECK(cmpfile(&c,"/bin/file1","fsroot/bin/file1")==0);
  uint8_t t[16]; CHECK(ext2_algo_read_file(&c,"/nope",t,16)<0);
  CHECK(ext2_algo_read_file(&c,"/bin/nope",t,16)<0);
  fclose(fp); free(buf);
}
/* patch bytes of an image copy */
static void patch(const char *src,const char *dst,long off,const void *data,size_t n){
  char cmd[256]; snprintf(cmd,sizeof cmd,"cp %s %s",src,dst); system(cmd);
  FILE *f=fopen(dst,"r+b"); fseek(f,off,SEEK_SET); fwrite(data,1,n,f); fclose(f);
}
static void sbcase(const char *name,long fieldoff,const void *val,size_t n,int expect_reject){
  patch("img1024.img","bad.img",1024+fieldoff,val,n);
  ext2_context_t c; uint8_t *buf=NULL; int r=mount("bad.img",&c,&buf);
  if(expect_reject ? r==0 : r!=0){ printf("FAIL sb case %s (r=%d)\n",name,r); fails++; }
  if(fp){fclose(fp);fp=NULL;} free(buf);
}
int main(void){
  run_valid("img1024.img"); run_valid("img4096.img");
  /* superblock field offsets: s_inodes_count@0 s_blocks_count@4 s_first_data_block@20 s_log_block_size@24
     s_blocks_per_group@32 s_inodes_per_group@40 s_rev_level@76 s_inode_size@88 */
  uint32_t v;
  v=3;          sbcase("log_block_size=3",24,&v,4,1);
  v=31;         sbcase("log_block_size=31 (UB shift)",24,&v,4,1);
  v=0;          sbcase("blocks_per_group=0",32,&v,4,1);
  v=0;          sbcase("inodes_per_group=0",40,&v,4,1);
  v=0xFFFFFFF;  sbcase("blocks_per_group huge",32,&v,4,1);
  v=0xFFFFFFF;  sbcase("inodes_per_group huge",40,&v,4,1);
  uint16_t h=100;   sbcase("inode_size=100",88,&h,2,1);
  h=384;            sbcase("inode_size=384 (not pow2)",88,&h,2,1);
  h=8192;           sbcase("inode_size>block",88,&h,2,1);
  h=128;            sbcase("inode_size=128 (valid)",88,&h,2,0);
  v=0;          sbcase("blocks_count=0",4,&v,4,1);

  /* corrupt root directory entries: find root dir block via inode 2 */
  { ext2_context_t c; uint8_t *buf; mount("img1024.img",&c,&buf);
    ext2_inode_t root; CHECK(ext2_algo_read_inode(&c,2,&root)==0);
    long dirblk=root.i_block[0]; fclose(fp); free(buf);
    uint16_t rec[]={0xFFFF,3,0,7,1021};   /* rec_len variants: huge, <8, zero, unaligned, near-end */
    for(unsigned i=0;i<sizeof rec/2;i++){
      patch("img1024.img","bad.img",dirblk*1024+4,&rec[i],2);   /* first entry rec_len */
      ext2_context_t c2; uint8_t *b2; CHECK(mount("bad.img",&c2,&b2)==0);
      uint32_t ino; ext2_algo_lookup(&c2,"/hello.txt",&ino);    /* must not crash/overrun */
      fclose(fp); free(b2);
    }
    uint8_t nl=255; patch("img1024.img","bad.img",dirblk*1024+6,&nl,1);    /* name_len=255 with small rec_len */
    ext2_context_t c3; uint8_t *b3; CHECK(mount("bad.img",&c3,&b3)==0);
    uint32_t ino; CHECK(ext2_algo_lookup(&c3,"/hello.txt",&ino)!=0); fclose(fp); free(b3);
  }
  /* out-of-range data block pointer: hello.txt i_block[0] = 0xFFFFFFF0 */
  { system("cp img1024.img bad.img; debugfs -w -R 'sif /hello.txt block[0] 4294967280' bad.img >/dev/null 2>&1");
    ext2_context_t c; uint8_t *buf; CHECK(mount("bad.img",&c,&buf)==0);
    uint8_t t[64]; CHECK(ext2_algo_read_file(&c,"/hello.txt",t,64)<0); fclose(fp); free(buf); }
  /* out-of-range indirect pointer for big.bin */
  { system("cp img1024.img bad.img; debugfs -w -R 'sif /big.bin block[IND] 4294967280' bad.img >/dev/null 2>&1");
    ext2_context_t c; uint8_t *buf; CHECK(mount("bad.img",&c,&buf)==0);
    uint8_t *t=malloc(40000); CHECK(ext2_algo_read_file(&c,"/big.bin",t,40000)<0); free(t); fclose(fp); free(buf); }
  /* inode number out of range / group out of range */
  { ext2_context_t c; uint8_t *buf; mount("img1024.img",&c,&buf); ext2_inode_t in;
    CHECK(ext2_algo_read_inode(&c,0,&in)!=0); CHECK(ext2_algo_read_inode(&c,0xFFFFFFFF,&in)!=0);
    CHECK(ext2_algo_read_inode(&c,c.sb.s_inodes_count+1,&in)!=0); fclose(fp); free(buf); }
  printf(fails?"EXT2 TESTS FAILED (%d)\n":"ALL EXT2 TESTS PASSED\n",fails); return fails!=0;
}
