#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#define DISK_SECTORS (64*1024*2)
static unsigned char g_disk[DISK_SECTORS * 512];
void ata_select(int d){(void)d;}
int  ata_current(void){return 0;}
int  ata_present(int d){return d==0;}
void ata_probe_all(void){}
void ata_read_sector(unsigned int lba, unsigned char* b){ for(int i=0;i<512;i++) b[i]=g_disk[lba*512+i]; }
void ata_write_sector(unsigned int lba, const unsigned char* b){ for(int i=0;i<512;i++) g_disk[lba*512+i]=b[i]; }
void print_string(const char* s){(void)s;}
void put_char(char c){(void)c;}
void print_int(int n){(void)n;}
#include "msfs.h"
static int fail=0,nt=0;
static void ck(int c,const char* n){nt++;printf(c?"[ OK ] %s\n":"[FAIL] %s\n",n);if(!c)fail++;}
int main(void){
  setvbuf(stdout,NULL,_IONBF,0);
  void* p=mmap((void*)0x02C00000,0x40000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
  if(p!=(void*)0x02C00000){fprintf(stderr,"mmap fail %p\n",p);return 2;}
  for(unsigned i=0;i<sizeof(g_disk);i++) g_disk[i]=(unsigned char)(i*7+3);
  msfs_use_drive(0);
  msfs_format();
  ck(msfs_probe()==1,"probe1");
  ck(msfs_write("/a.txt","AAAAA")==1,"write a");
  ck(msfs_mkdir("/dir")==1,"mkdir dir");
  ck(msfs_write("/dir/b.txt","BBBB")==1,"write dir/b");
  ck(msfs_write("/dir/c.txt","CCCC")==1,"write dir/c");
  char b[64];
  for(int i=0;i<64;i++)b[i]=0;
  int n=msfs_read_file("/a.txt",(unsigned char*)b,63); printf("  a='%s' (%d)\n",b,n);
  ck(n==5&&!strcmp(b,"AAAAA"),"read a");
  for(int i=0;i<64;i++)b[i]=0;
  n=msfs_read_file("/dir/b.txt",(unsigned char*)b,63); printf("  dir/b='%s' (%d)\n",b,n);
  ck(n==4&&!strcmp(b,"BBBB"),"read dir/b");
  ck(msfs_probe()==1,"re-probe");
  for(int i=0;i<64;i++)b[i]=0;
  n=msfs_read_file("/a.txt",(unsigned char*)b,63); printf("  a2='%s'\n",b);
  ck(n==5&&!strcmp(b,"AAAAA"),"re-read a");
  for(int i=0;i<64;i++)b[i]=0;
  n=msfs_read_file("/dir/b.txt",(unsigned char*)b,63); printf("  dir/b2='%s'\n",b);
  ck(n==4&&!strcmp(b,"BBBB"),"re-read dir/b");
  char names[64*13];int isd[64];unsigned sz[64];
  int c=msfs_read_dir("/dir",names,isd,sz,64);
  printf("  /dir has %d entries\n",c);
  ck(c==2,"dir has 2");
  printf("\nTests %d Failed %d\n",nt,fail);
  return fail;
}
