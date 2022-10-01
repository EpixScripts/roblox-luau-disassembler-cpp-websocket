#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace LuauDisassembler {
	enum LuaType;
	struct LuaImport;
	union LuaValueUnion;
	struct LuaValue;
	struct Proto;

	LuaImport dissect_import(uint32_t id, std::vector<LuaValue>& k);
	std::vector<Proto*> deserialize_bytecode(const char* data);
	std::string getStringForInstruction(Proto* proto, size_t& pc, bool displayLineInfo);
	std::string disassemble(const char* bytecode, size_t bytecode_size, bool displayLineInfo);
}