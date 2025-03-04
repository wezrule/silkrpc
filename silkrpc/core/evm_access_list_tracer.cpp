/*
   Copyright 2022 The Silkrpc Authors

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

#include <memory>

#include "evm_access_list_tracer.hpp"

#include <evmc/hex.hpp>
#include <evmc/instructions.h>
#include <intx/intx.hpp>
#include <silkworm/third_party/evmone/lib/evmone/execution_state.hpp>
#include <silkworm/third_party/evmone/lib/evmone/instructions.hpp>


#include <silkrpc/common/log.hpp>
#include <silkrpc/common/util.hpp>
#include <silkrpc/core/evm_executor.hpp>

namespace silkrpc {

const char* SLOAD = evmone::instr::traits[evmc_opcode::OP_SLOAD].name;
const char* SSTORE = evmone::instr::traits[evmc_opcode::OP_SSTORE].name;
const char* EXTCODECOPY = evmone::instr::traits[evmc_opcode::OP_EXTCODECOPY].name;
const char* EXTCODEHASH = evmone::instr::traits[evmc_opcode::OP_EXTCODEHASH].name;
const char* EXTCODESIZE = evmone::instr::traits[evmc_opcode::OP_EXTCODESIZE].name;
const char* BALANCE = evmone::instr::traits[evmc_opcode::OP_BALANCE].name;
const char* SELFDESTRUCT = evmone::instr::traits[evmc_opcode::OP_SELFDESTRUCT].name;
const char* DELEGATECALL = evmone::instr::traits[evmc_opcode::OP_DELEGATECALL].name;
const char* CALL = evmone::instr::traits[evmc_opcode::OP_CALL].name;
const char* STATICCALL = evmone::instr::traits[evmc_opcode::OP_STATICCALL].name;
const char* CALLCODE = evmone::instr::traits[evmc_opcode::OP_CALLCODE].name;

std::string get_opcode_name(const char* const* names, std::uint8_t opcode) {
    const auto name = names[opcode];
    return (name != nullptr) ? name : "opcode 0x" + evmc::hex(opcode) + " not defined";
}

inline evmc::address AccessListTracer::address_from_hex_string(const std::string& s) {
    const auto bytes = silkworm::from_hex(s);
    return silkworm::to_evmc_address(bytes.value_or(silkworm::Bytes{}));
}

void AccessListTracer::on_execution_start(evmc_revision rev, const evmc_message& msg, evmone::bytes_view code) noexcept {
    if (opcode_names_ == nullptr) {
        opcode_names_ = evmc_get_instruction_names_table(rev);
    }
}

void AccessListTracer::on_instruction_start(uint32_t pc, const intx::uint256 *stack_top, const int stack_height,
                 const evmone::ExecutionState& execution_state, const silkworm::IntraBlockState& intra_block_state) noexcept {
    assert(execution_state.msg);
    evmc::address recipient(execution_state.msg->recipient);

    const auto opcode = execution_state.original_code[pc];
    const auto opcode_name = get_opcode_name(opcode_names_, opcode);

    SILKRPC_DEBUG << "on_instruction_start:"
        << " pc: " << std::dec << pc
        << " opcode: 0x" << std::hex << evmc::hex(opcode)
        << " opcode_name: " << opcode_name
        << " recipient: " << recipient
        << " execution_state: {"
        << "   gas_left: " << std::dec << execution_state.gas_left
        << "   status: " << execution_state.status
        << "   msg.gas: " << std::dec << execution_state.msg->gas
        << "   msg.depth: " << std::dec << execution_state.msg->depth
        << "}\n";

    if (is_storage_opcode(opcode_name) && stack_height >= 1 ) {
        const auto address = silkworm::bytes32_from_hex(intx::hex(stack_top[0]));
        add_storage(recipient, address);
    } else if (is_contract_opcode(opcode_name) && stack_height >= 1 ) {
        const auto address = address_from_hex_string(intx::hex(stack_top[0]));
        if (!exclude(address)) {
            add_address(address);
        }
    } else if (is_call_opcode(opcode_name) && stack_height  >= 5) {
        const auto address = address_from_hex_string(intx::hex(stack_top[-1]));
        if (!exclude(address)) {
            add_address(address);
        }
    }
}

inline bool AccessListTracer::is_storage_opcode(const std::string & opcode_name) {
    return (opcode_name == SLOAD || opcode_name == SSTORE);
}

inline bool AccessListTracer::is_contract_opcode(const std::string & opcode_name) {
    return (opcode_name == EXTCODECOPY || opcode_name == EXTCODEHASH || opcode_name == EXTCODESIZE ||
            opcode_name == BALANCE || opcode_name == SELFDESTRUCT);
}

inline bool AccessListTracer::is_call_opcode(const std::string & opcode_name) {
    return (opcode_name == DELEGATECALL || opcode_name == CALL || opcode_name == STATICCALL || opcode_name == CALLCODE);
}


inline bool AccessListTracer::exclude(const evmc::address& address) {
    // return (address == from_ || address == to_ || is_precompiled(address)); // ADD check on precompiled when available from silkworm
    return (address == from_ || address == to_);
}

void AccessListTracer::add_storage(const evmc::address& address, const evmc::bytes32& storage) {
    SILKRPC_TRACE << "add_storage:" << address << " storage: " << storage << "\n";
    for (int i = 0; i < access_list_.size(); i++) {
        if (access_list_[i].account == address) {
            for (int j = 0; j < access_list_[i].storage_keys.size(); j++) {
                if (access_list_[i].storage_keys[j] == storage) {
                    return;
                }
            }
            access_list_[i].storage_keys.push_back(storage);
            return;
        }
    }
    silkworm::AccessListEntry item;
    item.account = address;
    item.storage_keys.push_back(storage);
    access_list_.push_back(item);
}

void AccessListTracer::add_address(const evmc::address& address) {
    SILKRPC_TRACE << "add_address:" << address << "\n";
    for (int i = 0; i < access_list_.size(); i++) {
        if (access_list_[i].account == address) {
            return;
        }
    }
    silkworm::AccessListEntry item;
    item.account = address;
    access_list_.push_back(item);
}

void AccessListTracer::dump(const std::string& user_string, const AccessList& acl) {
    std::cout << user_string << "\n";
    for (int i = 0; i < acl.size(); i++) {
        std::cout << "Address: " << acl[i].account << "\n";
        for (int z = 0; z < acl[i].storage_keys.size(); z++) {
            std::cout << "-> StorageKeys: " << acl[i].storage_keys[z] << "\n";
        }
    }
}

bool AccessListTracer::compare(const AccessList& acl1, const AccessList& acl2) {
    if (acl1.size() != acl2.size()) {
        return false;
    }
    for (int i = 0; i < acl1.size(); i++) {
        bool match_address = false;
        for (int j = 0; j < acl2.size(); j++) {
            if (acl2[j].account == acl1[i].account) {
                match_address = true;
                if (acl2[j].storage_keys.size() != acl1[i].storage_keys.size()) {
                    return false;
                }
                bool match_storage = false;
                for (int z = 0; z < acl1[i].storage_keys.size(); z++) {
                    for (int t = 0; t < acl2[j].storage_keys.size(); t++) {
                        if (acl2[j].storage_keys[t] == acl1[i].storage_keys[z]) {
                            match_storage = true;
                            break;
                        }
                    }
                    if (!match_storage) {
                        return false;
                    }
                }
                break;
            }
        }
        if (!match_address) {
            return false;
        }
    }
    return true;
}

} // namespace silkrpc
