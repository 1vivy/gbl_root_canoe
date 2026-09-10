/* Production provider selection and stop ordering with EFI service fixtures. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NULL
#include "../edk2/Ext4Pkg/Ext4Dxe/Ext4Dxe.c"

static EFI_BOOT_SERVICES Services;
EFI_BOOT_SERVICES *gBS = &Services;
EFI_GUID gEfiSimpleFileSystemProtocolGuid={1}, gEfiDiskIoProtocolGuid={2},
         gEfiDiskIo2ProtocolGuid={3}, gEfiBlockIoProtocolGuid={4};
static EFI_HANDLE Controller=(void*)1, Own=(void*)2, Foreign=(void*)3;
static EXT4_PARTITION Partition;
static EXT4_FILE Root;
static EXT4_BLOCK_GROUP_DESC Group;
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL ForeignFs;
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Published;
static EFI_STATUS DisconnectResult, ConnectResult, UninstallResult;
static unsigned OwnerCount, Connects, Disconnects, OwnDisconnects, Frees, Closes, Uninstalls;
static BOOLEAN KeepForeign, PublishOnConnect, ReadAfterFree;

VOID *EFIAPI AllocatePool(UINTN n) { return malloc(n); }
VOID *EFIAPI AllocateZeroPool(UINTN n) { return calloc(1,n); }
VOID EFIAPI FreePool(VOID *p) {
  if (p==&Partition || p==&Group) { Frees++; return; }
  free(p);
}
EFI_STATUS Ext4CloseInternal(EXT4_FILE *f) { assert(f==&Root); return EFI_SUCCESS; }
EFI_STATUS EFIAPI Ext4OpenVolume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL*f,EFI_FILE_PROTOCOL**r) {
  (void)f;(void)r;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI foreign_open(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL*f,EFI_FILE_PROTOCOL**r) {
  (void)f;(void)r;return EFI_SUCCESS;
}
EFI_STATUS Ext4OpenSuperblock(EXT4_PARTITION*p) { (void)p;return EFI_SUCCESS; }
static EFI_STATUS EFIAPI protocol(EFI_HANDLE h,EFI_GUID*g,VOID**out) {
  assert(h==Controller && g==&gEfiSimpleFileSystemProtocolGuid);
  *out=Published;return Published?EFI_SUCCESS:EFI_UNSUPPORTED;
}
static EFI_STATUS EFIAPI information(EFI_HANDLE h,EFI_GUID*g,EFI_OPEN_PROTOCOL_INFORMATION_ENTRY**out,UINTN*n) {
  assert(h==Controller && g==&gEfiDiskIoProtocolGuid);
  *n=OwnerCount;*out=calloc(OwnerCount,sizeof(**out));
  for(unsigned i=0;i<OwnerCount;i++) {
    (*out)[i].AgentHandle=i==0?Foreign:(void*)4;
    (*out)[i].ControllerHandle=Controller;
    (*out)[i].Attributes=EFI_OPEN_PROTOCOL_BY_DRIVER;
  }
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI disconnect(EFI_HANDLE h,EFI_HANDLE owner,EFI_HANDLE child) {
  assert(h==Controller && child==NULL);
  if (owner==Own) {
    OwnDisconnects++;
    if(Published!=&Partition.Interface) return EFI_NOT_FOUND;
    if(!EFI_ERROR(DisconnectResult)) Published=NULL;
    return DisconnectResult;
  }
  assert(owner==Foreign);Disconnects++;
  if (!EFI_ERROR(DisconnectResult) && !KeepForeign) Published=NULL;
  return DisconnectResult;
}
static EFI_STATUS EFIAPI connect(EFI_HANDLE h,EFI_HANDLE*drivers,EFI_DEVICE_PATH_PROTOCOL*p,BOOLEAN recursive) {
  assert(h==Controller && drivers && drivers[0]==Own && drivers[1]==NULL && p==NULL && !recursive);
  Connects++;if(PublishOnConnect)Published=&Partition.Interface;return ConnectResult;
}
static EFI_STATUS EFIAPI open_protocol(EFI_HANDLE h,EFI_GUID*g,VOID**out,EFI_HANDLE agent,EFI_HANDLE c,UINT32 attr) {
  assert(h==Controller && agent==Own && c==Controller && attr==EFI_OPEN_PROTOCOL_GET_PROTOCOL);
  return protocol(h,g,out);
}
static EFI_STATUS EFIAPI uninstall(EFI_HANDLE h,...) {
  assert(h==Controller && Published==&Partition.Interface);Uninstalls++;
  if(Frees!=0) ReadAfterFree=TRUE;
  if(!EFI_ERROR(UninstallResult))Published=NULL;
  return UninstallResult;
}
static EFI_STATUS EFIAPI close_protocol(EFI_HANDLE h,EFI_GUID*g,EFI_HANDLE agent,EFI_HANDLE c) {
  assert(h==Controller && agent==Own && c==Controller);
  assert(g==&gEfiDiskIoProtocolGuid || g==&gEfiDiskIo2ProtocolGuid || g==&gEfiBlockIoProtocolGuid);
  assert(Published==NULL);Closes++;return EFI_SUCCESS;
}
static void reset(void) {
  memset(&Partition,0,sizeof(Partition));Partition.Interface.OpenVolume=Ext4OpenVolume;
  Partition.Root=&Root;Partition.BlockGroups=&Group;
  ForeignFs.OpenVolume=foreign_open;Published=NULL;
  DisconnectResult=ConnectResult=UninstallResult=EFI_SUCCESS;
  OwnerCount=1;Connects=Disconnects=OwnDisconnects=Frees=Closes=Uninstalls=0;
  KeepForeign=ReadAfterFree=FALSE;PublishOnConnect=TRUE;
  gExt4BindingProtocol.DriverBindingHandle=Own;gExt4BindingProtocol.ImageHandle=Own;
}
int main(void) {
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
  Services.HandleProtocol=protocol;Services.OpenProtocolInformation=information;
  Services.DisconnectController=disconnect;Services.ConnectController=connect;
  Services.OpenProtocol=open_protocol;Services.CloseProtocol=close_protocol;
  Services.UninstallMultipleProtocolInterfaces=uninstall;
  reset();Published=&Partition.Interface;ConnectResult=EFI_NOT_FOUND;
  assert(Ext4OpenImageFileSystem(Controller,&fs)==EFI_SUCCESS && fs==Published);
  assert(Connects==0 && Disconnects==0);
  reset();assert(Ext4OpenImageFileSystem(Controller,&fs)==EFI_SUCCESS && fs==Published);
  assert(Connects==1 && Disconnects==0);
  reset();ConnectResult=EFI_NOT_FOUND;
  assert(Ext4OpenImageFileSystem(Controller,&fs)==EFI_SUCCESS);
  reset();PublishOnConnect=FALSE;ConnectResult=EFI_DEVICE_ERROR;
  assert(Ext4OpenImageFileSystem(Controller,&fs)==EFI_DEVICE_ERROR && fs==NULL);
  reset();Published=&ForeignFs;
  assert(Ext4OpenImageFileSystem(Controller,&fs)==EFI_SUCCESS && fs==&Partition.Interface);
  assert(Connects==1 && Disconnects==1);
  reset();Published=&ForeignFs;DisconnectResult=EFI_ACCESS_DENIED;
  assert(Ext4OpenImageFileSystem(Controller,&fs)==EFI_ACCESS_DENIED && fs==NULL);
  assert(Published==&ForeignFs && Connects==0);
  reset();Published=&ForeignFs;OwnerCount=0;
  assert(Ext4OpenImageFileSystem(Controller,&fs)==EFI_ACCESS_DENIED && Disconnects==0);
  reset();Published=&ForeignFs;OwnerCount=2;
  assert(Ext4OpenImageFileSystem(Controller,&fs)==EFI_ACCESS_DENIED && Disconnects==0);
  reset();Published=&ForeignFs;KeepForeign=TRUE;
  assert(Ext4OpenImageFileSystem(Controller,&fs)==EFI_ACCESS_DENIED && Connects==0);
  reset();Published=&Partition.Interface;
  assert(Ext4ReleaseImageFileSystem(Controller)==EFI_SUCCESS);
  assert(Published==NULL && OwnDisconnects==1 && Disconnects==0);
  assert(Ext4ReleaseImageFileSystem(Controller)==EFI_SUCCESS);
  assert(Published==NULL && OwnDisconnects==2 && Disconnects==0);
  reset();Published=&ForeignFs;
  assert(Ext4ReleaseImageFileSystem(Controller)==EFI_SUCCESS);
  assert(Published==&ForeignFs && Disconnects==0);
  reset();Published=&Partition.Interface;DisconnectResult=EFI_ACCESS_DENIED;
  assert(Ext4ReleaseImageFileSystem(Controller)==EFI_ACCESS_DENIED);
  assert(Published==&Partition.Interface && Disconnects==0);
  reset();Published=&ForeignFs;gExt4BindingProtocol.DriverBindingHandle=NULL;
  assert(Ext4ReleaseImageFileSystem(Controller)==EFI_SUCCESS && OwnDisconnects==0);
  assert(Published==&ForeignFs);
  assert(Ext4ReleaseImageFileSystem(NULL)==EFI_INVALID_PARAMETER);
  reset();Published=&Partition.Interface;UninstallResult=EFI_ACCESS_DENIED;
  assert(Ext4Stop(&gExt4BindingProtocol,Controller,0,NULL)==EFI_ACCESS_DENIED);
  assert(Frees==0 && Closes==0 && !Partition.Unmounting && !ReadAfterFree);
  UninstallResult=EFI_SUCCESS;
  assert(Ext4Stop(&gExt4BindingProtocol,Controller,0,NULL)==EFI_SUCCESS);
  assert(Frees==2 && Closes==2 && Published==NULL && !ReadAfterFree);
  reset();Published=&Partition.Interface;Partition.DiskIo2=(void*)5;
  assert(Ext4Stop(&gExt4BindingProtocol,Controller,0,NULL)==EFI_SUCCESS);
  assert(Frees==2 && Closes==3 && !ReadAfterFree);
  puts("PASS ext4 lifecycle: owned/foreign providers, targeted acquire/release, refusal, uninstall before free");
  return 0;
}
