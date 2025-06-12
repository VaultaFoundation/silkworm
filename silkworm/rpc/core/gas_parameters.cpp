#include "gas_parameters.hpp"
#include <silkworm/db/tables.hpp>
#include <silkworm/infra/concurrency/task.hpp>
#include <eosevm/consensus_parameters.hpp>
silkworm::Task<std::optional<eosevm::ConsensusParameters>> load_consensus_parameters(Transaction& tx, const std::optional<evmc::bytes32>& index) {
    if(!index.has_value()) co_return std::nullopt;
    auto data = co_await tx.get_one(silkworm::db::table::kConsensusParametersName, silkworm::ByteView{index->bytes});
    if (data.empty()) {
        co_return std::nullopt;
    }
    co_return eosevm::ConsensusParameters::decode(silkworm::ByteView{data});
}

silkworm::Task<load_gas_parameters_result> load_gas_parameters(Transaction& tx,
        const silkworm::ChainConfig& chain_config, const silkworm::Block& block) {

    auto eos_evm_version = chain_config.eos_evm_version(block.header);
    evmone::gas_parameters gas_params;
    evmone::eosevm::gas_prices gas_prices;
    if(eos_evm_version > 0) {
        auto block_index = block.get_consensus_parameter_index();
        auto consensus_params = co_await load_consensus_parameters(tx, block_index);
        if(consensus_params.has_value() && consensus_params->gas_fee_parameters.has_value()) {
            gas_params=evmone::gas_parameters(
              consensus_params->gas_fee_parameters->gas_txnewaccount,
              consensus_params->gas_fee_parameters->gas_newaccount,
              consensus_params->gas_fee_parameters->gas_txcreate,
              consensus_params->gas_fee_parameters->gas_codedeposit,
              consensus_params->gas_fee_parameters->gas_sset
            );
        }
        if(eos_evm_version >= 3) {
            auto gp = block.get_gas_prices();
            SILKWORM_ASSERT(gp.has_value());
            gas_prices.overhead_price = gp->overhead_price;
            gas_prices.storage_price = gp->storage_price;
        }
    }

    co_return std::make_tuple(eos_evm_version, gas_params, gas_prices);
}
