#include "patchs/libavb_force_success.h"
#include "patchs/pe_sections.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const uint8_t LibavbForceAnchor[] =
    "Persistent values required for AVB_HASHTREE_ERROR_MODE_MANAGED_RESTART_AND_EIO";

#define ARM64_PACIASP UINT32_C(0xD503233F)
#define ARM64_RET UINT32_C(0xD65F03C0)
#define ARM64_MOV_W0_ZERO UINT32_C(0x52800000)
#define ARM64_MOV_FROM_W3_MASK UINT32_C(0xFFFFFFE0)
#define ARM64_MOV_FROM_W3_PATTERN UINT32_C(0x2A0303E0)
#define ARM64_ORR_W3_ONE_BASE UINT32_C(0x32000060)
#define ARM64_MOV_TO_W0_MASK UINT32_C(0xFFE0FFFF)
#define ARM64_MOV_TO_W0_PATTERN UINT32_C(0x2A0003E0)

static uint32_t ReadU32(const uint8_t *Data) {
    return (uint32_t)Data[0]
        | ((uint32_t)Data[1] << 8)
        | ((uint32_t)Data[2] << 16)
        | ((uint32_t)Data[3] << 24);
}

static void WriteU32(uint8_t *Data, uint32_t Value) {
    Data[0] = (uint8_t)Value;
    Data[1] = (uint8_t)(Value >> 8);
    Data[2] = (uint8_t)(Value >> 16);
    Data[3] = (uint8_t)(Value >> 24);
}

static int32_t SignExtend(uint32_t Value, uint32_t Bits) {
    uint32_t Sign = UINT32_C(1) << (Bits - 1);
    uint32_t Mask = (UINT32_C(1) << Bits) - 1;
    Value &= Mask;
    return (Value & Sign) != 0 ? (int32_t)(Value | ~Mask) : (int32_t)Value;
}

static bool ReadInstruction(const uint8_t *Buffer,
                            size_t Size,
                            size_t Offset,
                            uint32_t *Instruction) {
    if (Instruction == NULL || Offset > Size || Size - Offset < 4) {
        return false;
    }
    *Instruction = ReadU32(Buffer + Offset);
    return true;
}

static bool DecodeAdrpAdd(const PE_IMAGE *Image,
                          size_t Offset,
                          uint32_t *TargetRva) {
    uint32_t Adrp;
    uint32_t Add;
    uint32_t ImmLo;
    uint32_t ImmHi;
    uint32_t Imm21;
    uint32_t AdrpRegister;
    uint32_t AddRegister;
    uint32_t AddDestination;
    uint32_t Imm12;
    uint32_t AdrpRva;
    int64_t Page;
    int64_t Target;

    if (TargetRva == NULL ||
        !ReadInstruction(Image->Data, Image->Size, Offset, &Adrp) ||
        !ReadInstruction(Image->Data, Image->Size, Offset + 4, &Add) ||
        !PeImageFileOffsetToRva(Image, Offset, 8, &AdrpRva)) {
        return false;
    }
    if ((Adrp & UINT32_C(0x9F000000)) != UINT32_C(0x90000000) ||
        (Add & UINT32_C(0xFF800000)) != UINT32_C(0x91000000)) {
        return false;
    }
    ImmLo = (Adrp >> 29) & 3;
    ImmHi = (Adrp >> 5) & UINT32_C(0x7FFFF);
    Imm21 = (ImmHi << 2) | ImmLo;
    AdrpRegister = Adrp & 0x1F;
    AddRegister = (Add >> 5) & 0x1F;
    AddDestination = Add & 0x1F;
    /* Register 31 is never address formation: ADRP cannot target XZR, and in
     * ADD (sf=1, S=0) an Rn or Rd of 31 denotes SP. Accepting it would let a
     * crafted ADRP+ADD pair pose as the sole anchor xref and steer the patch
     * into the wrong function. A real pointer pair uses X0-X30. */
    if (AdrpRegister == 31 || AddRegister == 31 || AddDestination == 31) {
        return false;
    }
    if (AddRegister != AdrpRegister || AddDestination != AdrpRegister) {
        return false;
    }
    Imm12 = (Add >> 10) & 0xFFF;
    if ((Add & UINT32_C(1) << 22) != 0) {
        Imm12 <<= 12;
    }
    Page = (int64_t)(AdrpRva & UINT32_C(0xFFFFF000)) +
           (int64_t)SignExtend(Imm21, 21) * INT64_C(4096);
    Target = Page + Imm12;
    if (Target < 0 || (uint64_t)Target > UINT32_MAX) {
        return false;
    }
    *TargetRva = (uint32_t)Target;
    return true;
}

static LIBAVB_FORCE_RESULT FindUniqueAnchor(const uint8_t *Buffer,
                                       size_t Size,
                                       size_t *Offset) {
    size_t AnchorSize = sizeof(LibavbForceAnchor) - 1;
    size_t Found = 0;
    size_t First = 0;
    size_t Index;

    if (Offset == NULL || AnchorSize == 0 || Size < AnchorSize) {
        return LIBAVB_FORCE_FAILURE;
    }
    for (Index = 0; Index <= Size - AnchorSize; ++Index) {
        if (memcmp(Buffer + Index, LibavbForceAnchor, AnchorSize) == 0) {
            if (Found == 0) {
                First = Index;
            }
            ++Found;
        }
    }
    if (Found == 0) {
        return LIBAVB_FORCE_FAILURE;
    }
    if (Found != 1) {
        return LIBAVB_FORCE_AMBIGUOUS;
    }
    *Offset = First;
    return LIBAVB_FORCE_SUCCESS;
}

static LIBAVB_FORCE_RESULT FindAdrpAddReference(const PE_IMAGE *Image,
                                           uint32_t TargetRva,
                                           size_t *Offset,
                                           size_t *SectionIndex) {
    size_t Found = 0;
    size_t First = 0;
    size_t FirstSection = 0;
    size_t Current;

    for (Current = 0; Current <= Image->Size - 8; Current += 4) {
        size_t CurrentSection;
        uint32_t Resolved;
        if (!PeImageFindSectionForOffset(Image, Current, 8, true, &CurrentSection) ||
            !DecodeAdrpAdd(Image, Current, &Resolved) || Resolved != TargetRva) {
            continue;
        }
        if (Found == 0) {
            First = Current;
            FirstSection = CurrentSection;
        }
        ++Found;
    }
    if (Found == 0) {
        return LIBAVB_FORCE_FAILURE;
    }
    if (Found != 1) {
        return LIBAVB_FORCE_AMBIGUOUS;
    }
    *Offset = First;
    *SectionIndex = FirstSection;
    return LIBAVB_FORCE_SUCCESS;
}

static bool InSection(const PE_IMAGE *Image,
                      size_t SectionIndex,
                      size_t Offset,
                      size_t Length) {
    size_t Actual;
    return PeImageFindSectionForOffset(Image, Offset, Length, true, &Actual) &&
           Actual == SectionIndex;
}

LIBAVB_FORCE_RESULT patch_libavb_force_success(char *Buffer, int32_t Size) {
    PE_IMAGE Image;
    LIBAVB_FORCE_RESULT Result;
    size_t AnchorOffset;
    size_t AdrpOffset;
    size_t FunctionSection;
    size_t FunctionEntry = 0;
    size_t MovFromW3 = 0;
    size_t Ret = 0;
    size_t MovToW0 = 0;
    size_t SectionEnd;
    size_t Probe;
    size_t Index;
    uint32_t AnchorRva;
    uint32_t MovFromW3Word;
    PE_SECTION Section;

    if (Buffer == NULL || Size <= 0 ||
        !PeImageInit(&Image, (const uint8_t *)Buffer, (size_t)Size)) {
        return LIBAVB_FORCE_FAILURE;
    }
    Result = FindUniqueAnchor((const uint8_t *)Buffer, (size_t)Size, &AnchorOffset);
    if (Result != LIBAVB_FORCE_SUCCESS) {
        return Result;
    }
    if (!PeImageFileOffsetToRva(&Image, AnchorOffset,
                                sizeof(LibavbForceAnchor) - 1, &AnchorRva)) {
        return LIBAVB_FORCE_FAILURE;
    }
    Result = FindAdrpAddReference(&Image, AnchorRva, &AdrpOffset, &FunctionSection);
    if (Result != LIBAVB_FORCE_SUCCESS) {
        return Result;
    }
    if (!PeImageGetSection(&Image, FunctionSection, &Section)) {
        return LIBAVB_FORCE_FAILURE;
    }
    if ((uintmax_t)Section.PointerToRawData +
        (uintmax_t)Section.SizeOfRawData > (uintmax_t)SIZE_MAX) {
        return LIBAVB_FORCE_FAILURE;
    }
    SectionEnd = (size_t)Section.PointerToRawData +
                 (size_t)Section.SizeOfRawData;

    Probe = AdrpOffset;
    while (Probe >= 4) {
        uint32_t Word;
        size_t Candidate = Probe - 4;
        if (!InSection(&Image, FunctionSection, Candidate, 4)) {
            break;
        }
        if (!ReadInstruction((const uint8_t *)Buffer, (size_t)Size, Candidate, &Word)) {
            break;
        }
        if (Word == ARM64_PACIASP) {
            FunctionEntry = Candidate;
            break;
        }
        Probe = Candidate;
    }
    if (FunctionEntry == 0) {
        return LIBAVB_FORCE_FAILURE;
    }

    for (Index = 0; Index < 30; ++Index) {
        uint32_t Word;
        if (Index > (SIZE_MAX - FunctionEntry) / 4) {
            break;
        }
        Probe = FunctionEntry + Index * 4;
        if (!InSection(&Image, FunctionSection, Probe, 4) ||
            !ReadInstruction((const uint8_t *)Buffer, (size_t)Size, Probe, &Word)) {
            break;
        }
        if (Index != 0 && Word == ARM64_PACIASP) {
            break;
        }
        if (Word == ARM64_RET) {
            break;
        }
        /* Rd==31 is WZR. `MOV WZR, W3` discards the value, and rewriting it to
         * `ORR WZR, W3, #1` would be a semantic no-op that still reported
         * success for a MANDATORY patch, shipping an ABL that still enforces
         * AVB. Keep scanning for a real destination register instead. */
        if ((Word & ARM64_MOV_FROM_W3_MASK) == ARM64_MOV_FROM_W3_PATTERN &&
            (Word & 0x1FU) != 31U) {
            MovFromW3 = Probe;
            break;
        }
    }
    if (MovFromW3 == 0) {
        return LIBAVB_FORCE_FAILURE;
    }

    for (Probe = FunctionEntry; Probe <= SectionEnd - 4; Probe += 4) {
        uint32_t Word;
        if (!InSection(&Image, FunctionSection, Probe, 4) ||
            !ReadInstruction((const uint8_t *)Buffer, (size_t)Size, Probe, &Word)) {
            break;
        }
        if (Probe != FunctionEntry && Word == ARM64_PACIASP) {
            break;
        }
        if (Word == ARM64_RET) {
            Ret = Probe;
            break;
        }
        if (Probe > SIZE_MAX - 4) {
            break;
        }
    }
    if (Ret == 0) {
        return LIBAVB_FORCE_FAILURE;
    }

    Probe = Ret;
    size_t WindowFloor = Ret > 0x40 ? Ret - 0x40 : 0;
    while (Probe > FunctionEntry && Probe > WindowFloor) {
        uint32_t Word;
        Probe -= 4;
        if (!InSection(&Image, FunctionSection, Probe, 4) ||
            !ReadInstruction((const uint8_t *)Buffer, (size_t)Size, Probe, &Word)) {
            break;
        }
        if ((Word & ARM64_MOV_TO_W0_MASK) == ARM64_MOV_TO_W0_PATTERN) {
            MovToW0 = Probe;
            break;
        }
    }
    if (MovToW0 == 0) {
        return LIBAVB_FORCE_FAILURE;
    }
    if (MovFromW3 == MovToW0) {
        return LIBAVB_FORCE_FAILURE;
    }

    if (!ReadInstruction((const uint8_t *)Buffer, (size_t)Size, MovFromW3,
                          &MovFromW3Word)) {
        return LIBAVB_FORCE_FAILURE;
    }
    WriteU32((uint8_t *)Buffer + MovFromW3,
             ARM64_ORR_W3_ONE_BASE | (MovFromW3Word & 0x1F));
    WriteU32((uint8_t *)Buffer + MovToW0, ARM64_MOV_W0_ZERO);
    return LIBAVB_FORCE_SUCCESS;
}
