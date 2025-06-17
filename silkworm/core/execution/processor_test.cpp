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

#include <catch2/catch_test_macros.hpp>
#include <evmc/evmc.hpp>

#include <silkworm/core/protocol/param.hpp>
#include <silkworm/core/state/in_memory_state.hpp>
#include <silkworm/core/types/address.hpp>
#include <silkworm/core/types/evmc_bytes32.hpp>
#include <eosevm/version.hpp>

namespace silkworm {

TEST_CASE("Zero gas price") {
    Block block{};
    block.header.number = 2'687'232;
    block.header.gas_limit = 3'303'221;
    block.header.beneficiary = 0x4bb96091ee9d802ed039c4d1a5f6216f90f81b01_address;

    // The sender does not exist
    evmc::address sender{0x004512399a230565b99be5c3b0030a56f3ace68c_address};

    Transaction txn{};
    txn.gas_limit = 764'017;
    txn.data = *from_hex("0x606060");
    txn.odd_y_parity = false;
    txn.r = 1;
    txn.s = 1;
    txn.set_sender(sender);

    InMemoryState state;
    auto rule_set{protocol::rule_set_factory(kMainnetConfig)};
    ExecutionProcessor processor{block, *rule_set, state, kMainnetConfig, false, {}, {}};

    Receipt receipt;
    processor.execute_transaction(txn, receipt);
    CHECK(receipt.success);
}

TEST_CASE("No refund on error") {
    Block block{};
    block.header.number = 10'050'107;
    block.header.gas_limit = 328'646;
    block.header.beneficiary = 0x5146556427ff689250ed1801a783d12138c3dd5e_address;
    evmc::address caller{0x834e9b529ac9fa63b39a06f8d8c9b0d6791fa5df_address};
    uint64_t nonce{3};

    // This contract initially sets its 0th storage to 0x2a.
    // When called, it updates the 0th storage to the input provided.
    Bytes code{*from_hex("602a60005560098060106000396000f36000358060005531")};
    /* https://github.com/CoinCulture/evm-tools
    0      PUSH1  => 2a
    2      PUSH1  => 00
    4      SSTORE
    5      PUSH1  => 09
    7      DUP1
    8      PUSH1  => 10
    10     PUSH1  => 00
    12     CODECOPY
    13     PUSH1  => 00
    15     RETURN
  -----------------------------
    16     PUSH1  => 00
    18     CALLDATALOAD
    19     DUP1
    20     PUSH1  => 00
    22     SSTORE
    23     BALANCE
    */

    InMemoryState state;
    auto rule_set{protocol::rule_set_factory(kMainnetConfig)};
    ExecutionProcessor processor{block, *rule_set, state, kMainnetConfig, false, {}, {}};

    Transaction txn{};
    txn.nonce = nonce;
    txn.max_priority_fee_per_gas = 59 * kGiga;
    txn.max_fee_per_gas = 59 * kGiga;
    txn.gas_limit = 103'858;
    txn.data = code;
    txn.odd_y_parity = false;
    txn.r = 1;
    txn.s = 1;

    processor.evm().state().add_to_balance(caller, kEther);
    processor.evm().state().set_nonce(caller, nonce);
    txn.set_sender(caller);

    Receipt receipt1;
    processor.execute_transaction(txn, receipt1);
    CHECK(receipt1.success);

    // Call the newly created contract
    txn.nonce = nonce + 1;
    txn.to = create_address(caller, nonce);

    // It should run SSTORE(0,0) with a potential refund
    txn.data.clear();

    // But then there's not enough gas for the BALANCE operation
    txn.gas_limit = protocol::fee::kGTransaction + 5'020;

    Receipt receipt2;
    processor.execute_transaction(txn, receipt2);
    CHECK(!receipt2.success);
    CHECK(receipt2.cumulative_gas_used - receipt1.cumulative_gas_used == txn.gas_limit);
}

TEST_CASE("Self-destruct") {
    Block block{};
    block.header.number = 1'487'375;
    block.header.gas_limit = 4'712'388;
    block.header.beneficiary = 0x61c808d82a3ac53231750dadc13c777b59310bd9_address;

    const evmc::address suicidal_address{0x6d20c1c07e56b7098eb8c50ee03ba0f6f498a91d_address};
    const evmc::address caller_address{0x4bf2054ffae7a454a35fd8cf4be21b23b1f25a6f_address};
    const evmc::address originator{0x5a0b54d5dc17e0aadc383d2db43b0a0d3e029c4c_address};

    // The contract self-destructs if called with zero value.
    Bytes suicidal_code{*from_hex("346007576000ff5b")};
    /* https://github.com/CoinCulture/evm-tools
    0      CALLVALUE
    1      PUSH1  => 07
    3      JUMPI
    4      PUSH1  => 00
    6      SUICIDE
    7      JUMPDEST
    */

    // The caller calls the input contract three times:
    // twice with zero value and once with non-zero value.
    Bytes caller_code{*from_hex("600080808080803561eeeef150600080808080803561eeeef15060008080806005813561eeeef1")};
    /* https://github.com/CoinCulture/evm-tools
    0      PUSH1  => 00
    2      DUP1
    3      DUP1
    4      DUP1
    5      DUP1
    6      DUP1
    7      CALLDATALOAD
    8      PUSH2  => eeee
    11     CALL
    12     POP
    13     PUSH1  => 00
    15     DUP1
    16     DUP1
    17     DUP1
    18     DUP1
    19     DUP1
    20     CALLDATALOAD
    21     PUSH2  => eeee
    24     CALL
    25     POP
    26     PUSH1  => 00
    28     DUP1
    29     DUP1
    30     DUP1
    31     PUSH1  => 05
    33     DUP2
    34     CALLDATALOAD
    35     PUSH2  => eeee
    38     CALL
    */

    InMemoryState state;
    auto rule_set{protocol::rule_set_factory(kMainnetConfig)};
    ExecutionProcessor processor{block, *rule_set, state, kMainnetConfig, false, {}, {}};

    processor.evm().state().add_to_balance(originator, kEther);
    processor.evm().state().set_code(caller_address, caller_code);
    processor.evm().state().set_code(suicidal_address, suicidal_code);

    Transaction txn{};
    txn.max_priority_fee_per_gas = 20 * kGiga;
    txn.max_fee_per_gas = 20 * kGiga;
    txn.gas_limit = 100'000;
    txn.to = caller_address;
    txn.odd_y_parity = false;
    txn.r = 1;
    txn.s = 1;
    txn.set_sender(originator);

    evmc::bytes32 address_as_hash{to_bytes32(suicidal_address.bytes)};
    txn.data = ByteView{address_as_hash};

    Receipt receipt1;
    processor.execute_transaction(txn, receipt1);
    CHECK(receipt1.success);

    CHECK(!processor.evm().state().exists(suicidal_address));

    // Now the contract is self-destructed, this is a simple value transfer
    txn.nonce = 1;
    txn.to = suicidal_address;
    txn.data.clear();

    Receipt receipt2;
    processor.execute_transaction(txn, receipt2);
    CHECK(receipt2.success);

    CHECK(processor.evm().state().exists(suicidal_address));
    CHECK(processor.evm().state().get_balance(suicidal_address) == 0);

    CHECK(receipt2.cumulative_gas_used == receipt1.cumulative_gas_used + protocol::fee::kGTransaction);
}

TEST_CASE("Out of Gas during account re-creation") {
    uint64_t block_num{2'081'788};
    Block block{};
    block.header.number = block_num;
    block.header.gas_limit = 4'712'388;
    block.header.beneficiary = 0xa42af2c70d316684e57aefcc6e393fecb1c7e84e_address;
    evmc::address caller{0xc789e5aba05051b1468ac980e30068e19fad8587_address};

    uint64_t nonce{0};
    evmc::address address{create_address(caller, nonce)};

    InMemoryState state;

    // Some funds were previously transferred to the address:
    // https://etherscan.io/address/0x78c65b078353a8c4ce58fb4b5acaac6042d591d5
    Account account{};
    account.balance = 66'252'368 * kGiga;
    state.update_account(address, /*initial=*/std::nullopt, account);

    Transaction txn{};
    txn.nonce = nonce;
    txn.max_priority_fee_per_gas = 20 * kGiga;
    txn.max_fee_per_gas = 20 * kGiga;
    txn.gas_limit = 690'000;
    txn.data = *from_hex(
        "0x6060604052604051610ca3380380610ca3833981016040528080518201919060200150505b600281511015"
        "61003357610002565b8060006000509080519060200190828054828255906000526020600020908101928215"
        "6100a4579160200282015b828111156100a35782518260006101000a81548173ffffffffffffffffffffffff"
        "ffffffffffffffff0219169083021790555091602001919060010190610061565b5b5090506100eb91906100"
        "b1565b808211156100e757600081816101000a81549073ffffffffffffffffffffffffffffffffffffffff02"
        "19169055506001016100b1565b5090565b50506000600160006101000a81548160ff02191690830217905550"
        "5b50610b8d806101166000396000f360606040523615610095576000357c0100000000000000000000000000"
        "000000000000000000000000000000900480632079fb9a14610120578063391252151461016257806345550a"
        "51146102235780637df73e27146102ac578063979f1976146102da578063a0b7967b14610306578063a68a76"
        "cc14610329578063abe3219c14610362578063fc0f392d1461038757610095565b61011e5b60003411156101"
        "1b577f6e89d517057028190560dd200cf6bf792842861353d1173761dfa362e1c133f0333460003660405180"
        "8573ffffffffffffffffffffffffffffffffffffffff16815260200184815260200180602001828103825284"
        "848281815260200192508082843782019150509550505050505060405180910390a15b5b565b005b61013660"
        "04808035906020019091905050610396565b604051808273ffffffffffffffffffffffffffffffffffffffff"
        "16815260200191505060405180910390f35b6102216004808035906020019091908035906020019091908035"
        "906020019082018035906020019191908080601f016020809104026020016040519081016040528093929190"
        "8181526020018383808284378201915050505050509090919080359060200190919080359060200190919080"
        "35906020019082018035906020019191908080601f0160208091040260200160405190810160405280939291"
        "908181526020018383808284378201915050505050509090919050506103d8565b005b610280600480803590"
        "6020019091908035906020019082018035906020019191908080601f01602080910402602001604051908101"
        "604052809392919081815260200183838082843782019150505050505090909190505061064b565b60405180"
        "8273ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390f35b6102c260"
        "048080359060200190919050506106fa565b60405180821515815260200191505060405180910390f35b6102"
        "f060048080359060200190919050506107a8565b6040518082815260200191505060405180910390f35b6103"
        "136004805050610891565b6040518082815260200191505060405180910390f35b6103366004805050610901"
        "565b604051808273ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390"
        "f35b61036f600480505061093b565b60405180821515815260200191505060405180910390f35b6103946004"
        "80505061094e565b005b600060005081815481101561000257906000526020600020900160005b9150909054"
        "906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b600060006103e5336106fa56"
        "5b15156103f057610002565b600160009054906101000a900460ff1680156104125750610410886106fa565b"
        "155b1561041c57610002565b4285101561042957610002565b610432846107a8565b50878787878760405180"
        "8673ffffffffffffffffffffffffffffffffffffffff166c0100000000000000000000000002815260140185"
        "81526020018480519060200190808383829060006004602084601f0104600f02600301f15090500183815260"
        "200182815260200195505050505050604051809103902091506104b7828461064b565b90506104c2816106fa"
        "565b15156104cd57610002565b3373ffffffffffffffffffffffffffffffffffffffff168173ffffffffffff"
        "ffffffffffffffffffffffffffff16141561050657610002565b8773ffffffffffffffffffffffffffffffff"
        "ffffffff16600088604051809050600060405180830381858888f19350505050151561054357610002565b7f"
        "59bed9ab5d78073465dd642a9e3e76dfdb7d53bcae9d09df7d0b8f5234d5a8063382848b8b8b604051808773"
        "ffffffffffffffffffffffffffffffffffffffff1681526020018673ffffffffffffffffffffffffffffffff"
        "ffffffff168152602001856000191681526020018473ffffffffffffffffffffffffffffffffffffffff1681"
        "5260200183815260200180602001828103825283818151815260200191508051906020019080838382906000"
        "6004602084601f0104600f02600301f150905090810190601f16801561062e57808203805160018360200361"
        "01000a031916815260200191505b5097505050505050505060405180910390a15b5050505050505050565b60"
        "006000600060006041855114151561066357610002565b602085015192506040850151915060ff6041860151"
        "169050601b8160ff16101561069057601b8101905080505b6001868285856040518085600019168152602001"
        "8460ff1681526020018360001916815260200182600019168152602001945050505050602060405180830381"
        "6000866161da5a03f1156100025750506040518051906020015093506106f1565b50505092915050565b6000"
        "6000600090505b600060005080549050811015610799578273ffffffffffffffffffffffffffffffffffffff"
        "ff16600060005082815481101561000257906000526020600020900160005b9054906101000a900473ffffff"
        "ffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1614156107"
        "8b57600191506107a2565b5b8080600101915050610703565b600091506107a2565b50919050565b60006000"
        "60006107b7336106fa565b15156107c257610002565b60009150600090505b600a8160ff16101561084b5783"
        "60026000508260ff16600a8110156100025790900160005b505414156107fd57610002565b60026000508260"
        "0a8110156100025790900160005b505460026000508260ff16600a8110156100025790900160005b50541015"
        "61083d578060ff16915081505b5b80806001019150506107cb565b600260005082600a811015610002579090"
        "0160005b505484101561086e57610002565b83600260005083600a8110156100025790900160005b50819055"
        "505b5050919050565b60006000600060009150600090505b600a8110156108f15781600260005082600a8110"
        "156100025790900160005b505411156108e357600260005081600a8110156100025790900160005b50549150"
        "81505b5b80806001019150506108a0565b6001820192506108fc565b505090565b600061090c336106fa565b"
        "151561091757610002565b6040516101c2806109cb833901809050604051809103906000f09050610938565b"
        "90565b600160009054906101000a900460ff1681565b610957336106fa565b151561096257610002565b6001"
        "600160006101000a81548160ff021916908302179055507f0909e8f76a4fd3e970f2eaef56c0ee6dfaf8b87c"
        "5b8d3f56ffce78e825a9115733604051808273ffffffffffffffffffffffffffffffffffffffff1681526020"
        "0191505060405180910390a15b5660606040525b33600060006101000a81548173ffffffffffffffffffffff"
        "ffffffffffffffffff021916908302179055505b6101838061003f6000396000f36060604052361561004857"
        "6000357c0100000000000000000000000000000000000000000000000000000000900480636b9f96ea146100"
        "a6578063ca325469146100b557610048565b6100a45b600060009054906101000a900473ffffffffffffffff"
        "ffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16600034604051809050"
        "600060405180830381858888f19350505050505b565b005b6100b360048050506100ee565b005b6100c26004"
        "80505061015d565b604051808273ffffffffffffffffffffffffffffffffffffffff16815260200191505060"
        "405180910390f35b600060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673"
        "ffffffffffffffffffffffffffffffffffffffff1660003073ffffffffffffffffffffffffffffffffffffff"
        "ff1631604051809050600060405180830381858888f19350505050505b565b600060009054906101000a9004"
        "73ffffffffffffffffffffffffffffffffffffffff1681560000000000000000000000000000000000000000"
        "0000000000000000000000200000000000000000000000000000000000000000000000000000000000000002"
        "000000000000000000000000c789e5aba05051b1468ac980e30068e19fad8587000000000000000000000000"
        "99c426b2a0453e27decaecd93c3722fb0f378fc5");
    txn.odd_y_parity = false;
    txn.r = 1;
    txn.s = 1;
    txn.set_sender(caller);

    auto rule_set{protocol::rule_set_factory(kMainnetConfig)};
    ExecutionProcessor processor{block, *rule_set, state, kMainnetConfig, false, {}, {}};
    processor.evm().state().add_to_balance(caller, kEther);

    Receipt receipt;
    processor.execute_transaction(txn, receipt);
    // out of gas
    CHECK(!receipt.success);

    processor.evm().state().write_to_db(block_num);

    // only the caller and the miner should change
    CHECK(state.read_account(address) == account);
}

TEST_CASE("Empty suicide beneficiary") {
    uint64_t block_num{2'687'389};
    Block block{};
    block.header.number = block_num;
    block.header.gas_limit = 4'712'388;
    block.header.beneficiary = 0x2a65aca4d5fc5b5c859090a6c34d164135398226_address;
    evmc::address caller{0x5ed8cee6b63b1c6afce3ad7c92f4fd7e1b8fad9f_address};
    evmc::address suicide_beneficiary{0xee098e6c2a43d9e2c04f08f0c3a87b0ba59079d5_address};

    Transaction txn{};
    txn.max_priority_fee_per_gas = 30 * kGiga;
    txn.max_fee_per_gas = 30 * kGiga;
    txn.gas_limit = 360'000;
    txn.data = *from_hex(
        "0x6000607f5359610043806100135939610056566c010000000000000000000000007fee098e6c2"
        "a43d9e2c04f08f0c3a87b0ba59079d4d53532071d6cd0cb86facd5605ff6100008061003f600039"
        "61003f565b6000f35b816000f0905050596100718061006c59396100dd5661005f8061000e60003"
        "961006d566000603f5359610043806100135939610056566c010000000000000000000000007fee"
        "098e6c2a43d9e2c04f08f0c3a87b0ba59079d4d53532071d6cd0cb86facd5605ff6100008061003"
        "f60003961003f565b6000f35b816000f0905050fe5b6000f35b816000f090506040526000600060"
        "0060006000604051620249f0f15061000080610108600039610108565b6000f3");
    txn.odd_y_parity = false;
    txn.r = 1;
    txn.s = 1;
    txn.set_sender(caller);

    InMemoryState state;

    auto rule_set{protocol::rule_set_factory(kMainnetConfig)};
    ExecutionProcessor processor{block, *rule_set, state, kMainnetConfig, false, {}, {}};
    processor.evm().state().add_to_balance(caller, kEther);

    Receipt receipt;
    processor.execute_transaction(txn, receipt);
    CHECK(receipt.success);

    processor.evm().state().write_to_db(block_num);

    // suicide_beneficiary should've been touched and deleted
    CHECK(!state.read_account(suicide_beneficiary));
}

TEST_CASE("CHAINID instruction") {
    // Set chain_id to a value other than 1
    ChainConfig config = kMainnetConfig;
    config.chain_id = 42;

    Block block{};
    block.header.number = 20'000'000;  // PUSH0 enabled
    block.header.gas_limit = 50'000;
    const auto caller = 0x5ed8cee6b63b1c6afce3ad7c92f4fd7e1b8fad9f_address;

    const auto code = *from_hex("465955");  // SSTORE(0, CHAINID)

    Transaction txn{};
    txn.to = 0xc0de_address;
    txn.gas_limit = block.header.gas_limit;
    txn.set_sender(caller);

    SECTION("protected") {
        txn.chain_id = config.chain_id;  // chain id matches in a valid transaction
    }
    SECTION("legacy") {
        txn.chain_id = std::nullopt;  // valid legacy transaction may not have the chain id
    }

    InMemoryState state;

    ExecutionProcessor processor{block, *protocol::rule_set_factory(config), state, config, true, {}, {}};
    processor.evm().state().add_to_balance(caller, kEther);
    processor.evm().state().set_code(*txn.to, code);

    Receipt receipt;
    processor.execute_transaction(txn, receipt);
    CHECK(receipt.success);

    const auto v = processor.evm().state().get_current_storage(*txn.to, 0x00_bytes32);
    CHECK(v == evmc::bytes32{config.chain_id});
}

TEST_CASE("EOS EVM refund v2") {
    auto deploy_and_execute = [&](uint64_t v, uint64_t times=10) {
        Block block{};
        block.header.number = 10'050'107;
        block.header.gas_limit = 10'000'000;
        block.header.nonce = eosevm::version_to_nonce(v);
        block.header.beneficiary = 0x5146556427ff689250ed1801a783d12138c3dd5e_address;
        evmc::address caller{0x834e9b529ac9fa63b39a06f8d8c9b0d6791fa5df_address};
        uint64_t nonce{3};
        /*
        // SPDX-License-Identifier: GPL-3.0
        pragma solidity >=0.8.2 <0.9.0;
        contract Refund {
            uint256 number;
            function run(uint256 times) public {
                for (uint i = 0; i < times; i++) {
                    number = 1;
                    number = 0;
                }
            }
        }
        */
        Bytes code{*from_hex("608060405234801561001057600080fd5b50610192806100206000396000f3fe608060405234801561001057600080fd5b506004361061002b5760003560e01c8063a444f5e914610030575b600080fd5b61004a600480360381019061004591906100b8565b61004c565b005b60005b8181101561007957600160008190555060008081905550808061007190610114565b91505061004f565b5050565b600080fd5b6000819050919050565b61009581610082565b81146100a057600080fd5b50565b6000813590506100b28161008c565b92915050565b6000602082840312156100ce576100cd61007d565b5b60006100dc848285016100a3565b91505092915050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601160045260246000fd5b600061011f82610082565b91507fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff8203610151576101506100e5565b5b60018201905091905056fea26469706673582212203d88f52fc817048f72a222d4f3e50f4c76512b2119e3b493958f9b2bc033363a64736f6c63430008110033")};
        InMemoryState state;
        auto rule_set{protocol::rule_set_factory(kEOSEVMMainnetConfig)};
        ExecutionProcessor processor{block, *rule_set, state, kEOSEVMMainnetConfig, false, {}, {}};
        Transaction txn;
        txn.nonce = nonce;
        txn.max_priority_fee_per_gas = 150 * kGiga;
        txn.max_fee_per_gas = 150 * kGiga;
        txn.gas_limit = 150'000;
        txn.data = code;
        txn.odd_y_parity = false;  // odd_y_parity
        txn.r = 1;      // r
        txn.s = 1;      // s

        processor.evm().state().add_to_balance(caller, 100*kEther);
        processor.evm().state().set_nonce(caller, nonce);
        txn.set_sender(caller);
        Receipt receipt1;
        processor.execute_transaction(txn, receipt1);
        CHECK(receipt1.success);
        // Call run(10) on the newly created contract //a444f5e9 = run, 00..0a = 10
        txn.nonce     = nonce + 1;
        txn.to        = create_address(caller, nonce);
        txn.data      = *from_hex("a444f5e9" + to_hex(evmc::bytes32{times}));
        txn.gas_limit = 800'000;
        Receipt receipt2;
        processor.execute_transaction(txn, receipt2);
        CHECK(receipt2.success);
        return receipt2.cumulative_gas_used - receipt1.cumulative_gas_used;
    };
    auto gas_used_v0 = deploy_and_execute(0);
    CHECK(gas_used_v0 == 115830);
    auto gas_used_v1 = deploy_and_execute(1);
    CHECK(gas_used_v1 == 181408);
    auto gas_used_v2 = deploy_and_execute(2);
    CHECK(gas_used_v2 == 27760);
    auto gas_used_v2_0_times = deploy_and_execute(2, 0);
    CHECK(gas_used_v2_0_times == 21608);
}

TEST_CASE("EOS EVM message filter") {
    Block block{};
    block.header.number = 100;
    block.header.gas_limit = 0x7fffffff;
    block.header.beneficiary = 0xbbbbbbbbbbbbbbbbbbbbbbbb0000000000000000_address;
    evmc::address caller{0x834e9b529ac9fa63b39a06f8d8c9b0d6791fa5df_address};
    uint64_t nonce{3};
    /*
    // SPDX-License-Identifier: GPL-3.0
    pragma solidity >=0.7.0 <0.9.0;
    contract Recursive {
        event Call(uint256 _value);
        function start(uint256 _depth) public {
            emit Call(_depth);
            if( _depth == 0 )
                return;
            Recursive(this).start(_depth-1);
        }
    }
    */
    Bytes code{*from_hex(
        "608060405234801561001057600080fd5b50610232806100206000396000f3fe608060405234"
        "801561001057600080fd5b506004361061002b5760003560e01c806395805dad14610030575b"
        "600080fd5b61004a60048036038101906100459190610142565b61004c565b005b7ff84df193"
        "bb49c064bf1e234bd59df0c2a313cac2b206d8dc62dfc812a1b84fa58160405161007b919061"
        "017e565b60405180910390a16000810315610104573073ffffffffffffffffffffffffffffff"
        "ffffffffff166395805dad6001836100b591906101c8565b6040518263ffffffff1660e01b81"
        "526004016100d1919061017e565b600060405180830381600087803b1580156100eb57600080"
        "fd5b505af11580156100ff573d6000803e3d6000fd5b505050505b50565b600080fd5b600081"
        "9050919050565b61011f8161010c565b811461012a57600080fd5b50565b6000813590506101"
        "3c81610116565b92915050565b60006020828403121561015857610157610107565b5b600061"
        "01668482850161012d565b91505092915050565b6101788161010c565b82525050565b600060"
        "2082019050610193600083018461016f565b92915050565b7f4e487b71000000000000000000"
        "00000000000000000000000000000000000000600052601160045260246000fd5b60006101d3"
        "8261010c565b91506101de8361010c565b92508282039050818111156101f6576101f5610199"
        "565b5b9291505056fea2646970667358221220dd3582a5d0cc9f3fc7818bf67fe1833fd59321"
        "b5e0c69cc7af71e8332df84d3e64736f6c63430008110033"
    )};
    InMemoryState state;
    auto rule_set{protocol::rule_set_factory(kEOSEVMMainnetConfig)};
    ExecutionProcessor processor{block, *rule_set, state, kEOSEVMMainnetConfig, false, {}, {}};

    processor.evm().state().add_to_balance(caller, kEther*100);
    processor.evm().state().set_nonce(caller, nonce);

    Transaction txn;
    txn.nonce = nonce;
    txn.max_priority_fee_per_gas = 150 * kGiga;
    txn.max_fee_per_gas = 150 * kGiga;
    txn.gas_limit = 1'000'000;
    txn.data = code;
    txn.odd_y_parity = false; // odd_y_parity
    txn.r = 1; // r
    txn.s = 1; // s

    txn.set_sender(caller);
    Receipt receipt1;
    processor.execute_transaction(txn, receipt1);
    CHECK(receipt1.success);
    // Call the newly created contract
    txn.nonce++;
    txn.to = create_address(caller, nonce);
    txn.data = *from_hex("0x95805dad0000000000000000000000000000000000000000000000000000000000000004"); //Call start(4)
    txn.gas_limit = 1'000'000;
    Receipt receipt2;
    processor.set_evm_message_filter([&](const evmc_message&) -> bool {
        return true;
    });
    processor.execute_transaction(txn, receipt2);
    CHECK(receipt2.success);
    const auto& filtered_messages = processor.state().filtered_messages();
    REQUIRE(filtered_messages.size()==5);
    for(size_t i=0; i<5; i++) {
        CHECK(filtered_messages[i].data == *from_hex("0x95805dad000000000000000000000000000000000000000000000000000000000000000" + std::to_string(i)));
    }
    // Call reserved address
    txn.nonce++;
    txn.to = 0xbbbbbbbbbbbbbbbbbbbbbbbb0000000000000000_address;
    txn.data = *from_hex("0xB0CA");
    txn.gas_limit = 1'000'000;
    Receipt receipt3;
    processor.execute_transaction(txn, receipt3);
    const auto& fm2 = processor.state().filtered_messages();
    CHECK(receipt3.success);
    CHECK(fm2.size()==1);
    CHECK(fm2[0].data == *from_hex("0xB0CA"));
}

TEST_CASE("EOS EVM message filter revert") {
    Block block{};
    block.header.number = 100;
    block.header.gas_limit = 0x7fffffff;
    block.header.beneficiary = 0xbbbbbbbbbbbbbbbbbbbbbbbb0000000000000000_address;
    evmc::address caller{0x834e9b529ac9fa63b39a06f8d8c9b0d6791fa5df_address};
    uint64_t nonce{3};
    /*
    // SPDX-License-Identifier: GPL-3.0
    pragma solidity >=0.7.0 <0.9.0;
    contract FailAndSucceed {
        function docall(string memory destination, bool atomic, bytes memory data) public {
            address eosevm = 0xBbBBbbBBbBbbbBBBbBbbBBbB56E4000000000000;
            (bool success, ) = address(eosevm).call(abi.encodeWithSignature("bridgeMsgV0(string,bool,bytes)", destination, atomic, data));
        }
        function dofail(string memory destination, bool atomic, bytes memory data) public {
            docall(destination, atomic, data);
            revert("dofail will revert");
        }
        function dosucceed(string memory destination, bool atomic, bytes memory data) public {
            docall(destination, atomic, data);
        }
        function failsucceed(string memory fail_dest, bool fail_atomic, bytes memory fail_data, string memory ok_dest, bool ok_atomic, bytes memory ok_data) public {
            (bool success1, ) = address(this).call(abi.encodeWithSignature("dofail(string,bool,bytes)", fail_dest, fail_atomic, fail_data));
            (bool success2, ) = address(this).call(abi.encodeWithSignature("dosucceed(string,bool,bytes)", ok_dest, ok_atomic, ok_data));
        }
        function succeedfail(string memory fail_dest, bool fail_atomic, bytes memory fail_data, string memory ok_dest, bool ok_atomic, bytes memory ok_data) public {
            (bool success1, ) = address(this).call(abi.encodeWithSignature("dosucceed(string,bool,bytes)", ok_dest, ok_atomic, ok_data));
            (bool success2, ) = address(this).call(abi.encodeWithSignature("dofail(string,bool,bytes)", fail_dest, fail_atomic, fail_data));
        }
    }
    */
    Bytes code{*from_hex("608060405234801561001057600080fd5b50610c40806100206000396000f3fe608060405234801561001057600080fd5b50600436106100575760003560e01c8063112eb9961461005c578063323e9502146100785780636a20d99114610094578063bac53e88146100b0578063dc4f58b0146100cc575b600080fd5b6100766004803603810190610071919061089b565b6100e8565b005b610092600480360381019061008d9190610998565b6102ee565b005b6100ae60048036038101906100a99190610998565b610334565b005b6100ca60048036038101906100c5919061089b565b610452565b005b6100e660048036038101906100e19190610998565b610658565b005b60003073ffffffffffffffffffffffffffffffffffffffff1687878760405160240161011693929190610b06565b6040516020818303038152906040527f323e9502000000000000000000000000000000000000000000000000000000007bffffffffffffffffffffffffffffffffffffffffffffffffffffffff19166020820180517bffffffffffffffffffffffffffffffffffffffffffffffffffffffff83818316178352505050506040516101a09190610b87565b6000604051808303816000865af19150503d80600081146101dd576040519150601f19603f3d011682016040523d82523d6000602084013e6101e2565b606091505b5050905060003073ffffffffffffffffffffffffffffffffffffffff1685858560405160240161021493929190610b06565b6040516020818303038152906040527fdc4f58b0000000000000000000000000000000000000000000000000000000007bffffffffffffffffffffffffffffffffffffffffffffffffffffffff19166020820180517bffffffffffffffffffffffffffffffffffffffffffffffffffffffff838183161783525050505060405161029e9190610b87565b6000604051808303816000865af19150503d80600081146102db576040519150601f19603f3d011682016040523d82523d6000602084013e6102e0565b606091505b505090505050505050505050565b6102f9838383610334565b6040517f08c379a000000000000000000000000000000000000000000000000000000000815260040161032b90610bea565b60405180910390fd5b600073bbbbbbbbbbbbbbbbbbbbbbbb56e4000000000000905060008173ffffffffffffffffffffffffffffffffffffffff1685858560405160240161037b93929190610b06565b6040516020818303038152906040527ff781185b000000000000000000000000000000000000000000000000000000007bffffffffffffffffffffffffffffffffffffffffffffffffffffffff19166020820180517bffffffffffffffffffffffffffffffffffffffffffffffffffffffff83818316178352505050506040516104059190610b87565b6000604051808303816000865af19150503d8060008114610442576040519150601f19603f3d011682016040523d82523d6000602084013e610447565b606091505b505090505050505050565b60003073ffffffffffffffffffffffffffffffffffffffff1684848460405160240161048093929190610b06565b6040516020818303038152906040527fdc4f58b0000000000000000000000000000000000000000000000000000000007bffffffffffffffffffffffffffffffffffffffffffffffffffffffff19166020820180517bffffffffffffffffffffffffffffffffffffffffffffffffffffffff838183161783525050505060405161050a9190610b87565b6000604051808303816000865af19150503d8060008114610547576040519150601f19603f3d011682016040523d82523d6000602084013e61054c565b606091505b5050905060003073ffffffffffffffffffffffffffffffffffffffff1688888860405160240161057e93929190610b06565b6040516020818303038152906040527f323e9502000000000000000000000000000000000000000000000000000000007bffffffffffffffffffffffffffffffffffffffffffffffffffffffff19166020820180517bffffffffffffffffffffffffffffffffffffffffffffffffffffffff83818316178352505050506040516106089190610b87565b6000604051808303816000865af19150503d8060008114610645576040519150601f19603f3d011682016040523d82523d6000602084013e61064a565b606091505b505090505050505050505050565b610663838383610334565b505050565b6000604051905090565b600080fd5b600080fd5b600080fd5b600080fd5b6000601f19601f8301169050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b6106cf82610686565b810181811067ffffffffffffffff821117156106ee576106ed610697565b5b80604052505050565b6000610701610668565b905061070d82826106c6565b919050565b600067ffffffffffffffff82111561072d5761072c610697565b5b61073682610686565b9050602081019050919050565b82818337600083830152505050565b600061076561076084610712565b6106f7565b90508281526020810184848401111561078157610780610681565b5b61078c848285610743565b509392505050565b600082601f8301126107a9576107a861067c565b5b81356107b9848260208601610752565b91505092915050565b60008115159050919050565b6107d7816107c2565b81146107e257600080fd5b50565b6000813590506107f4816107ce565b92915050565b600067ffffffffffffffff82111561081557610814610697565b5b61081e82610686565b9050602081019050919050565b600061083e610839846107fa565b6106f7565b90508281526020810184848401111561085a57610859610681565b5b610865848285610743565b509392505050565b600082601f8301126108825761088161067c565b5b813561089284826020860161082b565b91505092915050565b60008060008060008060c087890312156108b8576108b7610672565b5b600087013567ffffffffffffffff8111156108d6576108d5610677565b5b6108e289828a01610794565b96505060206108f389828a016107e5565b955050604087013567ffffffffffffffff81111561091457610913610677565b5b61092089828a0161086d565b945050606087013567ffffffffffffffff81111561094157610940610677565b5b61094d89828a01610794565b935050608061095e89828a016107e5565b92505060a087013567ffffffffffffffff81111561097f5761097e610677565b5b61098b89828a0161086d565b9150509295509295509295565b6000806000606084860312156109b1576109b0610672565b5b600084013567ffffffffffffffff8111156109cf576109ce610677565b5b6109db86828701610794565b93505060206109ec868287016107e5565b925050604084013567ffffffffffffffff811115610a0d57610a0c610677565b5b610a198682870161086d565b9150509250925092565b600081519050919050565b600082825260208201905092915050565b60005b83811015610a5d578082015181840152602081019050610a42565b60008484015250505050565b6000610a7482610a23565b610a7e8185610a2e565b9350610a8e818560208601610a3f565b610a9781610686565b840191505092915050565b610aab816107c2565b82525050565b600081519050919050565b600082825260208201905092915050565b6000610ad882610ab1565b610ae28185610abc565b9350610af2818560208601610a3f565b610afb81610686565b840191505092915050565b60006060820190508181036000830152610b208186610a69565b9050610b2f6020830185610aa2565b8181036040830152610b418184610acd565b9050949350505050565b600081905092915050565b6000610b6182610ab1565b610b6b8185610b4b565b9350610b7b818560208601610a3f565b80840191505092915050565b6000610b938284610b56565b915081905092915050565b7f646f6661696c2077696c6c207265766572740000000000000000000000000000600082015250565b6000610bd4601283610a2e565b9150610bdf82610b9e565b602082019050919050565b60006020820190508181036000830152610c0381610bc7565b905091905056fea2646970667358221220500d36b4db9944b5a4e2cff9a613da1768ab3aa5dabc585fbed5ad20679b640064736f6c63430008120033")};
    InMemoryState state;
    auto rule_set{protocol::rule_set_factory(kEOSEVMMainnetConfig)};
    ExecutionProcessor processor{block, *rule_set, state, kEOSEVMMainnetConfig, false, {}, {}};
    Transaction txn;
    txn.nonce = nonce;
    txn.max_priority_fee_per_gas = 150 * kGiga;
    txn.max_fee_per_gas = 150 * kGiga;
    txn.gas_limit = 1'000'000;
    txn.data = code;
    txn.odd_y_parity = false; // odd_y_parity
    txn.r = 1; // r
    txn.s = 1; // s

    processor.evm().state().add_to_balance(caller, kEther*100);
    processor.evm().state().set_nonce(caller, nonce);
    txn.set_sender(caller);
    Receipt receipt1;
    processor.execute_transaction(txn, receipt1);
    CHECK(receipt1.success);
    //Call failsucceed(no,false,0x00,yes,true,0x01)
    txn.nonce = txn.nonce + 1;
    txn.to = create_address(caller, nonce);
    txn.data = *from_hex("0x112eb99600000000000000000000000000000000000000000000000000000000000000c00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000010000000000000000000000000000000000000000000000000000000000000001400000000000000000000000000000000000000000000000000000000000000001000000000000000000000000000000000000000000000000000000000000018000000000000000000000000000000000000000000000000000000000000000026e6f000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000003796573000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000010100000000000000000000000000000000000000000000000000000000000000");
    txn.gas_limit = 1'000'000;
    Receipt receipt2;
    processor.set_evm_message_filter([&](const evmc_message& message) -> bool {
        return message.recipient == 0xbbbbbbbbbbbbbbbbbbbbbbbb56e4000000000000_address && message.input_size > 0;
    });
    processor.execute_transaction(txn, receipt2);
    CHECK(receipt2.success);
    const auto& filtered_messages = processor.state().filtered_messages();
    //check message captured is => bridgeMsgV0(yes,true,0x01)
    //f781185b = sha3('bridgeMsgV0(string,bool,bytes)')[:4]
    //0000000000000000000000000000000000000000000000000000000000000060 //offset p1
    //0000000000000000000000000000000000000000000000000000000000000001 //p2=true
    //00000000000000000000000000000000000000000000000000000000000000a0 //offset p3
    //0000000000000000000000000000000000000000000000000000000000000003 //p1 len
    //7965730000000000000000000000000000000000000000000000000000000000 //796573='yes'
    //0000000000000000000000000000000000000000000000000000000000000001 //p3 len
    //0100000000000000000000000000000000000000000000000000000000000000 //0x01
    REQUIRE(filtered_messages.size()==1);
    CHECK(filtered_messages[0].data == *from_hex("0xf781185b0000000000000000000000000000000000000000000000000000000000000060000000000000000000000000000000000000000000000000000000000000000100000000000000000000000000000000000000000000000000000000000000a00000000000000000000000000000000000000000000000000000000000000003796573000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000010100000000000000000000000000000000000000000000000000000000000000"));
    //Call succeedfail(no,false,0x00,yes,true,0x01)
    txn.nonce = txn.nonce + 1;
    txn.to = create_address(caller, nonce);
    txn.data = *from_hex("0x112eb99600000000000000000000000000000000000000000000000000000000000000c00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000010000000000000000000000000000000000000000000000000000000000000001400000000000000000000000000000000000000000000000000000000000000001000000000000000000000000000000000000000000000000000000000000018000000000000000000000000000000000000000000000000000000000000000026e6f000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000003796573000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000010100000000000000000000000000000000000000000000000000000000000000");
    txn.gas_limit = 1'000'000;
    Receipt receipt3;
    processor.state().filtered_messages().clear();
    processor.execute_transaction(txn, receipt3);
    CHECK(receipt3.success);
    const auto& filtered_messages2 = processor.state().filtered_messages();
    //check message captured is => bridgeMsgV0(yes,true,0x01)
    //f781185b = sha3('bridgeMsgV0(string,bool,bytes)')[:4]
    //0000000000000000000000000000000000000000000000000000000000000060 //offset p1
    //0000000000000000000000000000000000000000000000000000000000000001 //p2=true
    //00000000000000000000000000000000000000000000000000000000000000a0 //offset p3
    //0000000000000000000000000000000000000000000000000000000000000003 //p1 len
    //7965730000000000000000000000000000000000000000000000000000000000 //796573='yes'
    //0000000000000000000000000000000000000000000000000000000000000001 //p3 len
    //0100000000000000000000000000000000000000000000000000000000000000 //0x01
    REQUIRE(filtered_messages2.size()==1);
    CHECK(filtered_messages2[0].data == *from_hex("0xf781185b0000000000000000000000000000000000000000000000000000000000000060000000000000000000000000000000000000000000000000000000000000000100000000000000000000000000000000000000000000000000000000000000a00000000000000000000000000000000000000000000000000000000000000003796573000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000010100000000000000000000000000000000000000000000000000000000000000"));
}

TEST_CASE("EOS EVM No fee burn when chain uses trust ruleset") {

    intx::uint256 max_priority_fee_per_gas = 5 * kGiga;
    intx::uint256 max_fee_per_gas = 105 * kGiga;
    intx::uint256 base_fee_per_gas = 80 * kGiga;

    auto deploy_contract = [&](const ChainConfig& chain_config) -> auto {

        Block block{};
        block.header.number = 12'965'001;
        block.header.gas_limit = 0x7fffffff;
        block.header.beneficiary = 0xbbbbbbbbbbbbbbbbbbbbbbbb0000000000000000_address;
        block.header.nonce = eosevm::version_to_nonce(1);
        block.header.base_fee_per_gas = base_fee_per_gas;

        evmc::address caller{0x834e9b529ac9fa63b39a06f8d8c9b0d6791fa5df_address};
        uint64_t nonce{3};

        /*
        // SPDX-License-Identifier: GPL-3.0
        pragma solidity >=0.7.0 <0.9.0;
        contract Recursive {
            event Call(uint256 _value);
            function start(uint256 _depth) public {
                emit Call(_depth);
                if( _depth == 0 )
                    return;
                Recursive(this).start(_depth-1);
            }
        }
        */
        Bytes code{*from_hex(
            "608060405234801561001057600080fd5b50610232806100206000396000f3fe608060405234"
            "801561001057600080fd5b506004361061002b5760003560e01c806395805dad14610030575b"
            "600080fd5b61004a60048036038101906100459190610142565b61004c565b005b7ff84df193"
            "bb49c064bf1e234bd59df0c2a313cac2b206d8dc62dfc812a1b84fa58160405161007b919061"
            "017e565b60405180910390a16000810315610104573073ffffffffffffffffffffffffffffff"
            "ffffffffff166395805dad6001836100b591906101c8565b6040518263ffffffff1660e01b81"
            "526004016100d1919061017e565b600060405180830381600087803b1580156100eb57600080"
            "fd5b505af11580156100ff573d6000803e3d6000fd5b505050505b50565b600080fd5b600081"
            "9050919050565b61011f8161010c565b811461012a57600080fd5b50565b6000813590506101"
            "3c81610116565b92915050565b60006020828403121561015857610157610107565b5b600061"
            "01668482850161012d565b91505092915050565b6101788161010c565b82525050565b600060"
            "2082019050610193600083018461016f565b92915050565b7f4e487b71000000000000000000"
            "00000000000000000000000000000000000000600052601160045260246000fd5b60006101d3"
            "8261010c565b91506101de8361010c565b92508282039050818111156101f6576101f5610199"
            "565b5b9291505056fea2646970667358221220dd3582a5d0cc9f3fc7818bf67fe1833fd59321"
            "b5e0c69cc7af71e8332df84d3e64736f6c63430008110033"
        )};

        InMemoryState state;
        auto rule_set{protocol::rule_set_factory(chain_config)};
        ExecutionProcessor processor{block, *rule_set, state, chain_config, false, {}, {}};

        Transaction txn;
        txn.type = TransactionType::kDynamicFee;
        txn.nonce = nonce;
        txn.max_priority_fee_per_gas = max_priority_fee_per_gas;
        txn.max_fee_per_gas = max_fee_per_gas;
        txn.gas_limit = 1'000'000;
        txn.data = code;
        txn.odd_y_parity = false;  // odd_y_parity
        txn.r = 1;      // r
        txn.s = 1;      // s

        processor.evm().state().add_to_balance(caller, kEther*100);
        processor.evm().state().set_nonce(caller, nonce);
        txn.set_sender(caller);

        Receipt receipt1;
        processor.execute_transaction(txn, receipt1);
        CHECK(receipt1.success);

        return std::make_tuple(
            receipt1.cumulative_gas_used,
            processor.evm().state().get_balance(block.header.beneficiary)
        );
    };

    auto pf = std::min(max_priority_fee_per_gas, max_fee_per_gas - base_fee_per_gas);
    auto ep = pf + base_fee_per_gas;

    // All fees credited to beneficiary_balance (kTrust RuleSet)
    auto [gas_used, beneficiary_balance] = deploy_contract(kEOSEVMMainnetConfig);
    CHECK(beneficiary_balance == ep*gas_used);

    // Priority fee credited to beneficiary_balance (kEthash RuleSet)
    std::tie(gas_used, beneficiary_balance) = deploy_contract(kMainnetConfig);
    CHECK(beneficiary_balance == pf*gas_used);
}

TEST_CASE("EOS EVM v3 contract creation") {

    intx::uint256 max_priority_fee_per_gas = 5 * kGiga;
    intx::uint256 max_fee_per_gas = 105 * kGiga;
    intx::uint256 base_fee_per_gas = 80 * kGiga;

    auto deploy_contract = [&](const ChainConfig& chain_config, uint64_t gas_limit) -> auto {
        Block block{};
        block.header.number = 9'069'000;
        block.header.gas_limit = 0x7fffffff;
        block.header.beneficiary = 0xbbbbbbbbbbbbbbbbbbbbbbbb0000000000000001_address;
        block.header.nonce = eosevm::version_to_nonce(3);
        block.header.base_fee_per_gas = base_fee_per_gas;

        evmc::address caller{0x834e9b529ac9fa63b39a06f8d8c9b0d6791fa5df_address};
        uint64_t nonce{3};

        /*
        // SPDX-License-Identifier: MIT
        pragma solidity ^0.8.0;
        contract HeavyInit {
            // Example of 10 storage slots
            uint256[10] public storageSlots;
            constructor() {
                for (uint256 i = 0; i < 10; i++) {
                    storageSlots[i] = 1;
                }
            }
            function retrieve(uint256 index) public view returns (uint256){
                return storageSlots[index];
            }
        }
        */
        Bytes code{*from_hex("6080604052348015600e575f80fd5b505f5b600a811015603c5760015f82600a8110602b57602a6041565b5b018190555080806001019150506011565b50606e565b7f4e487b71000000000000000000000000000000000000000000000000000000005f52603260045260245ffd5b6101ba8061007b5f395ff3fe608060405234801561000f575f80fd5b5060043610610034575f3560e01c80635387694b146100385780638f88708b14610068575b5f80fd5b610052600480360381019061004d9190610104565b610098565b60405161005f919061013e565b60405180910390f35b610082600480360381019061007d9190610104565b6100b0565b60405161008f919061013e565b60405180910390f35b5f81600a81106100a6575f80fd5b015f915090505481565b5f8082600a81106100c4576100c3610157565b5b01549050919050565b5f80fd5b5f819050919050565b6100e3816100d1565b81146100ed575f80fd5b50565b5f813590506100fe816100da565b92915050565b5f60208284031215610119576101186100cd565b5b5f610126848285016100f0565b91505092915050565b610138816100d1565b82525050565b5f6020820190506101515f83018461012f565b92915050565b7f4e487b71000000000000000000000000000000000000000000000000000000005f52603260045260245ffdfea2646970667358221220702d8bb14041667c80c0f9380e5ddc6b1b1e6fdcf1e572f9af923407673c305e64736f6c634300081a0033")};

        InMemoryState state;
        auto rule_set{protocol::rule_set_factory(chain_config)};

        evmone::gas_parameters gas_params;
        gas_params.values_.G_txcreate = 50000;
        evmone::eosevm::gas_prices gas_prices{.overhead_price = 42500000001, .storage_price = 42500000000 };
        ExecutionProcessor processor{block, *rule_set, state, chain_config, false, gas_params, gas_prices};

        Transaction txn;
        txn.type = TransactionType::kDynamicFee;
        txn.nonce = nonce;
        txn.max_priority_fee_per_gas = max_priority_fee_per_gas;
        txn.max_fee_per_gas = max_fee_per_gas;
        txn.gas_limit = gas_limit;
        txn.data = code;
        txn.odd_y_parity = false;  // odd_y_parity
        txn.r = 1;      // r
        txn.s = 1;      // s

        processor.evm().state().add_to_balance(caller, kEther*100);
        processor.evm().state().set_nonce(caller, nonce);
        txn.set_sender(caller);

        Receipt receipt1;
        processor.execute_transaction(txn, receipt1);

        return std::make_tuple(
            receipt1,
            processor.evm().state().get_balance(block.header.beneficiary)
        );
    };

    // g_txcreate = 25000 (50000 original)
    // g0 = 29092+g_txcreate = 54092
    // init = 23156 (real) + 100000 (storage spec, 200k original) + 28000 (cpu spec) = 151156
    // code_deposit = 442 * 100 (200 original) = 44200

    auto [receipt1, _unused1] = deploy_contract(kEOSEVMMainnetConfig, 54092);
    CHECK(receipt1.success == false);
    CHECK(receipt1.cumulative_gas_used == 29092); // Only the real intrinsic gas (g_txcreate is refunded)

    auto [receipt2, _unused2] = deploy_contract(kEOSEVMMainnetConfig, 54092 + 151156 - 1);
    CHECK(receipt2.success == false);
    CHECK(receipt2.cumulative_gas_used == 29092+23156-1); // Only the real intrinsic+constructor

    auto [receipt3, _unused3] = deploy_contract(kEOSEVMMainnetConfig, 54092 + 151156 + 44200 - 1);
    CHECK(receipt3.success == false);
    CHECK(receipt3.cumulative_gas_used == 29092+23156); // Only the real intrinsic+constructor (full)

    auto [receipt4, _unused4] = deploy_contract(kEOSEVMMainnetConfig, 54092 + 151156 + 44200);
    CHECK(receipt4.success == true);
    CHECK(receipt4.cumulative_gas_used == 54092+151156+44200);
}

TEST_CASE("EOS EVM v3 final refund") {

    intx::uint256 max_priority_fee_per_gas = 5 * kGiga;
    intx::uint256 max_fee_per_gas = 105 * kGiga;

    auto deploy_contract = [&](const ChainConfig& chain_config, uint64_t gas_limit, const evmone::eosevm::gas_prices& gas_prices) -> auto {

        Block block{};
        block.header.number = 9'069'000;
        block.header.gas_limit = 0x7fffffff;
        block.header.beneficiary = 0xbbbbbbbbbbbbbbbbbbbbbbbb0000000000000000_address;
        block.header.nonce = eosevm::version_to_nonce(3);
        block.header.base_fee_per_gas = gas_prices.get_base_price();

        evmc::address caller{0x834e9b529ac9fa63b39a06f8d8c9b0d6791fa5df_address};
        uint64_t nonce{3};

        /*
        // SPDX-License-Identifier: MIT
        pragma solidity ^0.8.0;
        contract HeavyInit {
            // Example of 10 storage slots
            uint256[10] public storageSlots;
            constructor() {
                for (uint256 i = 0; i < 10; i++) {
                    storageSlots[i] = 1;
                }
            }
            function retrieve(uint256 index) public view returns (uint256){
                return storageSlots[index];
            }
        }
        */
        Bytes code{*from_hex("6080604052348015600e575f80fd5b505f5b600a811015603c5760015f82600a8110602b57602a6041565b5b018190555080806001019150506011565b50606e565b7f4e487b71000000000000000000000000000000000000000000000000000000005f52603260045260245ffd5b6101ba8061007b5f395ff3fe608060405234801561000f575f80fd5b5060043610610034575f3560e01c80635387694b146100385780638f88708b14610068575b5f80fd5b610052600480360381019061004d9190610104565b610098565b60405161005f919061013e565b60405180910390f35b610082600480360381019061007d9190610104565b6100b0565b60405161008f919061013e565b60405180910390f35b5f81600a81106100a6575f80fd5b015f915090505481565b5f8082600a81106100c4576100c3610157565b5b01549050919050565b5f80fd5b5f819050919050565b6100e3816100d1565b81146100ed575f80fd5b50565b5f813590506100fe816100da565b92915050565b5f60208284031215610119576101186100cd565b5b5f610126848285016100f0565b91505092915050565b610138816100d1565b82525050565b5f6020820190506101515f83018461012f565b92915050565b7f4e487b71000000000000000000000000000000000000000000000000000000005f52603260045260245ffdfea2646970667358221220702d8bb14041667c80c0f9380e5ddc6b1b1e6fdcf1e572f9af923407673c305e64736f6c634300081a0033")};

        InMemoryState state;
        auto rule_set{protocol::rule_set_factory(chain_config)};

        evmone::gas_parameters gas_params;
        gas_params.values_.G_txcreate = 50000;

        ExecutionProcessor processor{block, *rule_set, state, chain_config, false, gas_params, gas_prices};

        Transaction txn;
        txn.type = TransactionType::kDynamicFee;
        txn.nonce = nonce;
        txn.max_priority_fee_per_gas = max_priority_fee_per_gas;
        txn.max_fee_per_gas = max_fee_per_gas;
        txn.gas_limit = gas_limit;
        txn.data = code;
        txn.odd_y_parity = false;  // odd_y_parity
        txn.r = 1;      // r
        txn.s = 1;      // s

        processor.evm().state().add_to_balance(caller, kEther*100);
        processor.evm().state().set_nonce(caller, nonce);
        txn.set_sender(caller);

        Receipt receipt1;
        auto res = processor.execute_transaction(txn, receipt1);

        return std::make_tuple(
            res,
            receipt1,
            processor.evm().state().get_balance(block.header.beneficiary),
            txn.effective_gas_price(*block.header.base_fee_per_gas)
        );
    };

    evmone::eosevm::gas_prices gp;
    gp.overhead_price = 70 * kGiga;
    gp.storage_price = 80 * kGiga;
    auto base_fee_per_gas = gp.get_base_price();
    auto inclusion_price = std::min(max_priority_fee_per_gas, max_fee_per_gas - base_fee_per_gas);

    // ***** storage_price >= overhead_price *****
    // g_txcreate = 47058 (50000 scaled)
    // g0 = 29092 (cpu real) + g_txcreate (storage spec) = 76150
    // init = 23156 (cpu real) + 188230 (200000 scaled) (storage spec) + 28000 (cpu spec) = 239386
    // code_deposit = 442 * 188 (200 scaled) = 83096
    // cpu_gas_consumed = 23156 + 28000 + 29092 = 80248
    // storage_gas_consumed = 47058 + 188230 + 83096 = 318384
    CHECK(76150+239386+83096 == 80248+318384);

    uint64_t expected_refund = 9440;

    auto [res, receipt, balance, effective_gas_price] = deploy_contract(kEOSEVMMainnetConfig, 76150+239386+83096, gp);
    CHECK(receipt.success == true);
    CHECK(receipt.cumulative_gas_used == 76150+239386+83096-expected_refund);

    auto inclusion_fee = inclusion_price * intx::uint256(res.cpu_gas_consumed);
    CHECK(res.inclusion_fee == inclusion_fee);
    CHECK(res.cpu_gas_consumed == 80248);

    auto storage_fee = res.discounted_storage_gas_consumed*effective_gas_price;
    CHECK(res.storage_fee == storage_fee);
    CHECK(res.discounted_storage_gas_consumed == 318384);

    CHECK(receipt.cumulative_gas_used+expected_refund == 80248+318384);
    CHECK(balance == storage_fee + inclusion_fee + res.overhead_fee);

    // storage_price < overhead_price
    // g_txcreate = 41176 (50000 scaled)
    // g0 = 29092 (cpu real) + g_txcreate (storage spec) = 70268
    // init = 23156 (cpu real) + 164700 (200000 scaled) (storage spec) + 28000 (cpu spec) = 215856
    // code_deposit = 442 * 164 (200 scaled) = 72488
    // cpu_gas_consumed = 23156 + 28000 + 29092 = 80248
    // storage_gas_consumed = 41176 + 164700 + 72488 = 278364
    CHECK(70268+215856+72488 == 80248+278364);

    gp.overhead_price = 80 * kGiga;
    gp.storage_price = 70 * kGiga;
    base_fee_per_gas = gp.get_base_price();
    inclusion_price = std::min(max_priority_fee_per_gas, max_fee_per_gas - base_fee_per_gas);
    expected_refund = 0;

    std::tie(res, receipt, balance, effective_gas_price) = deploy_contract(kEOSEVMMainnetConfig, 70268+215856+72488, gp);
    CHECK(receipt.success == true);
    CHECK(receipt.cumulative_gas_used == 70268+215856+72488-expected_refund);

    inclusion_fee = inclusion_price * intx::uint256(res.cpu_gas_consumed);
    CHECK(res.inclusion_fee == inclusion_fee);
    CHECK(res.cpu_gas_consumed == 80248);

    storage_fee = res.discounted_storage_gas_consumed*effective_gas_price;
    CHECK(res.storage_fee == storage_fee);
    CHECK(res.discounted_storage_gas_consumed == 278364);

    CHECK(receipt.cumulative_gas_used+expected_refund == 80248+278364);
    CHECK(balance == storage_fee + inclusion_fee + res.overhead_fee);
}


}  // namespace silkworm
