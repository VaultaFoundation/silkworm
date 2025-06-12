#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <intx/intx.hpp>
#include <evmc/evmc.hpp>

#if not defined(ANTELOPE)
#include <silkworm/core/common/base.hpp>
#include <silkworm/core/common/bytes.hpp>
#endif


namespace eosevm {

// Note: GasFeeParameters struct is NOT versioned, version will be handled by ConsensusParameters.
// If we want to change this struct, create GasFeeParametersV2 and let ConsensusParameters use it. 
struct GasFeeParameters {
    // gas_txnewaccount = account_bytes * gas_per_byte
    uint64_t gas_txnewaccount;
    // gas_newaccount = account_bytes * gas_per_byte
    uint64_t gas_newaccount;
    // gas_txcreate = gas_create = contract_fixed_bytes * gas_per_byte
    uint64_t gas_txcreate;
    // gas_codedeposit = gas_per_byte
    uint64_t gas_codedeposit;
    // gas_sset = 100 + storage_slot_bytes * gas_per_byte 
    uint64_t gas_sset;

    #if not defined(ANTELOPE)
    // Encode for storage in db.
    [[nodiscard]] silkworm::Bytes encode() const noexcept;

    // Decode from storage in db.
    static std::optional<GasFeeParameters> decode(silkworm::ByteView encoded) noexcept;
    #endif

    friend bool operator==(const GasFeeParameters&, const GasFeeParameters&) = default;
};

struct ConsensusParameters {
    std::optional<GasFeeParameters> gas_fee_parameters;

    #if not defined(ANTELOPE)
    // Encode for storage in db.
    [[nodiscard]] silkworm::Bytes encode() const noexcept;

    // Decode from storage in db.
    static std::optional<ConsensusParameters> decode(silkworm::ByteView encoded) noexcept;
    evmc::bytes32 hash() const noexcept;
    #endif

    friend bool operator==(const ConsensusParameters&, const ConsensusParameters&) = default;
};

} // namespace eosevm
