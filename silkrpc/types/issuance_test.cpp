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

#include "issuance.hpp"

#include <catch2/catch.hpp>

#include <silkrpc/common/log.hpp>

namespace silkrpc {

using Catch::Matchers::Message;

TEST_CASE("create empty issuance", "[silkrpc][types][issuance]") {
    Issuance i{};
    CHECK(i.block_reward == std::nullopt);
    CHECK(i.ommer_reward == std::nullopt);
    CHECK(i.issuance == std::nullopt);
}

TEST_CASE("print empty issuance", "[silkrpc][types][issuance]") {
    Issuance i{};
    CHECK_NOTHROW(null_stream() << i);
}

} // namespace silkrpc

