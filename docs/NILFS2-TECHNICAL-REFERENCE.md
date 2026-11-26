# NILFS2: technical reference

## History and design

NILFS, developed by NTT, is a continuously checkpointed log-structured filesystem. NILFS2, its second on-disk generation, entered Linux 2.6.30. Unlike an ordinary journalling filesystem, the log is the principal storage representation; new versions of data and metadata are appended. Ordinary checkpoints are generated as the filesystem changes; selected checkpoints can be retained as snapshots. An independent implementation must preserve checkpoint visibility and distinguish a historical virtual-block identity from the current physical storage location. Garbage collection is therefore part of correctness, not simply optional performance tuning.

## Volume geometry and superblocks

The on-disk byte order is little-endian. The primary superblock starts at byte 1024. The secondary starts at (floor(device_bytes/4096)-1)*4096, provided that location is valid and outside allocatable segment space. Relative offsets in the superblock: major revision 0 (32 bits), minor 4, magic 6 (0x3434), checksum byte count 8, flags 10, CRC seed 12, checksum 16, log block size 20; 64-bit segment count 24, device size 32, first data block 40; blocks per segment 48 and reserved percentage 52. Block bytes = 1024*2^log_block_size. The declared size and segment geometry must fit the image; the format header defines 1 to 64 KiB blocks, but implementation and host profiles may restrict this further.

## Superblock: checkpoint anchors

Offsets 56,64,72 and 80 contain the 64-bit latest checkpoint number, latest partial-segment block address, latest segment sequence and free-block count. Times begin at 88; mount counts and state at 112..119; first ordinary inode at 140. Declared inode/DAT/checkpoint/segment-usage entry sizes at 144,146,148,150 (16-bit). UUID at 152; 80-byte label at 168; compatible, read-only-compatible and incompatible 64-bit feature words at 256,264,272. Validate checksum over the declared prefix with the checksum field treated according to the native routine. Compare both valid copies and corroborate their latest segment anchors; do not choose merely by wall-clock time. Unsupported incompatible features prohibit mounting.

## Segment summaries and partial segments

A full segment is an allocation and cleaning unit; one or more partial segments contain appended updates. A partial segment begins with a summary. Header offsets: data checksum 0, summary checksum 4, magic 0x1EAFFA11 at 8, header bytes 12 (16-bit), flags 14, sequence 16, creation time 24, next segment block address 32 (all 64-bit), block count 40, file-info count 44, summary byte count 48, reserved 52, checkpoint number 56. File-info records are 24 bytes: inode, checkpoint (64-bit each), total blocks and data blocks (32-bit each). Subsequent block-info records depend on whether the owner is DAT, a data block or a tree node. Validate summary spans and payload counts before parsing variable records; summaries can cross block boundaries.

## Super-root and bootstrap order

A partial segment with the super-root flag 0x0004 ends in a super-root block; other summary flags identify logical-log begin/end, data-only synchronization and cleaner activity. Super-root offset 0 has checksum, 4 byte count, 6 flags, 8 last non-cleaner timestamp. At 16 are three variable-size inode records in order: DAT (inode 3), checkpoint file (4), segment usage file (5). With 128-byte inodes these begin at 16,144,272. Decode the DAT mapping using physical block pointers; its own lookup must not recursively depend on DAT. Then read the checkpoint file using the root metadata mappings, obtain the selected checkpoint's inode-file inode (6), locate ordinary inode 2 (root), and traverse directories. Never assemble a synthetic state from unrelated super-roots.

## Inode field inventory

Minimum inode record is 128 bytes. Offsets: 64-bit allocated-block count 0, byte size 8, ctime 16, mtime 24; 32-bit ctime and mtime nanoseconds 32/36, UID/GID 40/44; 16-bit mode and link count 48/50; flags 52; seven 64-bit mapping words at 56..111; xattr reference 112, generation 120 and padding 124. Reserved inode IDs include root 2, DAT 3, cpfile 4, sufile 5 and ifile 6; first ordinary inode is normally 11. Respect declared record size, reserved identities and type. Special-device inodes repurpose mapping storage and must not be traversed as ordinary files. Inode numbers are located through ifile's entry allocator, not a fixed physical inode-table offset.

## Direct mapping and B-tree nodes

The 56-byte inode mapping starts with an eight-byte header. In direct mode the remaining six 64-bit pointers address logical blocks 0..5. In tree mode the header carries flags, level, child count and padding; the inode root can hold three key-pointer pairs. Non-root B-tree nodes have an eight-byte header, eight bytes of extra padding and separate arrays of 64-bit keys and pointers. At 4096 bytes the non-root capacity is floor((4096-16)/16)=255. Pointer-array position is based on node capacity, not current child count. Keys are logical block offsets; select the lower-bound subtree as specified by the sorted-key representation, then require an exact logical key at the final mapping level. Validate root flag, level, count, ordering and cycles. Root metadata DAT uses physical mapping, while checkpoint-visible mappings use virtual identities.

## Persistent allocator for DAT and ifile

For block size B and entry size E, the persistent entry allocator groups 8B entries per bitmap block. Each entry-data block holds floor(B/E) entries; a group spans 1+ceil(8B/floor(B/E)) logical blocks including its bitmap. Each descriptor block holds B/4 four-byte free-entry counters. Let G=floor(entry_id/(8B)), R=entry_id mod (8B), D=B/4 and P=blocks_per_group. Descriptor block offset is floor(G/D)*(1+D*P); bitmap offset is descriptor+1+(G mod D)*P; entry block offset is bitmap+1+floor(R/floor(B/E)); in-block byte offset is (R mod floor(B/E))*E. These are logical metadata-file offsets, resolved through the file's mapping. Check the allocation bit, free counter and size before interpreting the record.

## DAT and worked virtual mapping

A DAT record is at least 32 bytes: physical block at 0, first checkpoint at 8, exclusive end checkpoint at 16, reserved at 24 (all 64-bit). A virtual block persists across relocation while its physical location can change. For a 4096-byte block and 32-byte DAT entry, each group holds 32768 entries, 128 per entry block and 257 blocks per group. Virtual block 130 is in group zero, descriptor at logical block 0, bitmap at 1, entry block at 3 and slot 2 (byte 64). Resolve DAT logical block 3 through DAT's physical mapping. If the selected record maps to physical block 8000, the payload starts at byte 32768000. Validate the lifetime interval against the chosen checkpoint for forensic ownership; do not mistake an expired identity for a free physical block while a retained checkpoint still uses it.

## Checkpoint file and snapshots

The cpfile header is 32 bytes: checkpoint count, snapshot count, next and previous snapshot checkpoint numbers (64-bit each). Each checkpoint record is at least 192 bytes: flags 0, valid-count field 4, next/previous snapshot links 8/16, checkpoint number 24, time 32, incremented blocks 40, inode count 48, block count 56, ifile inode at 64. The first checkpoint slot follows the header rounded up to record units. For record size C, entry number = cno+ceil(32/C)-1; logical block = floor(entry_number/floor(B/C)), slot = remainder. Snapshot flag bit 0 marks preservation; invalid bit 1 excludes a deleted checkpoint. Follow and cross-check snapshot list links, never interpret a checkpoint's obsolete inode-file image as the live tree. Snapshot mounts are read-only historical views.

## Directories and data mapping

A directory record contains 64-bit inode at 0, 16-bit record length at 8, one-byte name length at 10, file type at 11 and name at 12. Records are aligned to eight bytes; minimum length is align_up(12+name_length,8). Inode zero is unused; 64 KiB blocks need the format-specific maximum-record-length encoding. Verify boundaries and name lengths before advancing. Resolve the selected checkpoint's ifile and mapping; for file logical block k, map through direct or tree representation to a virtual block, translate through DAT, then clip the physical read to EOF. Absent mappings can represent holes under native rules; an invalid DAT entry is not a hole. Preserve inode identity for hard links and check dot/dot-dot relationships.

## Segment usage, allocation and cleaning

The sufile header begins with 64-bit clean-segment count, dirty count and last allocated segment number. Entries are at least 16 bytes: last modification time 0, used block count 8, flags 12. Active, dirty and error bits are 0,1,2; clean has no flags. Entry number for segment s equals s+ceil(24/entry_size); map into logical blocks with floor(B/entry_size) records each. A cleaner identifies live virtual blocks using summaries, DAT lifetime and retained checkpoint membership. It moves live blocks, updates DAT locations and publishes the replacement log state before releasing the old segment. Root metadata is treated specially; old versions are not retained like ordinary checkpoint-visible files. Never discard a segment simply because it is old or has few current references.

## Consistency, recovery and defragmentation

A logical log may span several partial segments; verify summary CRCs, payload coverage, sequence and begin/end markers. Recover from the most recent coherent super-root and only admit the exact native roll-forward treatment of complete data-sync logs beyond it. A dirty superblock or higher segment sequence alone is not proof of commitment. For write qualification, inject failures at every append, super-root publication, superblock update and cleaning boundary. Defrag must preserve virtual identities, checkpoint visibility, and allocation invariants; physical contiguity improvement is not permission to rewrite historical views. Reserve cleaning headroom; avoid circular allocation where collection needs space that it cannot obtain.

## References and verification

Pinned layout: https://github.com/torvalds/linux/blob/v6.12/include/uapi/linux/nilfs2_ondisk.h . Algorithms: https://github.com/torvalds/linux/tree/v6.12/fs/nilfs2 (alloc.c, dat.c, cpfile.c, sufile.c, btree.c, recovery.c). Design and tools: https://nilfs.sourceforge.io/ and https://docs.kernel.org/filesystems/nilfs2.html . Test independently formatted images, both superblocks, multiple retained snapshots, DAT allocator group boundaries, tree splits, sparse mappings, damaged summaries, truncated logical logs and cleaning with snapshots. Compare every historical and current payload against independent mounts. Exact summary checksum coverage, binfo subtypes, maximum-record encoding and all recovery paths need further source/fixture validation before independent writer compatibility is claimed. The repository's prior design documents are preserved as historical appendices.

The PDF preserves both earlier documents as historical appendices.
