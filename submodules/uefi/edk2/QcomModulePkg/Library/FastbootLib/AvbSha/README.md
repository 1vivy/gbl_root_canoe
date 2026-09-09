# AVB SHA-256 import

Source: Android Open Source Project external/avb, commit
`761178607206f4cb2af79ed9eec52d8cbd814adb`, `libavb/sha/sha256_impl.c`
and its four required headers. Source URL:
https://android.googlesource.com/platform/external/avb/+/761178607206f4cb2af79ed9eec52d8cbd814adb/libavb/sha/sha256_impl.c

The algorithm source and `avb_crypto.h`/`sha/avb_crypto_ops_impl.h` are unchanged.
Only `avb_sysdeps.h` platform typedefs and `avb_sha.h` relative include path are
adapted. `FastbootHash.c` supplies memory operations through EDK II BaseMemoryLib.
Copyright and licenses remain in every file. Unlike the old Qualcomm snapshot,
this revision has a 64-bit total length and final bit-length encoding.
