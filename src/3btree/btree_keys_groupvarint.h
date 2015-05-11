/*
 * Copyright (C) 2005-2015 Christoph Rupp (chris@crupp.de).
 * All Rights Reserved.
 *
 * NOTICE: All information contained herein is, and remains the property
 * of Christoph Rupp and his suppliers, if any. The intellectual and
 * technical concepts contained herein are proprietary to Christoph Rupp
 * and his suppliers and may be covered by Patents, patents in process,
 * and are protected by trade secret or copyright law. Dissemination of
 * this information or reproduction of this material is strictly forbidden
 * unless prior written permission is obtained from Christoph Rupp.
 */

/*
 * Compressed 32bit integer keys
 *
 * @exception_safe: strong
 * @thread_safe: no
 */

#ifndef HAM_BTREE_KEYS_GROUPVARINT_H
#define HAM_BTREE_KEYS_GROUPVARINT_H

#include <sstream>
#include <iostream>
#include <algorithm>

#include "0root/root.h"

// Always verify that a file of level N does not include headers > N!
#include "3btree/btree_keys_block.h"

#ifndef HAM_ROOT_H
#  error "root.h was not included"
#endif

namespace hamsterdb {

//
// The template classes in this file are wrapped in a separate namespace
// to avoid naming clashes with other KeyLists
//
namespace Zint32 {

const uint32_t varintgb_mask[4] = { 0xFF, 0xFFFF, 0xFFFFFF, 0xFFFFFFFF };

// This structure is an "index" entry which describes the location
// of a variable-length block
#include "1base/packstart.h"
HAM_PACK_0 class HAM_PACK_1 GroupVarintIndex : public IndexBase {
  public:
    enum {
      // Initial size of a new block
      kInitialBlockSize = 17, // 1 + 4 * 4

      // Grow blocks by this factor
      kGrowFactor = 17,

      // Maximum GroupVarints per block
      kMaxGroupVarintsPerBlock = 8,

      // Maximum keys per block
      kMaxKeysPerBlock = kMaxGroupVarintsPerBlock * 4,

      // Maximum size of an encoded integer
      kMaxSizePerInt = 8,

      // Maximum block size - not relevant
      kMaxBlockSize = 102400,
    };

    // initialize this block index
    void initialize(uint32_t offset, uint32_t block_size) {
      IndexBase::initialize(offset);
      m_block_size = block_size;
      m_used_size = 0;
      m_key_count = 0;
    }

    // returns the used size of the block
    uint32_t used_size() const {
      return (m_used_size);
    }

    // sets the used size of the block
    void set_used_size(uint32_t size) {
      m_used_size = size;
    }

    // returns the total block size
    uint32_t block_size() const {
      return (m_block_size);
    }

    // sets the total block size
    void set_block_size(uint32_t size) {
      m_block_size = size;
    }

    // returns the key count
    uint32_t key_count() const {
      return (m_key_count);
    }

    // sets the key count
    void set_key_count(uint32_t key_count) {
      m_key_count = key_count;
    }

    // copies this block to the |dest| block
    void copy_to(const uint8_t *block_data, GroupVarintIndex *dest,
                    uint8_t *dest_data) {
      dest->set_value(value());
      dest->set_key_count(key_count());
      dest->set_used_size(used_size());
      ::memcpy(dest_data, block_data, block_size());
    }

  private:
    // the total size of this block; max 255 bytes
    unsigned int m_block_size : 8;

    // used size of this block; max 255 bytes
    unsigned int m_used_size : 8;

    // the number of keys in this block; max 255 (kMaxKeysPerBlock)
    unsigned int m_key_count : 8;
} HAM_PACK_2;
#include "1base/packstop.h"

struct GroupVarintCodecImpl : public BlockCodecBase<GroupVarintIndex>
{
  enum {
    kHasCompressApi = 1,
    kHasSelectApi = 1,
    kHasFindLowerBoundApi = 1,
    kHasInsertApi = 1,
  };

  static uint32_t compress_block(GroupVarintIndex *index, const uint32_t *in,
                  uint32_t *out) {
    ham_assert(index->key_count() > 0);
    return ((uint32_t)encodeArray(index->value(), in,
                            (size_t)index->key_count() - 1, out));
  }

  static uint32_t *uncompress_block(GroupVarintIndex *index,
                  const uint32_t *block_data, uint32_t *out) {
    size_t nvalue = index->key_count() - 1;
    if (nvalue > 0) {
      decodeArray(index->value(), block_data, (size_t)index->used_size(),
                      out, nvalue);
    }
    return (out);
  }

  static bool insert(GroupVarintIndex *index, uint32_t *in,
                  uint32_t key, int *pslot) {
    uint32_t initial = index->value();
    int slot = 0;

    uint32_t out[GroupVarintIndex::kMaxKeysPerBlock];

    // if index->value is replaced then the whole block has to be decompressed.
    if (key < initial) {
      if (index->key_count() > 1) {
        uncompress_block(index, in, out);
        std::memmove(out + 1, out, sizeof(uint32_t) * (index->key_count() - 1));
      }
      out[0] = initial;
      index->set_value(key);
      index->set_key_count(index->key_count() + 1);
      index->set_used_size((uint32_t)encodeArray(index->value(), out,
                                (size_t)index->key_count() - 1, in));
      *pslot = 1;
      return (true);
    }

    // skip as many groups as possible
    uint8_t *inbyte = reinterpret_cast<uint8_t *> (in);
    const uint8_t *const endbyte = inbyte + index->used_size();
    uint8_t *new_inbyte = inbyte;
    uint32_t new_initial = index->value();
    uint32_t remaining = index->key_count() - 1;

    uint32_t *pout = &out[0];
    bool is_inserted = false;

    while (endbyte > inbyte + 1 + 4 * 4) {
      uint32_t next_initial = initial;
      const uint8_t *next = decodeGroupVarIntDelta(inbyte, &next_initial, pout);

      remaining -= 4;

      // skip this group? then immediately proceed to the next one
      if (key > out[3]) {
        inbyte = (uint8_t *)next;
        initial = next_initial;
        slot += 4;
        continue;
      }

      if (is_inserted == false) {
        new_initial = initial;
        new_inbyte = inbyte;
        initial = next_initial;

        // otherwise make sure that the key does not yet exist
        if (key == out[0] || key == out[1] || key == out[2] || key == out[3])
          return (false);

        // insert the new key
        if (key < pout[0]) {
          std::memmove(&pout[1], &pout[0], 4 * sizeof(uint32_t));
          pout[0] = key;
          *pslot = slot + 1;
        }
        else if (key < pout[1]) {
          std::memmove(&pout[2], &pout[1], 3 * sizeof(uint32_t));
          pout[1] = key;
          *pslot = slot + 2;
        }
        else if (key < pout[2]) {
          std::memmove(&pout[3], &pout[2], 2 * sizeof(uint32_t));
          pout[2] = key;
          *pslot = slot + 3;
        }
        else { // if (key < pout[3])
          pout[4] = pout[3];
          pout[3] = key;
          *pslot = slot + 4;
        }

        is_inserted = true;
        pout += 5; // 4 decoded integers, 1 new key
      }
      else {
        pout += 4;
        slot += 4;
        initial = next_initial;
      }

      inbyte = (uint8_t *)next;
    }

    // from here on all remaining keys will be decoded and re-encoded
    if (is_inserted == false) {
      new_initial = initial;
      new_inbyte = inbyte;
    }

    // continue with the remaining deltas and insert the key if it was not
    // yet inserted
    while (endbyte > inbyte && remaining > 0) {
      uint32_t ints_decoded = remaining;
      inbyte = (uint8_t *)decodeSingleVarintDelta(inbyte, &initial,
                      &pout, &ints_decoded);
      // decodeSingleVarintDelta() increments pout; set it back to the previous
      // position
      pout -= ints_decoded;
      remaining -= ints_decoded;
      assert(inbyte <= endbyte);

      // check if the key already exists; if yes then return false.
      // if not then insert the key, or append it to the list of
      // decoded values 
      if (is_inserted == false) {
        if (key == pout[0])
          return (false);
        if (key < pout[0]) {
          std::memmove(&pout[1], &pout[0], ints_decoded * sizeof(uint32_t));
          pout[0] = key;
          *pslot = slot + 1;
          is_inserted = true;
        }
        else if (ints_decoded > 1) {
          if (key == pout[1])
            return (false);
          if (key < pout[1]) {
            std::memmove(&pout[2], &pout[1], (ints_decoded - 1) * sizeof(uint32_t));
            pout[1] = key;
            *pslot = slot + 2;
            is_inserted = true;
          }
          else if (ints_decoded > 2) {
            if (key == pout[2])
              return (false);
            if (key < pout[2]) {
              std::memmove(&pout[3], &pout[2], (ints_decoded - 2) * sizeof(uint32_t));
              pout[2] = key;
              *pslot = slot + 3;
              is_inserted = true;
            }
            else if (ints_decoded > 3) {
              if (key == pout[3])
                return (false);
              if (key < pout[3]) {
                pout[4] = pout[3];
                pout[3] = key;
                *pslot = slot + 4;
                is_inserted = true;
              }
            }
          }
        }
        if (is_inserted)
          pout += ints_decoded + 1;
        else {
          pout += ints_decoded;
          slot += ints_decoded;
        }
      }
      else {
        // is_inserted == true
        pout += ints_decoded;
      }
    }

    // otherwise append the key
    if (is_inserted == false) {
      *pslot = 1 + slot;
      *pout = key;
      pout++;
    }

    // now re-encode the decoded values. The encoded values are written
    // to |new_inbyte|, with |new_initial| as the initial value for the
    // delta calculation.
    size_t ints_to_write = pout - &out[0];
    uint32_t written = encodeArray(new_initial, &out[0], ints_to_write,
                    (uint32_t *)new_inbyte);
    index->set_key_count(index->key_count() + 1);
    index->set_used_size((uint32_t)(new_inbyte - (uint8_t *)in) + written);
    return (true);
  }

  static int find_lower_bound(GroupVarintIndex *index,
                  const uint32_t *in, uint32_t key, uint32_t *presult) {
    const uint8_t *inbyte = reinterpret_cast<const uint8_t *> (in);
    const uint8_t *const endbyte = inbyte + index->used_size();
    uint32_t out[4];
    int i = 0;
    uint32_t initial = index->value();
    uint32_t nvalue = index->key_count() - 1;

    while (endbyte > inbyte + 1 + 4 * 4) {
      inbyte = decodeGroupVarIntDelta(inbyte, &initial, out);
      if (key <= out[3]) {
        if (key <= out[0]) {
          *presult = out[0];
          return (i + 0);
        }
        if (key <= out[1]) {
          *presult = out[1];
          return (i + 1);
        }
        if (key <= out[2]) {
          *presult = out[2];
          return (i + 2);
        }
        *presult = out[3];
        return (i + 3);
      }
      i += 4;
    }

    while (endbyte > inbyte && nvalue > 0) {
      uint32_t *p = &out[0];
      nvalue = index->key_count() - 1 - i;
      inbyte = decodeSingleVarintDelta(inbyte, &initial, &p, &nvalue);
      assert(inbyte <= endbyte);
      if (key <= out[0]) {
        *presult = out[0];
        return (i + 0);
      }
      if (nvalue > 0 && key <= out[1]) {
        *presult = out[1];
        return (i + 1);
      }
      if (nvalue > 1 && key <= out[2]) {
        *presult = out[2];
        return (i + 2);
      }
      if (nvalue > 2 && key <= out[3]) {
        *presult = out[3];
        return (i + 3);
      }
      i += nvalue;
    }
    *presult = key + 1;
    return (i);
  }

  // Returns a decompressed value
  static uint32_t select(GroupVarintIndex *index, uint32_t *in, int slot) {
    const uint8_t *inbyte = reinterpret_cast<const uint8_t *> (in);
    uint32_t out[4];
    uint32_t initial = index->value();
    uint32_t nvalue = index->key_count() - 1;
    int i = 0;

    if (slot + 3 < (int)nvalue) {
      while (true) {
        inbyte = decodeGroupVarIntDelta(inbyte, &initial, out);
        i += 4;
        if (i > slot)
          return (out[slot - (i - 4)]);
      }
      ham_assert(false); // we should never get here
    } // else

    // we finish with the uncommon case
    while (i + 3 < slot) { // a single branch will do for this case (bulk of the computation)
      inbyte = decodeGroupVarIntDelta(inbyte, &initial, out);
      i += 4;
    }
    // lots of branching ahead...
    while (i + 3 < (int)nvalue) {
      inbyte = decodeGroupVarIntDelta(inbyte, &initial, out);
      i += 4;
      if (i > slot)
        return (out[slot - (i - 4)]);
    }
    {
      nvalue = nvalue - i;
      inbyte = decodeCarefully(inbyte, &initial, out, &nvalue);
      if (slot == i)
        return (out[0]);
      if (nvalue > 1 && slot == i + 1)
        return (out[1]);
      if (nvalue > 2 && slot == i + 2)
        return (out[2]);
      if (nvalue > 3 && slot == i + 3)
        return (out[3]);
    }
    ham_assert(false); // we should never get here
    throw Exception(HAM_INTERNAL_ERROR);
  }

  static size_t encodeArray(uint32_t initial, const uint32_t *in,
                  size_t length, uint32_t *out) {
    uint8_t *bout = reinterpret_cast<uint8_t *> (out);
    const uint8_t *const initbout = reinterpret_cast<uint8_t *> (out);

    size_t k = 0;
    for (; k + 3 < length;) {
      uint8_t * keyp = bout++;
      *keyp = 0;
      for (int j = 0; j < 8; j += 2, ++k) {
        const uint32_t val = in[k] - initial;
        initial = in[k];
        if (val < (1U << 8)) {
          *bout++ = static_cast<uint8_t> (val);
        } else if (val < (1U << 16)) {
          *bout++ = static_cast<uint8_t> (val);
          *bout++ = static_cast<uint8_t> (val >> 8);
          *keyp |= static_cast<uint8_t>(1 << j);
        } else if (val < (1U << 24)) {
          *bout++ = static_cast<uint8_t> (val);
          *bout++ = static_cast<uint8_t> (val >> 8);
          *bout++ = static_cast<uint8_t> (val >> 16);
          *keyp |= static_cast<uint8_t>(2 << j);
        } else {
          // the compiler will do the right thing
          *reinterpret_cast<uint32_t *> (bout) = val;
          bout += 4;
          *keyp |= static_cast<uint8_t>(3 << j);
        }
      }
    }
    if (k < length) {
      uint8_t * keyp = bout++;
      *keyp = 0;
      for (int j = 0; k < length && j < 8; j += 2, ++k) {
        const uint32_t val = in[k] - initial;
        initial = in[k];
        if (val < (1U << 8)) {
          *bout++ = static_cast<uint8_t> (val);
        } else if (val < (1U << 16)) {
          *bout++ = static_cast<uint8_t> (val);
          *bout++ = static_cast<uint8_t> (val >> 8);
          *keyp |= static_cast<uint8_t>(1 << j);
        } else if (val < (1U << 24)) {
          *bout++ = static_cast<uint8_t> (val);
          *bout++ = static_cast<uint8_t> (val >> 8);
          *bout++ = static_cast<uint8_t> (val >> 16);
          *keyp |= static_cast<uint8_t>(2 << j);
        } else {
          // the compiler will do the right thing
          *reinterpret_cast<uint32_t *> (bout) = val;
          bout += 4;
          *keyp |= static_cast<uint8_t>(3 << j);
        }
      }
    }

    return (bout - initbout);
  }

  static const uint8_t *decodeCarefully(const uint8_t *inbyte,
                    uint32_t *initial, uint32_t *out, uint32_t *count) {
    uint32_t val;
    uint32_t k, key = *inbyte++;
    for (k = 0; k < *count && k < 4; k++) {
      const uint32_t howmanybyte = key & 3;
      key = static_cast<uint8_t>(key>>2);
      val = static_cast<uint32_t> (*inbyte++);
      if (howmanybyte >= 1) {
        val |= (static_cast<uint32_t> (*inbyte++) << 8) ;
        if (howmanybyte >= 2) {
          val |= (static_cast<uint32_t> (*inbyte++) << 16) ;
          if (howmanybyte >= 3) {
            val |= (static_cast<uint32_t> (*inbyte++) << 24);
          }
        }
      }
      *initial += val;
      *out = *initial;
      out++;
    }
    *count = k;
    return (inbyte);
  }

  template <class T>
  static inline bool needPaddingTo32Bits(T value) {
    return value & 3;
  }

  static void decodeArray(uint32_t initial, const uint32_t *in,
                    size_t size, uint32_t *out, size_t nvalue) {
    const uint8_t * inbyte = reinterpret_cast<const uint8_t *> (in);
    const uint8_t * const endbyte = inbyte + size;
    const uint32_t * const endout(out + nvalue);

    while (endbyte > inbyte + 1 + 4 * 4) {
      inbyte = decodeGroupVarIntDelta(inbyte, &initial, out);
      out += 4;
    }
    while (endbyte > inbyte) {
      uint32_t nvalue = endout - out;
      inbyte = decodeSingleVarintDelta(inbyte, &initial, &out, &nvalue);
      assert(inbyte <= endbyte);
    }
  }

  template <class T>
  static T *padTo32bits(T *inbyte) {
    return reinterpret_cast<T *>((reinterpret_cast<uintptr_t>(inbyte)
                                      + 3) & ~3);
  }

  static const uint8_t *decodeGroupVarIntDelta(const uint8_t* in, uint32_t *val,
                  uint32_t* out) {
    const uint32_t sel = *in++;
    if (sel == 0) {
      out[0] = (* val += static_cast<uint32_t> (in[0]));
      out[1] = (* val += static_cast<uint32_t> (in[1]));
      out[2] = (* val += static_cast<uint32_t> (in[2]));
      out[3] = (* val += static_cast<uint32_t> (in[3]));
      return in + 4;
    }
    const uint32_t sel1 = (sel & 3);
    *val += *((uint32_t*)(in)) & varintgb_mask[sel1];
    *out++ = *val;
    in += sel1 + 1;
    const uint32_t sel2 = ((sel >> 2) & 3);
    *val += *((uint32_t*)(in)) & varintgb_mask[sel2];
    *out++ = *val;
    in += sel2 + 1;
    const uint32_t sel3 = ((sel >> 4) & 3);
    *val += *((uint32_t*)(in)) & varintgb_mask[sel3];
    *out++ = *val;
    in += sel3 + 1;
    const uint32_t sel4 = (sel >> 6);
    *val += *((uint32_t*)(in)) & varintgb_mask[sel4];
    *out++ = *val;
    in += sel4 + 1;
    return in;
  }

  static const uint8_t *decodeSingleVarintDelta(const uint8_t *inbyte,
                  uint32_t *initial, uint32_t **out, uint32_t *count) {
    uint32_t val;
    uint32_t k, key = *inbyte++;
    for (k = 0; k < *count && k < 4; k++) {
      const uint32_t howmanybyte = key & 3;
      key = static_cast<uint8_t>(key>>2);
      val = static_cast<uint32_t> (*inbyte++);
      if (howmanybyte >= 1) {
        val |= (static_cast<uint32_t> (*inbyte++) << 8) ;
        if (howmanybyte >= 2) {
          val |= (static_cast<uint32_t> (*inbyte++) << 16) ;
          if (howmanybyte >= 3) {
            val |= (static_cast<uint32_t> (*inbyte++) << 24);
          }
        }
      }
      *initial += val;
      **out = *initial;
      (*out)++;
    }
    *count = k;
    return (inbyte);
  }
};

typedef Zint32Codec<GroupVarintIndex, GroupVarintCodecImpl> GroupVarintCodec;

class GroupVarintKeyList : public BlockKeyList<GroupVarintCodec>
{
  public:
    // Constructor
    GroupVarintKeyList(LocalDatabase *db)
      : BlockKeyList<GroupVarintCodec>(db) {
    }
};

} // namespace Zint32

} // namespace hamsterdb

#endif /* HAM_BTREE_KEYS_GROUPVARINT_H */