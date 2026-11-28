/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native bounded recognition using the documented on-disk structures and
 * the pinned Linux v6.12 format reference. No kernel, libc or tool backend.
 * Recognition is not evidence of a safe mounted namespace or mutation.
 */
#include <infiltratr/filesystem_module_v2.h>
static inline uint32_t le16(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8);}
static inline uint32_t le32(const uint8_t *p){return le16(p)|(le16(p+2)<<16);}
static inline uint64_t le64(const uint8_t *p){return le32(p)|((uint64_t)le32(p+4)<<32);}
static inline int equal(const uint8_t *a,const uint8_t *b,uint32_t n){
 for(uint32_t i=0;i<n;i++)if(a[i]!=b[i])return 0;
 return 1;
}
static inline uint32_t crc32(uint32_t c,const uint8_t *p,uint32_t n){
 while(n--){c^=*p++;for(unsigned i=0;i<8u;i++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}return c;
}
static int read_bytes(const IfsFsHostRuntimeV2 *h,uint64_t off,void *p,uint32_t n){
 if(off>h->volume_bytes||n>h->volume_bytes-off)return IFS_FS_NOT_FOUND;
 return ifs_fs_host_read_bytes_v2(h,off,p,n);
}

static int anchor(const IfsFsHostRuntimeV2 *h,const uint8_t *s)
{
    uint8_t p[4096];uint32_t b=1024u<<le32(s+20), seg=le32(s+48),seed=le32(s+12);
    uint64_t start=le64(s+64),limit=le64(s+32)/b;
    if(start>=limit)return IFS_FS_CORRUPT;
    int r=read_bytes(h,start*b,p,b);if(r)return r;
    uint32_t n=le32(p+40),head=le16(p+12),sum=le32(p+48),data=le32(p);
    if(le32(p+8)!=0x1eaffa11u||!(le16(p+14)&4u)||le64(p+16)!=le64(s+72)||
       !n||n>seg||n>limit-start||n>4096u||head<64u||head>b||
       sum<head||(uint64_t)sum>(uint64_t)n*b||le64(p+56)!=le64(s+56))
       return IFS_FS_CORRUPT;
    uint32_t c=crc32(seed,p+4,b-4);
    for(uint32_t k=1;k<n;k++){r=read_bytes(h,(start+k)*b,p,b);if(r)return r;c=crc32(c,p,b);}
    if(c!=data||n<2u)return IFS_FS_CORRUPT;
    /* The super-root has a separate native checksum over its declared
     * record length, excluding the checksum itself. */
    uint32_t bytes=le16(p+4);
    if(bytes<16u+3u*le16(s+144)||bytes>b||crc32(seed,p+4,bytes-4)!=le32(p))return IFS_FS_CORRUPT;
    return IFS_FS_OK;
}
static int sb_header(const IfsFsHostRuntimeV2 *h,uint8_t *p)
{
    if(le16(p+6)!=0x3434u)return IFS_FS_NOT_FOUND;
    if(le32(p)!=2u||le32(p+20)>2u||le64(p+272))return IFS_FS_UNSUPPORTED;
    uint32_t n=le16(p+8),seed=le32(p+12),want=le32(p+16);
    if(n<280u||n>1024u)return IFS_FS_CORRUPT;
    for(unsigned k=16;k<20;k++)p[k]=0;
    if(crc32(seed,p,n)!=want)return IFS_FS_CORRUPT;
    uint64_t size=le64(p+32),segments=le64(p+24),first=le64(p+40),block=1024u<<le32(p+20);
    uint32_t per=le32(p+48),inode=le16(p+144);
    if(!size||size>h->volume_bytes||!segments||!per||per>4096u||segments>size/block/per||
       first>=per||!le64(p+56)||le64(p+64)<first||le64(p+64)>=segments*per||
       le64(p+80)>segments*per||inode<128u||inode>(block-16u)/3u||
       le16(p+146)<32u||le16(p+148)<192u||le16(p+150)<16u)return IFS_FS_CORRUPT;
    return IFS_FS_OK;
}
/* Native redundant superblocks are intentionally allowed to contain
 * different committed checkpoints. Select by checkpoint number before
 * validating the referenced log, never silently fall back to older
 * content when the latest checksum-valid header has a corrupt anchor. */
static int select_header(const IfsFsHostRuntimeV2*h,uint8_t*a,uint8_t*b,const uint8_t**chosen,int mount){
 int x=sb_header(h,a),y=sb_header(h,b);
 if(x&&y)return x==IFS_FS_NOT_FOUND?y:x;
 if(mount&&(x||y))return IFS_FS_UNSUPPORTED;
 if(!x&&!y&&(!equal(a+20,b+20,36)||!equal(a+152,b+152,16)||!equal(a+144,b+144,8)||le32(a+12)!=le32(b+12)))return IFS_FS_CORRUPT;
 if(!x&&!y&&le64(a+56)==le64(b+56)&&!equal(a+56,b+56,24))return IFS_FS_CORRUPT;
 *chosen=x?b:y?a:le64(b+56)>le64(a+56)?b:a;
 if(mount&&(le16(*chosen+116)!=1u||(!x&&!y&&le64(a+56)==le64(b+56)&&(le16(a+116)!=1u||le16(b+116)!=1u))))return IFS_FS_UNSUPPORTED;
 return anchor(h,*chosen);
}
static int identify(const IfsFsHostRuntimeV2 *h,uint32_t *variant)
{
    uint8_t a[1024],b[1024];int r=read_bytes(h,1024,a,sizeof(a));if(r)return r;
    if(h->volume_bytes<8192u)return IFS_FS_NOT_FOUND;
    r=read_bytes(h,((h->volume_bytes/4096u)-1u)*4096u,b,sizeof(b));if(r)return r;
    const uint8_t*selected; r=select_header(h,a,b,&selected,0);if(r)return r;
    *variant=2;return IFS_FS_OK;
}
static int probe(const IfsFsHostRuntimeV2 *h,uint32_t *variant){
 if(!variant||!ifs_fs_host_v2_valid(h))return IFS_FS_INVALID;
 *variant=0;
 if(h->volume_bytes%h->logical_sector_bytes)return IFS_FS_INVALID;
 return identify(h,variant);
}
#include <infiltratr/filesystem_session_v2.h>
#include "reader.h"
static const IfsFsCoreOpsV2 core={
 .header={IFS_FS_GROUP_ABI_V2,sizeof(IfsFsCoreOpsV2),IFS_FS_GROUP_CORE,0,IFS_FS_CORE_PROBE|IFS_FS_CORE_MOUNT|IFS_FS_CORE_STAT|IFS_FS_CORE_LIST|IFS_FS_CORE_READ},.probe=probe,.mount=mount_volume,.unmount=unmount_volume,.stat=mounted_stat,.directory_entry=mounted_directory_entry,.read=mounted_read
};
static const IfsFsGroupHeaderV2 *query(uint32_t id,uint32_t min,uint32_t max){
 if(min>2u||max<2u)return NULL;
 if(id==IFS_FS_GROUP_CORE)return &core.header;
 return id==IFS_FS_GROUP_VOLUME?&volume_ops.header:NULL;
}
static const IfsFsModuleApiV2 api={
 .abi_version=2,.structure_bytes=sizeof(IfsFsModuleApiV2),.format_id="nilfs2",
#ifdef IFS_FREESTANDING
 .runtime_profiles=IFS_FS_PROFILE_FREESTANDING,
#else
 .runtime_profiles=IFS_FS_PROFILE_HOSTED,
#endif
 .group_mask=IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE)|IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME),.query_group=query
};
__attribute__((visibility("default"))) const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void){return &api;}
