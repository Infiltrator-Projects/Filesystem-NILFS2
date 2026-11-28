/* SPDX-License-Identifier: GPL-3.0-or-later
 * Offline NILFS2 reader of a fully committed clean checkpoint.
 * DAT metadata uses physical mappings; all other metadata and ordinary
 * inode mappings use virtual block numbers resolved through the DAT.
 * The on-disk allocator is consulted before every DAT/ifile record.
 * No replay, cleaning, snapshot selection or writable operations.
 */
#define BS 4096u
#define INODE_BYTES 128u
#define MAX_SEGMENTS 1048576u
#define MAX_DIR_BLOCKS 4096u
typedef struct {
 const IfsFsHostRuntimeV2*h;
 uint64_t first,limit,segments,cno,free;
 uint32_t per;
 uint8_t dat[INODE_BYTES],cp[INODE_BYTES],su[INODE_BYTES],ifile[INODE_BYTES];
 uint8_t *live;
 int verified;
} Volume;
static void copy_bytes(uint8_t*d,const uint8_t*s,unsigned n){while(n--)*d++=*s++;}
static int physical(Volume*v,uint64_t b,uint8_t*p){
 if(b<v->first||b>=v->limit)return IFS_FS_CORRUPT;
 if(v->verified&&!(v->live[(b/v->per)/8u]&(1u<<((b/v->per)%8u))))return IFS_FS_CORRUPT;
 return ifs_fs_host_read_bytes_v2(v->h,b*BS,p,BS);
}
static int translate(Volume*v,uint64_t virtual,uint64_t*result);
/* Mapping node format: root keys and pointers occupy the fixed inode
 * bmap area; external nodes have the extra eight-byte pad and fixed
 * capacity key/pointer arrays. Bounds and ordering are checked before
 * following the chosen lower-bound child. DAT tree nodes are physical,
 * so translation never recursively depends on DAT itself. */
static int map(Volume*v,const uint8_t*inode,uint64_t key,int virt,uint64_t*result){
 const uint8_t*p=inode+56;uint8_t block[BS];
 *result=0;
 if(!(p[0]&1u)){
  if(p[0])return IFS_FS_UNSUPPORTED;
  if(key>=6u)return IFS_FS_OK;
  *result=le64(p+8+key*8u);return 0;
 }
 unsigned level=p[1],capacity=3u,base=8u;
 uint64_t visited[14];unsigned depth=0;
 if(!level||level>=14u)return IFS_FS_CORRUPT;
 for(;;){
  unsigned n=le16(p+2);
  if(p[0]!=(depth?0u:1u)||p[1]!=level||!n||n>capacity)return IFS_FS_CORRUPT;
  uint64_t prev=0,ptr=0,selected=0;
  for(unsigned j=0;j<n;j++){
   uint64_t k=le64(p+base+j*8u),b=le64(p+base+capacity*8u+j*8u);
   if((j&&k<=prev)||!b)return IFS_FS_CORRUPT;
   if(k<=key){selected=k;ptr=b;}prev=k;
  }
  if(!ptr)return 0;
  if(level==1u){if(selected==key)*result=ptr;return 0;}
  uint64_t b=ptr;int rc;
  if(virt){rc=translate(v,ptr,&b);if(rc)return rc;}
  for(unsigned j=0;j<depth;j++)if(visited[j]==b)return IFS_FS_CORRUPT;
  if(depth>=13u)return IFS_FS_CORRUPT;
  visited[depth++]=b;
  rc=physical(v,b,block);if(rc)return rc;
  p=block;capacity=(BS-16u)/16u;base=16u;level--;
 }
}
static int meta(Volume*v,const uint8_t*inode,uint64_t logical,int virt,uint8_t*p){
 uint64_t id;int rc=map(v,inode,logical,virt,&id);if(rc)return rc;
 if(!id)return IFS_FS_CORRUPT;
 if(virt){rc=translate(v,id,&id);if(rc)return rc;}
 return physical(v,id,p);
}
/* Persistent allocator: a descriptor supplies the free counter, its
 * bitmap supplies ownership, and the entry block supplies the record.
 * Check the selected group's complete bitmap count before reading. */
static int allocated_record(Volume*v,const uint8_t*inode,uint64_t id,unsigned bytes,int virt,uint8_t*out){
 const uint64_t entries=8u*BS,groups=BS/4u,per=BS/bytes,span=1u+(entries+per-1u)/per;
 uint64_t group=id/entries,slot=id%entries,desc_group=group/groups;
 if(desc_group>UINT64_MAX/(1u+groups*span))return IFS_FS_CORRUPT;
 uint64_t desc=desc_group*(1u+groups*span),delta=1u+(group%groups)*span;
 if(desc>UINT64_MAX-delta-span)return IFS_FS_CORRUPT;
 uint64_t bitmap=desc+delta;uint8_t p[BS];
 int rc=meta(v,inode,desc,virt,p);if(rc)return rc;
 uint32_t free=le32(p+4u*(group%groups));if(free>entries)return IFS_FS_CORRUPT;
 rc=meta(v,inode,bitmap,virt,p);if(rc)return rc;
 if(!(p[slot/8u]&(1u<<(slot%8u))))return IFS_FS_CORRUPT;
 uint32_t used=0;
 for(unsigned k=0;k<BS;k++)for(unsigned j=0;j<8u;j++)used+=(p[k]>>j)&1u;
 if(used+free!=entries)return IFS_FS_CORRUPT;
 rc=meta(v,inode,bitmap+1u+slot/per,virt,p);if(rc)return rc;
 copy_bytes(out,p+(slot%per)*bytes,bytes);return 0;
}
static int translate(Volume*v,uint64_t virtual,uint64_t*result){
 uint8_t r[32];if(!virtual)return IFS_FS_CORRUPT;
 int rc=allocated_record(v,v->dat,virtual,32,0,r);if(rc)return rc;
 uint64_t b=le64(r),start=le64(r+8),end=le64(r+16);
 if(!start||start>v->cno||end<=v->cno||b<v->first||b>=v->limit)return IFS_FS_CORRUPT;
 *result=b;return 0;
}
static int inode(Volume*v,uint64_t id,uint8_t*out){
 if(id!=2u&&id<11u)return IFS_FS_CORRUPT;
 int rc=allocated_record(v,v->ifile,id,128,1,out);if(rc)return rc;
 if(!le16(out+50)||le64(out+8)>v->limit*BS||le64(out)>v->limit)return IFS_FS_CORRUPT;
 return 0;
}
static int data_read(Volume*v,const uint8_t*i,uint64_t off,void*dest,uint32_t n,int holes){
 uint64_t size=le64(i+8);uint8_t*p=dest,block[BS];
 if((!p&&n)||off>size||n>size-off)return IFS_FS_INVALID;
 while(n){
  uint64_t b;int rc=map(v,i,off/BS,1,&b);if(rc)return rc;
  unsigned skip=(unsigned)(off%BS),take=BS-skip;if(take>n)take=n;
  if(!b){if(!holes)return IFS_FS_CORRUPT;for(unsigned j=0;j<take;j++)p[j]=0;}
  else{rc=translate(v,b,&b);if(rc)return rc;rc=physical(v,b,block);if(rc)return rc;copy_bytes(p,block+skip,take);}
  off+=take;p+=take;n-=take;
 }
 return 0;
}
typedef struct{uint64_t id;unsigned type,n;char name[256];} Dirent;
static int same(const uint8_t*p,const char*q,unsigned n){for(unsigned j=0;j<n;j++)if(p[j]!=(uint8_t)q[j])return 0;return 1;}
static int scan(Volume*v,const uint8_t*i,const char*name,unsigned length,uint32_t index,Dirent*out){
 if((le16(i+48)&0170000u)!=0040000u)return IFS_FS_INVALID;
 uint64_t size=le64(i+8);if(!size||size%BS||size/BS>MAX_DIR_BLOCKS)return IFS_FS_UNSUPPORTED;
 unsigned hits=0,dots=0;uint32_t ordinal=0;uint8_t p[BS];
 for(uint64_t off=0;off<size;off+=BS){
  int rc=data_read(v,i,off,p,BS,0);if(rc)return rc;
  for(unsigned at=0;at<BS;){
   if(BS-at<12u)return IFS_FS_CORRUPT;
   const uint8_t*d=p+at;unsigned rec=le16(d+8),n=d[10],type=d[11];uint64_t id=le64(d);
   if(rec<16u||rec%8u||rec>BS-at||n>rec-12u)return IFS_FS_CORRUPT;
   at+=rec;if(!id)continue;
   if(!n||type>7u)return IFS_FS_CORRUPT;
   for(unsigned j=0;j<n;j++)if(!d[12+j]||d[12+j]=='/')return IFS_FS_CORRUPT;
   if((n==1u&&d[12]=='.')||(n==2u&&d[12]=='.'&&d[13]=='.')){
    unsigned flag=n==1u?1u:2u;
    if((dots&flag)||type!=2u||off!=0u||id<2u)return IFS_FS_CORRUPT;
    dots|=flag;continue;
   }
   if((name&&n==length&&same(d+12,name,n))||(!name&&ordinal==index)){
    if(++hits!=1u)return IFS_FS_CORRUPT;
    out->id=id;out->type=type;out->n=n;
    for(unsigned j=0;j<n;j++)out->name[j]=(char)d[12+j];
    out->name[n]=0;
   }
   ordinal++;
  }
 }
 if(dots!=3u)return IFS_FS_CORRUPT;
 return hits?0:name?IFS_FS_NOT_FOUND:IFS_FS_END;
}
static int entry_inode(Volume*v,const Dirent*d,uint8_t*i){
 int rc=inode(v,d->id,i);if(rc)return rc;
 unsigned mode=le16(i+48)&0170000u;
 if((d->type==1u&&mode!=0100000u)||(d->type==2u&&mode!=0040000u)||(d->type==7u&&mode!=0120000u))return IFS_FS_CORRUPT;
 if(d->type!=0u&&d->type!=1u&&d->type!=2u&&d->type!=7u)return IFS_FS_UNSUPPORTED;
 return 0;
}
static int resolve(Volume*v,const char*path,uint8_t*i,const char**name,unsigned*length){
 if(!v||!path||path[0]!='/')return IFS_FS_INVALID;
 int rc=inode(v,2,i);if(rc)return rc;
 *name=path;*length=1u;const char*p=path+1;
 for(unsigned depth=0;*p;depth++){
  if(depth>=64u)return IFS_FS_UNSUPPORTED;
  unsigned n=0;while(p[n]&&p[n]!='/'){if(n==255u)return IFS_FS_INVALID;n++;}
  if(!n||(n==1u&&p[0]=='.')||(n==2u&&p[0]=='.'&&p[1]=='.'))return IFS_FS_INVALID;
  Dirent d;rc=scan(v,i,p,n,0,&d);if(rc)return rc;
  rc=entry_inode(v,&d,i);if(rc)return rc;
  *name=p;*length=n;p+=n;if(*p=='/'){p++;if(!*p)return IFS_FS_INVALID;}
 }
 return 0;
}
static int fill(const uint8_t*i,const char*n,unsigned len,IfsFsEntryV2*e){
 if(!e||e->structure_bytes<sizeof(*e)||!e->name||len>=e->name_capacity)return IFS_FS_INVALID;
 switch(le16(i+48)&0170000u){case 0100000u:e->type=IFS_FS_ENTRY_FILE;break;case 0040000u:e->type=IFS_FS_ENTRY_DIRECTORY;break;case 0120000u:e->type=IFS_FS_ENTRY_LINK;break;default:return IFS_FS_UNSUPPORTED;}
 for(unsigned j=0;j<len;j++)e->name[j]=n[j];
 e->name[len]=0;e->name_bytes=len;e->size_bytes=le64(i+8);e->attributes=0;return 0;
}
/* The segment-usage file, not a guessed device capacity, establishes
 * the set of non-clean segments and actual clean-segment capacity.
 * Metadata is subsequently re-read with the established ownership map. */
static int usage(Volume*v){
 uint8_t p[BS];int rc=meta(v,v->su,0,1,p);if(rc)return rc;
 uint64_t want=le64(p),dirty=le64(p+8),clean=0,seen_dirty=0,loaded=0;
 if(want>v->segments||dirty>v->segments-want)return IFS_FS_CORRUPT;
 for(uint64_t s=0;s<v->segments;s++){
  uint64_t slot=s+2u,logical=slot/(BS/16u);
  if(logical!=loaded){rc=meta(v,v->su,logical,1,p);if(rc)return rc;loaded=logical;}
  const uint8_t*d=p+(slot%(BS/16u))*16u;unsigned n=le32(d+8),flags=le32(d+12);
  if(n>v->per||flags&~7u||flags&4u)return IFS_FS_UNSUPPORTED;
  if(!flags){if(n)return IFS_FS_CORRUPT;clean++;}
  else{if(flags&2u)seen_dirty++;v->live[s/8u]|=(uint8_t)(1u<<(s%8u));}
 }
 if(clean!=want||seen_dirty!=dirty)return IFS_FS_CORRUPT;
 v->free=clean*v->per*BS;return 0;
}
static int info(void*vol,IfsFsVolumeInfoV2*out){
 Volume*v=vol;if(!v||!out||out->structure_bytes<sizeof(*out))return IFS_FS_INVALID;
 out->variant=2;out->block_bytes=BS;out->max_name_bytes=255;out->total_bytes=v->limit*BS;out->free_bytes=v->free;return 0;
}
static void destroy(Volume*v){if(v->live)v->h->release(v->h->context,v->live);v->h->release(v->h->context,v);}
static int checkpoint(Volume*v,uint8_t*out){
 uint8_t p[BS];uint64_t slots=BS/192u;
 int rc=meta(v,v->cp,v->cno/slots,1,p);if(rc)return rc;
 const uint8_t*d=p+(v->cno%slots)*192u;
 if(le32(d)&2u||le64(d+24)!=v->cno||!le64(d+48))return IFS_FS_CORRUPT;
 copy_bytes(out,d+64,128);return 0;
}
static int mount_volume(const IfsFsHostRuntimeV2*h,void**out,IfsFsVolumeInfoV2*vi){
 if(!out||!vi||vi->structure_bytes<sizeof(*vi)||!ifs_fs_host_v2_valid(h))return IFS_FS_INVALID;
 *out=NULL;
 if((h->capabilities&(IFS_FS_HOST_READ_ONLY|IFS_FS_HOST_EXCLUSIVE))!=(IFS_FS_HOST_READ_ONLY|IFS_FS_HOST_EXCLUSIVE))return IFS_FS_ACCESS;
 uint32_t variant;int rc=identify(h,&variant);if(rc)return rc;
 uint8_t a[1024],b[1024],p[BS],root[128],verify[128];
 rc=read_bytes(h,1024,a,1024);if(rc)return rc;
 rc=read_bytes(h,(h->volume_bytes/BS-1u)*BS,b,1024);if(rc)return rc;
 /* Initial profile requires two coherent committed clean copies.
  * Do not silently fall back to an older checkpoint or replay logs. */
 if(sb(h,a)||sb(h,b)||!equal(a+56,b+56,24)||le16(a+116)!=1u||le16(b+116)!=1u||
    le32(a+20)!=2u||le16(a+144)!=128u||le16(a+146)!=32u||le16(a+148)!=192u||le16(a+150)!=16u||
    le64(a+264)&~UINT64_C(1)||le64(a+24)>MAX_SEGMENTS)return IFS_FS_UNSUPPORTED;
 Volume*v=h->allocate(h->context,sizeof(*v));if(!v)return IFS_FS_NO_MEMORY;
 v->h=h;v->first=le64(a+40);v->segments=le64(a+24);v->per=le32(a+48);v->limit=v->segments*v->per;v->cno=le64(a+56);v->free=0;v->live=NULL;v->verified=0;
 uint64_t start=le64(a+64);
 rc=physical(v,start,p);if(rc)goto done;
 uint32_t n=le32(p+40);
 if(!n||n>v->limit-start||(start+n-1u)/v->per!=start/v->per){rc=IFS_FS_CORRUPT;goto done;}
 rc=physical(v,start+n-1u,p);if(rc)goto done;
 copy_bytes(v->dat,p+16,128);copy_bytes(v->cp,p+144,128);copy_bytes(v->su,p+272,128);
 size_t bytes=(size_t)((v->segments+7u)/8u);v->live=h->allocate(h->context,bytes);
 if(!v->live){rc=IFS_FS_NO_MEMORY;goto done;}
 for(size_t j=0;j<bytes;j++)v->live[j]=0;
 rc=usage(v);if(rc)goto done;
 v->verified=1;
 rc=physical(v,start+n-1u,p);if(rc)goto done;
 rc=checkpoint(v,v->ifile);if(rc)goto done;
 rc=inode(v,2,root);if(rc)goto done;
 rc=checkpoint(v,verify);if(rc)goto done;
 if(!equal(v->ifile,verify,128)){rc=IFS_FS_CORRUPT;goto done;}
 Dirent d;rc=scan(v,root,NULL,0,UINT32_MAX,&d);
 if(rc==IFS_FS_END)rc=0;
 if(rc)goto done;
 *out=v;return info(v,vi);
done:destroy(v);return rc;
}
static int unmount_volume(void*v){if(!v)return IFS_FS_INVALID;destroy(v);return 0;}
static int stat_path(void*v,const char*path,IfsFsEntryV2*e){uint8_t i[128];const char*n;unsigned l;int rc=resolve(v,path,i,&n,&l);return rc?rc:fill(i,n,l,e);}
static int list_path(void*v,const char*path,uint32_t index,IfsFsEntryV2*e){
 uint8_t i[128];const char*n;unsigned l;Dirent d;int rc=resolve(v,path,i,&n,&l);
 if(!rc)rc=scan(v,i,NULL,0,index,&d);
 if(!rc)rc=entry_inode(v,&d,i);
 return rc?rc:fill(i,d.name,d.n,e);
}
static int read_path(void*v,const char*path,uint64_t off,void*out,uint32_t n){
 uint8_t i[128];const char*name;unsigned l;int rc=resolve(v,path,i,&name,&l);if(rc)return rc;
 if((le16(i+48)&0170000u)!=0100000u)return IFS_FS_INVALID;
 return data_read(v,i,off,out,n,1);
}
static const IfsFsVolumeOpsV2 volume_ops={.header={2,sizeof(IfsFsVolumeOpsV2),IFS_FS_GROUP_VOLUME,0,IFS_FS_VOLUME_QUERY},.query=info};
