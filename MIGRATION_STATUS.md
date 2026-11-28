# NILFS2 Common V2 migration

Native CRC-validated superblock copies and bounded checksummed partial-segment/super-root anchor; no checkpoint namespace or DAT traversal.

CORE PROBE is implemented. No mounted operations, allocation map, native formatting, mutation, defragmentation, journal recovery, physical BOOT or kernel qualification are claimed. CI tests the actual shared object with bounded sector I/O and independent disposable formatter images; the formatter is never a runtime backend. Further namespace work and independent payload comparison are required before publishing a runtime package. The previous documentation release remains valid.

The [original pinned source](https://github.com/Infiltrator-Projects/Filesystems/tree/101026021d9088d80b7f0f472d9061f0e2c688a3/native/filesystems/nilfs2) remains central reference material; its original notices and licences remain intact.

## Native checkpoint reader development

CORE mount/stat/list/read and VOLUME query are now implemented for the bounded clean baseline profile, with real DAT and ifile allocator traversal and segment usage accounting. Exact-source independent mount, populated payload, corrupt metadata and sanitizer qualification must be established before runtime packaging. No native mutation or recovery is claimed.
