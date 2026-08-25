/*
 * USB mass-storage descriptor set.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <Uefi.h>
#include <Protocol/EFIUsbDevice.h>
#include <IndustryStandard/Usb.h>

#define SFB_MSC_ENDPOINT_NUMBER  1
#define SFB_MSC_ENDPOINT_IN      0x81
#define SFB_MSC_ENDPOINT_OUT     0x01

#define SFB_MSC_VENDOR_ID        0x18D1
#define SFB_MSC_PRODUCT_ID       0xD00E

#define SFB_MSC_ENDPOINT_ADDRESS(Index, In) \
  ((UINT8)((Index) | ((In) ? 0x80 : 0)))

STATIC EFI_USB_DEVICE_DESCRIPTOR mMscDeviceDescriptor = {
  sizeof (EFI_USB_DEVICE_DESCRIPTOR),
  USB_DESC_TYPE_DEVICE,
  0x0200,
  0,
  0,
  0,
  64,
  SFB_MSC_VENDOR_ID,
  SFB_MSC_PRODUCT_ID,
  0x0100,
  1,
  2,
  3,
  1
};

STATIC EFI_USB_DEVICE_DESCRIPTOR mMscSsDeviceDescriptor = {
  sizeof (EFI_USB_DEVICE_DESCRIPTOR),
  USB_DESC_TYPE_DEVICE,
  0x0300,
  0,
  0,
  0,
  9,
  SFB_MSC_VENDOR_ID,
  SFB_MSC_PRODUCT_ID,
  0x0100,
  1,
  2,
  3,
  1
};

STATIC EFI_USB_DEVICE_QUALIFIER_DESCRIPTOR mMscDeviceQualifier = {
  sizeof (EFI_USB_DEVICE_QUALIFIER_DESCRIPTOR),
  USB_DESC_TYPE_DEVICE_QUALIFIER,
  0x0200,
  0,
  0,
  0,
  64,
  1,
  0
};

#pragma pack(1)
typedef struct {
  EFI_USB_CONFIG_DESCRIPTOR Config;
  EFI_USB_INTERFACE_DESCRIPTOR Interface;
  EFI_USB_ENDPOINT_DESCRIPTOR BulkIn;
  EFI_USB_ENDPOINT_DESCRIPTOR BulkOut;
} SFB_MSC_HS_TREE;

typedef struct {
  EFI_USB_CONFIG_DESCRIPTOR Config;
  EFI_USB_INTERFACE_DESCRIPTOR Interface;
  EFI_USB_ENDPOINT_DESCRIPTOR BulkIn;
  EFI_USB_SS_ENDPOINT_COMPANION_DESCRIPTOR BulkInCompanion;
  EFI_USB_ENDPOINT_DESCRIPTOR BulkOut;
  EFI_USB_SS_ENDPOINT_COMPANION_DESCRIPTOR BulkOutCompanion;
} SFB_MSC_SS_TREE;
#pragma pack()

STATIC SFB_MSC_HS_TREE mMscHsTree = {
  {
    sizeof (EFI_USB_CONFIG_DESCRIPTOR),
    USB_DESC_TYPE_CONFIG,
    sizeof (SFB_MSC_HS_TREE),
    1,
    1,
    0,
    0x80,
    0x50
  },
  {
    sizeof (EFI_USB_INTERFACE_DESCRIPTOR),
    USB_DESC_TYPE_INTERFACE,
    0,
    0,
    2,
    0x08,
    0x06,
    0x50,
    4
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    SFB_MSC_ENDPOINT_ADDRESS (SFB_MSC_ENDPOINT_NUMBER, TRUE),
    USB_ENDPOINT_BULK,
    512,
    0
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    SFB_MSC_ENDPOINT_ADDRESS (SFB_MSC_ENDPOINT_NUMBER, FALSE),
    USB_ENDPOINT_BULK,
    512,
    0
  }
};

STATIC SFB_MSC_SS_TREE mMscSsTree = {
  {
    sizeof (EFI_USB_CONFIG_DESCRIPTOR),
    USB_DESC_TYPE_CONFIG,
    sizeof (SFB_MSC_SS_TREE),
    1,
    1,
    0,
    0x80,
    0x10
  },
  {
    sizeof (EFI_USB_INTERFACE_DESCRIPTOR),
    USB_DESC_TYPE_INTERFACE,
    0,
    0,
    2,
    0x08,
    0x06,
    0x50,
    4
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    SFB_MSC_ENDPOINT_ADDRESS (SFB_MSC_ENDPOINT_NUMBER, TRUE),
    USB_ENDPOINT_BULK,
    1024,
    0
  },
  {
    sizeof (EFI_USB_SS_ENDPOINT_COMPANION_DESCRIPTOR),
    USB_DESC_TYPE_SS_ENDPOINT_COMPANION,
    0,
    0,
    0
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    SFB_MSC_ENDPOINT_ADDRESS (SFB_MSC_ENDPOINT_NUMBER, FALSE),
    USB_ENDPOINT_BULK,
    1024,
    0
  },
  {
    sizeof (EFI_USB_SS_ENDPOINT_COMPANION_DESCRIPTOR),
    USB_DESC_TYPE_SS_ENDPOINT_COMPANION,
    0,
    0,
    0
  }
};

STATIC CONST UINT8 mMscString0[] = {
  4, USB_DESC_TYPE_STRING, 0x09, 0x04
};

STATIC CONST UINT8 mMscManufacturer[] = {
  12, USB_DESC_TYPE_STRING, 'C', 0, 'A', 0, 'N', 0, 'O', 0, 'E', 0
};

STATIC CONST UINT8 mMscProduct[] = {
  34, USB_DESC_TYPE_STRING,
  'U', 0, 'S', 0, 'B', 0, ' ', 0,
  'M', 0, 'a', 0, 's', 0, 's', 0, ' ', 0,
  'S', 0, 't', 0, 'o', 0, 'r', 0, 'a', 0, 'g', 0, 'e', 0
};

STATIC CONST UINT8 mMscSerial[] = {
  12, USB_DESC_TYPE_STRING, 'c', 0, 'a', 0, 'n', 0, 'o', 0, 'e', 0
};

STATIC EFI_USB_STRING_DESCRIPTOR *mMscStrings[] = {
  (EFI_USB_STRING_DESCRIPTOR *)mMscString0,
  (EFI_USB_STRING_DESCRIPTOR *)mMscManufacturer,
  (EFI_USB_STRING_DESCRIPTOR *)mMscProduct,
  (EFI_USB_STRING_DESCRIPTOR *)mMscSerial
};

STATIC VOID *mMscHsDescriptors[] = {
  &mMscHsTree
};

STATIC VOID *mMscSsDescriptors[] = {
  &mMscSsTree
};

EFI_STATUS
SfbMscBuildDescriptorSet (OUT USB_DEVICE_DESCRIPTOR_SET *DescriptorSet)
{
  if (DescriptorSet == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  DescriptorSet->DeviceDescriptor = &mMscDeviceDescriptor;
  DescriptorSet->Descriptors = mMscHsDescriptors;
  DescriptorSet->SSDeviceDescriptor = &mMscSsDeviceDescriptor;
  DescriptorSet->SSDescriptors = mMscSsDescriptors;
  DescriptorSet->DeviceQualifierDescriptor = &mMscDeviceQualifier;
  DescriptorSet->BinaryDeviceOjectStore = NULL;
  DescriptorSet->StringDescriptorCount = 4;
  DescriptorSet->StringDescritors = mMscStrings;
  return EFI_SUCCESS;
}
