/*
   Copyright 2021 The Silkrpc Authors

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

#include "evm_executor.hpp"

#include <optional>
#include <string>
#include <vector>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_future.hpp>
#include <catch2/catch.hpp>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>

#include <silkrpc/common/util.hpp>
#include <silkrpc/types/transaction.hpp>

namespace silkrpc {

using Catch::Matchers::Message;
using evmc::literals::operator""_address, evmc::literals::operator""_bytes32;

TEST_CASE("EVMexecutor") {
    SILKRPC_LOG_STREAMS(null_stream(), null_stream());

    class StubDatabase : public core::rawdb::DatabaseReader {
        boost::asio::awaitable<KeyValue> get(const std::string& table, const silkworm::ByteView& key) const override {
            co_return KeyValue{};
        }
        boost::asio::awaitable<silkworm::Bytes> get_one(const std::string& table, const silkworm::ByteView& key) const override {
            co_return silkworm::Bytes{};
        }
        boost::asio::awaitable<std::optional<silkworm::Bytes>> get_both_range(const std::string& table, const silkworm::ByteView& key, const silkworm::ByteView& subkey) const override {
            co_return silkworm::Bytes{};
        }
        boost::asio::awaitable<void> walk(const std::string& table, const silkworm::ByteView& start_key, uint32_t fixed_bits, core::rawdb::Walker w) const override {
            co_return;
        }
        boost::asio::awaitable<void> for_prefix(const std::string& table, const silkworm::ByteView& prefix, core::rawdb::Walker w) const override {
            co_return;
        }
    };

    SECTION("failed if gas_limit < intrisicgas") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 10000;
        silkworm::Transaction txn{};
        txn.from = 0xa872626373628737383927236382161739290870_address;
        silkworm::Block block{};
        block.header.number = block_number;
        boost::asio::io_context& io_context = my_pool.next_io_context();

        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto execution_result = boost::asio::co_spawn(my_pool.next_io_context().get_executor(), executor.call(block, txn, {}), boost::asio::use_future);
        auto result = execution_result.get();
        my_pool.stop();
        my_pool.join();
        CHECK(result.error_code == 1000);
        CHECK(result.pre_check_error.value() == "intrinsic gas too low: have 0, want 53000");
    }

    SECTION("failed if base_fee_per_gas > max_fee_per_gas ") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.base_fee_per_gas = 0x7;
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.max_fee_per_gas = 0x2;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto execution_result = boost::asio::co_spawn(my_pool.next_io_context().get_executor(), executor.call(block, txn, {}), boost::asio::use_future);
        auto result = execution_result.get();
        my_pool.stop();
        my_pool.join();
        CHECK(result.error_code == 1000);
        CHECK(result.pre_check_error.value() == "fee cap less than block base fee: address 0xa872626373628737383927236382161739290870, gasFeeCap: 2 baseFee: 7");
    }

    SECTION("failed if  max_priority_fee_per_gas > max_fee_per_gas ") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.base_fee_per_gas = 0x1;
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.max_fee_per_gas = 0x2;
        txn.from = 0xa872626373628737383927236382161739290870_address;
        txn.max_priority_fee_per_gas = 0x18;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto execution_result = boost::asio::co_spawn(my_pool.next_io_context().get_executor(), executor.call(block, txn, {}), boost::asio::use_future);
        auto result = execution_result.get();
        my_pool.stop();
        my_pool.join();
        CHECK(result.error_code == 1000);
        CHECK(result.pre_check_error.value() == "tip higher than fee cap: address 0xa872626373628737383927236382161739290870, tip: 24 gasFeeCap: 2");
    }

    SECTION("failed if transaction cost greater user amount") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.base_fee_per_gas = 0x1;
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.max_fee_per_gas = 0x2;
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto execution_result = boost::asio::co_spawn(my_pool.next_io_context().get_executor(), executor.call(block, txn, {}), boost::asio::use_future);
        auto result = execution_result.get();
        my_pool.stop();
        my_pool.join();
        CHECK(result.error_code == 1000);
        CHECK(result.pre_check_error.value() == "insufficient funds for gas * price + value: address 0xa872626373628737383927236382161739290870 have 0 want 60000");
    }

    SECTION("doesn t fail if transaction cost greater user amount && gasBailout == true") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.base_fee_per_gas = 0x1;
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.max_fee_per_gas = 0x2;
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto execution_result = boost::asio::co_spawn(my_pool.next_io_context().get_executor(), executor.call(block, txn, {}, false, /* gasBailout */true), boost::asio::use_future);
        auto result = execution_result.get();
        executor.reset();
        my_pool.stop();
        my_pool.join();
        CHECK(result.error_code == 0);
    }


    AccessList access_list{
        {0xde0b295669a9fd93d5f28d9ec85e40f4cb697bae_address,
            {
                0x0000000000000000000000000000000000000000000000000000000000000003_bytes32,
                0x0000000000000000000000000000000000000000000000000000000000000007_bytes32,
            }
        },
        {0xbb9bc244d798123fde783fcc1c72d3bb8c189413_address, {}},
    };

    SECTION("call returns SUCCESS") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.gas_limit = 600000;
        txn.from = 0xa872626373628737383927236382161739290870_address;
        txn.access_list = access_list;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto execution_result = boost::asio::co_spawn(my_pool.next_io_context().get_executor(), executor.call(block, txn, {}, true, true), boost::asio::use_future);
        auto result = execution_result.get();
        my_pool.stop();
        my_pool.join();
        CHECK(result.error_code == 0);
    }

    static silkworm::Bytes error_data{
                               0x08, 0xc3, 0x79, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x4f, 0x77, 0x6e, 0x61, 0x62, 0x6c, 0x65, 0x3a, 0x20, 0x63,
                               0x61, 0x6c, 0x6c, 0x65, 0x72, 0x20, 0x69, 0x73, 0x20, 0x6e, 0x6f, 0x74, 0x20, 0x74, 0x68, 0x65, 0x20, 0x6f, 0x77, 0x6e, 0x65, 0x72};

    static silkworm::Bytes short_error_data_1{0x08, 0xc3};

    static silkworm::Bytes short_error_data_2{
                               0x08, 0xc3, 0x79, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

    static silkworm::Bytes short_error_data_3{
                               0x08, 0xc3, 0x79, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00 };

    static silkworm::Bytes short_error_data_4{
                               0x08, 0xc3, 0x79, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x4f, 0x77, 0x6e, 0x61, 0x62, 0x6c, 0x65, 0x3a,
                               0x20, 0x63, 0x61, 0x6c, 0x6c, 0x65, 0x72, 0x20, 0x69, 0x73, 0x20};

    SECTION("get_error_message(EVMC_FAILURE) with short error_data_1") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto error_message = executor.get_error_message(evmc_status_code::EVMC_FAILURE, short_error_data_1);
        my_pool.stop();
        my_pool.join();
        CHECK(error_message == "execution failed"); // only short answer because error_data is too short */
    }

    SECTION("get_error_message(EVMC_FAILURE) with short error_data_2") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto error_message = executor.get_error_message(evmc_status_code::EVMC_FAILURE, short_error_data_2);
        my_pool.stop();
        my_pool.join();
        CHECK(error_message == "execution failed"); // only short answer because error_data is too short */
    }

    SECTION("get_error_message(EVMC_FAILURE) with short error_data_3") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto error_message = executor.get_error_message(evmc_status_code::EVMC_FAILURE, short_error_data_3);
        my_pool.stop();
        my_pool.join();
        CHECK(error_message == "execution failed"); // only short answer because error_data is too short */
    }

    SECTION("get_error_message(EVMC_FAILURE) with short error_data_4") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto error_message = executor.get_error_message(evmc_status_code::EVMC_FAILURE, short_error_data_4);
        my_pool.stop();
        my_pool.join();
        CHECK(error_message == "execution failed"); // only short answer because error_data is too short */
    }

    SECTION("get_error_message(EVMC_FAILURE) with full error") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto error_message = executor.get_error_message(evmc_status_code::EVMC_FAILURE, error_data);
        my_pool.stop();
        my_pool.join();
        CHECK(error_message == "execution failed: Ownable: caller is not the owner");
    }

    SECTION("get_error_message(EVMC_FAILURE) with short error") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto error_message = executor.get_error_message(evmc_status_code::EVMC_FAILURE, error_data, false);
        my_pool.stop();
        my_pool.join();
        CHECK(error_message == "execution failed");
    }

    SECTION("get_error_message(EVMC_REVERT) with short error") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto error_message = executor.get_error_message(evmc_status_code::EVMC_REVERT, error_data, false);
        my_pool.stop();
        my_pool.join();
        CHECK(error_message == "execution reverted");
    }

    SECTION("get_error_message(EVMC_OUT_OF_GAS) with short error") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto error_message = executor.get_error_message(evmc_status_code::EVMC_OUT_OF_GAS, error_data, false);
        my_pool.stop();
        my_pool.join();
        CHECK(error_message == "out of gas");
    }

    SECTION("get_error_message(EVMC_INVALID_INSTRUCTION) with short error") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto error_message = executor.get_error_message(evmc_status_code::EVMC_INVALID_INSTRUCTION, error_data, false);
        my_pool.stop();
        my_pool.join();
        CHECK(error_message == "invalid instruction");
    }

    SECTION("get_error_message(EVMC_UNDEFINED_INSTRUCTION) with short error") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto error_message = executor.get_error_message(evmc_status_code::EVMC_UNDEFINED_INSTRUCTION, error_data, false);
        my_pool.stop();
        my_pool.join();
        CHECK(error_message == "invalid opcode");
    }

    SECTION("get_error_message(EVMC_STACK_OVERFLOW) with short error") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto error_message = executor.get_error_message(evmc_status_code::EVMC_STACK_OVERFLOW, error_data, false);
        my_pool.stop();
        my_pool.join();
        CHECK(error_message == "stack overflow");
    }

    SECTION("get_error_message(EVMC_STACK_UNDERFLOW) with short error") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto error_message = executor.get_error_message(evmc_status_code::EVMC_STACK_UNDERFLOW, error_data, false);
        my_pool.stop();
        my_pool.join();
        CHECK(error_message == "stack underflow");
    }

    SECTION("get_error_message(EVMC_BAD_JUMP_DESTINATION) with short error") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto error_message = executor.get_error_message(evmc_status_code::EVMC_BAD_JUMP_DESTINATION, error_data, false);
        my_pool.stop();
        my_pool.join();
        CHECK(error_message == "invalid jump destination");
    }

    SECTION("get_error_message(EVMC_INVALID_MEMORY_ACCESS) with short error") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto error_message = executor.get_error_message(evmc_status_code::EVMC_INVALID_MEMORY_ACCESS, error_data, false);
        my_pool.stop();
        my_pool.join();
        CHECK(error_message == "invalid memory access");
    }

    SECTION("get_error_message(EVMC_CALL_DEPTH_EXCEEDED) with short error") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto error_message = executor.get_error_message(evmc_status_code::EVMC_CALL_DEPTH_EXCEEDED, error_data, false);
        my_pool.stop();
        my_pool.join();
        CHECK(error_message == "call depth exceeded");
    }

    SECTION("get_error_message(EVMC_STATIC_MODE_VIOLATION) with short error") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto error_message = executor.get_error_message(evmc_status_code::EVMC_STATIC_MODE_VIOLATION, error_data, false);
        my_pool.stop();
        my_pool.join();
        CHECK(error_message == "static mode violation");
    }

    SECTION("get_error_message(EVMC_PRECOMPILE_FAILURE) with short error") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto error_message = executor.get_error_message(evmc_status_code::EVMC_PRECOMPILE_FAILURE, error_data, false);
        my_pool.stop();
        my_pool.join();
        CHECK(error_message == "precompile failure");
    }

    SECTION("get_error_message(EVMC_CONTRACT_VALIDATION_FAILURE) with short error") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto error_message = executor.get_error_message(evmc_status_code::EVMC_CONTRACT_VALIDATION_FAILURE, error_data, false);
        my_pool.stop();
        my_pool.join();
        CHECK(error_message == "contract validation failure");
    }

    SECTION("get_error_message(EVMC_ARGUMENT_OUT_OF_RANGE) with short error") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto error_message = executor.get_error_message(evmc_status_code::EVMC_ARGUMENT_OUT_OF_RANGE, error_data, false);
        my_pool.stop();
        my_pool.join();
        CHECK(error_message == "argument out of range");
    }

    SECTION("get_error_message(wrong status_code) with short error") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto error_message = executor.get_error_message(8888, error_data, false);
        my_pool.stop();
        my_pool.join();
        CHECK(error_message == "unknown error code");
    }

    SECTION("get_error_message(EVMC_WASM_UNREACHABLE_INSTRUCTION) with short error") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto error_message = executor.get_error_message(evmc_status_code::EVMC_WASM_UNREACHABLE_INSTRUCTION, error_data, false);
        my_pool.stop();
        my_pool.join();
        CHECK(error_message == "wasm unreachable instruction");
    }

    SECTION("get_error_message(EVMC_WASM_TRAP) with short error") {
        StubDatabase tx_database;
        const uint64_t chain_id = 5;
        const auto chain_config_ptr = lookup_chain_config(chain_id);

        ChannelFactory my_channel = []() { return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials()); };
        ContextPool my_pool{1, my_channel};
        boost::asio::thread_pool workers{1};
        my_pool.start();

        const auto block_number = 6000000;
        silkworm::Block block{};
        block.header.number = block_number;
        silkworm::Transaction txn{};
        txn.gas_limit = 60000;
        txn.from = 0xa872626373628737383927236382161739290870_address;

        boost::asio::io_context& io_context = my_pool.next_io_context();
        state::RemoteState remote_state{io_context, tx_database, block_number};
        EVMExecutor executor{io_context, tx_database, *chain_config_ptr, workers, block_number, remote_state};
        auto error_message = executor.get_error_message(evmc_status_code::EVMC_WASM_TRAP, error_data, false);
        my_pool.stop();
        my_pool.join();
        CHECK(error_message == "wasm trap");
    }
}

} // namespace silkrpc
