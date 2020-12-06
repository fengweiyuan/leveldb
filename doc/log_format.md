leveldb Log format
==================
The log file contents are a sequence of 32KB blocks.  The only exception is that
the tail of the file may contain a partial block.

> 日志文件是由32KB的块组成，文件尾部的块可能不足32KB。每个32KB块，包含如下这些内容:

Each block consists of a sequence of records:

    block := record* trailer?
    record :=
      checksum: uint32     // crc32c of type and data[] ; little-endian
      length: uint16       // little-endian
      type: uint8          // One of FULL, FIRST, MIDDLE, LAST
      data: uint8[length]

> 日志的头部，4个字节存checksum，然后2个字节存长度(所以1条日志不能超过1024*1024=1MB)，1个字节存类型。然后存日志的内容。


A record never starts within the last six bytes of a block (since it won't fit).
Any leftover bytes here form the trailer, which must consist entirely of zero
bytes and must be skipped by readers.

> 最短的日志条目(不包含任何数据)是7Byte，所以如果一个block还剩下6个Byte，就不会拿来用了，而是直接全部填充0.

Aside: if exactly seven bytes are left in the current block, and a new non-zero
length record is added, the writer must emit a FIRST record (which contains zero
bytes of user data) to fill up the trailing seven bytes of the block and then
emit all of the user data in subsequent blocks.

More types may be added in the future.  Some Readers may skip record types they
do not understand, others may report that some data was skipped.

> 将来可能会添加更多类型，所以一些日志解释工具，可能就不能很好地工作了，要注意这个问题。

    FULL == 1    说明该log record包含一个完整的user record
    FIRST == 2   说明是user record的第一条log record
    MIDDLE == 3  说明是user record中间的log record
    LAST == 4    说明是user record最后的一条log record

The FULL record contains the contents of an entire user record.

FIRST, MIDDLE, LAST are types used for user records that have been split into
multiple fragments (typically because of block boundaries).  FIRST is the type
of the first fragment of a user record, LAST is the type of the last fragment of
a user record, and MIDDLE is the type of all interior fragments of a user
record.

Example: consider a sequence of user records:

    A: length 1000
    B: length 97270
    C: length 8000

**A** will be stored as a FULL record in the first block.

**B** will be split into three fragments: first fragment occupies the rest of
the first block, second fragment occupies the entirety of the second block, and
the third fragment occupies a prefix of the third block.  This will leave six
bytes free in the third block, which will be left empty as the trailer.

**C** will be stored as a FULL record in the fourth block.

> 这里有一个例子，有三条用户日志，分别是1000字节，97270字节，8000字节。
而我们也知道，一个block是32KB=32768字节。
所以存入A日志时，会用FULL标记，说明日志没有分到多个block上存储。
B日志会被拆分成三部分，分别存储在第1、2、3个block中。这时，block3还剩下6Byte，将全部填充0。
C日志也会用FULL标记，存储在第4个block上。

----

## Some benefits over the recordio format:

1. We do not need any heuristics for resyncing - just go to next block boundary
   and scan.  If there is a corruption, skip to the next block.  As a
   side-benefit, we do not get confused when part of the contents of one log
   file are embedded as a record inside another log file.

2. Splitting at approximate boundaries (e.g., for mapreduce) is simple: find the
   next block boundary and skip records until we hit a FULL or FIRST record.

3. We do not need extra buffering for large records.

## Some downsides compared to recordio format:

1. No packing of tiny records.  This could be fixed by adding a new record type,
   so it is a shortcoming of the current implementation, not necessarily the
   format.

2. No compression.  Again, this could be fixed by adding new record types.
