# NILFS2 Common V2 migration

The native offline reader implements committed super-root bootstrap, physical DAT and virtual mapping, checkpoint ifile resolution, allocator ownership and segment accounting, CORE probe/mount/stat/list/read and VOLUME query for the bounded 4 KiB baseline profile. Both superblock headers must validate; the highest checkpoint anchor must be clean and checksum-valid. Refuse conflicting equal-checkpoint anchors and never fall back to stale data.

[Exact development CI 46135161568](https://github.com/Infiltrator-Projects/Filesystem-NILFS2/actions/runs/46135161568) passed the independent formatter namespace, independently populated 33,001-byte random payload, nested and sparse files, image hash verification, hosted/import-free structural and sanitizer tests. The reference driver created a disposable image exclusively as an oracle; provider I/O took place after unmount. Runtime v0.1.0 publication requires the new exact-revision qualification and package reconstruction/installation checks.

Snapshot selection, replay, cleaning, native mutation, formatting, defragmentation and physical BOOT/kernel qualification remain outstanding. The [pinned format source](https://github.com/Infiltrator-Projects/Filesystems/tree/101026021d9088d80b7f0f472d9061f0e2c688a3/native/filesystems/nilfs2) is reference evidence, not a backend. New code GPL-3.0-or-later.

### Mounted volume lifetime

The provider copies the caller's size-versioned host descriptor and assigns each mount an opaque generation. An operation reserves its mount until its callback returns. A concurrent unmount or second operation on that mount returns `IFS_FS_BUSY`. Independent mounts remain usable. Unmount removes the generation before releasing native state, so a closed handle returns `IFS_FS_INVALID` even after a new mount reuses the same memory. The Common V2 regression holds a directory read inside a host I/O callback while a second thread queries and unmounts both volumes.
