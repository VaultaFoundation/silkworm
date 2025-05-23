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
#include <vector>
#include <tuple>

#include <evmone/test/state/block.hpp>
#include <evmone/gas_prices.hpp>
#include <evmone/execution_result.hpp>
#include <evmone/filtered_message.hpp>

#include <silkworm/core/execution/evm.hpp>
#include <silkworm/core/protocol/rule_set.hpp>
#include <silkworm/core/state/state.hpp>
#include <silkworm/core/types/block.hpp>
#include <silkworm/core/types/receipt.hpp>
#include <silkworm/core/types/transaction.hpp>

namespace silkworm {

class ExecutionProcessor {
  public:
    ExecutionProcessor(const ExecutionProcessor&) = delete;
    ExecutionProcessor& operator=(const ExecutionProcessor&) = delete;

    ExecutionProcessor(const Block& block, protocol::RuleSet& rule_set, State& state, const ChainConfig& config, bool evm1_v2, const evmone::gas_parameters& gas_params, const evmone::eosevm::gas_prices& gas_prices);

    /**
     * Execute a transaction, but do not write to the DB yet.
     * Precondition: transaction must be valid.
     */
    evmone::eosevm::execution_result execute_transaction(const Transaction& txn, Receipt& receipt) noexcept;

    CallResult call(const Transaction& txn, const std::vector<std::shared_ptr<EvmTracer>>& tracers, bool refund) noexcept;

    //! \brief Execute the block.
    //! \remarks Warning: This method does not verify state root; pre-Byzantium receipt root isn't validated either.
    //! \pre RuleSet's validate_block_header & pre_validate_block_body must return kOk.
    ValidationResult execute_block(std::vector<Receipt>& receipts) noexcept;

    //! \brief Flush IntraBlockState into cumulative State.
    void flush_state();

    EVM& evm() noexcept { return evm_; }
    const EVM& evm() const noexcept { return evm_; }
    IntraBlockState& intra_block_state() { return state_; }
    const IntraBlockState& intra_block_state() const { return state_; }

    void reset();

    // Added by ENF --v
    IntraBlockState& state() noexcept { return state_; }

    // ENF: moved from private so available by evm_contract::validate_transaction
    uint64_t available_gas() const noexcept;
    void set_evm_message_filter(std::optional<evmone::eosevm::filter_function> message_filter) {
      message_filter_ = message_filter;
      evm_.set_message_filter(message_filter);
    }
    // Added by ENF --^

  private:
    //! Update the transaction-context-wide access sets introduced by EIP-2929 and refined in EIP-3651
    void update_access_lists(const evmc::address& sender, const Transaction& txn, evmc_revision rev) noexcept;

    /**
     * Execute the block, but do not write to the DB yet.
     * Does not perform any post-execution validation (for example, receipt root is not checked).
     * Precondition: validate_block_header & pre_validate_block_body must return kOk.
     */
    ValidationResult execute_block_no_post_validation(std::vector<Receipt>& receipts) noexcept;

    //! \brief Notify the registered tracers at the start of block execution.
    void notify_block_execution_start(const Block& block);

    //! \brief Notify the registered tracers at the end of block execution.
    void notify_block_execution_end(const Block& block);

    std::tuple<intx::uint256, intx::uint256, uint64_t, uint64_t, uint64_t> process_refunded_execution(bool do_refund, 
      const evmc_revision rev, const uint64_t version,
      const evmc::Result& vm_res, const bool is_contract_creation,
      const uint64_t gas_limit, const evmone::gas_parameters& scaled_gas_params,
      const intx::uint256& effective_gas_price, const evmone::eosevm::gas_prices& gas_prices,
      const intx::uint256& inclusion_price, const intx::uint256& priority_fee_per_gas, const evmc::address& sender);

    uint64_t cumulative_gas_used_{0};
    IntraBlockState state_;
    protocol::RuleSet& rule_set_;
    EVM evm_;
    evmone::state::BlockInfo evm1_block_;
    std::optional<evmone::eosevm::filter_function> message_filter_;

    //! Execute transactions using evmone APIv2 only and apply the result state diff to the state.
    bool evm1_v2_ = false;
    const evmone::gas_parameters gas_params_;
    const evmone::eosevm::gas_prices gas_prices_;
};

}  // namespace silkworm
