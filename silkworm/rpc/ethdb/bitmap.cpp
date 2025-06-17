/*
   Copyright 2023 The Silkworm Authors

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "bitmap.hpp"

#include <climits>
#include <memory>
#include <utility>
#include <vector>

#include <gsl/narrow>

#include <silkworm/core/types/evmc_bytes32.hpp>
#include <silkworm/infra/common/log.hpp>
#include <silkworm/rpc/ethdb/walk.hpp>
#include <silkworm/db/datastore/kvdb/bitmap.hpp>

namespace silkworm::rpc::ethdb::bitmap {

using roaring_bitmap_t = roaring::api::roaring_bitmap_t;
using Roaring64Map = roaring::Roaring64Map;
using rpc::ethdb::walk;

static Roaring64Map fast_or(size_t n, const std::vector<std::unique_ptr<Roaring64Map>>& inputs) {
    Roaring64Map result;
    for (size_t k = 0; k < n; ++k) {
        result |= *inputs[k];
    }
    return result;
}

Task<Roaring64Map> get(
    db::kv::api::Transaction& tx,
    const std::string& table,
    Bytes& key,
    uint32_t from_block,
    uint32_t to_block) {
    std::vector<std::unique_ptr<Roaring64Map>> chunks;

    Bytes from_key{key.begin(), key.end()};
    from_key.resize(key.size() + sizeof(uint16_t));
    endian::store_big_u16(&from_key[key.size()], 0);
    SILK_DEBUG << "table: " << table << " key: " << key << " from_key: " << from_key;

    auto walker = [&](const Bytes& k, const Bytes& v) {
        SILK_TRACE << "k: " << k << " v: " << v;
        auto chunk = std::make_unique<Roaring64Map>(silkworm::datastore::kvdb::bitmap::parse(v));
        SILK_TRACE << "chunk: " << chunk->toString();
        auto block = chunk->maximum();
        if (block >= from_block && chunk->minimum() <= to_block) {
            chunks.push_back(std::move(chunk));
        }
        return block < to_block;
    };
    co_await walk(tx, table, from_key, gsl::narrow<uint32_t>(key.size() * CHAR_BIT), walker);

    auto result{fast_or(chunks.size(), chunks)};
    SILK_DEBUG << "result: " << result.toString();
    co_return result;
}

Task<Roaring64Map> from_topics(
    db::kv::api::Transaction& tx,
    const std::string& table,
    const FilterTopics& topics,
    uint64_t start,
    uint64_t end) {
    SILK_DEBUG << "#topics: " << topics.size() << " start: " << start << " end: " << end;
    roaring::Roaring64Map result_bitmap;
    for (const auto& subtopics : topics) {
        SILK_DEBUG << "#subtopics: " << subtopics.size();
        roaring::Roaring64Map subtopic_bitmap;
        for (auto& topic : subtopics) {
            Bytes topic_key{std::begin(topic.bytes), std::end(topic.bytes)};
            SILK_TRACE << "topic: " << to_hex(topic) << " topic_key: " << to_hex(topic_key);
            auto bitmap = co_await ethdb::bitmap::get(tx, table, topic_key, gsl::narrow<uint32_t>(start), gsl::narrow<uint32_t>(end));
            SILK_TRACE << "bitmap: " << bitmap.toString();
            subtopic_bitmap |= bitmap;
            SILK_TRACE << "subtopic_bitmap: " << subtopic_bitmap.toString();
        }
        if (!subtopic_bitmap.isEmpty()) {
            if (result_bitmap.isEmpty()) {
                result_bitmap = subtopic_bitmap;
            } else {
                result_bitmap &= subtopic_bitmap;
            }
        }
        SILK_DEBUG << "result_bitmap: " << result_bitmap.toString();
    }
    co_return result_bitmap;
}

Task<Roaring64Map> from_addresses(
    db::kv::api::Transaction& tx,
    const std::string& table,
    const FilterAddresses& addresses,
    uint64_t start,
    uint64_t end) {
    SILK_TRACE << "#addresses: " << addresses.size() << " start: " << start << " end: " << end;
    roaring::Roaring64Map result_bitmap;
    for (auto& address : addresses) {
        Bytes address_key{std::begin(address.bytes), std::end(address.bytes)};
        auto bitmap = co_await ethdb::bitmap::get(tx, table, address_key, gsl::narrow<uint32_t>(start), gsl::narrow<uint32_t>(end));
        SILK_TRACE << "bitmap: " << bitmap.toString();
        result_bitmap |= bitmap;
    }
    SILK_TRACE << "result_bitmap: " << result_bitmap.toString();
    co_return result_bitmap;
}

}  // namespace silkworm::rpc::ethdb::bitmap
