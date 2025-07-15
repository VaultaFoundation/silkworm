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

#pragma once

#include <algorithm>
#include <array>
#include <initializer_list>
#include <iterator>
#include <map>
#include <utility>
#include <type_traits>

#include <silkworm/core/common/assert.hpp>

template <typename T, typename = void>
struct is_totally_ordered : std::false_type {};

template <typename T>
struct is_totally_ordered<T, std::void_t<
    // Check for comparison operators
    decltype(std::declval<T>() < std::declval<T>()),
    decltype(std::declval<T>() > std::declval<T>()),
    decltype(std::declval<T>() <= std::declval<T>()),
    decltype(std::declval<T>() >= std::declval<T>()),
    decltype(std::declval<T>() == std::declval<T>()),
    decltype(std::declval<T>() != std::declval<T>())
>> : std::true_type {};

template <typename T, typename = void>
struct is_equality_comparable : std::false_type {};

template <typename T>
struct is_equality_comparable<T, std::void_t<
    decltype(std::declval<T>() == std::declval<T>()),
    decltype(std::declval<T>() != std::declval<T>())
>> : std::true_type {};

namespace silkworm {
// Forward declaration of SmallMap to allow self-referential types
template <typename Key, typename T, size_t maximum_size>
class SmallMap;
}

// Specialize is_equality_comparable for SmallMap
template <typename Key, typename T, size_t maximum_size>
struct is_equality_comparable<silkworm::SmallMap<Key, T, maximum_size>> : is_equality_comparable<T> {};

namespace silkworm {

// SmallMap is a constexpr-friendly immutable map suitable for a small number of elements.
template <typename Key, typename T, size_t maximum_size = 8>
class SmallMap {
    static_assert(is_totally_ordered<Key>::value, "Key type must support total ordering (comparison operators).");
    static_assert(std::is_default_constructible<T>::value, "T type must be default constructible.");
  public:
    using ValueType = std::pair<Key, T>;

    constexpr SmallMap() noexcept = default;

    constexpr SmallMap(std::initializer_list<ValueType> init) : size_(init.size()) {
        SILKWORM_ASSERT(size_ <= maximum_size);
        for (size_t i{0}; i < size_; ++i) {
            data_[i] = *(std::data(init) + i);
        }
        sort();
    }

    template <typename InputIt, typename = std::enable_if_t<std::is_base_of_v<std::input_iterator_tag, typename std::iterator_traits<InputIt>::iterator_category>>>
    constexpr SmallMap(InputIt first, InputIt last) {
        for (InputIt it{first}; it != last; ++it) {
            SILKWORM_ASSERT(size_ < maximum_size);
            data_[size_++] = *it;
        }
        sort();
    }

    constexpr SmallMap(const SmallMap& other) : size_{other.size_} {
        for (size_t i{0}; i < maximum_size; ++i) {
            data_[i] = other.data_[i];
        }
    }
    constexpr SmallMap& operator=(const SmallMap& other) {
        if (this == &other) {
            return *this;
        }
        size_ = other.size_;
        for (size_t i{0}; i < maximum_size; ++i) {
            data_[i] = other.data_[i];
        }
        return *this;
    }

    constexpr bool empty() const noexcept {
        return size_ == 0;
    }

    constexpr size_t size() const noexcept {
        return size_;
    }

    static constexpr size_t max_size() noexcept {
        return maximum_size;
    }

    constexpr auto begin() const noexcept {
        return data_.begin();
    }

    constexpr auto end() const noexcept {
        return begin() + size_;
    }

    constexpr const T* find(const Key& key) const noexcept {
        // linear search is faster than binary for small sizes
        for (size_t i{0}; i < size_; ++i) {
            if (data_[i].first == key) {
                return &data_[i].second;
            }
        }
        return nullptr;
    }

    template <typename NewKeyType = Key, typename = std::enable_if_t<std::is_constructible_v<NewKeyType, Key>>>
    std::map<NewKeyType, T> to_std_map() const {
        std::map<NewKeyType, T> ret;
        for (const auto& [key, val] : *this) {
            ret[NewKeyType(key)] = val;
        }
        return ret;
    }

    template <typename... Args>
    constexpr void emplace_back(const Key& key, Args&&... args) {
        SILKWORM_ASSERT(size_ < maximum_size);
        data_[size_++] = ValueType{key, T{std::forward<Args>(args)...}};
        sort();
    }

  private:
    constexpr void sort() {
        std::sort(data_.begin(), data_.begin() + size_,
                  [](const ValueType& a, const ValueType& b) { return a.first < b.first; });
    }

    std::array<ValueType, maximum_size> data_{};
    size_t size_{0};
};

template <typename Key, typename T,
          typename = std::enable_if_t<is_totally_ordered<Key>::value>,
          typename = std::enable_if_t<is_equality_comparable<T>::value>>
constexpr bool operator==(const SmallMap<Key, T>& a, const SmallMap<Key, T>& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end());
}

}  // namespace silkworm
