#include "consensus_parameters.hpp"

#if not defined(ANTELOPE)
#include <silkworm/core/common/assert.hpp>
#include <silkworm/core/common/endian.hpp>
#include <silkworm/core/common/util.hpp>
#endif

namespace eosevm {

#if not defined(ANTELOPE)
[[nodiscard]] silkworm::Bytes GasFeeParameters::encode() const noexcept  {
    silkworm::Bytes ret(40, '\0');
    silkworm::endian::store_big_u64(&ret[0], gas_txnewaccount);
    silkworm::endian::store_big_u64(&ret[8], gas_newaccount);
    silkworm::endian::store_big_u64(&ret[16], gas_txcreate);
    silkworm::endian::store_big_u64(&ret[24], gas_codedeposit);
    silkworm::endian::store_big_u64(&ret[32], gas_sset);

    return ret;
}

std::optional<GasFeeParameters> GasFeeParameters::decode(silkworm::ByteView encoded) noexcept {
    SILKWORM_ASSERT(encoded.length() >= 40);
    GasFeeParameters feeParams;
    feeParams.gas_txnewaccount = silkworm::endian::load_big_u64(&encoded[0]);
    feeParams.gas_newaccount = silkworm::endian::load_big_u64(&encoded[8]);
    feeParams.gas_txcreate = silkworm::endian::load_big_u64(&encoded[16]);
    feeParams.gas_codedeposit = silkworm::endian::load_big_u64(&encoded[24]);
    feeParams.gas_sset = silkworm::endian::load_big_u64(&encoded[32]);

    return feeParams;
}
#endif

#if not defined(ANTELOPE)
[[nodiscard]] silkworm::Bytes ConsensusParameters::encode() const noexcept  {
    SILKWORM_ASSERT(gas_fee_parameters.has_value());
    constexpr size_t size_before_fee_param = sizeof(uint64_t);
    auto value = gas_fee_parameters->encode();
    silkworm::Bytes ret(value.length() + size_before_fee_param, '\0');
    // Always store as latest supported version: currently 0.
    silkworm::endian::store_big_u64(&ret[0], 0);
    std::memcpy(&ret[size_before_fee_param], &value[0], value.length());
    return ret;
};

std::optional<ConsensusParameters> ConsensusParameters::decode(silkworm::ByteView encoded) noexcept {
    SILKWORM_ASSERT(encoded.length() > sizeof(uint64_t));
    ConsensusParameters config{};
    const auto version = silkworm::endian::load_big_u64(&encoded[0]);

    // Parse according to version. For now, only 0.
    switch (version) {
        case 0: {
            config.gas_fee_parameters = GasFeeParameters::decode(silkworm::ByteView{&encoded[sizeof(uint64_t)], encoded.length() - sizeof(uint64_t)});
            break;
        }
        default: {
            SILKWORM_ASSERT(false);
        }
    }

    return config;
}

[[nodiscard]] evmc::bytes32 ConsensusParameters::hash() const noexcept  {
    auto encoded = this->encode();
    evmc::bytes32 header_hash = std::bit_cast<evmc_bytes32>(silkworm::keccak256(encoded)); 
    return header_hash;
}
#endif

} // namespace eosevm
