#pragma once

#include <cstdint>
#include <tuple>

#include <evmone/execution_state.hpp>
#include <evmone/gas_prices.hpp>
#include <silkworm/core/chain/config.hpp>
#include <silkworm/db/kv/api/transaction.hpp>
#include <eosevm/consensus_parameters.hpp>

using namespace silkworm::db::kv::api;

using load_gas_parameters_result = std::tuple<uint64_t, evmone::gas_parameters, evmone::eosevm::gas_prices>;
silkworm::Task<load_gas_parameters_result> load_gas_parameters( Transaction& tx, const silkworm::ChainConfig& chain_config, const silkworm::Block& block);

silkworm::Task<std::optional<eosevm::ConsensusParameters>> load_consensus_parameters(Transaction& tx, const std::optional<evmc::bytes32>& index);