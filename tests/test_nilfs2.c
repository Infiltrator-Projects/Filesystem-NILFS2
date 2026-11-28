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
   assert(core->directory_entry(v,"/",0,&e)==IFS_FS_END);
   assert(core->stat(v,"/absent",&e)==IFS_FS_NOT_FOUND);
   assert(core->unmount(v)==0&&!c.allocs);h.capabilities&=~IFS_FS_HOST_EXCLUSIVE;
   continue;
  }
  memset(image,0,SIZE);fixture();expect(core,&h,&c,IFS_FS_OK);
  c.fail=1;expect(core,&h,&c,IFS_FS_IO_ERROR);c.fail=0;
  corrupt();expect(core,&h,&c,IFS_FS_CORRUPT);
  memset(image,0,SIZE);fixture();absent();expect(core,&h,&c,IFS_FS_NOT_FOUND);
 }
 if(c.fd>=0)assert(!close(c.fd));
 free(image);assert(!dlclose(lib));
 puts("NILFS2 native recognition, bounded reads and corruption refusal PASS");return 0;
}
