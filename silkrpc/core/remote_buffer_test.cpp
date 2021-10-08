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

#include "remote_buffer.hpp"

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <catch2/catch.hpp>
#include <evmc/evmc.hpp>
#include <silkworm/common/base.hpp>

#include <silkrpc/common/log.hpp>
#include <silkrpc/core/rawdb/accessors.hpp>

namespace silkrpc::state {

using Catch::Matchers::Message;
using evmc::literals::operator""_bytes32;

TEST_CASE("async remote buffer", "[silkrpc][core][remote_buffer]") {
    SILKRPC_LOG_VERBOSITY(LogLevel::None);

    class MockDatabaseReader : public core::rawdb::DatabaseReader {
    public:
        MockDatabaseReader() = default;
        explicit MockDatabaseReader(const silkworm::Bytes& value) : value_(value) {}

        asio::awaitable<KeyValue> get(const std::string& table, const silkworm::ByteView& key) const override {
            co_return KeyValue{};
        }
        asio::awaitable<silkworm::Bytes> get_one(const std::string& table, const silkworm::ByteView& key) const override {
            co_return value_;
        }
        asio::awaitable<std::optional<silkworm::Bytes>> get_both_range(const std::string& table, const silkworm::ByteView& key, const silkworm::ByteView& subkey) const override {
            co_return silkworm::Bytes{};
        }
        asio::awaitable<void> walk(const std::string& table, const silkworm::ByteView& start_key, uint32_t fixed_bits, core::rawdb::Walker w) const override {
            co_return;
        }
        asio::awaitable<void> for_prefix(const std::string& table, const silkworm::ByteView& prefix, core::rawdb::Walker w) const override {
            co_return;
        }
    private:
        silkworm::Bytes value_;
    };

    SECTION("read code for empty hash") {
        asio::io_context io_context;
        MockDatabaseReader db_reader;
        const uint64_t block_number = 1'000'000;
        AsyncRemoteBuffer arb{io_context, db_reader, block_number};
        auto future_code{asio::co_spawn(io_context, arb.read_code(silkworm::kEmptyHash), asio::use_future)};
        io_context.run();
        CHECK(future_code.get() == silkworm::ByteView{});
    }

    SECTION("read code for non-empty hash") {
        asio::io_context io_context;
        silkworm::Bytes code{*silkworm::from_hex("0x0608")};
        MockDatabaseReader db_reader{code};
        const uint64_t block_number = 1'000'000;
        AsyncRemoteBuffer arb{io_context, db_reader, block_number};
        const auto code_hash{0x04491edcd115127caedbd478e2e7895ed80c7847e903431f94f9cfa579cad47f_bytes32};
        auto future_code{asio::co_spawn(io_context, arb.read_code(code_hash), asio::use_future)};
        io_context.run();
        CHECK(future_code.get() == silkworm::ByteView{code});
    }
}

} // namespace silkrpc::state

