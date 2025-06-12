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

#include "stage.hpp"

#include <magic_enum.hpp>

#include <silkworm/db/stages.hpp>
#include <silkworm/db/access_layer.hpp>
namespace silkworm::stagedsync {

using namespace silkworm::db::stages;

Stage::Stage(SyncContext* sync_context, const char* stage_name)
    : sync_context_{sync_context}, stage_name_{stage_name} {}

BlockNum Stage::get_progress(db::ROTxn& txn) {
    return read_stage_progress(txn, stage_name_);
}

BlockNum Stage::get_prune_progress(db::ROTxn& txn) {
    return read_stage_prune_progress(txn, stage_name_);
}

void Stage::set_prune_progress(db::RWTxn& txn, BlockNum progress) {
    write_stage_prune_progress(txn, stage_name_, progress);
}

void Stage::update_progress(db::RWTxn& txn, BlockNum progress) {
    write_stage_progress(txn, stage_name_, progress);
}

void Stage::check_block_sequence(BlockNum actual, BlockNum expected) {
    if (actual != expected) {
        const std::string what{"bad block sequence : expected " + std::to_string(expected) + " got " +
                               std::to_string(actual)};
        throw StageError(Stage::Result::kBadChainSequence, what);
    }
}

void Stage::throw_if_stopping() {
    if (is_stopping()) throw StageError(Stage::Result::kAborted);
}

const evmone::gas_parameters& Stage::get_gas_params(db::ROTxn& txn, const Block& block) {
    auto curr_consensus_parameter_index = block.get_consensus_parameter_index();
    if(curr_consensus_parameter_index != last_consensus_parameter_index) {
        auto consensus_params = silkworm::db::read_consensus_parameters(txn, block);
        if(consensus_params.has_value() && consensus_params->gas_fee_parameters.has_value()) {
            last_gas_params = evmone::gas_parameters(
                consensus_params->gas_fee_parameters->gas_txnewaccount,
                consensus_params->gas_fee_parameters->gas_newaccount,
                consensus_params->gas_fee_parameters->gas_txcreate,
                consensus_params->gas_fee_parameters->gas_codedeposit,
                consensus_params->gas_fee_parameters->gas_sset
            );
        } else {
            last_gas_params=evmone::gas_parameters{};
        }
        last_consensus_parameter_index = curr_consensus_parameter_index;
    }
    return last_gas_params;
}

evmone::eosevm::gas_prices Stage::get_gas_prices(const Block& block) {
    auto gas_price = block.get_gas_prices();
    if(!gas_price.has_value()) {
        return evmone::eosevm::gas_prices{};
    }
    return evmone::eosevm::gas_prices{gas_price->overhead_price, gas_price->storage_price};
}

StageError::StageError(Stage::Result err)
    : err_{magic_enum::enum_integer<Stage::Result>(err)},
      message_{std::string(magic_enum::enum_name<Stage::Result>(err))} {}

StageError::StageError(Stage::Result err, std::string message)
    : err_{magic_enum::enum_integer<Stage::Result>(err)}, message_{std::move(message)} {}

}  // namespace silkworm::stagedsync
