# Filesystem-NILFS2

Independent Common V2 native recognition development. Native CRC-validated superblock copies and bounded checksummed partial-segment/super-root anchor; no checkpoint namespace or DAT traversal.

The implementation exports only CORE PROBE; it neither mounts nor modifies media. Hosted and import-free builds, malformed metadata fixtures, independent formatter images and sanitizer tests are defined in CI. Exact-revision test results establish qualification, not the presence of this source. The published v0.0.1 package remains documentation-only; no runtime installer is published at this stage.

## Technical references

- [Integrated format manual](docs/NILFS2.pdf)
- [Technical reference source](docs/NILFS2-TECHNICAL-REFERENCE.md)
- [Design](native/filesystems/nilfs2/DESIGN.md)
- [Migration status](MIGRATION_STATUS.md)
- [Pinned central source](https://github.com/Infiltrator-Projects/Filesystems/tree/101026021d9088d80b7f0f472d9061f0e2c688a3/native/filesystems/nilfs2)
- [Common V2](https://github.com/Infiltrator-Projects/Libraries)

Original implementation GPL-3.0-or-later; inherited sources retain their notices.
