/*
   Copyright 2022 The Silkworm Authors

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

#include "trie_cursor.hpp"

#include <bitset>

#include <silkworm/common/assert.hpp>
#include <silkworm/common/bits.hpp>
#include <silkworm/common/endian.hpp>
#include <silkworm/trie/nibbles.hpp>

namespace silkworm::trie {

Cursor::Cursor(mdbx::cursor& db_cursor, PrefixSet& changed, ByteView prefix)
    : db_cursor_{db_cursor}, changed_{changed}, prefix_{prefix} {
    subnodes_.reserve(64);
    consume_node(/*key=*/{}, /*exact=*/true);
}

void Cursor::consume_node(ByteView key, bool exact) {
    const Bytes db_key{prefix_ + Bytes{key}};
    const auto db_data{exact ? db_cursor_.find(db::to_slice(db_key), /*throw_notfound=*/false)
                             : db_cursor_.lower_bound(db::to_slice(db_key), /*throw_notfound=*/false)};

    if (!exact) {
        if (!db_data) {
            // end-of-tree
            subnodes_.clear();
            return;
        }
        key = db::from_slice(db_data.key);
        if (!has_prefix(key, prefix_)) {
            subnodes_.clear();
            return;
        }
        key.remove_prefix(prefix_.length());
    }

    std::optional<Node> node{std::nullopt};
    if (db_data) {
        node = Node::from_encoded_storage(db::from_slice(db_data.value));
        SILKWORM_ASSERT(node.has_value());
        SILKWORM_ASSERT(node->state_mask() != 0);
    }

    int nibble{0};
    if (!node.has_value() || node->root_hash().has_value()) {
        nibble = -1;
    } else {
        while ((node->state_mask() & (1u << nibble)) == 0) {
            ++nibble;
        }
    }

    if (!key.empty() && !subnodes_.empty()) {
        // the root might have nullopt node and thus no state bits, so we rely on the DB
        subnodes_[0].nibble = key[0];
    }

    subnodes_.push_back(SubNode{Bytes{key}, node, nibble});

    update_skip_state();

    // don't erase nodes with valid root hashes
    if (db_data && (!can_skip_state_ || nibble != -1)) {
        db_cursor_.erase();
    }
}

void Cursor::next() {
    if (subnodes_.empty()) {
        // end-of-tree
        return;
    }

    if (!can_skip_state_ && children_are_in_trie()) {
        // go to the child node
        SubNode& sn{subnodes_.back()};
        if (sn.nibble < 0) {
            move_to_next_sibling(/*allow_root_to_child_nibble_within_subnode=*/true);
        } else {
            consume_node(*key(), /*exact=*/false);
        }
    } else {
        move_to_next_sibling(/*allow_root_to_child_nibble_within_subnode=*/false);
    }

    update_skip_state();
}

void Cursor::update_skip_state() {
    const std::optional<Bytes> k{key()};
    if (!k.has_value() || changed_.contains(prefix_ + k.value())) {
        can_skip_state_ = false;
    } else {
        can_skip_state_ = subnodes_.back().hash_flag();
    }
}

void Cursor::move_to_next_sibling(bool allow_root_to_child_nibble_within_subnode) {
    if (subnodes_.empty()) {
        // end-of-tree
        return;
    }

    SubNode& sub_node{subnodes_.back()};

    if (sub_node.nibble >= 15 || (sub_node.nibble < 0 && !allow_root_to_child_nibble_within_subnode)) {
        // this node is fully traversed
        subnodes_.pop_back();
        move_to_next_sibling(false);  // on parent
        return;
    }

    ++sub_node.nibble;

    if (!sub_node.node.has_value()) {
        // we can't rely on the state flag, so search in the DB
        consume_node(*key(), /*exact=*/false);
        return;
    }

    while (sub_node.nibble < 16) {
        if (sub_node.state_flag()) {
            return;
        }
        sub_node.nibble++;
    }

    // this node is fully traversed
    subnodes_.pop_back();
    move_to_next_sibling(false);  // on parent
}

Bytes Cursor::SubNode::full_key() const {
    Bytes out{key};
    if (nibble >= 0) {
        out.push_back(nibble);
    }
    return out;
}

bool Cursor::SubNode::state_flag() const {
    if (nibble < 0 || !node.has_value()) {
        return true;
    }
    return node->state_mask() & (1u << nibble);
}

bool Cursor::SubNode::tree_flag() const {
    if (nibble < 0 || !node.has_value()) {
        return true;
    }
    return node->tree_mask() & (1u << nibble);
}

bool Cursor::SubNode::hash_flag() const {
    if (!node.has_value()) {
        return false;
    }

    if (nibble < 0) {
        return node->root_hash().has_value();
    }
    return node->hash_mask() & (1u << nibble);
}

const evmc::bytes32* Cursor::SubNode::hash() const {
    if (!hash_flag()) {
        return nullptr;
    }

    if (nibble < 0) {
        return &node->root_hash().value();
    }

    const unsigned first_nibbles_mask{(1u << nibble) - 1};
    const size_t hash_idx{std::bitset<16>(node->hash_mask() & first_nibbles_mask).count()};
    return &node->hashes()[hash_idx];
}

std::optional<Bytes> Cursor::key() const {
    if (subnodes_.empty()) {
        return std::nullopt;
    }
    return subnodes_.back().full_key();
}

const evmc::bytes32* Cursor::hash() const {
    if (subnodes_.empty()) {
        return nullptr;
    }
    return subnodes_.back().hash();
}

bool Cursor::children_are_in_trie() const {
    if (subnodes_.empty()) {
        return false;
    }
    return subnodes_.back().tree_flag();
}

std::optional<Bytes> Cursor::first_uncovered_prefix() const {
    std::optional<Bytes> k{key()};
    if (can_skip_state_ && k.has_value()) {
        k = increment_nibbled_key(*k);
    }
    if (k == std::nullopt) {
        return std::nullopt;
    }
    return from_nibbles(*k);
}

Bytes increment_nibbled_key(ByteView nibbles) {
    Bytes ret;
    if (!nibbles.empty()) {
        auto rit{std::find_if(nibbles.rbegin(), nibbles.rend(), [](uint8_t nibble) { return nibble < 0xf; })};
        if (rit != nibbles.rend()) {
            auto count{std::distance(nibbles.begin(), rit.base())};
            ret.assign(nibbles.substr(0, count));
            ++ret.back();
        }
    }
    return ret;
}

std::optional<Bytes> compute_next_uncovered_prefix(ByteView previous, ByteView prefix) {
    Bytes ret;
    if (!previous.empty()) {
        ret = increment_nibbled_key(previous);
    } else {
        ret.assign(prefix);
    }
    if (ret.size() & 1) {
        ret.append({'\0'});
    }
    return from_nibbles(ret);
}

AccCursor::AccCursor(mdbx::cursor& db_cursor, PrefixSet& changed, ByteView prefix, etl::Collector* collector)
    : db_cursor_{db_cursor}, changed_{changed}, collector_{collector}, sub_nodes_(64, SubNode{}) {
    prefix_.reserve(64);
    prev_.reserve(64);
    curr_.reserve(64);
    next_.reserve(64);
    buff_.reserve(64);

    prefix_.assign(prefix);
}

bool AccCursor::seek(ByteView prefix) {
    skip_state_ = true;
    auto [_, next_created]{changed_.contains_and_next_marked({})};
    next_created_ = next_created;
    prev_.assign(curr_);
    prefix_.assign(prefix);

    if (!seek_in_db()) {
        curr_.clear();
        skip_state_ = false;
        return false;
    }

    if (consume()) {
        return true;
    }
    return next();
}

bool AccCursor::move_next() {
    skip_state_ = true;
    prev_.assign(curr_);
    preorder_traversal_step_no_indepth();

    if (sub_nodes_[level_].key.empty()) {
        curr_.clear();
        skip_state_ = skip_state_ && increment_nibbled_key(prev_).empty();
        return false;
    }

    if (consume()) {
        return has_tree();
    }

    return next();
}

bool AccCursor::has_state() {
    auto& sub_node{sub_nodes_[level_]};
    return ((1 << sub_node.child_id) & sub_node.has_state) != 0;
}

bool AccCursor::has_tree() {
    auto& sub_node{sub_nodes_[level_]};
    return ((1 << sub_node.child_id) & sub_node.has_tree) != 0;
}

bool AccCursor::has_hash() {
    auto& sub_node{sub_nodes_[level_]};
    return ((1 << sub_node.child_id) & sub_node.has_hash) != 0;
}

bool AccCursor::next() {
    skip_state_ = skip_state_ && has_tree();
    preorder_traversal_step();
    while (true) {
        if (sub_nodes_[level_].key.empty()) {
            curr_.clear();
            skip_state_ = skip_state_ && increment_nibbled_key(prev_).empty();
            return false;
        }
        if (consume()) {
            return has_tree();
        }
        skip_state_ = skip_state_ && has_tree();
        preorder_traversal_step();
    }
}

void AccCursor::preorder_traversal_step() {
    if (has_tree()) {
        next_.assign(sub_nodes_[level_].key);
        next_.append({static_cast<uint8_t>(sub_nodes_[level_].child_id)});
        if (seek_in_db(next_)) {
            return;
        }
    }
    preorder_traversal_step_no_indepth();
}

void AccCursor::preorder_traversal_step_no_indepth() {
    if (next_sibling_in_mem() || next_sibling_of_parent_in_mem()) {
        return;
    }
    next_sibling_in_db();
}

void AccCursor::delete_current() {
    auto& sub_node{sub_nodes_[level_]};
    if (!sub_node.deleted && !sub_node.key.empty()) {
        if (collector_) {
            collector_->collect({Bytes{sub_node.key}, Bytes{}});
        }
        sub_node.deleted = true;
    }
}
void AccCursor::parse_subnode(ByteView key, ByteView value) {
    // At least state/tree/hash masks need to be present
    if (value.length() < 6) {
        throw std::invalid_argument("Wrong node raw length: expected >= 6 got " + std::to_string(value.length()));
    }
    // Beyond the 6th byte the length must be a multiple of kHashLength
    if ((value.length() - 6) % kHashLength != 0) {
        throw std::invalid_argument("Wrong node raw hashes length: not a multiple of " + std::to_string(kHashLength));
    }

    size_t from{level_ + 1};
    size_t to{key.length()};
    if (level_ >= key.length()) {
        from = key.length() + 1;
        to = level_ + 2;
    }
    for (size_t i{from}; i < to; ++i) {
        auto& sub_node{sub_nodes_.at(i)};
        sub_node.key = ByteView();
        sub_node.value = ByteView();
        sub_node.has_state = 0;
        sub_node.has_tree = 0;
        sub_node.has_hash = 0;
        sub_node.hash_id = 0;
        sub_node.child_id = 0;
        sub_node.deleted = false;
    }

    level_ = key.length();
    auto& sub_node{sub_nodes_[level_]};
    sub_node.key = key;
    sub_node.value = value;
    sub_node.deleted = false;
    sub_node.has_state = endian::load_big_u16(&value.data()[0]);
    sub_node.has_tree = endian::load_big_u16(&value.data()[2]);
    sub_node.has_hash = endian::load_big_u16(&value.data()[4]);
    sub_node.hash_id = -1;
    sub_node.child_id = static_cast<int8_t>(ctz_16(sub_node.has_state) - 1);
}

void AccCursor::next_sibling_in_db() {
    auto& sub_node{sub_nodes_[level_]};
    auto incremented_key{increment_nibbled_key(sub_node.key)};
    if (incremented_key.empty()) {
        sub_node.key = ByteView();
        return;
    }
    next_.assign(incremented_key);
    (void)seek_in_db();
}

bool AccCursor::next_sibling_in_mem() {
    auto& sub_node{sub_nodes_[level_]};
    while (sub_node.child_id < static_cast<int8_t>(bitlen_16(sub_node.has_state))) {
        ++sub_node.child_id;
        if (has_hash()) {
            ++sub_node.hash_id;
            return true;
        }
        if (has_tree()) {
            return true;
        }
        if (has_state()) {
            skip_state_ = false;
        }
    }
    return false;
}

bool AccCursor::next_sibling_of_parent_in_mem() {
    while (level_ > 1) {
        if (sub_nodes_[level_].key.empty()) {
            size_t up_level{level_ - 1};
            while (sub_nodes_[up_level].key.empty() && up_level > 1) {
                --up_level;
            }
            next_.assign(sub_nodes_[level_].key);
            next_.append({static_cast<uint8_t>(sub_nodes_[level_].child_id)});
            buff_.assign(sub_nodes_[up_level].key);
            buff_.append({static_cast<uint8_t>(sub_nodes_[up_level].child_id)});
            if (seek_in_db(buff_)) {
                return true;
            }
            level_ = up_level + 1;
            continue;
        }
        --level_;
        if (next_sibling_in_mem()) {
            return true;
        }
    }
    return false;
}

bool AccCursor::seek_in_db(ByteView within_prefix) {
    auto& sub_node{sub_nodes_[level_]};
    const auto data{next_.empty() ? db_cursor_.to_first(false) : db_cursor_.lower_bound(db::to_slice(next_), false)};
    if (!within_prefix.empty()) {
        if (!data || !has_prefix(db::from_slice(data.key), within_prefix)) {
            return false;
        }
    } else {
        if (!data || !has_prefix(db::from_slice(data.key), prefix_)) {
            sub_node.key = ByteView();
            sub_node.value = ByteView();
            return false;
        }
    }
    parse_subnode(db::from_slice(data.key), db::from_slice(data.value));
    next_sibling_in_mem();
    return true;
}

bool AccCursor::consume() {
    if (has_hash()) {
        auto& sub_node{sub_nodes_[level_]};
        buff_.assign(sub_node.key);
        buff_.append({static_cast<uint8_t>(sub_node.child_id)});
        auto [contains, next_created]{changed_.contains_and_next_marked(buff_)};
        if (!contains) {
            skip_state_ = skip_state_ && key_is_before(buff_, next_created);
            next_created_ = next_created;
            curr_.assign(buff_);
            return true;
        }
    }
    delete_current();
    return false;
}

bool AccCursor::key_is_before(ByteView k1, ByteView k2) {
    if (k1.empty()) {
        return false;
    }
    if (k2.empty()) {
        return true;
    }
    return k1 < k2;
}

}  // namespace silkworm::trie
