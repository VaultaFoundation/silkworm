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

#include "processor.hpp"

#include <evmone/test/state/state.hpp>

#include <silkworm/core/common/assert.hpp>
#include <silkworm/core/protocol/intrinsic_gas.hpp>
#include <silkworm/core/protocol/param.hpp>
#include <silkworm/core/trie/vector_root.hpp>
#include <evmone/refund.hpp>

namespace silkworm {

evmc::Result to_result(const CallResult& r) {
    return evmc::Result(r.status, static_cast<int64_t>(r.gas_left), static_cast<int64_t>(r.gas_refund),
        0, static_cast<int64_t>(r.storage_gas_consumed), static_cast<int64_t>(r.storage_gas_refund),
        static_cast<int64_t>(r.speculative_cpu_gas_consumed), r.data.data(), r.data.size());
}
class StateView final : public evmone::state::StateView {
    IntraBlockState& state_;

  public:
    explicit StateView(IntraBlockState& state) noexcept : state_{state} {}

    std::optional<Account> get_account(const evmc::address& addr) const noexcept override {
        const auto* obj = state_.get_object(addr);
        if (obj == nullptr || !obj->current.has_value())
            return std::nullopt;

        const auto& cur = *obj->current;
        return Account{
            .nonce = cur.nonce,
            .balance = cur.balance,
            .code_hash = cur.code_hash,

            // This information is only needed to implement EIP-7610 (create address collision).
            // Proper way of doing so is to inspect the account's storage root hash,
            // but this information is currently unavailable to EVM.
            // The false value is safe "do nothing" option.
            .has_storage = false,
        };
    }

    evmone::bytes get_account_code(const evmc::address& addr) const noexcept override {
        return evmone::bytes{state_.get_code(addr)};
    }

    evmc::bytes32 get_storage(const evmc::address& addr, const evmc::bytes32& key) const noexcept override {
        return state_.get_original_storage(addr, key);
    }
};

namespace {
    class BlockHashes final : public evmone::state::BlockHashes {
        EVM& evm_;

      public:
        explicit BlockHashes(EVM& evm) noexcept : evm_{evm} {}
        evmc::bytes32 get_block_hash(int64_t block_number) const noexcept override {
            return evm_.get_block_hash(block_number);
        }
    };

    /// Checks the result of the transaction execution in evmone (APIv2)
    /// against the result produced by Silkworm.
    void check_evm1_execution_result(const evmone::state::TransactionReceipt& receipt, const IntraBlockState& state) {
        const evmone::state::StateDiff& state_diff = receipt.state_diff;
        SILKWORM_ASSERT(state.filtered_messages() == receipt.filtered_messages);

        for (const auto& entry : state_diff.modified_accounts) {
            if (std::ranges::find(state_diff.deleted_accounts, entry.addr) != state_diff.deleted_accounts.end()) {
                continue;
            }

            for (const auto& [k, v] : entry.modified_storage) {
                auto expected = state.get_current_storage(entry.addr, k);
                if (v != expected) {
                    std::cerr << "k: " << hex(k) << "e1: " << hex(v) << ", silkworm: " << hex(expected) << "\n";
                }
            } 
        }
        for (const auto& a : state_diff.deleted_accounts) {
            SILKWORM_ASSERT(!state.exists(a));
        }
        for (const auto& m : state_diff.modified_accounts) {
            if (std::ranges::find(state_diff.deleted_accounts, m.addr) != state_diff.deleted_accounts.end()) {
                continue;
            }

            SILKWORM_ASSERT(state.get_nonce(m.addr) == m.nonce);
            if (m.balance != state.get_balance(m.addr)) {
                std::cerr << "b: " << hex(m.addr) << " " << to_string(m.balance) << ", silkworm: " << to_string(state.get_balance(m.addr)) << "\n";
                SILKWORM_ASSERT(state.get_balance(m.addr) == m.balance);
            }
            if (!m.code.empty()) {
                SILKWORM_ASSERT(state.get_code(m.addr) == m.code);
            }
        }
    }
}  // namespace

ExecutionProcessor::ExecutionProcessor(const Block& block, protocol::RuleSet& rule_set, State& state,
                                       const ChainConfig& config, bool evm1_v2, const evmone::gas_parameters& gas_params, const evmone::eosevm::gas_prices& gas_prices)
    : state_{state}, rule_set_{rule_set}, evm_{block, state_, config}, evm1_v2_{evm1_v2}, gas_params_{gas_params}, gas_prices_{gas_prices} {
    evm_.beneficiary = rule_set.get_beneficiary(block.header);
    evm_.transfer = rule_set.transfer_func();

    evm1_block_ = {
        .number = static_cast<int64_t>(block.header.number),
        .timestamp = static_cast<int64_t>(block.header.timestamp),
        .gas_limit = static_cast<int64_t>(block.header.gas_limit),
        .coinbase = block.header.beneficiary,
        .difficulty = static_cast<int64_t>(block.header.difficulty),
        .prev_randao = block.header.difficulty == 0 ? block.header.prev_randao : intx::be::store<evmone::state::bytes32>(intx::uint256{block.header.difficulty}),
        .base_fee = static_cast<uint64_t>(block.header.base_fee_per_gas.value_or(0)),
        .excess_blob_gas = block.header.excess_blob_gas.value_or(0),
        .blob_base_fee = block.header.blob_gas_price().value_or(0),
    };
    for (const auto& o : block.ommers)
        evm1_block_.ommers.emplace_back(evmone::state::Ommer{o.beneficiary, static_cast<uint32_t>(block.header.number - o.number)});
    if (block.withdrawals) {
        for (const auto& w : *block.withdrawals)
            evm1_block_.withdrawals.emplace_back(evmone::state::Withdrawal{w.index, w.validator_index, w.address, w.amount});
    }
}

evmone::eosevm::execution_result ExecutionProcessor::execute_transaction(const Transaction& txn, Receipt& receipt) noexcept {
    // Plain debug assertion instead of SILKWORM_ASSERT not to validate txn twice (see execute_block_no_post_validation)
    assert(protocol::validate_transaction(txn, state_, available_gas()) == ValidationResult::kOk);

    StateView evm1_state_view{state_};
    BlockHashes evm1_block_hashes{evm_};

    evmone::state::Transaction evm1_txn{
        .type = static_cast<evmone::state::Transaction::Type>(txn.type),
        .data = txn.data,
        .gas_limit = static_cast<int64_t>(txn.gas_limit),
        .max_gas_price = txn.max_fee_per_gas,
        .max_priority_gas_price = txn.max_priority_fee_per_gas,
        .max_blob_gas_price = txn.max_fee_per_blob_gas,
        .sender = *txn.sender(),
        .to = txn.to,
        .value = txn.value,
        // access_list
        // blob_hashes
        // TODO: This should be corrected in the evmone APIv2,
        //   because it uses transaction's chain id for CHAINID instruction.
        .chain_id = evm().config().chain_id,
        .nonce = txn.nonce};
    for (const auto& [account, storage_keys] : txn.access_list)
        evm1_txn.access_list.emplace_back(account, storage_keys);
    for (const evmc::bytes32& h : txn.blob_versioned_hashes)
        evm1_txn.blob_hashes.emplace_back(h);

    const BlockHeader& header{evm_.block().header};
    const intx::uint256 base_fee_per_gas{header.base_fee_per_gas.value_or(0)};
    const intx::uint256 effective_gas_price{txn.effective_gas_price(base_fee_per_gas)};

    const auto eos_evm_version = evm_.get_eos_evm_version();
    intx::uint256 inclusion_price;
    evmone::gas_parameters scaled_gas_params;
    if( eos_evm_version >= 3 ) {
        inclusion_price = std::min(txn.max_priority_fee_per_gas, txn.max_fee_per_gas - base_fee_per_gas);
        const intx::uint256 factor_num{gas_prices_.storage_price};
        const intx::uint256 factor_den{base_fee_per_gas + inclusion_price};
        SILKWORM_ASSERT(factor_den > 0);
        scaled_gas_params = evmone::gas_parameters::apply_discount_factor(factor_num, factor_den, gas_params_);
    } else {
        scaled_gas_params = gas_params_;
    }

    const auto rev = evm_.revision();
    const auto g0 = protocol::intrinsic_gas(txn, rev, eos_evm_version, scaled_gas_params);
    SILKWORM_ASSERT(g0 <= INT64_MAX);  // true due to the precondition (transaction must be valid)
    const auto execution_gas_limit = txn.gas_limit - static_cast<uint64_t>(g0);

    // Execute transaction with evmone APIv2.
    // This must be done before the Silkworm execution so that the state is unmodified.
    // evmone will not modify the state itself: state is read-only and the state modifications
    // are provided as the state diff in the returned receipt.
    auto evm1_receipt = evmone::state::transition(
        evm1_state_view, evm1_block_, evm1_block_hashes, evm1_txn, rev, evm_.vm(), {.execution_gas_limit = static_cast<int64_t>(execution_gas_limit)},
        eos_evm_version, scaled_gas_params, gas_prices_, evm().config().is_trust(), message_filter_);

    const auto gas_used = static_cast<uint64_t>(evm1_receipt.gas_used);
    cumulative_gas_used_ += gas_used;

    // Prepare the receipt using the result from evmone.
    receipt.type = txn.type;
    receipt.success = evm1_receipt.status == EVMC_SUCCESS;
    receipt.cumulative_gas_used = cumulative_gas_used_;
    receipt.logs.clear();  // can be dirty
    receipt.logs.reserve(evm1_receipt.logs.size());
    for (auto& [addr, data, topics] : evm1_receipt.logs)
        receipt.logs.emplace_back(Log{addr, std::move(topics), std::move(data)});
    receipt.bloom = logs_bloom(receipt.logs);

    if (evm1_v2_) {
        // Apply the state diff produced by evmone APIv2 to the state and skip the Silkworm execution.
        const auto& state_diff = evm1_receipt.state_diff;
        for (const auto& m : state_diff.modified_accounts) {
            if (!m.code.empty()) {
                state_.create_contract(m.addr);
                state_.set_code(m.addr, m.code);
            }

            auto& acc = state_.get_or_create_object(m.addr);
            acc.current->nonce = m.nonce;
            acc.current->balance = m.balance;

            auto& storage = state_.storage_[m.addr];
            for (const auto& [k, v] : m.modified_storage) {
                storage.committed[k].original = v;
            }
        }

        for (const auto& a : state_diff.deleted_accounts) {
            state_.destruct(a);
        }

        state_.set_filtered_messages(evm1_receipt.filtered_messages);

        return evm1_receipt.exec_res;
    }

    state_.clear_journal_and_substate();

    const std::optional<evmc::address> sender{txn.sender()};
    SILKWORM_ASSERT(sender);

    update_access_lists(*sender, txn, rev);

    if (txn.to) {
        // EVM itself increments the nonce for contract creation
        state_.set_nonce(*sender, txn.nonce + 1);
    }

    const intx::uint256 sender_initial_balance{state_.get_balance(*sender)};
    const intx::uint256 recipient_initial_balance{state_.get_balance(evm_.beneficiary)};

    // EIP-1559 normal gas cost
    state_.subtract_from_balance(*sender, txn.gas_limit * effective_gas_price);

    // EIP-4844 blob gas cost (calc_data_fee)
    const intx::uint256 blob_gas_price{header.blob_gas_price().value_or(0)};
    state_.subtract_from_balance(*sender, txn.total_blob_gas() * blob_gas_price);

    const CallResult vm_res = evm_.execute(txn, execution_gas_limit, scaled_gas_params);
    SILKWORM_ASSERT((vm_res.status == EVMC_SUCCESS) == receipt.success);
    SILKWORM_ASSERT(state_.logs().size() == receipt.logs.size());

    // Process refund and credit beneficiary
    auto [beneficiary_amount, sender_amount, final_gas_used, final_gas_left, final_gas_refund] = process_refunded_execution(true, rev, eos_evm_version, to_result(vm_res), !txn.to.has_value(),
        txn.gas_limit, scaled_gas_params, effective_gas_price, gas_prices_, inclusion_price, txn.priority_fee_per_gas(base_fee_per_gas), *sender);

    if (rev >= EVMC_LONDON) {
        const evmc::address* burnt_contract{protocol::bor::config_value_lookup(evm_.config().burnt_contract,
                                                                               header.number)};
        if (burnt_contract) {
            const intx::uint256 would_be_burnt{gas_used * base_fee_per_gas};
            state_.add_to_balance(*burnt_contract, would_be_burnt);
        }
    }
    rule_set_.add_fee_transfer_log(state_, sender_amount, *sender, sender_initial_balance,
                                    evm_.beneficiary, recipient_initial_balance);

    state_.finalize_transaction(rev);
    check_evm1_execution_result(evm1_receipt, state_);
    return evm1_receipt.exec_res;
}

CallResult ExecutionProcessor::call(const Transaction& txn, const std::vector<std::shared_ptr<EvmTracer>>& tracers, bool refund) noexcept {
    const std::optional<evmc::address> sender{txn.sender()};
    const auto eos_evm_version = evm_.get_eos_evm_version();
    SILKWORM_ASSERT(sender);

    const BlockHeader& header{evm_.block().header};
    const intx::uint256 base_fee_per_gas{header.base_fee_per_gas.value_or(0)};

    const intx::uint256 effective_gas_price{txn.max_fee_per_gas >= base_fee_per_gas ? txn.effective_gas_price(base_fee_per_gas)
                                                                                    : txn.max_priority_fee_per_gas};

    intx::uint256 inclusion_price;
    evmone::gas_parameters scaled_gas_params;
    if( eos_evm_version >= 3 ) {
        inclusion_price = std::min(txn.max_priority_fee_per_gas, txn.max_fee_per_gas >= base_fee_per_gas ? txn.max_fee_per_gas - base_fee_per_gas : 0);
        const intx::uint256 factor_num{gas_prices_.storage_price};
        const intx::uint256 factor_den{base_fee_per_gas + inclusion_price};
        SILKWORM_ASSERT(factor_den > 0);
        scaled_gas_params = evmone::gas_parameters::apply_discount_factor(factor_num, factor_den, gas_params_);
    } else {
        scaled_gas_params = gas_params_;
    }

    ValidationResult validation_result = protocol::validate_call_precheck(txn, evm_, eos_evm_version, scaled_gas_params);
    if (validation_result != ValidationResult::kOk) {
        return {validation_result, EVMC_SUCCESS, 0, {}, {}};
    }

    for (auto& tracer : tracers) {
        evm_.add_tracer(*tracer);
    }

    const evmc_revision rev{evm_.revision()};
    update_access_lists(*sender, txn, rev);

    if (txn.to) {
        state_.set_nonce(*sender, state_.get_nonce(*txn.sender()) + 1);
    }

    intx::uint256 required_funds = protocol::compute_call_cost(txn, effective_gas_price, evm_);
    if (evm().bailout) {
        // If the bailout option is on, add the required funds to the sender's balance
        // so that after the transaction costs are deducted, the sender's balance is unchanged.
        state_.add_to_balance(*txn.sender(), required_funds);
    }

    validation_result = protocol::validate_call_funds(txn, evm_, state_.get_balance(*txn.sender()), evm().bailout);
    if (validation_result != ValidationResult::kOk) {
        return {validation_result, EVMC_SUCCESS, 0, {}, {}};
    }
    state_.subtract_from_balance(*txn.sender(), required_funds);
    const intx::uint128 g0{protocol::intrinsic_gas(txn, evm_.revision(), eos_evm_version, scaled_gas_params)};
    const auto result = evm_.execute(txn, txn.gas_limit - static_cast<uint64_t>(g0), scaled_gas_params);

    // Process refund and credit beneficiary
    bool do_refund = refund && !evm().bailout;
    auto [beneficiary_amount, sender_amount, final_gas_used, final_gas_left, final_gas_refund] = process_refunded_execution(do_refund, rev, eos_evm_version, to_result(result), !txn.to.has_value(),
        txn.gas_limit, scaled_gas_params, effective_gas_price, gas_prices_, inclusion_price, txn.priority_fee_per_gas(base_fee_per_gas), *sender);

    for (auto& tracer : evm_.tracers()) {
        tracer.get().on_reward_granted(result, state_);
    }
    state_.finalize_transaction(evm_.revision());

    evm_.remove_tracers();
    return {ValidationResult::kOk, result.status, final_gas_left, final_gas_refund, result.storage_gas_consumed, result.storage_gas_refund, result.speculative_cpu_gas_consumed, result.data, result.error_message};
}

std::tuple<intx::uint256, intx::uint256, uint64_t, uint64_t, uint64_t> ExecutionProcessor::process_refunded_execution(bool do_refund, 
    const evmc_revision rev, const uint64_t version,
    const evmc::Result& result, const bool is_contract_creation,
    const uint64_t gas_limit, const evmone::gas_parameters& scaled_gas_params,
    const intx::uint256& effective_gas_price, const evmone::eosevm::gas_prices& gas_prices,
    const intx::uint256& inclusion_price, const intx::uint256& priority_fee_per_gas,
    const evmc::address& sender) {

    uint64_t gas_left = static_cast<uint64_t>(result.gas_left);
    uint64_t gas_used = gas_limit - gas_left;
    uint64_t gas_refund{0};

    intx::uint256 beneficiary_amount{0};
    intx::uint256 sender_amount{0};

    const intx::uint256 beneficiary_price{evm().config().is_trust() ? effective_gas_price : priority_fee_per_gas};

    if (do_refund) {
        auto res = evmone::eosevm::refund(rev, version, result, is_contract_creation,
            gas_limit, scaled_gas_params, effective_gas_price, gas_prices, inclusion_price);

        gas_used = std::visit([](const auto& v){ return v.gas_used; }, res);
        gas_left = std::visit([](const auto& v){ return v.gas_left; }, res);
        gas_refund = std::visit([](const auto& v){ return v.gas_refund; }, res);

        beneficiary_amount = version >= 3 ? std::get<evmone::eosevm::refund_result_v3>(res).final_fee : gas_used * beneficiary_price;
        sender_amount = gas_left * effective_gas_price;
    } else {
        beneficiary_amount = gas_used * beneficiary_price;
    }
    state_.add_to_balance(evm_.beneficiary, beneficiary_amount);
    state_.add_to_balance(sender, sender_amount);
    return {beneficiary_amount, sender_amount, gas_used, gas_left, gas_refund};
}

void ExecutionProcessor::reset() {
    state_.clear_journal_and_substate();
}

uint64_t ExecutionProcessor::available_gas() const noexcept {
    return evm_.block().header.gas_limit - cumulative_gas_used_;
}

void ExecutionProcessor::update_access_lists(const evmc::address& sender, const Transaction& txn, evmc_revision rev) noexcept {
    state_.access_account(sender);

    if (txn.to) {
        state_.access_account(*txn.to);
    }

    for (const AccessListEntry& ae : txn.access_list) {
        state_.access_account(ae.account);
        for (const evmc::bytes32& key : ae.storage_keys) {
            state_.access_storage(ae.account, key);
        }
    }

    if (rev >= EVMC_SHANGHAI) {
        // EIP-3651: Warm COINBASE
        state_.access_account(evm_.beneficiary);
    }
}

// uint64_t ExecutionProcessor::refund_gas(const Transaction& txn, const intx::uint256& effective_gas_price, uint64_t gas_left, uint64_t gas_refund) noexcept {
//     const evmc_revision rev{evm_.revision()};
//     const auto version = evm_.get_eos_evm_version();
//     assert(version < 3);
//     if( version < 2 ) {
//         const uint64_t max_refund_quotient{rev >= EVMC_LONDON ? protocol::kMaxRefundQuotientLondon
//                                                               : protocol::kMaxRefundQuotientFrontier};
//         const uint64_t max_refund{(txn.gas_limit - gas_left) / max_refund_quotient};
//         uint64_t refund = std::min(gas_refund, max_refund);
//         gas_left += refund;
//     } else {
//         gas_left += gas_refund;
//         if( gas_left > txn.gas_limit - silkworm::protocol::fee::kGTransaction ) {
//             gas_left = txn.gas_limit - silkworm::protocol::fee::kGTransaction;
//         }
//     }

//     state_.add_to_balance(*txn.sender(), gas_left * effective_gas_price);

//     return gas_left;
// }

ValidationResult ExecutionProcessor::execute_block_no_post_validation(std::vector<Receipt>& receipts) noexcept {
    const evmc_revision rev{evm_.revision()};
    rule_set_.initialize(evm_);
    state_.finalize_transaction(rev);

    cumulative_gas_used_ = 0;

    const Block& block{evm_.block()};
    notify_block_execution_start(block);

    receipts.resize(block.transactions.size());
    auto receipt_it{receipts.begin()};

    for (const auto& txn : block.transactions) {
        if(is_reserved_address(*txn.sender())) {
            //must mirror contract's initial state of reserved address
            state_.set_balance(*txn.sender(), txn.value + intx::uint256(txn.gas_limit) * txn.max_fee_per_gas);
            state_.set_nonce(*txn.sender(), txn.nonce);
        }
        const ValidationResult err{protocol::validate_transaction(txn, state_, available_gas())};
        if (err != ValidationResult::kOk) {
            return err;
        }
        execute_transaction(txn, *receipt_it);
        ++receipt_it;
    }

    std::vector<Log> logs;
    logs.reserve(receipts.size());
    for (const auto& receipt : receipts) {
        std::ranges::copy(receipt.logs, std::back_inserter(logs));
    }
    state_.clear_journal_and_substate();
    const auto finalization_result = rule_set_.finalize(state_, block, evm_, logs);
    state_.finalize_transaction(rev);

    notify_block_execution_end(block);

    return finalization_result;
}

ValidationResult ExecutionProcessor::execute_block(std::vector<Receipt>& receipts) noexcept {
    if (const ValidationResult res{execute_block_no_post_validation(receipts)}; res != ValidationResult::kOk) {
        return res;
    }

    const auto& header{evm_.block().header};

    if (cumulative_gas_used_ != header.gas_used) {
        return ValidationResult::kWrongBlockGas;
    }

    if (evm_.revision() >= EVMC_BYZANTIUM) {
        // Prior to Byzantium (EIP-658), receipts contained the root of the state after each individual transaction.
        // We don't calculate such intermediate state roots and thus can't verify the receipt root before Byzantium.
        static constexpr auto kEncoder = [](Bytes& to, const Receipt& r) { rlp::encode(to, r); };
        evmc::bytes32 receipt_root{trie::root_hash(receipts, kEncoder)};
        if (receipt_root != header.receipts_root) {
            return ValidationResult::kWrongReceiptsRoot;
        }
    }

    Bloom bloom{};  // zero initialization
    for (const Receipt& receipt : receipts) {
        join(bloom, receipt.bloom);
    }
    if (bloom != header.logs_bloom) {
        return ValidationResult::kWrongLogsBloom;
    }

    return ValidationResult::kOk;
}

void ExecutionProcessor::flush_state() {
    state_.write_to_db(evm_.block().header.number);
}

//! \brief Notify the registered tracers at the start of block execution.
void ExecutionProcessor::notify_block_execution_start(const Block& block) {
    for (auto& tracer : evm_.tracers()) {
        tracer.get().on_block_start(block);
    }
}

//! \brief Notify the registered tracers at the end of block execution.
void ExecutionProcessor::notify_block_execution_end(const Block& block) {
    for (auto& tracer : evm_.tracers()) {
        tracer.get().on_block_end(block);
    }
}

}  // namespace silkworm
