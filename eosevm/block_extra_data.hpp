#pragma once

#include <optional>
#include <eosevm/gas_prices.hpp>
namespace eosevm {

struct block_extra_data {
    std::optional<evmc::bytes32> consensus_parameter_index;
    std::optional<gas_prices> gasprices;

    bool operator==(const block_extra_data& other) const noexcept {
        return consensus_parameter_index == other.consensus_parameter_index &&
            gasprices == other.gasprices;
    }
};

} // namespace eosevm

namespace silkworm { namespace rlp {
    silkworm::Bytes encode(const eosevm::block_extra_data&);
    DecodingResult decode(silkworm::ByteView& from, eosevm::block_extra_data& to) noexcept;
}}  // namespace silkworm::rlp
