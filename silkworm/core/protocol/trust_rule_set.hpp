#pragma once

#include <silkworm/core/protocol/rule_set.hpp>

namespace silkworm::protocol {

struct TrustRuleSet : RuleSet {

    explicit TrustRuleSet(const ChainConfig& chain_config) : RuleSet(chain_config, /*prohibit_ommers=*/false) {}

    //! \brief Validates the difficulty and the seal of the header
    //! \note Used by validate_block_header
    virtual ValidationResult validate_difficulty_and_seal(const BlockHeader& header, const BlockHeader& parent) {
        (void)header;
        (void)parent;
        return ValidationResult::kOk;
    }

    //! \brief Performs validation of block body that can be done prior to sender recovery and execution.
    //! \brief See [YP] Sections 4.3.2 "Holistic Validity" and 11.1 "Ommer Validation".
    //! \param [in] block: block to pre-validate.
    //! \param [in] state: current state.
    //! \note Shouldn't be used for genesis block.
    virtual ValidationResult pre_validate_block_body(const Block& block, const BlockState& state) {
        (void)block;
        (void)state;
        return ValidationResult::kOk;
    }

    //! \brief See [YP] Section 4.3.4 "Block Header Validity".
    //! \param [in] header: header to validate.
    //! \param [in] state: current state.
    //! \param [in] with_future_timestamp_check : whether to check header timestamp is in the future wrt host current
    //! time \see https://github.com/erigontech/silkworm/issues/448
    //! \note Shouldn't be used for genesis block.
    virtual ValidationResult validate_block_header(const BlockHeader& header, const BlockState& state,
                                                   bool with_future_timestamp_check) {
        (void)header;
        (void)state;
        (void)with_future_timestamp_check;
        return ValidationResult::kOk;
    }

    //! \brief Performs validation of block ommers only.
    //! \brief See [YP] Sections 11.1 "Ommer Validation".
    //! \param [in] block: block to validate.
    //! \param [in] state: current state.
    virtual ValidationResult validate_ommers(const Block& block, const BlockState& state) {
        (void)block;
        (void)state;
        return ValidationResult::kOk;
    }

    //! \brief Initializes block execution by applying changes stipulated by the protocol
    //! (e.g. storing parent beacon root)
    virtual void initialize(EVM& evm) {
        (void)evm;
    }

    //! \brief Finalizes block execution by applying changes stipulated by the protocol
    //! (e.g. block rewards, withdrawals)
    //! \param [in] state: current state.
    //! \param [in] block: current block to apply rewards for.
    //! \remarks For Ethash See [YP] Section 11.3 "Reward Application".
    virtual ValidationResult finalize(IntraBlockState& state, const Block& block, EVM& evm, const std::vector<Log>& logs) {
        (void)state;
        (void)block;
        (void)evm;
        (void)logs;
        return ValidationResult::kOk;
    }

};

}  // namespace silkworm::protocol