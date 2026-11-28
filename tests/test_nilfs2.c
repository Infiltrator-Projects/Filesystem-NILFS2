/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <infiltratr/filesystem_module_v2.h>
#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define SIZE (134217728u)
static uint8_t *image;
struct context{int fd,fail;uint32_t sector,allocs;uint64_t bytes;};
static int rd(void *p,uint64_t off,void *out,uint32_t n){
 struct context *c=p;assert(n&&n<=4096u&&!(off%c->sector)&&!(n%c->sector)&&off<=c->bytes&&n<=c->bytes-off);
 if(c->fail)return -1;
 if(c->fd>=0)return pread(c->fd,out,n,(off_t)off)==(ssize_t)n?0:-1;
 memcpy(out,image+off,n);return 0;
}
static void *allocate(void *p,size_t n){struct context*c=p;void *m=malloc(n);if(m)c->allocs++;return m;}
static void release(void *p,void *m){struct context*c=p;assert(m&&c->allocs);c->allocs--;free(m);}
static inline void put16(uint8_t*p,uint32_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static inline void put32(uint8_t*p,uint32_t n){put16(p,n);put16(p+2,n>>16);}
static inline void put64(uint8_t*p,uint64_t n){put32(p,(uint32_t)n);put32(p+4,(uint32_t)(n>>32));}
static inline uint32_t crc32(uint32_t c,const uint8_t*p,uint32_t n){
 while(n--){c^=*p++;for(unsigned i=0;i<8u;i++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}return c;
}

static void fixture(void){
 uint8_t *p=image+1024;put32(p,2);put16(p+6,0x3434);put16(p+8,280);put32(p+12,0x12345678);put32(p+20,2);
 put64(p+24,SIZE/4096/512-1);put64(p+32,SIZE);put64(p+40,1);put32(p+48,512);
 put64(p+56,1);put64(p+64,1);put64(p+72,1);put64(p+80,1000);put16(p+144,128);put16(p+146,32);put16(p+148,192);put16(p+150,16);
 put32(p+16,crc32(0x12345678,p,280));memcpy(image+SIZE-4096,p,1024);
 p=image+4096;put32(p+8,0x1eaffa11);put16(p+12,64);put16(p+14,7);put64(p+16,1);put32(p+40,2);put32(p+48,64);put64(p+56,1);
 uint8_t *root=p+4096;put16(root+4,400);put32(root,crc32(0x12345678,root+4,396));put32(p,crc32(0x12345678,p+4,8192-4));
}
static void corrupt(void){image[8192+10]^=1;}
static void absent(void){image[1030]=0;image[SIZE-4096+6]=0;}


#define B 4096u
#define SEED 0x12345678u
static uint8_t *blk(unsigned n){return image+n*B;}
static void inode_bytes(uint8_t*p,unsigned mode,unsigned links,uint64_t bytes,unsigned blocks){
 put64(p,blocks);put64(p+8,bytes);put16(p+48,mode);put16(p+50,links);
}
static void ptr(uint8_t*i,unsigned slot,uint64_t b){assert(slot<6);put64(i+64+slot*8,b);}
static void allocate_bit(uint8_t*p,unsigned n){p[n/8]|=(uint8_t)(1u<<(n%8));}
static void dirent(uint8_t*p,unsigned id,unsigned rec,unsigned type,const char*name){
 put64(p,id);put16(p+8,rec);p[10]=(uint8_t)strlen(name);p[11]=(uint8_t)type;memcpy(p+12,name,strlen(name));
}
static void refresh_anchor(void){
 uint8_t *p=blk(1),*r=blk(14);put32(r,crc32(SEED,r+4,396));put32(p,crc32(SEED,p+4,14u*B-4u));
}
static void populated(void){
 memset(image,0,SIZE);fixture();
 memset(blk(1),0,14u*B);
 uint8_t*s=image+1024;put16(s+116,1);put64(s+80,(SIZE/B/512-2)*512u);
 put32(s+16,0);put32(s+16,crc32(SEED,s,280));memcpy(image+SIZE-B,s,1024);
 uint8_t*p=blk(1);put32(p+8,0x1eaffa11);put16(p+12,64);put16(p+14,7);
 put64(p+16,1);put32(p+40,14);put32(p+48,64);put64(p+56,1);
 uint8_t*r=blk(14);put16(r+4,400);
 uint8_t*dat=r+16,*cp=r+144,*su=r+272;
 inode_bytes(dat,0100644,1,3u*B,3);ptr(dat,0,2);ptr(dat,1,3);ptr(dat,2,4);
 inode_bytes(cp,0100644,1,B,1);ptr(cp,0,1);
 inode_bytes(su,0100644,1,B,1);ptr(su,0,2);
 put32(blk(2),32768u-9u);
 for(unsigned v=1;v<=9;v++){
  allocate_bit(blk(3),v);
  uint8_t*e=blk(4)+32u*v;
  put64(e,4u+v);put64(e+8,1);put64(e+16,UINT64_MAX);
 }
 p=blk(5);put64(p,1);uint8_t*c=p+192;put64(c+24,1);put64(c+48,3);
 uint8_t*ifile=c+64;inode_bytes(ifile,0100644,1,3u*B,3);
 ptr(ifile,0,3);ptr(ifile,1,4);ptr(ifile,2,5);
 p=blk(6);put64(p,SIZE/B/512-2);put64(p+8,1);put64(p+16,0);
 put32(p+32+8,14);put32(p+32+12,2);
 put32(blk(7),32768u-3u);allocate_bit(blk(8),2);allocate_bit(blk(8),11);allocate_bit(blk(8),12);
 p=blk(9)+2u*128u;inode_bytes(p,0040755,2,B,1);ptr(p,0,6);
 p=blk(9)+11u*128u;inode_bytes(p,0100644,1,B*2u+5u,2);ptr(p,0,7);ptr(p,2,8);
 p=blk(9)+12u*128u;inode_bytes(p,0100644,1,5,1);ptr(p,0,9);
 p=blk(10);dirent(p,2,16,2,".");dirent(p+16,2,16,2,"..");
 dirent(p+32,11,24,1,"sparse.bin");dirent(p+56,12,B-56u,1,"hello.txt");
 memset(blk(11),'A',B);memcpy(blk(12),"tail!",5);memcpy(blk(13),"hello",5);
 refresh_anchor();
}
static void check_payload(const IfsFsCoreOpsV2*core,IfsFsHostRuntimeV2*h,struct context*c){
 populated();
 h->capabilities|=IFS_FS_HOST_EXCLUSIVE;
 void*v=NULL;IfsFsVolumeInfoV2 info={.structure_bytes=sizeof(info)};
 int rc=core->mount(h,&v,&info);if(rc)fprintf(stderr,"populated NILFS2 fixture mount %d\n",rc);
 assert(rc==0&&v&&info.free_bytes==(uint64_t)(SIZE/B/512-2)*512u*B);
 char name[256];IfsFsEntryV2 e={.structure_bytes=sizeof(e),.name=name,.name_capacity=sizeof(name)};
 assert(core->directory_entry(v,"/",0,&e)==0&&!strcmp(name,"sparse.bin"));
 assert(core->directory_entry(v,"/",1,&e)==0&&!strcmp(name,"hello.txt"));
 assert(core->directory_entry(v,"/",2,&e)==IFS_FS_END);
 assert(core->stat(v,"/sparse.bin",&e)==0&&e.size_bytes==2u*B+5u);
 uint8_t out[B+6u];
 assert(core->read(v,"/sparse.bin",B-1u,out,sizeof(out))==0);
 assert(out[0]=='A'&&!memcmp(out+B+1,"tail!",5));
 for(unsigned j=1;j<=B;j++)assert(out[j]==0);
 assert(core->read(v,"/hello.txt",0,out,5)==0&&!memcmp(out,"hello",5));
 assert(core->unmount(v)==0&&!c->allocs);
 /* A DAT record that expires before the mounted checkpoint must never
  * yield the stale physical payload. Recompute outer CRCs deliberately. */
 put64(blk(4)+9u*32u+16,1);refresh_anchor();
 assert(core->mount(h,&v,&info)==0&&v);
 assert(core->read(v,"/hello.txt",0,out,5)==IFS_FS_CORRUPT);
 assert(core->unmount(v)==0&&!c->allocs);
 populated();put32(blk(7),32768u-2u);refresh_anchor();
 assert(core->mount(h,&v,&info)==IFS_FS_CORRUPT&&!v&&!c->allocs);
 h->capabilities&=~IFS_FS_HOST_EXCLUSIVE;
}
static void expect(const IfsFsCoreOpsV2 *core,IfsFsHostRuntimeV2*h,struct context*c,int want){
 uint32_t v=99;int rc=core->probe(h,&v);
 if(rc!=want)fprintf(stderr,"expected %d got %d at sector %u\n",want,rc,c->sector);
 assert(rc==want&&(!rc?v!=0:v==0)&&!c->allocs);
}
int main(int argc,char **argv){
 assert(argc==2||argc==3);void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);assert(lib);
 IfsFsModuleApiEntryV2 get=(IfsFsModuleApiEntryV2)dlsym(lib,IFS_FS_COMMON_EXPORT_V2);assert(get);
 const IfsFsModuleApiV2*a=get();assert(ifs_fs_api_v2_valid(a,"nilfs2"));
 const IfsFsCoreOpsV2*core=(const void*)ifs_fs_group_v2(a,IFS_FS_GROUP_CORE);
 assert(core&&core->mount&&core->read&&!core->write&&!ifs_fs_group_v2(a,IFS_FS_GROUP_DEFRAG));
 struct context c={.fd=-1,.bytes=SIZE};
 if(argc==3){struct stat st;c.fd=open(argv[2],O_RDONLY);assert(c.fd>=0&&!fstat(c.fd,&st)&&st.st_size>0);c.bytes=(uint64_t)st.st_size;}
 else {image=calloc(1,SIZE);assert(image);}
 IfsFsHostRuntimeV2 h={.abi_version=2,.structure_bytes=sizeof(h),.capabilities=IFS_FS_HOST_READ_ONLY,.volume_bytes=c.bytes,.maximum_transfer_bytes=4096,.context=&c,.read_at=rd,.allocate=allocate,.release=release};
 for(c.sector=512;c.sector<=4096;c.sector*=8){
  h.logical_sector_bytes=h.physical_sector_bytes=c.sector;
  if(argc==3){
   expect(core,&h,&c,IFS_FS_OK);
   void *v=NULL;IfsFsVolumeInfoV2 info={.structure_bytes=sizeof(info)};
   assert(core->mount(&h,&v,&info)==IFS_FS_ACCESS&&!v);
   h.capabilities|=IFS_FS_HOST_EXCLUSIVE;
   int rc=core->mount(&h,&v,&info);if(rc)fprintf(stderr,"native NILFS2 mount failed: %d\n",rc);
   assert(rc==0&&v&&info.block_bytes==4096&&info.free_bytes>0);
   char name[256];IfsFsEntryV2 e={.structure_bytes=sizeof(e),.name=name,.name_capacity=sizeof(name)};
   assert(core->stat(v,"/",&e)==0&&e.type==IFS_FS_ENTRY_DIRECTORY);
   /* Ubuntu's nilfs-utils v2.2.9 explicitly creates the zero-length
    * .nilfs regular file (nilfs_mkfs_make_rootdir / make_dot_nilfs).
    * This is the independent formatter's actual namespace, not an
    * empty-root assumption. Check identity, type, length and exact EOF. */
   assert(core->directory_entry(v,"/",0,&e)==0&&!strcmp(name,".nilfs")&&e.type==IFS_FS_ENTRY_FILE&&e.size_bytes==0);
   assert(core->stat(v,"/.nilfs",&e)==0&&e.type==IFS_FS_ENTRY_FILE&&e.size_bytes==0);
   uint8_t byte=0;
   assert(core->read(v,"/.nilfs",0,&byte,0)==0);
   assert(core->read(v,"/.nilfs",0,&byte,1)==IFS_FS_INVALID);
   assert(core->directory_entry(v,"/",1,&e)==IFS_FS_END);
   assert(core->stat(v,"/absent",&e)==IFS_FS_NOT_FOUND);
   assert(core->unmount(v)==0&&!c.allocs);h.capabilities&=~IFS_FS_HOST_EXCLUSIVE;
   continue;
  }
  memset(image,0,SIZE);fixture();expect(core,&h,&c,IFS_FS_OK);
  c.fail=1;expect(core,&h,&c,IFS_FS_IO_ERROR);c.fail=0;
  corrupt();expect(core,&h,&c,IFS_FS_CORRUPT);
  memset(image,0,SIZE);fixture();absent();expect(core,&h,&c,IFS_FS_NOT_FOUND);
  check_payload(core,&h,&c);
 }
 if(c.fd>=0)assert(!close(c.fd));
 free(image);assert(!dlclose(lib));
 puts("NILFS2 native recognition, bounded reads and corruption refusal PASS");return 0;
}
