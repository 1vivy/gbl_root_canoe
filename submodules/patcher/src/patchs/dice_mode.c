/* Host-prepared ABLs must populate Normal DICE mode on both BCC paths.
 * Resolve the unique DEBUG-string xrefs and their nearby instruction shapes
 * before allowing the caller to write either site. Unknown builds fail closed.
 */
#include "patchs/dice_mode.h"
#include "patchs/pe_sections.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char MainLog[] = "VB: PopulateBccParams: Parameter receivedis NULL";
static const char DummyLog[] = "VB: Setting Dummy DICE params\n";

#define CINC_W8 UINT32_C(0x1A8A0548)
#define MOV_W9_THREE UINT32_C(0x52800069)
#define CSEL_W8 UINT32_C(0x1A890108)
#define MOV_W8_NORMAL UINT32_C(0x52800028)
#define MOV_W8_DEBUG UINT32_C(0x52800048)
#define DUMMY_CBZ UINT32_C(0x34000068)
#define STR_W8_X19 UINT32_C(0xB9006268)
#define NOP UINT32_C(0xD503201F)

static uint32_t Word(const PE_IMAGE *Image, size_t Offset) {
    const uint8_t *At = Image->Data + Offset;
    return (uint32_t)At[0] | ((uint32_t)At[1] << 8) |
           ((uint32_t)At[2] << 16) | ((uint32_t)At[3] << 24);
}

static void WriteWord(uint8_t *At, uint32_t Value) {
    for (unsigned Index = 0; Index < 4; ++Index) {
        At[Index] = (uint8_t)(Value >> (Index * 8));
    }
}

static bool InCode(const PE_IMAGE *Image, size_t Offset, size_t Length,
                   size_t Section) {
    size_t Actual;
    return PeImageFindSectionForOffset(Image, Offset, Length, true, &Actual) &&
           Actual == Section;
}

static bool UniqueReference(const PE_IMAGE *Image, const char *Log,
                            size_t *Reference, size_t *Section) {
    size_t Length = strlen(Log) + 1;
    size_t String = 0;
    uint32_t Rva;
    unsigned Matches = 0;
    if (Image->Size < Length) return false;
    for (size_t Off = 0; Off <= Image->Size - Length; ++Off) {
        if (memcmp(Image->Data + Off, Log, Length) == 0) {
            if (++Matches > 1) return false;
            String = Off;
        }
    }
    if (Matches != 1 || !PeImageFileOffsetToRva(Image, String, Length, &Rva) ||
        Image->Size < 8) return false;
    Matches = 0;
    for (size_t Off = 0; Off <= Image->Size - 8; Off += 4) {
        size_t CodeSection;
        uint32_t Target;
        if (!PeImageFindSectionForOffset(Image, Off, 8, true, &CodeSection) ||
            !PeImageDecodeAdrpAdd(Image, Off, &Target) || Target != Rva) continue;
        if (++Matches > 1) return false;
        *Reference = Off;
        *Section = CodeSection;
    }
    return Matches == 1;
}

static bool MainSite(const PE_IMAGE *Image, size_t Reference,
                     size_t Section, size_t *Site) {
    unsigned Matches = 0;
    size_t Start = Reference > 0x200 ? Reference - 0x200 : 0;
    for (size_t Off = Start; Off < Reference; Off += 4) {
        bool Cinc = false, Mov = false, Store = false;
        if (Off < 0x30 || !InCode(Image, Off - 0x30, 0x44, Section) ||
            Word(Image, Off) != CSEL_W8 ||
            (Word(Image, Off + 4) & UINT32_C(0x9F00001F)) !=
                UINT32_C(0x90000009)) continue; /* ADRP X9 for BCC global */
        for (size_t At = Off - 0x30; At < Off; At += 4) {
            Cinc |= Word(Image, At) == CINC_W8;
            Mov |= Word(Image, At) == MOV_W9_THREE;
        }
        for (size_t At = Off + 4; At < Off + 20; At += 4) {
            Store |= (Word(Image, At) & UINT32_C(0xFFC003FF)) ==
                     UINT32_C(0xB9000128); /* STR W8,[X9,#imm] */
        }
        if (!Cinc || !Mov || !Store || ++Matches > 1) continue;
        *Site = Off;
    }
    return Matches == 1;
}

static bool DummySite(const PE_IMAGE *Image, size_t Reference,
                      size_t Section, size_t *Site) {
    unsigned Matches = 0;
    size_t End = Reference + 0x38;
    if (End > Image->Size) End = Image->Size;
    for (size_t Off = Reference + 8; Off + 8 <= End; Off += 4) {
        if (!InCode(Image, Off - 4, 12, Section) ||
            Word(Image, Off - 4) != DUMMY_CBZ ||
            Word(Image, Off) != MOV_W8_DEBUG ||
            Word(Image, Off + 4) != STR_W8_X19) continue;
        if (++Matches > 1) return false;
        *Site = Off;
    }
    return Matches == 1;
}

bool PlanDiceModeNormal(const char *Buffer, int32_t Size, DICE_PLAN *Plan) {
    PE_IMAGE Image;
    size_t MainReference, DummyReference, MainSection, DummySection;
    DICE_PLAN Sites;
    if (Plan == NULL || Buffer == NULL || Size <= 0 ||
        !PeImageInit(&Image, (const uint8_t *)Buffer, (size_t)Size) ||
        !UniqueReference(&Image, MainLog, &MainReference, &MainSection) ||
        !UniqueReference(&Image, DummyLog, &DummyReference, &DummySection) ||
        !MainSite(&Image, MainReference, MainSection, &Sites.Main) ||
        !DummySite(&Image, DummyReference, DummySection, &Sites.Dummy)) return false;
    *Plan = Sites;
    return true;
}

void ApplyDiceModeNormal(char *Buffer, const DICE_PLAN *Plan) {
    WriteWord((uint8_t *)Buffer + Plan->Main, MOV_W8_NORMAL);
    /* The dummy BCC was zeroed; its old CBZ skipped the Debug-mode store
     * when locked. Make the Normal-mode store unconditional instead. */
    WriteWord((uint8_t *)Buffer + Plan->Dummy - 4, NOP);
    WriteWord((uint8_t *)Buffer + Plan->Dummy, MOV_W8_NORMAL);
}
