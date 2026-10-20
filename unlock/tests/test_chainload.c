/* Windows Boot Manager chainload: real chainload.c, fake boot services. */
#include "../chainload.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"%s:%d: CHECK(%s) failed\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
#define H(n) ((EFI_HANDLE)(UINTN)(n))
enum path_kind {PATH_NVME,PATH_USB,PATH_BAD,PATH_NONE};
typedef struct {EFI_HANDLE handle;enum path_kind path;BOOLEAN file,directory;} FS_FAKE;
enum load_mode {LOAD_OK,LOAD_FAIL,LOAD_SECURITY,LOAD_OTHER_DEVICE,LOAD_WRONG_TYPE};
static EFI_BOOT_SERVICES bs;
static EFI_LOADED_IMAGE_PROTOCOL self_image,boot_image;
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL volumes[6];
static EFI_FILE_PROTOCOL root_file,boot_file;
static FS_FAKE fses[6];
static UINTN fs_count,locate_count;
static EFI_STATUS locate_status;
static enum load_mode load_mode;
static EFI_STATUS start_status;
static FS_FAKE *opened_fs;
static int pool_live,files_open,load_calls,start_calls,unload_calls,watchdog_set,watchdog_clear;
static UINTN last_watchdog=999;
static UINT8 loaded_path[512];static UINTN loaded_path_bytes;
static EFI_GUID lg=EFI_LOADED_IMAGE_PROTOCOL_GUID,dg=EFI_DEVICE_PATH_PROTOCOL_GUID,
  fg=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,ig=EFI_FILE_INFO_ID;
/* PciRoot(0)/Pci(0x1d,0)/NVMe(1,...)/HD(...) shortened to PciRoot/Pci/NVMe; then End. */
static const UINT8 nvme_path[]={2,1,12,0,0xd0,0x41,3,0xa,0,0,0,0, 1,1,6,0,0,0x1d,
  3,0x17,16,0,1,0,0,0,1,2,3,4,5,6,7,8, 0x7f,0xff,4,0};
static const UINT8 usb_path[]={2,1,12,0,0xd0,0x41,3,0xa,0,0,0,0, 3,5,6,0,1,0, 0x7f,0xff,4,0};
static const UINT8 bad_path[]={2,1,2,0, 0x7f,0xff,4,0};
static int guid(const EFI_GUID *a,const EFI_GUID *b){return !memcmp(a,b,sizeof(*a));}
static FS_FAKE *find(EFI_HANDLE h){for(UINTN i=0;i<fs_count;i++)if(fses[i].handle==h)return &fses[i];return NULL;}
static EFI_STATUS EFIAPI allocate_pool(EFI_MEMORY_TYPE type,UINTN n,VOID **p) {
  CHECK(type==EfiLoaderData && n && n<4096);*p=malloc(n);CHECK(*p);pool_live++;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI free_pool(VOID *p){CHECK(p && pool_live>0);free(p);pool_live--;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI locate(EFI_LOCATE_SEARCH_TYPE type,EFI_GUID *g,VOID *key,UINTN *n,EFI_HANDLE **out) {
  CHECK(type==ByProtocol && guid(g,&fg) && !key);
  if(EFI_ERROR(locate_status)){*n=0;*out=NULL;return locate_status;}
  *n=locate_count?locate_count:fs_count;allocate_pool(EfiLoaderData,*n*sizeof(EFI_HANDLE),(VOID **)out);
  for(UINTN i=0;i<*n;i++)(*out)[i]=i<fs_count?fses[i].handle:H(100+i);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI handle(EFI_HANDLE h,EFI_GUID *g,VOID **p) {
  *p=NULL;
  if(h==H(1) && guid(g,&lg))*p=&self_image;
  else if(h==H(20) && guid(g,&lg))*p=&boot_image;
  else if(find(h) && guid(g,&fg))*p=&volumes[find(h)-fses];
  else if(find(h) && guid(g,&dg)) {
    enum path_kind k=find(h)->path;
    if(k==PATH_NONE)return EFI_UNSUPPORTED;
    *p=(VOID *)(k==PATH_NVME?nvme_path:k==PATH_USB?usb_path:bad_path);
  }
  return *p?EFI_SUCCESS:EFI_UNSUPPORTED;
}
static void narrow(char *d,UINTN n,const CHAR16 *s){UINTN i=0;while(i+1<n && s[i]){d[i]=(char)s[i];i++;}d[i]=0;}
static EFI_STATUS EFIAPI file_close(EFI_FILE_PROTOCOL *f){CHECK(files_open>0);(void)f;files_open--;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI file_open(EFI_FILE_PROTOCOL *f,EFI_FILE_PROTOCOL **out,CHAR16 *name,UINT64 mode,UINT64 attributes) {
  char text[128];narrow(text,sizeof(text),name);
  CHECK(f==&root_file && opened_fs && mode==EFI_FILE_MODE_READ && !attributes);
  CHECK(!strcmp(text,"\\EFI\\Microsoft\\Boot\\bootmgfw.efi"));
  *out=NULL;if(!opened_fs->file)return EFI_NOT_FOUND;
  files_open++;*out=&boot_file;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI file_info(EFI_FILE_PROTOCOL *f,EFI_GUID *g,UINTN *n,VOID *data) {
  CHECK(f==&boot_file && guid(g,&ig) && *n>=sizeof(EFI_FILE_INFO));
  EFI_FILE_INFO *info=data;memset(info,0,sizeof(*info));info->Size=sizeof(*info);
  info->Attribute=opened_fs->directory?EFI_FILE_DIRECTORY:0;info->FileSize=opened_fs->directory?0:2400000;
  *n=sizeof(*info);return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI file_write(EFI_FILE_PROTOCOL *f,UINTN *n,VOID *d){(void)f;(void)n;(void)d;CHECK(0);return EFI_ACCESS_DENIED;}
static EFI_STATUS EFIAPI volume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *p,EFI_FILE_PROTOCOL **root) {
  opened_fs=&fses[p-volumes];CHECK(opened_fs->path==PATH_NVME);
  files_open++;*root=&root_file;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI load_image(BOOLEAN policy,EFI_HANDLE parent,EFI_DEVICE_PATH_PROTOCOL *path,VOID *source,UINTN size,EFI_HANDLE *image) {
  CHECK(!policy && parent==H(1) && path && !source && !size && image && !files_open);load_calls++;
  const UINT8 *p=(const UINT8 *)path;UINTN total=0;
  for(;;){UINTN n=p[total+2]|(p[total+3]<<8);CHECK(n>=4 && total+n<=sizeof(loaded_path));total+=n;if(p[total-n]==0x7f)break;}
  memcpy(loaded_path,p,total);loaded_path_bytes=total;
  if(load_mode==LOAD_FAIL){*image=NULL;return EFI_NOT_FOUND;}
  *image=H(20);
  memset(&boot_image,0,sizeof(boot_image));boot_image.ParentHandle=parent;boot_image.ImageCodeType=EfiLoaderCode;
  boot_image.DeviceHandle=load_mode==LOAD_OTHER_DEVICE?H(4):H(3);
  if(load_mode==LOAD_WRONG_TYPE)boot_image.ImageCodeType=EfiBootServicesCode;
  return load_mode==LOAD_SECURITY?EFI_SECURITY_VIOLATION:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI unload_image(EFI_HANDLE image){CHECK(image==H(20) && !start_calls);unload_calls++;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI start_image(EFI_HANDLE image,UINTN *n,CHAR16 **data) {
  CHECK(image==H(20) && n && data && !unload_calls && last_watchdog==GU_CHAIN_WATCHDOG_SECONDS);start_calls++;
  if(EFI_ERROR(start_status)){*n=8;allocate_pool(EfiLoaderData,8,(VOID **)data);memset(*data,0,8);}
  return start_status;
}
static EFI_STATUS EFIAPI watchdog(UINTN seconds,UINT64 code,UINTN n,CHAR16 *data) {
  CHECK(!code && !n && !data);last_watchdog=seconds;if(seconds)watchdog_set++;else watchdog_clear++;return EFI_SUCCESS;
}
static void setup(void) {
  memset(&bs,0,sizeof(bs));memset(fses,0,sizeof(fses));fs_count=locate_count=0;locate_status=EFI_SUCCESS;
  load_mode=LOAD_OK;start_status=EFI_SUCCESS;opened_fs=NULL;last_watchdog=999;loaded_path_bytes=0;
  pool_live=files_open=load_calls=start_calls=unload_calls=watchdog_set=watchdog_clear=0;
  bs.AllocatePool=allocate_pool;bs.FreePool=free_pool;bs.LocateHandleBuffer=locate;bs.HandleProtocol=handle;
  bs.LoadImage=load_image;bs.StartImage=start_image;bs.UnloadImage=unload_image;bs.SetWatchdogTimer=watchdog;
  memset(&self_image,0,sizeof(self_image));self_image.DeviceHandle=H(2);
  for(int i=0;i<6;i++)volumes[i].OpenVolume=volume;
  memset(&root_file,0,sizeof(root_file));root_file.Open=file_open;root_file.Close=file_close;root_file.Write=file_write;
  memset(&boot_file,0,sizeof(boot_file));boot_file.Close=file_close;boot_file.GetInfo=file_info;boot_file.Write=file_write;
}
static void add(UINTN h,enum path_kind path,BOOLEAN file){fses[fs_count++]=(FS_FAKE){H(h),path,file,FALSE};}
static void standard(void){add(2,PATH_USB,TRUE);add(3,PATH_NVME,TRUE);}
static void finished(GU_CHAIN *c){CHECK(!pool_live && !files_open);(void)c;}
static void expect_none(GU_CHAIN *c,EFI_STATUS want) {
  CHECK(GuChainPreload(&bs,H(1),c)==want && c->status==want && !c->image && !load_calls && !unload_calls);
  CHECK(GuChainStart(&bs,c)==EFI_NOT_READY && !start_calls && !watchdog_set);finished(c);
}
int main(void) {
  GU_CHAIN c;int cases=0;
  /* Success: USB self skipped, NVMe Windows loaded, path = NVMe prefix + File + End. */
  setup();standard();
  CHECK(GuChainPreload(&bs,H(1),&c)==EFI_SUCCESS && c.image==H(20) && c.device==H(3));
  CHECK(c.scanned==2 && c.skipped_self==1 && c.candidates==1 && load_calls==1 && !start_calls);
  {
    UINTN prefix=sizeof(nvme_path)-4,name=sizeof(GU_CHAIN_FILE);
    CHECK(loaded_path_bytes==prefix+4+name+4 && !memcmp(loaded_path,nvme_path,prefix));
    CHECK(loaded_path[prefix]==MEDIA_DEVICE_PATH && loaded_path[prefix+1]==MEDIA_FILEPATH_DP);
    CHECK(loaded_path[prefix+2]==(UINT8)(4+name) && !loaded_path[prefix+3]);
    CHECK(!memcmp(loaded_path+prefix+4,GU_CHAIN_FILE,name));
    CHECK(!memcmp(loaded_path+prefix+4+name,"\x7f\xff\x04\x00",4));
  }
  CHECK(!pool_live && !files_open);
  CHECK(GuChainStart(&bs,&c)==EFI_SUCCESS && start_calls==1 && !c.image && !unload_calls);
  CHECK(watchdog_set==1 && watchdog_clear==1 && !last_watchdog);
  CHECK(GuChainDiscard(&bs,&c)==EFI_SUCCESS && !unload_calls);finished(&c);cases++;
  /* Cancel/failure path: preloaded image is unloaded exactly once, never started. */
  setup();standard();
  CHECK(GuChainPreload(&bs,H(1),&c)==EFI_SUCCESS);
  CHECK(GuChainDiscard(&bs,&c)==EFI_SUCCESS && unload_calls==1 && !c.image);
  CHECK(GuChainDiscard(&bs,&c)==EFI_SUCCESS && unload_calls==1);
  CHECK(GuChainStart(&bs,&c)==EFI_NOT_READY && !start_calls);finished(&c);cases++;
  /* Boot manager returns (e.g. error): watchdog cleared, exit data freed, handle dropped. */
  setup();standard();start_status=EFI_LOAD_ERROR;
  CHECK(GuChainPreload(&bs,H(1),&c)==EFI_SUCCESS);
  CHECK(GuChainStart(&bs,&c)==EFI_LOAD_ERROR && !c.image && watchdog_clear==1 && !last_watchdog);finished(&c);cases++;
  /* No Windows anywhere. */
  setup();add(2,PATH_USB,FALSE);add(3,PATH_NVME,FALSE);expect_none(&c,EFI_NOT_FOUND);cases++;
  /* Only the launching USB holds bootmgfw.efi (Windows install media). */
  setup();add(2,PATH_USB,TRUE);add(3,PATH_NVME,FALSE);expect_none(&c,EFI_NOT_FOUND);CHECK(c.skipped_self==1);cases++;
  /* Another USB stick with Windows media is never chosen. */
  setup();add(2,PATH_USB,FALSE);add(5,PATH_USB,TRUE);expect_none(&c,EFI_NOT_FOUND);CHECK(c.skipped_usb==1);cases++;
  /* Two internal Windows installs: ambiguous, refuse. */
  setup();standard();add(4,PATH_NVME,TRUE);expect_none(&c,EFI_UNSUPPORTED);CHECK(c.candidates==2);cases++;
  /* A directory with the boot manager's name is not the boot manager. */
  setup();add(2,PATH_USB,FALSE);add(3,PATH_NVME,TRUE);fses[1].directory=TRUE;expect_none(&c,EFI_NOT_FOUND);cases++;
  /* Malformed or missing device paths are skipped. */
  setup();add(2,PATH_USB,FALSE);add(6,PATH_BAD,TRUE);add(7,PATH_NONE,TRUE);expect_none(&c,EFI_NOT_FOUND);CHECK(c.skipped_path==2);cases++;
  /* Firmware enumeration failures. */
  setup();locate_status=EFI_NOT_FOUND;expect_none(&c,EFI_NOT_FOUND);cases++;
  setup();standard();locate_count=GU_CHAIN_MAX_FS+1;expect_none(&c,EFI_NOT_FOUND);cases++;
  /* LoadImage failures never leave an image or memory behind. */
  setup();standard();load_mode=LOAD_FAIL;
  CHECK(GuChainPreload(&bs,H(1),&c)==EFI_NOT_FOUND && !c.image && load_calls==1 && !unload_calls);finished(&c);cases++;
  setup();standard();load_mode=LOAD_SECURITY;
  CHECK(GuChainPreload(&bs,H(1),&c)==EFI_SECURITY_VIOLATION && !c.image && unload_calls==1);finished(&c);cases++;
  setup();standard();load_mode=LOAD_OTHER_DEVICE;
  CHECK(GuChainPreload(&bs,H(1),&c)==EFI_COMPROMISED_DATA && !c.image && unload_calls==1);finished(&c);cases++;
  setup();standard();load_mode=LOAD_WRONG_TYPE;
  CHECK(GuChainPreload(&bs,H(1),&c)==EFI_COMPROMISED_DATA && !c.image && unload_calls==1);finished(&c);cases++;
  printf("%d chainload scenarios passed; firmware services simulated.\n",cases);
  return 0;
}
