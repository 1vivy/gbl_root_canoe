#ifndef PATCHS_PE_SECTIONS_H
#define PATCHS_PE_SECTIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PE_IMAGE_SCN_MEM_EXECUTE UINT32_C(0x20000000)

typedef struct {
    const uint8_t *Data;
    size_t Size;
    size_t SectionTable;
    uint16_t NumberOfSections;
} PE_IMAGE;

typedef struct {
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t Characteristics;
} PE_SECTION;

/* Parse and bounds-check a PE32/PE32+ image and all of its sections. */
bool PeImageInit(PE_IMAGE *Image, const uint8_t *Data, size_t Size);

bool PeImageGetSection(const PE_IMAGE *Image, size_t Index, PE_SECTION *Section);

/* Find the section containing a file range. */
bool PeImageFindSectionForOffset(const PE_IMAGE *Image,
                                 size_t Offset,
                                 size_t Length,
                                 bool ExecutableOnly,
                                 size_t *SectionIndex);

/* Translate a file range to its section-relative RVA. */
bool PeImageFileOffsetToRva(const PE_IMAGE *Image,
                            size_t Offset,
                            size_t Length,
                            uint32_t *Rva);

/* Translate an RVA range back to a file range. */
bool PeImageRvaToFileOffset(const PE_IMAGE *Image,
                            uint32_t Rva,
                            size_t Length,
                            size_t *Offset);

#endif /* PATCHS_PE_SECTIONS_H */
