#include "ownership.h"
#include "runtime.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/GraphicsOutput.h>
static EFI_GUID PciGuid=EFI_PCI_IO_PROTOCOL_GUID,
  BindingGuid=EFI_DRIVER_BINDING_PROTOCOL_GUID,LoadedGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID,
  GopGuid=EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,PathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID;
typedef struct {EFI_GUID protocol;EFI_OPEN_PROTOCOL_INFORMATION_ENTRY entry;} OWNER_RECORD;
#define MAX_RECORDS 32
static BOOLEAN Passive(UINT32 A) {
  return A==EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL || A==EFI_OPEN_PROTOCOL_GET_PROTOCOL || A==EFI_OPEN_PROTOCOL_TEST_PROTOCOL;
}
/* Caller holds TPL_NOTIFY through the snapshot and, for Claim, acquisition. */
static EFI_STATUS Snapshot(OWNER_RECORD *Records,UINTN *Count) {
  EFI_GUID **protocols=NULL;UINTN protocol_count=0;BOOLEAN pci=FALSE;*Count=0;
  if(!Gu.bs->ProtocolsPerHandle)return EFI_UNSUPPORTED;
  EFI_STATUS status=Gu.bs->ProtocolsPerHandle(Gu.handle,&protocols,&protocol_count);
  if(!EFI_ERROR(status) && (!protocols || !protocol_count || protocol_count>32))status=EFI_COMPROMISED_DATA;
  for(UINTN i=0;!EFI_ERROR(status) && i<protocol_count;i++) {
    EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *entries=NULL;UINTN count=0;
    if(!protocols[i]){status=EFI_COMPROMISED_DATA;break;}
    if(!memcmp(protocols[i],&PciGuid,sizeof(PciGuid)))pci=TRUE;
    status=Gu.bs->OpenProtocolInformation(Gu.handle,protocols[i],&entries,&count);
    if(!EFI_ERROR(status) && (count>MAX_RECORDS-*Count || (count && !entries)))status=EFI_COMPROMISED_DATA;
    if(!EFI_ERROR(status))for(UINTN j=0;j<count;j++) {
      Records[*Count].protocol=*protocols[i];Records[*Count].entry=entries[j];(*Count)++;
    }
    if(entries && EFI_ERROR(Gu.bs->FreePool(entries)))status=EFI_DEVICE_ERROR;
  }
  if(protocols && EFI_ERROR(Gu.bs->FreePool(protocols)))status=EFI_DEVICE_ERROR;
  return !EFI_ERROR(status) && !pci?EFI_NOT_FOUND:status;
}
static VOID LogRecords(const OWNER_RECORD *Records,UINTN Count,EFI_STATUS Status) {
  GuLog("ownership: records=%zu exclusive=%u status=%#llx\n",(size_t)Count,Gu.owned,(unsigned long long)Status);
  for(UINTN i=0;i<Count;i++) {
    const EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *e=&Records[i].entry;
    GuLog("owner[%u] protocol=%08x attributes=%#x count=%u agent=%#llx controller=%#llx\n",
      (unsigned)i,Records[i].protocol.Data1,e->Attributes,e->OpenCount,
      (unsigned long long)(UINTN)e->AgentHandle,(unsigned long long)(UINTN)e->ControllerHandle);
  }
}
EFI_STATUS GuClaimController(VOID) {
  OWNER_RECORD records[MAX_RECORDS];UINTN count=0;
  EFI_TPL old=Gu.bs->RaiseTPL(TPL_NOTIFY);
  EFI_STATUS status=Snapshot(records,&count);
  if(!EFI_ERROR(status))for(UINTN i=0;i<count;i++)if(!Passive(records[i].entry.Attributes))status=EFI_ACCESS_DENIED;
  if(!EFI_ERROR(status)) {
    EFI_PCI_IO_PROTOCOL *pci=NULL;
    status=Gu.bs->OpenProtocol(Gu.handle,&PciGuid,(VOID **)&pci,Gu.image,NULL,EFI_OPEN_PROTOCOL_EXCLUSIVE);
    if(!EFI_ERROR(status)){Gu.owned=TRUE;if(pci!=Gu.pci)status=EFI_COMPROMISED_DATA;}
  }
  Gu.bs->RestoreTPL(old);LogRecords(records,count,status);return status;
}
static BOOLEAN ConsoleHandle(EFI_SYSTEM_TABLE *Table,EFI_HANDLE Handle) {
  return Handle && (Handle==Table->ConsoleOutHandle || Handle==Table->ConsoleInHandle || Handle==Table->StandardErrorHandle);
}
/* A child is releasable only as a non-console GOP output of the target
   (PciRoot/Pci(1,0)/Pci(0,0)/AcpiAdr...). Intel and console handles never are. */
static EFI_STATUS TargetGopChild(EFI_SYSTEM_TABLE *Table,EFI_HANDLE Child,BOOLEAN Log,UINTN Index) {
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop=NULL;EFI_DEVICE_PATH_PROTOCOL *path=NULL;
  EFI_STATUS gop_status=Gu.bs->HandleProtocol(Child,&GopGuid,(VOID **)&gop);
  EFI_STATUS path_status=Gu.bs->HandleProtocol(Child,&PathGuid,(VOID **)&path);
  int route=!EFI_ERROR(path_status) && path?GuDisplayPath(path,256):0;
  BOOLEAN console=ConsoleHandle(Table,Child);
  if(Log)GuLog("release child[%u]: handle=%#llx gop=%#llx path=%#llx route=%d console=%u\n",(unsigned)Index,
    (unsigned long long)(UINTN)Child,(unsigned long long)gop_status,(unsigned long long)path_status,route,(unsigned)console);
  if(EFI_ERROR(gop_status) || !gop || route!=2 || console)return EFI_ACCESS_DENIED;
  return EFI_SUCCESS;
}
EFI_STATUS GuFindReleaseOwner(EFI_SYSTEM_TABLE *Table,GU_RELEASE_OWNER *Owner,BOOLEAN Log) {
  OWNER_RECORD records[MAX_RECORDS];GU_OPEN_RECORD opens[MAX_RECORDS];UINTN count=0;memset(Owner,0,sizeof(*Owner));
  if(!Gu.bs->DisconnectController || Gu.owned)return EFI_UNSUPPORTED;
  EFI_TPL old=Gu.bs->RaiseTPL(TPL_NOTIFY);
  EFI_STATUS status=Snapshot(records,&count);
  Gu.bs->RestoreTPL(old);if(Log)LogRecords(records,count,status);
  if(EFI_ERROR(status))return status;
  for(UINTN i=0;i<count;i++) {
    const EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *e=&records[i].entry;
    opens[i].Attributes=e->Attributes;opens[i].OpenCount=e->OpenCount;
    opens[i].Agent=e->AgentHandle;opens[i].Controller=e->ControllerHandle;
    opens[i].PciIo=!memcmp(&records[i].protocol,&PciGuid,sizeof(PciGuid));
  }
  /* Single device driver only. Its children may only be target GOP outputs:
     DisconnectController(ChildHandle=NULL) destroys them (and stops their
     console consumers) before Stop. Physical console children need not equal
     the System Table's virtual console handles, so each child is re-proven. */
  status=GuReleasePolicy(opens,count,Gu.handle,&Owner->agent,Owner->children,&Owner->child_count);
  if(EFI_ERROR(status)) {
    if(Log)GuLog("release refused: second driver, foreign owner, unsupported open or too many children: %#llx\n",(unsigned long long)status);
    memset(Owner,0,sizeof(*Owner));return EFI_ACCESS_DENIED;
  }
  if(Owner->agent==Gu.image || Owner->agent==Gu.handle || ConsoleHandle(Table,Owner->agent)){memset(Owner,0,sizeof(*Owner));return EFI_ACCESS_DENIED;}
  for(UINTN i=0;i<Owner->child_count;i++)if(EFI_ERROR(TargetGopChild(Table,Owner->children[i],Log,i))) {
    if(Log)GuLog("release refused: child %u is not a non-console target GOP output\n",(unsigned)i);
    memset(Owner,0,sizeof(*Owner));return EFI_ACCESS_DENIED;
  }
  EFI_DRIVER_BINDING_PROTOCOL *binding=NULL;EFI_LOADED_IMAGE_PROTOCOL *image=NULL;
  status=Gu.bs->HandleProtocol(Owner->agent,&BindingGuid,(VOID **)&binding);
  if(EFI_ERROR(status) || !binding || binding->DriverBindingHandle!=Owner->agent || !binding->Stop ||
    !binding->ImageHandle || binding->ImageHandle==Gu.image)return EFI_UNSUPPORTED;
  status=Gu.bs->HandleProtocol(binding->ImageHandle,&LoadedGuid,(VOID **)&image);
  if(EFI_ERROR(status) || !image)return EFI_UNSUPPORTED;
  if(Log)GuLog("release candidate: agent=%#llx image=%#llx source=%#llx base=%#llx bytes=%llu version=%#x code_type=%u children=%zu\n",
    (unsigned long long)(UINTN)Owner->agent,(unsigned long long)(UINTN)binding->ImageHandle,
    (unsigned long long)(UINTN)image->DeviceHandle,(unsigned long long)(UINTN)image->ImageBase,
    (unsigned long long)image->ImageSize,binding->Version,(unsigned)image->ImageCodeType,(size_t)Owner->child_count);
  if(!image->ImageBase || !image->ImageSize || image->ImageSize>64U*1024U*1024U || image->ImageCodeType!=EfiBootServicesCode)return EFI_UNSUPPORTED;
  Owner->image=binding->ImageHandle;Owner->base=image->ImageBase;Owner->size=image->ImageSize;
  Owner->version=binding->Version;Owner->stop=binding->Stop;return EFI_SUCCESS;
}
BOOLEAN GuSameReleaseOwner(const GU_RELEASE_OWNER *A,const GU_RELEASE_OWNER *B) {
  if(A->agent!=B->agent || A->image!=B->image || A->base!=B->base || A->size!=B->size ||
    A->version!=B->version || A->stop!=B->stop || A->child_count!=B->child_count)return FALSE;
  for(UINTN i=0;i<A->child_count;i++)if(A->children[i]!=B->children[i])return FALSE;
  return TRUE;
}
EFI_STATUS GuRefreshTargetPci(VOID) {
  EFI_PCI_IO_PROTOCOL *fresh=NULL;UINTN segment,bus,device,function;UINT32 config[64]={0};
  Gu.pci=NULL;
  EFI_STATUS status=Gu.bs->HandleProtocol(Gu.handle,&PciGuid,(VOID **)&fresh);
  if(EFI_ERROR(status) || !fresh || !fresh->GetLocation || !fresh->Pci.Read || !fresh->Pci.Write)return EFI_NOT_FOUND;
  status=fresh->GetLocation(fresh,&segment,&bus,&device,&function);if(EFI_ERROR(status))return status;
  const UINT32 offsets[]={0,8,12,44};
  for(UINTN i=0;i<4;i++) {
    status=fresh->Pci.Read(fresh,EfiPciIoWidthUint32,offsets[i],1,&config[offsets[i]/4]);if(EFI_ERROR(status))return status;
  }
  if(!GuTargetConfig(config,segment,bus,device,function))return EFI_UNSUPPORTED;
  Gu.pci=fresh;return EFI_SUCCESS;
}
