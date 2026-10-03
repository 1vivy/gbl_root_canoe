#include "patchs/pe_sections.h"

#include <limits.h>
#define PE_IMAGE_MAX_SECTIONS 96u

static uint16_t ReadU16(const uint8_t *Data) {
    return (uint16_t)Data[0] | ((uint16_t)Data[1] << 8);
}

static uint32_t ReadU32(const uint8_t *Data) {
    return (uint32_t)Data[0]
        | ((uint32_t)Data[1] << 8)
        | ((uint32_t)Data[2] << 16)
        | ((uint32_t)Data[3] << 24);
}

static bool RangeInside(size_t Offset, size_t Length, size_t Size) {
    return Offset <= Size && Length <= Size - Offset;
}

static bool SectionRawRange(const PE_SECTION *Section,
                            size_t *RawStart,
                            size_t *RawEnd) {
    size_t Start = Section->PointerToRawData;
    size_t Length = Section->SizeOfRawData;
    if (Length > SIZE_MAX - Start) {
        return false;
    }
    *RawStart = Start;
    *RawEnd = Start + Length;
    return true;
}

static uint64_t SectionMappedLength(const PE_SECTION *Section) {
    return Section->VirtualSize != 0
        ? Section->VirtualSize
        : Section->SizeOfRawData;
}

bool PeImageGetSection(const PE_IMAGE *Image, size_t Index, PE_SECTION *Section) {
    const uint8_t *Header;
    if (Image == NULL || Section == NULL || Index >= Image->NumberOfSections) {
        return false;
    }
    Header = Image->Data + Image->SectionTable + Index * 40;
    Section->VirtualSize = ReadU32(Header + 8);
    Section->VirtualAddress = ReadU32(Header + 12);
    Section->SizeOfRawData = ReadU32(Header + 16);
    Section->PointerToRawData = ReadU32(Header + 20);
    Section->Characteristics = ReadU32(Header + 36);
    return true;
}

bool PeImageInit(PE_IMAGE *Image, const uint8_t *Data, size_t Size) {
    size_t PeOffset;
    size_t OptionalOffset;
    size_t SectionTable;
    size_t SectionBytes;
    uint16_t NumberOfSections;
    uint16_t Machine;
    size_t HeadersEnd;
    uint16_t OptionalMagic;
    uint16_t OptionalSize;
    size_t Index;

    if (Image == NULL || Data == NULL || Size < 0x40 ||
        Data[0] != 'M' || Data[1] != 'Z') {
        return false;
    }
    PeOffset = ReadU32(Data + 0x3C);
    if (!RangeInside(PeOffset, 4 + 20, Size) ||
        Data[PeOffset] != 'P' || Data[PeOffset + 1] != 'E' ||
        Data[PeOffset + 2] != 0 || Data[PeOffset + 3] != 0) {
        return false;
    }
    Machine = ReadU16(Data + PeOffset + 4);
    NumberOfSections = ReadU16(Data + PeOffset + 6);
    OptionalSize = ReadU16(Data + PeOffset + 20);

    if (Machine != UINT16_C(0xAA64) || NumberOfSections == 0 ||
        NumberOfSections > PE_IMAGE_MAX_SECTIONS) {
        return false;
    }
    OptionalOffset = PeOffset + 24;
    if (!RangeInside(OptionalOffset, OptionalSize, Size) ||
        OptionalSize < 112) {
        return false;
    }
    OptionalMagic = ReadU16(Data + OptionalOffset);
    if (OptionalMagic != UINT16_C(0x20B)) {
        return false;
    }
    SectionTable = OptionalOffset + OptionalSize;
    SectionBytes = (size_t)NumberOfSections * 40;
    if ((NumberOfSections != 0 &&
         SectionBytes / 40 != NumberOfSections) ||
        !RangeInside(SectionTable, SectionBytes, Size)) {
        return false;
    }
    HeadersEnd = SectionTable + SectionBytes;

    Image->Data = Data;
    Image->Size = Size;
    Image->SectionTable = SectionTable;
    Image->NumberOfSections = NumberOfSections;

    for (Index = 0; Index < NumberOfSections; ++Index) {
        PE_SECTION Section;
        size_t RawStart;
        size_t RawEnd;
        uint64_t MappedEnd;
        if (!PeImageGetSection(Image, Index, &Section) ||
            !SectionRawRange(&Section, &RawStart, &RawEnd) ||
            !RangeInside(RawStart, Section.SizeOfRawData, Size)) {
            return false;
        }
        if (Section.SizeOfRawData != 0 && RawStart < HeadersEnd) {
            return false;
        }
        MappedEnd = (uint64_t)Section.VirtualAddress + SectionMappedLength(&Section);
        if (MappedEnd > UINT32_MAX + UINT64_C(1)) {
            return false;
        }
        for (size_t Previous = 0; Previous < Index; ++Previous) {
            PE_SECTION Other;
            size_t OtherStart;
            size_t OtherEnd;
            if (!PeImageGetSection(Image, Previous, &Other) ||
                !SectionRawRange(&Other, &OtherStart, &OtherEnd)) {
                return false;
            }
            if (Section.SizeOfRawData != 0 && Other.SizeOfRawData != 0 &&
                RawStart < OtherEnd && OtherStart < RawEnd) {
                return false;
            }
            {
                uint64_t SectionLength = SectionMappedLength(&Section);
                uint64_t OtherLength = SectionMappedLength(&Other);
                uint64_t OtherMappedEnd =
                    (uint64_t)Other.VirtualAddress + OtherLength;
                if (SectionLength != 0 && OtherLength != 0 &&
                    (uint64_t)Section.VirtualAddress < OtherMappedEnd &&
                    (uint64_t)Other.VirtualAddress < MappedEnd) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool PeImageFindSectionForOffset(const PE_IMAGE *Image,
                                 size_t Offset,
                                 size_t Length,
                                 bool ExecutableOnly,
                                 size_t *SectionIndex) {
    size_t Index;
    if (Image == NULL || Image->Data == NULL || Length == 0 ||
        !RangeInside(Offset, Length, Image->Size)) {
        return false;
    }
    for (Index = 0; Index < Image->NumberOfSections; ++Index) {
        PE_SECTION Section;
        size_t RawStart;
        size_t RawEnd;
        size_t Relative;
        uint64_t MappedLength;
        if (!PeImageGetSection(Image, Index, &Section) ||
            (ExecutableOnly &&
             (Section.Characteristics & PE_IMAGE_SCN_MEM_EXECUTE) == 0) ||
            Section.SizeOfRawData == 0 ||
            !SectionRawRange(&Section, &RawStart, &RawEnd) ||
            Offset < RawStart || Offset > RawEnd ||
            Length > RawEnd - Offset) {
            continue;
        }
        Relative = Offset - RawStart;
        MappedLength = SectionMappedLength(&Section);
        if ((uint64_t)Relative + Length > MappedLength) {
            continue;
        }
        if (SectionIndex != NULL) {
            *SectionIndex = Index;
        }
        return true;
    }
    return false;
}

bool PeImageFileOffsetToRva(const PE_IMAGE *Image,
                            size_t Offset,
                            size_t Length,
                            uint32_t *Rva) {
    size_t Index;
    if (Rva == NULL ||
        !PeImageFindSectionForOffset(Image, Offset, Length, false, &Index)) {
        return false;
    }
    PE_SECTION Section;
    size_t RawStart;
    if (!PeImageGetSection(Image, Index, &Section) ||
        !SectionRawRange(&Section, &RawStart, &(size_t){0})) {
        return false;
    }
    uint64_t Value = (uint64_t)Section.VirtualAddress + (Offset - RawStart);
    if (Value > UINT32_MAX) {
        return false;
    }
    *Rva = (uint32_t)Value;
    return true;
}

bool PeImageRvaToFileOffset(const PE_IMAGE *Image,
                            uint32_t Rva,
                            size_t Length,
                            size_t *Offset) {
    size_t Index;
    if (Image == NULL || Image->Data == NULL || Length == 0 || Offset == NULL) {
        return false;
    }
    for (Index = 0; Index < Image->NumberOfSections; ++Index) {
        PE_SECTION Section;
        uint64_t Relative;
        uint64_t MappedLength;
        size_t RawStart;
        if (!PeImageGetSection(Image, Index, &Section) ||
            Section.SizeOfRawData == 0 ||
            !SectionRawRange(&Section, &RawStart, &(size_t){0})) {
            continue;
        }
        if ((uint64_t)Rva < Section.VirtualAddress) {
            continue;
        }
        Relative = (uint64_t)Rva - Section.VirtualAddress;
        MappedLength = SectionMappedLength(&Section);
        if (Relative > MappedLength ||
            Length > MappedLength - Relative ||
            Relative > Section.SizeOfRawData ||
            Length > (uint64_t)Section.SizeOfRawData - Relative) {
            continue;
        }
        if (RawStart > SIZE_MAX - (size_t)Relative ||
            !RangeInside(RawStart + (size_t)Relative, Length, Image->Size)) {
            continue;
        }
        *Offset = RawStart + (size_t)Relative;
        return true;
    }
    return false;
}

bool PeImageDecodeAdrpAdd(const PE_IMAGE *Image, size_t Offset,
                          uint32_t *TargetRva) {
    uint32_t Adrp, Add, AdrpRva, Imm21, Imm12, Reg;
    int32_t SignedImm;
    int64_t Target;

    if (TargetRva == NULL || Image == NULL || Image->Data == NULL ||
        !RangeInside(Offset, 8, Image->Size) ||
        !PeImageFileOffsetToRva(Image, Offset, 8, &AdrpRva)) {
        return false;
    }
    Adrp = ReadU32(Image->Data + Offset);
    Add = ReadU32(Image->Data + Offset + 4);
    if ((Adrp & UINT32_C(0x9F000000)) != UINT32_C(0x90000000) ||
        (Add & UINT32_C(0xFF800000)) != UINT32_C(0x91000000)) {
        return false;
    }
    Reg = Adrp & 31;
    if (Reg == 31 || (Add & 31) != Reg || ((Add >> 5) & 31) != Reg) {
        return false;
    }
    Imm21 = (((Adrp >> 5) & UINT32_C(0x7FFFF)) << 2) | ((Adrp >> 29) & 3);
    SignedImm = (int32_t)((Imm21 ^ UINT32_C(0x100000)) - UINT32_C(0x100000));
    Imm12 = (Add >> 10) & 0xFFF;
    if ((Add & (UINT32_C(1) << 22)) != 0) {
        Imm12 <<= 12;
    }
    Target = (int64_t)(AdrpRva & UINT32_C(0xFFFFF000)) +
             (int64_t)SignedImm * 4096 + Imm12;
    if (Target < 0 || (uint64_t)Target > UINT32_MAX) {
        return false;
    }
    *TargetRva = (uint32_t)Target;
    return true;
}
