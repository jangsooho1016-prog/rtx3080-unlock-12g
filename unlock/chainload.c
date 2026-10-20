#include "chainload.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>
static EFI_GUID ChainLoadedGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID,ChainPathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID,
  ChainFsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,ChainInfoGuid=EFI_FILE_INFO_ID;
static CHAR16 ChainFile[]=GU_CHAIN_FILE;
/* Bounded walk of a whole device path. Returns the byte length before the
   End-Entire node, or 0 for a malformed path. */
static UINTN ChainPathPrefix(const EFI_DEVICE_PATH_PROTOCOL *Path,BOOLEAN *Usb) {
  UINTN total=0;*Usb=FALSE;
  for(UINTN nodes=0;nodes<64 && total+4<=4096;nodes++) {
    UINTN n=Path->Length[0]+((UINTN)Path->Length[1]<<8);
    if(n<4 || n>4096-total)return 0;
    if(Path->Type==MESSAGING_DEVICE_PATH && Path->SubType==MSG_USB_DP)*Usb=TRUE;
    if(Path->Type==END_DEVICE_PATH_TYPE)
      return n==4 && Path->SubType==END_ENTIRE_DEVICE_PATH_SUBTYPE && total?total:0;
    total+=n;Path=(const VOID *)((const UINT8 *)Path+n);
  }
  return 0;
}
/* Read-only existence check: a regular, non-empty file at GU_CHAIN_FILE. */
static BOOLEAN ChainHasFile(EFI_BOOT_SERVICES *Bs,EFI_HANDLE Handle) {
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs=NULL;EFI_FILE_PROTOCOL *root=NULL,*file=NULL;BOOLEAN found=FALSE;
  if(EFI_ERROR(Bs->HandleProtocol(Handle,&ChainFsGuid,(VOID **)&fs)) || !fs || !fs->OpenVolume)return FALSE;
  if(EFI_ERROR(fs->OpenVolume(fs,&root)) || !root)return FALSE;
  if(!EFI_ERROR(root->Open(root,&file,ChainFile,EFI_FILE_MODE_READ,0)) && file) {
    union {EFI_FILE_INFO info;UINT8 bytes[512];} data;UINTN n=sizeof(data);
    found=!EFI_ERROR(file->GetInfo(file,&ChainInfoGuid,&n,&data)) && n>=SIZE_OF_EFI_FILE_INFO && n<=sizeof(data) &&
      !(data.info.Attribute&EFI_FILE_DIRECTORY) && data.info.FileSize;
    file->Close(file);
  }
  root->Close(root);
  return found;
}
EFI_STATUS GuChainPreload(EFI_BOOT_SERVICES *Bs,EFI_HANDLE Self,GU_CHAIN *Chain) {
  EFI_LOADED_IMAGE_PROTOCOL *self=NULL;EFI_HANDLE *handles=NULL,device=NULL;UINTN count=0;
  EFI_DEVICE_PATH_PROTOCOL *base=NULL;UINTN prefix=0;
  Chain->image=Chain->device=NULL;
  Chain->scanned=Chain->skipped_self=Chain->skipped_usb=Chain->skipped_path=Chain->candidates=0;
  EFI_STATUS status=Bs->HandleProtocol(Self,&ChainLoadedGuid,(VOID **)&self);
  if(EFI_ERROR(status) || !self)return Chain->status=EFI_UNSUPPORTED;
  status=Bs->LocateHandleBuffer(ByProtocol,&ChainFsGuid,NULL,&count,&handles);
  if(EFI_ERROR(status) || !handles || !count || count>GU_CHAIN_MAX_FS) {
    if(handles)Bs->FreePool(handles);
    return Chain->status=EFI_ERROR(status)?status:EFI_NOT_FOUND;
  }
  for(UINTN i=0;i<count;i++) {
    EFI_DEVICE_PATH_PROTOCOL *path=NULL;BOOLEAN usb=FALSE;UINTN n=0;Chain->scanned++;
    /* The USB stick that launched this app may itself carry Windows media. */
    if(handles[i]==self->DeviceHandle){Chain->skipped_self++;continue;}
    if(EFI_ERROR(Bs->HandleProtocol(handles[i],&ChainPathGuid,(VOID **)&path)) || !path ||
       !(n=ChainPathPrefix(path,&usb))){Chain->skipped_path++;continue;}
    if(usb){Chain->skipped_usb++;continue;}
    if(!ChainHasFile(Bs,handles[i]))continue;
    Chain->candidates++;device=handles[i];base=path;prefix=n;
  }
  Bs->FreePool(handles);
  /* Ambiguity is refused: two Windows installs must be picked in firmware. */
  if(Chain->candidates!=1)return Chain->status=Chain->candidates?EFI_UNSUPPORTED:EFI_NOT_FOUND;
  UINTN file_node=4+sizeof(ChainFile),bytes=prefix+file_node+4;
  UINT8 *full=NULL;
  status=Bs->AllocatePool(EfiLoaderData,bytes,(VOID **)&full);
  if(EFI_ERROR(status) || !full)return Chain->status=EFI_ERROR(status)?status:EFI_OUT_OF_RESOURCES;
  const UINT8 *source=(const UINT8 *)base,*name=(const UINT8 *)ChainFile;
  for(UINTN i=0;i<prefix;i++)full[i]=source[i];
  UINT8 *node=full+prefix;
  node[0]=MEDIA_DEVICE_PATH;node[1]=MEDIA_FILEPATH_DP;node[2]=(UINT8)file_node;node[3]=(UINT8)(file_node>>8);
  for(UINTN i=0;i<sizeof(ChainFile);i++)node[4+i]=name[i];
  node+=file_node;node[0]=END_DEVICE_PATH_TYPE;node[1]=END_ENTIRE_DEVICE_PATH_SUBTYPE;node[2]=4;node[3]=0;
  EFI_HANDLE image=NULL;
  /* Load only. StartImage waits until the GPU transaction is closed. */
  status=Bs->LoadImage(FALSE,Self,(EFI_DEVICE_PATH_PROTOCOL *)full,NULL,0,&image);
  Bs->FreePool(full);
  if(EFI_ERROR(status)) {
    /* EFI_SECURITY_VIOLATION may still return a handle that must be unloaded. */
    if(image)Bs->UnloadImage(image);
    return Chain->status=status;
  }
  if(!image)return Chain->status=EFI_DEVICE_ERROR;
  EFI_LOADED_IMAGE_PROTOCOL *loaded=NULL;
  status=Bs->HandleProtocol(image,&ChainLoadedGuid,(VOID **)&loaded);
  if(EFI_ERROR(status) || !loaded || loaded->DeviceHandle!=device || loaded->ParentHandle!=Self ||
     loaded->ImageCodeType!=EfiLoaderCode) {
    Bs->UnloadImage(image);
    return Chain->status=EFI_ERROR(status)?status:EFI_COMPROMISED_DATA;
  }
  Chain->image=image;Chain->device=device;
  return Chain->status=EFI_SUCCESS;
}
EFI_STATUS GuChainDiscard(EFI_BOOT_SERVICES *Bs,GU_CHAIN *Chain) {
  if(!Chain->image)return EFI_SUCCESS;
  EFI_HANDLE image=Chain->image;Chain->image=NULL;
  return Bs->UnloadImage(image);
}
EFI_STATUS GuChainStart(EFI_BOOT_SERVICES *Bs,GU_CHAIN *Chain) {
  if(!Chain->image)return EFI_NOT_READY;
  /* After StartImage the handle belongs to firmware; never reuse or unload it. */
  EFI_HANDLE image=Chain->image;Chain->image=NULL;
  /* Same as a firmware Boot#### launch: 5-minute watchdog the OS loader disables. */
  Bs->SetWatchdogTimer(GU_CHAIN_WATCHDOG_SECONDS,0,0,NULL);
  UINTN exit_bytes=0;CHAR16 *exit_data=NULL;
  EFI_STATUS status=Bs->StartImage(image,&exit_bytes,&exit_data);
  /* Reached only if Windows Boot Manager returns instead of booting. */
  Bs->SetWatchdogTimer(0,0,0,NULL);
  if(exit_data)Bs->FreePool(exit_data);
  return status;
}
