// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/log_writer.h"

#include <cstdint>

#include "leveldb/env.h"
#include "util/coding.h"
#include "util/crc32c.h"

namespace leveldb {
namespace log {

static void InitTypeCrc(uint32_t* type_crc) {
  for (int i = 0; i <= kMaxRecordType; i++) {
    char t = static_cast<char>(i);
    type_crc[i] = crc32c::Value(&t, 1);
  }
}

Writer::Writer(WritableFile* dest) : dest_(dest), block_offset_(0) {
  InitTypeCrc(type_crc_);
}

Writer::Writer(WritableFile* dest, uint64_t dest_length)
    : dest_(dest), block_offset_(dest_length % kBlockSize) {
  InitTypeCrc(type_crc_);
}

Writer::~Writer() = default;

/**
 * AddRecord方法，会考虑日志是否需要分割，并将日志完整或仅仅是当中一个分片，物理写入到日志文件中
 */
Status Writer::AddRecord(const Slice& slice) {
  const char* ptr = slice.data();

  // left是这个日志条目有多长
  size_t left = slice.size();

  // Fragment the record if necessary and emit it.  Note that if slice
  // is empty, we still want to iterate once to emit a single
  // zero-length record
  // 考虑这个日志条目是否需要分割
  Status s;
  // 说明是刚开始考虑该日志条目的写入
  bool begin = true;  
  do {
    // 这个block还剩下多少字节的空间
    const int leftover = kBlockSize - block_offset_;
    assert(leftover >= 0);
    // 如果这个block剩余的空间不足7个字节，那么，切换到一个新的block，旧的block剩余部分都填充0
    if (leftover < kHeaderSize) {
      // Switch to a new block
      if (leftover > 0) {
        // Fill the trailer (literal below relies on kHeaderSize being 7)
        // 剩余部分填充为0，但这个依赖kHeaderSize是7，不然编译时被改了就麻烦
        static_assert(kHeaderSize == 7, "");   
        dest_->Append(Slice("\x00\x00\x00\x00\x00\x00", leftover));
      }
      // 指针重置为0
      block_offset_ = 0;
    }

    // Invariant: we never leave < kHeaderSize bytes in a block.
    // 32768 - 32767 - 7 >= 0 不成立，来到此处，block_offset后至少保留7个字节
    // 32768 - 32761 - 7 >= 0 成立
    assert(kBlockSize - block_offset_ - kHeaderSize >= 0);

    // 计算这个block还有多少可剩余的写入
    const size_t avail = kBlockSize - block_offset_ - kHeaderSize;

    // 这个片段有多长
    const size_t fragment_length = (left < avail) ? left : avail;

    RecordType type;
    // 如果left == fragment_length说明这个block放得下，此时end = true
    // 如果left != fragment_length说明这个block放不下，此时end = false
    const bool end = (left == fragment_length);
    if (begin && end) {
      type = kFullType;
    } else if (begin) {
      type = kFirstType;
    } else if (end) {
      type = kLastType;
    } else {
      type = kMiddleType;
    }

    // 发出物理写入
    s = EmitPhysicalRecord(type, ptr, fragment_length);
    // 处理日志条目(slice)的指针也往前移
    ptr += fragment_length;
    // left不断减少知道为0
    left -= fragment_length;
    // 处理完了以后begin标记就置false，表明该日志不是首次被循环处理
    begin = false;
  } while (s.ok() && left > 0);
  return s;
}

/**
 * 物理写入到日志文件中
 * t 日志类型
 * ptr 指针表明从哪里开始取数据
 * length 取数据长度
 */
Status Writer::EmitPhysicalRecord(RecordType t, const char* ptr,
                                  size_t length) {
  // 一次物理写入可以写多长呢？不能超过2的16次方=1MB
  assert(length <= 0xffff);  // Must fit in two bytes

  // 一次物理写入只能在一个block上，所以这里会规定，当前block的指针，前移一个header(7字节)，再前移写入长度，不能超过一个block的大小(32KB)
  assert(block_offset_ + kHeaderSize + length <= kBlockSize);

  // Format the header
  // 弄出7个字节的数组，在栈空间
  char buf[kHeaderSize];
  buf[4] = static_cast<char>(length & 0xff);  // 把长度重写到第4、5个字节，可这里为什么要用这么复杂的写法？效率更高？
  buf[5] = static_cast<char>(length >> 8);
  buf[6] = static_cast<char>(t);              // 第7个字节写类型

  // Compute the crc of the record type and the payload.
  // 计算写入内容开始至某个长度，以及类型，一起来计算一个crc32
  uint32_t crc = crc32c::Extend(type_crc_[t], ptr, length);
  crc = crc32c::Mask(crc);  // Adjust for storage
  EncodeFixed32(buf, crc);

  // Write the header and the payload
  // 先写头部，然后写条目内容，并且flush()一下确保缓存写到了硬盘
  Status s = dest_->Append(Slice(buf, kHeaderSize));
  if (s.ok()) {
    s = dest_->Append(Slice(ptr, length));
    if (s.ok()) {
      s = dest_->Flush();
    }
  }

  // 32KB块的指针前移
  block_offset_ += kHeaderSize + length;
  return s;
}

}  // namespace log
}  // namespace leveldb
