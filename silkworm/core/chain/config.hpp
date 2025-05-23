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

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <evmc/evmc.h>
#include <intx/intx.hpp>
#include <nlohmann/json.hpp>

#include <silkworm/core/common/base.hpp>
#include <silkworm/core/common/small_map.hpp>
#include <silkworm/core/common/util.hpp>
#include <silkworm/core/protocol/bor/config.hpp>
#include <silkworm/core/protocol/ethash_config.hpp>
#include <silkworm/core/protocol/trust_config.hpp>

namespace silkworm {
    struct BlockHeader;
}
namespace silkworm {
namespace protocol {

    // Already merged at genesis
    struct NoPreMergeConfig {
        bool operator==(const NoPreMergeConfig&) const = default;
    };

    //! \see IRuleSet
    using PreMergeRuleSetConfig = std::variant<NoPreMergeConfig, EthashConfig, bor::Config, TrustConfig>;

}  // namespace protocol

using ChainId = uint64_t;

struct ChainConfig {
    //! \brief Returns the chain identifier
    //! \see https://eips.ethereum.org/EIPS/eip-155
    ChainId chain_id{0};

    //! \brief Holds the hash of genesis block
    std::optional<evmc::bytes32> genesis_hash;

    const std::optional<BlockNum>& homestead_block() const{
        SILKWORM_ASSERT(!is_trust());
        return _homestead_block;
    }

    const std::optional<BlockNum>& dao_block() const{
        SILKWORM_ASSERT(!is_trust());
        return _dao_block;
    }

    const std::optional<BlockNum>& tangerine_whistle_block() const{
        SILKWORM_ASSERT(!is_trust());
        return _tangerine_whistle_block;
    }

    const std::optional<BlockNum>& spurious_dragon_block() const{
        SILKWORM_ASSERT(!is_trust());
        return _spurious_dragon_block;
    }

    const std::optional<BlockNum>& byzantium_block() const{
        SILKWORM_ASSERT(!is_trust());
        return _byzantium_block;
    }

    const std::optional<BlockNum>& constantinople_block() const{
        SILKWORM_ASSERT(!is_trust());
        return _constantinople_block;
    }

    const std::optional<BlockNum>& petersburg_block() const{
        SILKWORM_ASSERT(!is_trust());
        return _petersburg_block;
    }

    const std::optional<BlockNum>& istanbul_block() const{
        SILKWORM_ASSERT(!is_trust());
        return _istanbul_block;
    }

    const std::optional<BlockNum>& muir_glacier_block() const{
        SILKWORM_ASSERT(!is_trust());
        return _muir_glacier_block;
    }

    const std::optional<BlockNum>& berlin_block() const{
        SILKWORM_ASSERT(!is_trust());
        return _berlin_block;
    }

    const std::optional<BlockNum>& london_block() const{
        SILKWORM_ASSERT(!is_trust());
        return _london_block;
    }

    const std::optional<BlockNum>& arrow_glacier_block() const{
        SILKWORM_ASSERT(!is_trust());
        return _arrow_glacier_block;
    }

    const std::optional<BlockNum>& gray_glacier_block() const{
        SILKWORM_ASSERT(!is_trust());
        return _gray_glacier_block;
    }

    const std::optional<intx::uint256>& terminal_total_difficulty() const{
        SILKWORM_ASSERT(!is_trust());
        return _terminal_total_difficulty;
    }

    const std::optional<BlockTime>& shanghai_time() const{
        SILKWORM_ASSERT(!is_trust());
        return _shanghai_time;
    }

    const std::optional<BlockTime>& cancun_time() const{
        SILKWORM_ASSERT(!is_trust());
        return _cancun_time;
    }

    const std::optional<BlockTime>& prague_time() const{
        SILKWORM_ASSERT(!is_trust());
        return _prague_time;
    }

    const std::optional<BlockTime>& merge_netsplit_block() const{
        SILKWORM_ASSERT(!is_trust());
        return _merge_netsplit_block;
    }

    // https://github.com/ethereum/execution-specs/tree/master/network-upgrades/mainnet-upgrades
    std::optional<BlockNum> _homestead_block{std::nullopt};
    std::optional<BlockNum> _dao_block{std::nullopt};
    std::optional<BlockNum> _tangerine_whistle_block{std::nullopt};
    std::optional<BlockNum> _spurious_dragon_block{std::nullopt};
    std::optional<BlockNum> _byzantium_block{std::nullopt};
    std::optional<BlockNum> _constantinople_block{std::nullopt};
    std::optional<BlockNum> _petersburg_block{std::nullopt};
    std::optional<BlockNum> _istanbul_block{std::nullopt};
    std::optional<BlockNum> _muir_glacier_block{std::nullopt};
    std::optional<BlockNum> _berlin_block{std::nullopt};
    std::optional<BlockNum> _london_block{std::nullopt};

    // (Optional) contract where EIP-1559 fees will be sent to that otherwise would be burnt since the London fork
    SmallMap<BlockNum, evmc::address> burnt_contract{};

    std::optional<BlockNum> _arrow_glacier_block{std::nullopt};
    std::optional<BlockNum> _gray_glacier_block{std::nullopt};

    //! \brief PoW to PoS switch
    //! \see EIP-3675: Upgrade consensus to Proof-of-Stake
    std::optional<intx::uint256> _terminal_total_difficulty{std::nullopt};
    std::optional<BlockNum> _merge_netsplit_block{std::nullopt};  // FORK_NEXT_VALUE in EIP-3675

    // Starting from Shanghai, forks are triggered by block time rather than number
    std::optional<BlockTime> _shanghai_time{std::nullopt};
    std::optional<BlockTime> _cancun_time{std::nullopt};
    std::optional<BlockTime> _prague_time{std::nullopt};

    // EOSEVM version
    std::optional<uint64_t> _version{std::nullopt};
    bool is_trust()const {
        return std::holds_alternative<protocol::TrustConfig>(rule_set_config);
    }

    //! \brief Returns the config of the (pre-Merge) protocol rule set
    protocol::PreMergeRuleSetConfig rule_set_config{protocol::NoPreMergeConfig{}};

    // The Shanghai hard fork has withdrawals, but Agra does not
    bool withdrawals_activated(BlockTime block_time) const noexcept;
    bool is_london(BlockNum block_num) const noexcept;

    //! \brief Returns the revision level at given block number
    //! \details In other words, on behalf of Json chain config data
    //! returns whether specific HF have occurred
    evmc_revision determine_revision_by_block(BlockNum block_num, uint64_t block_time) const noexcept;
    evmc_revision revision(const BlockHeader& header) const noexcept;
    uint64_t eos_evm_version(const BlockHeader& header) const noexcept;

    std::vector<BlockNum> distinct_fork_block_nums() const;
    std::vector<BlockTime> distinct_fork_times() const;
    std::vector<uint64_t> distinct_fork_points() const;

    //! \brief Check invariant on pre-Merge config validity
    bool valid_pre_merge_config() const noexcept;

    //! \brief Return the JSON representation of this object
    nlohmann::json to_json() const noexcept;

    /*Sample JSON input:
    {
            "chainId":1,
            "homesteadBlock":1150000,
            "daoForkBlock":1920000,
            "eip150Block":2463000,
            "eip155Block":2675000,
            "byzantiumBlock":4370000,
            "constantinopleBlock":7280000,
            "petersburgBlock":7280000,
            "istanbulBlock":9069000,
            "muirGlacierBlock":9200000,
            "berlinBlock":12244000
    }
    */
    //! \brief Try parse a JSON object into strongly typed ChainConfig
    //! \remark Should this return std::nullopt the parsing has failed
    static std::optional<ChainConfig> from_json(const nlohmann::json& json) noexcept;

    friend bool operator==(const ChainConfig&, const ChainConfig&) = default;
};

std::ostream& operator<<(std::ostream& out, const ChainConfig& obj);

inline constexpr ChainConfig get_kEOSEVMConfigTemplate(uint64_t _chain_id) {
    return ChainConfig{
        .chain_id = _chain_id,
        ._homestead_block = 0,
        ._dao_block = 0,
        ._tangerine_whistle_block = 0,
        ._spurious_dragon_block = 0,
        ._byzantium_block = 0,
        ._constantinople_block = 0,
        ._petersburg_block = 0,
        ._istanbul_block = 0,
        .rule_set_config = protocol::TrustConfig{},
    };
}

#if not defined(ANTELOPE)
inline constexpr ChainConfig kEOSEVMMainnetConfig = get_kEOSEVMConfigTemplate(17777);
#endif


using namespace evmc::literals;

inline constexpr evmc::bytes32 kMainnetGenesisHash{0xd4e56740f876aef8c010b86a40d5f56745a118d0906a34e69aec8c0db1cb8fa3_bytes32};
constinit extern const ChainConfig kMainnetConfig;

inline constexpr evmc::bytes32 kHoleskyGenesisHash{0xb5f7f912443c940f21fd611f12828d75b534364ed9e95ca4e307729a4661bde4_bytes32};
constinit extern const ChainConfig kHoleskyConfig;

inline constexpr evmc::bytes32 kSepoliaGenesisHash{0x25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9_bytes32};
constinit extern const ChainConfig kSepoliaConfig;

inline constexpr evmc::bytes32 kBorMainnetGenesisHash{0xa9c28ce2141b56c474f1dc504bee9b01eb1bd7d1a507580d5519d4437a97de1b_bytes32};
constinit extern const ChainConfig kBorMainnetConfig;

inline constexpr evmc::bytes32 kAmoyGenesisHash{0x7202b2b53c5a0836e773e319d18922cc756dd67432f9a1f65352b61f4406c697_bytes32};
constinit extern const ChainConfig kAmoyConfig;

//! \brief Known chain names mapped to their respective chain IDs
inline constexpr SmallMap<std::string_view, ChainId> kKnownChainNameToId{
    {"amoy"sv, 80002},
    {"bor-mainnet"sv, 137},
    {"holesky"sv, 17000},
    {"mainnet"sv, 1},
    {"sepolia"sv, 11155111},
};

//! \brief Known chain IDs mapped to their respective chain configs
inline constexpr SmallMap<ChainId, const ChainConfig*> kKnownChainConfigs{
    {*kKnownChainNameToId.find("mainnet"sv), &kMainnetConfig},
    {*kKnownChainNameToId.find("amoy"sv), &kAmoyConfig},
    {*kKnownChainNameToId.find("bor-mainnet"sv), &kBorMainnetConfig},
    {*kKnownChainNameToId.find("holesky"sv), &kHoleskyConfig},
    {*kKnownChainNameToId.find("sepolia"sv), &kSepoliaConfig},
};

}  // namespace silkworm
