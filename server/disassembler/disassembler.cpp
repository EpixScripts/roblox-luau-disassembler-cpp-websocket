#include <cstdint>
#include <vector>
#include <string>
#include <sstream>
#include <format>
#include <iostream>

#include "bytecode.hpp"

namespace LuauDisassembler {
	enum LuaType : uint8_t {
		LUA_TNIL,
		LUA_TBOOLEAN,
		LUA_TNUMBER,
		LUA_TSTRING,
		LUA_TIMPORT,
	};

	struct LuaImport {
		uint8_t count = 0;
		std::string displayString;
	};

	union LuaValueUnion {
		bool boolean;
		double number;
		std::string str;
		LuaImport import;

		LuaValueUnion() : import(LuaImport()) {}
		~LuaValueUnion() {}
	};

	struct LuaValue {
		union LuaValueUnion value;
		uint8_t type = LUA_TNIL;

		LuaValue() : value(LuaValueUnion()), type(LUA_TNIL) {}
		LuaValue(const LuaValue&) {}
	};

	struct Proto {
		uint8_t maxstacksize = 0;
		uint8_t numparams = 0;
		uint8_t nups = 0;
		uint8_t is_vararg = 0;

		std::vector<uint32_t> code;
		std::vector<LuaValue> k;
		std::vector<uint32_t> p;
		uint8_t* lineinfo;
		int* abslineinfo;

		std::string debugname = "UNNAMED";

		uint8_t linegaplog2 = 0;
		uint32_t sizelineinfo = 0;

		uint32_t sizelocvars = 0;
		uint32_t sizeupvalues = 0;

		uint32_t linedefined = 0;

		Proto() :
			maxstacksize(0),
			numparams(0),
			nups(0),
			is_vararg(0),
			code(),
			k(),
			p(),
			lineinfo(nullptr),
			abslineinfo(nullptr),
			debugname("UNNAMED"),
			linegaplog2(0),
			sizelineinfo(0),
			sizelocvars(0),
			sizeupvalues(0),
			linedefined(0)
		{}
		~Proto() {
			delete[] lineinfo;
		}
	};

	LuaImport dissect_import(uint32_t id, std::vector<LuaValue>& k) {
		uint8_t count = id >> 30;
		int id0 = count > 0 ? int(id >> 20) & 1023 : -1;
		int id1 = count > 1 ? int(id >> 10) & 1023 : -1;
		int id2 = count > 2 ? int(id) & 1023 : -1;

		std::string displayString = k[id0].value.str;
		if (id1 >= 0) {
			displayString += "." + k[id1].value.str;
			if (id2 >= 0) {
				displayString += "." + k[id2].value.str;
			}
		}

		return { count, displayString };
	}

	template<typename T>
	inline T read(const char* data, size_t& offset) {
		T result = 0;
		memcpy(&result, data + offset, sizeof(T));
		offset += sizeof(T);

		return result;
	}

	uint32_t readLEB128(const char* data, size_t& offset) {
		uint32_t result = 0;
		uint32_t shift = 0;

		uint8_t byte = 0;

		do {
			byte = read<uint8_t>(data, offset);
			result |= (byte & 127) << shift;
			shift += 7;
		} while (byte & 128);

		return result;
	}

	inline int getLineNumberFromPc(Proto* p, int pc) {
		if (!p->lineinfo)
			return 0;

		return p->abslineinfo[pc >> p->linegaplog2] + p->lineinfo[pc];
	}

	std::vector<Proto*> deserialize_bytecode(const char* data) {
		size_t offset = 0;

		uint8_t version = read<uint8_t>(data, offset);
		if (version == 0 || version != 2) {
			throw std::exception("Invalid bytecode");
		}

		uint32_t stringCount = readLEB128(data, offset);

		std::vector<std::string> stringTable;
		stringTable.reserve(stringCount);

		for (uint32_t i = 0; i < stringCount; i++) {
			uint32_t stringLength = readLEB128(data, offset);
			stringTable.emplace_back(std::string(data + offset, stringLength));
			offset += stringLength;
		}

		uint32_t protoCount = readLEB128(data, offset);

		std::vector<Proto*> protoTable;
		protoTable.reserve(protoCount);

		for (uint32_t i = 0; i < protoCount; i++) {
			Proto* p = new Proto;

			p->maxstacksize = read<uint8_t>(data, offset);
			p->numparams = read<uint8_t>(data, offset);
			p->nups = read<uint8_t>(data, offset);
			p->is_vararg = read<uint8_t>(data, offset);

			uint32_t sizecode = readLEB128(data, offset);
			p->code.reserve(sizecode);
			for (uint32_t j = 0; j < sizecode; j++)
				p->code.push_back(read<uint32_t>(data, offset));

			uint32_t sizek = readLEB128(data, offset);
			p->k.reserve(sizek);

			for (uint32_t j = 0; j < sizek; j++) {
				p->k.push_back(LuaValue());

				uint8_t constantType = read<uint8_t>(data, offset);
				LuaValue* constantValue = &p->k[j];
				switch (constantType) {
				case 0: { // nil
					constantValue->type = LUA_TNIL;
					break;
				}
				case 1: { // boolean
					uint8_t v = read<uint8_t>(data, offset);
					constantValue->type = LUA_TBOOLEAN;
					constantValue->value.boolean = v;
					break;
				}
				case 2: { // number
					double v = read<double>(data, offset);
					constantValue->type = LUA_TNUMBER;
					constantValue->value.number = v;
					break;
				}
				case 3: { // string
					uint32_t id = readLEB128(data, offset);
					constantValue->type = LUA_TSTRING;
					constantValue->value.str = std::string(stringTable[id - 1]);
					break;
				}
				case 4: { // import
					uint32_t iid = read<uint32_t>(data, offset);
					constantValue->type = LUA_TIMPORT;
					constantValue->value.import = dissect_import(iid, p->k);
					break;
				}
				case 5: { // table
					uint32_t keys = readLEB128(data, offset);
					for (uint32_t i = 0; i < keys; ++i) {
						uint32_t key = readLEB128(data, offset);
					}
					break;
				}
				case 6: { // closure
					readLEB128(data, offset); // fid
					break;
				}
				default: {
					throw std::exception("Unknown constant type");
					break;
				}
				}
			}

			uint32_t sizep = readLEB128(data, offset);
			p->p.reserve(sizep);
			for (uint32_t j = 0; j < sizep; j++) {
				p->p.push_back(0);
				p->p[j] = readLEB128(data, offset);
			}

			p->linedefined = readLEB128(data, offset);

			uint32_t debugname_id = readLEB128(data, offset);
			if (debugname_id)
				p->debugname = stringTable[debugname_id - 1];

			uint8_t lineinfo = read<uint8_t>(data, offset);
			if (lineinfo) {
				p->linegaplog2 = read<uint8_t>(data, offset);

				int intervals = ((sizecode - 1) >> p->linegaplog2) + 1;
				int absoffset = (sizecode + 3) & ~3;

				p->sizelineinfo = absoffset + intervals * sizeof(int);
				p->lineinfo = new uint8_t[p->sizelineinfo];
				p->abslineinfo = (int*)(p->lineinfo + absoffset);

				uint8_t lastoffset = 0;
				for (size_t j = 0; j < p->code.size(); j++) {
					lastoffset += read<uint8_t>(data, offset);
					p->lineinfo[j] = lastoffset;
				}

				int lastLine = 0;
				for (int j = 0; j < intervals; j++) {
					lastLine += read<uint32_t>(data, offset);
					p->abslineinfo[j] = lastLine;
				}
			}

			uint8_t debuginfo = read<uint8_t>(data, offset);
			if (debuginfo) {
				p->sizelocvars = readLEB128(data, offset);
				for (uint32_t j = 0; j < p->sizelocvars; j++) {
					readLEB128(data, offset);
					readLEB128(data, offset);
					readLEB128(data, offset);
					offset++;
				}

				p->sizeupvalues = readLEB128(data, offset);
				for (uint32_t j = 0; j < p->sizeupvalues; j++)
					readLEB128(data, offset);
			}

			protoTable.push_back(p);
		}

		uint32_t mainid = readLEB128(data, offset);

		return protoTable;
	}

	std::string listChildProtos(std::vector<uint32_t>& childProtoList, size_t listSize) {
		std::stringstream ss;
		ss << "\n; child protos: ";
		for (size_t i = 0; i < listSize; i++) {
			if (i != 0)
				ss << ", ";
			ss << childProtoList[i];
		}
		ss << "\n";
		return ss.str();
	}

	std::string getConstantString(LuaValue* constant) {
		switch (constant->type) {
		case LUA_TNIL: {
			return "nil";
		}
		case LUA_TBOOLEAN: {
			return constant->value.boolean != false ? "true" : "false";
		}
		case LUA_TSTRING: {
			return "'" + constant->value.str + "'";
		}
		case LUA_TNUMBER: {
			char buffer[20];
			sprintf_s(buffer, "%4.3f", constant->value.number);
			return std::string(buffer);
		}
		default: {
			return "unknown";
		}
		}
	}

	const char* CAPTURE_TYPES[3] = { "VAL", "REF", "UPVAL" };

	std::string getStringForInstruction(Proto* proto, size_t& pc, bool displayLineInfo) {
		std::vector<uint32_t>& code = proto->code;
		std::vector<LuaValue>& k = proto->k;

		uint32_t instruction = code[pc];
		uint32_t opcode = LUAU_INSN_OP(instruction);

		char instructionIndexTextBuffer[32];
		if (displayLineInfo)
			sprintf_s(instructionIndexTextBuffer, "L%i [%03i] ", getLineNumberFromPc(proto, pc), pc);
		else
			sprintf_s(instructionIndexTextBuffer, "[%03i] ", pc);

		std::string result = instructionIndexTextBuffer;

		switch (opcode) {
		case LOP_NOP: {
			char formattedInstruction[17];
			sprintf_s(formattedInstruction, "NOP (%#010X)", instruction);
			result += formattedInstruction;
			break;
		}
		case LOP_LOADNIL: {
			result += "LOADNIL " + std::to_string(LUAU_INSN_A(instruction));
			break;
		}
		case LOP_LOADB: {
			uint32_t targetRegister = LUAU_INSN_A(instruction);
			uint32_t boolValue = LUAU_INSN_B(instruction);
			uint32_t jumpOffset = LUAU_INSN_C(instruction);

			if (jumpOffset > 0) {
				char formattedInstruction[38];
				sprintf_s(
					formattedInstruction,
					"LOADB %i %i %i ; %s, jump to %i",
					targetRegister,
					boolValue,
					jumpOffset,
					boolValue != 0 ? "true" : "false",
					pc + jumpOffset + 1
				);
				result += formattedInstruction;
			}
			else {
				char formattedInstruction[20];
				sprintf_s(
					formattedInstruction,
					"LOADB %i %i ; %s",
					targetRegister,
					boolValue,
					boolValue != 0 ? "true" : "false"
				);
				result += formattedInstruction;
			}
			break;
		}
		case LOP_LOADN: {
			char formattedInstruction[17];
			sprintf_s(
				formattedInstruction,
				"LOADN %i %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_D(instruction)
			);
			result += formattedInstruction;
			break;
		}
		case LOP_LOADK: {
			int16_t constantIndex = LUAU_INSN_D(instruction);
			std::string constantString = getConstantString(&k[constantIndex]);
			char formattedInstruction[255];
			sprintf_s(
				formattedInstruction,
				"LOADK %i %i ; K(%i) = %s",
				LUAU_INSN_A(instruction),
				constantIndex,
				constantIndex,
				constantString.c_str()
			);
			result += formattedInstruction;
			break;
		}
		case LOP_MOVE: {
			char formattedInstruction[13];
			sprintf_s(
				formattedInstruction,
				"MOVE %i %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction)
			);
			result += formattedInstruction;
			break;
		}
		case LOP_GETGLOBAL: {
			char formattedInstruction[127];
			pc++;
			uint32_t aux = code[pc];
			sprintf_s(
				formattedInstruction,
				"GETGLOBAL %i %i ; K(%i) = '%s'",
				LUAU_INSN_A(instruction),
				aux,
				aux,
				k[aux].value.str.c_str()
			);
			result += formattedInstruction;
			break;
		}
		case LOP_SETGLOBAL: {
			char formattedInstruction[127];
			pc++;
			uint32_t aux = code[pc];
			sprintf_s(
				formattedInstruction,
				"SETGLOBAL %i %i ; K(%i) = '%s'",
				LUAU_INSN_A(instruction),
				aux,
				aux,
				k[aux].value.str.c_str()
			);
			result += formattedInstruction;
			break;
		}
		case LOP_GETUPVAL: {
			char formattedInstruction[18];
			sprintf_s(
				formattedInstruction,
				"GETUPVAL %i %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction)
			);
			result += formattedInstruction;
			break;
		}
		case LOP_SETUPVAL: {
			char formattedInstruction[18];
			sprintf_s(
				formattedInstruction,
				"SETUPVAL %i %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction)
			);
			result += formattedInstruction;
			break;
		}
		case LOP_CLOSEUPVALS: {
			result += "CLOSEUPVALS " + std::to_string(LUAU_INSN_A(instruction));
			break;
		}
		case LOP_GETIMPORT: {
			pc++;
			uint32_t aux = code[pc];;
			LuaImport import = dissect_import(aux, k);
			char formattedInstruction[127];
			sprintf_s(
				formattedInstruction,
				"GETIMPORT %i %i ; count = %i, '%s'",
				LUAU_INSN_A(instruction),
				LUAU_INSN_D(instruction),
				import.count,
				import.displayString.c_str()
			);
			result += formattedInstruction;
			break;
		}
		case LOP_GETTABLE: {
			char formattedInstruction[21];
			sprintf_s(
				formattedInstruction,
				"GETTABLE %i %i %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				LUAU_INSN_C(instruction)
			);
			result += formattedInstruction;
			break;
		}
		case LOP_SETTABLE: {
			char formattedInstruction[21];
			sprintf_s(
				formattedInstruction,
				"SETTABLE %i %i %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				LUAU_INSN_C(instruction)
			);
			result += formattedInstruction;
			break;
		}
		case LOP_GETTABLEKS: {
			pc++;
			uint32_t aux = code[pc];
			char formattedInstruction[127];
			sprintf_s(
				formattedInstruction,
				"GETTABLEKS %i %i %i ; K(%i) = '%s'",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				aux,
				aux,
				k[aux].value.str.c_str()
			);
			result += formattedInstruction;
			break;
		}
		case LOP_SETTABLEKS: {
			pc++;
			uint32_t aux = code[pc];
			char formattedInstruction[127];
			sprintf_s(
				formattedInstruction,
				"SETTABLEKS %i %i %i ; K(%i) = '%s'",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				aux,
				aux,
				k[aux].value.str.c_str()
			);
			result += formattedInstruction;
			break;
		}
		case LOP_GETTABLEN: {
			uint8_t argc = LUAU_INSN_C(instruction);
			char formattedInstruction[36];
			sprintf_s(
				formattedInstruction,
				"GETTABLEN %i %i %i ; index = %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				argc,
				argc + 1
			);
			result += formattedInstruction;
			break;
		}
		case LOP_SETTABLEN: {
			uint8_t argc = LUAU_INSN_C(instruction);
			char formattedInstruction[36];
			sprintf_s(
				formattedInstruction,
				"SETTABLEN %i %i %i ; index = %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				argc,
				argc + 1
			);
			result += formattedInstruction;
			break;
		}
		case LOP_NEWCLOSURE: {
			int16_t childProtoId = LUAU_INSN_D(instruction);
			char formattedInstruction[41];
			sprintf_s(
				formattedInstruction,
				"NEWCLOSURE %i %i ; global id = %i",
				LUAU_INSN_A(instruction),
				childProtoId,
				proto->p[childProtoId]
			);
			result += formattedInstruction;
			break;
		}
		case LOP_NAMECALL: {
			pc++;
			uint32_t aux = code[pc];
			char formattedInstruction[127];
			sprintf_s(
				formattedInstruction,
				"NAMECALL %i %i %i ; K(%i) = '%s'",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				aux,
				aux,
				k[aux].value.str.c_str()
			);
			result += formattedInstruction;
			break;
		}
		case LOP_CALL: {
			uint8_t nargs = LUAU_INSN_B(instruction);
			uint8_t nresults = LUAU_INSN_C(instruction);
			char formattedInstruction[54];
			sprintf_s(
				formattedInstruction,
				"CALL %i %i %i ; %s arguments, %s results",
				LUAU_INSN_A(instruction),
				nargs,
				nresults,
				nargs == 0 ? "MULTRET" : std::to_string(nargs - 1).c_str(),
				nresults == 0 ? "MULTRET" : std::to_string(nresults - 1).c_str()
			);
			result += formattedInstruction;
			break;
		}
		case LOP_RETURN: {
			uint8_t arga = LUAU_INSN_A(instruction);
			uint8_t argb = LUAU_INSN_B(instruction);
			char formattedInstruction[64];
			sprintf_s(
				formattedInstruction,
				"RETURN %i %i ; values start at %i, num returned values = %s",
				arga,
				argb,
				arga,
				argb == 0 ? "MULTRET" : std::to_string(argb - 1).c_str()
			);
			result += formattedInstruction;
			break;
		}
		case LOP_JUMP: {
			int16_t offset = LUAU_INSN_D(instruction);
			char formattedInstruction[24];
			sprintf_s(
				formattedInstruction,
				"JUMP %i ; to %i",
				offset,
				pc + offset + 1
			);
			result += formattedInstruction;
			break;
		}
		case LOP_JUMPBACK: {
			int16_t offset = LUAU_INSN_D(instruction);
			char formattedInstruction[24];
			sprintf_s(
				formattedInstruction,
				"JUMPBACK %i ; to %i",
				offset,
				pc + offset + 1
			);
			result += formattedInstruction;
			break;
		}
		case LOP_JUMPIF: {
			int16_t offset = LUAU_INSN_D(instruction);
			char formattedInstruction[30];
			sprintf_s(
				formattedInstruction,
				"JUMPIF %i %i ; to %i",
				LUAU_INSN_A(instruction),
				offset,
				pc + offset + 1
			);
			result += formattedInstruction;
			break;
		}
		case LOP_JUMPIFNOT: {
			int16_t offset = LUAU_INSN_D(instruction);
			char formattedInstruction[30];
			sprintf_s(
				formattedInstruction,
				"JUMPIFNOT %i %i ; to %i",
				LUAU_INSN_A(instruction),
				offset,
				pc + offset + 1
			);
			result += formattedInstruction;
			break;
		}
		case LOP_JUMPIFEQ: {
			int16_t offset = LUAU_INSN_D(instruction);
			pc++;
			uint32_t jumpTo = pc + offset;
			uint32_t aux = code[pc];
			char formattedInstruction[36];
			sprintf_s(
				formattedInstruction,
				"JUMPIFEQ %i %i %i ; to %i",
				LUAU_INSN_A(instruction),
				aux,
				offset,
				jumpTo
			);
			result += formattedInstruction;
			break;
		}
		case LOP_JUMPIFLE: {
			int16_t offset = LUAU_INSN_D(instruction);
			pc++;
			uint32_t jumpTo = pc + offset;
			uint32_t aux = code[pc];
			char formattedInstruction[36];
			sprintf_s(
				formattedInstruction,
				"JUMPIFLE %i %i %i ; to %i",
				LUAU_INSN_A(instruction),
				aux,
				offset,
				jumpTo
			);
			result += formattedInstruction;
			break;
		}
		case LOP_JUMPIFLT: {
			int16_t offset = LUAU_INSN_D(instruction);
			pc++;
			uint32_t jumpTo = pc + offset;
			uint32_t aux = code[pc];
			char formattedInstruction[36];
			sprintf_s(
				formattedInstruction,
				"JUMPIFLT %i %i %i ; to %i",
				LUAU_INSN_A(instruction),
				aux,
				offset,
				jumpTo
			);
			result += formattedInstruction;
			break;
		}
		case LOP_JUMPIFNOTEQ: {
			int16_t offset = LUAU_INSN_D(instruction);
			pc++;
			uint32_t jumpTo = pc + offset;
			uint32_t aux = code[pc];
			char formattedInstruction[36];
			sprintf_s(
				formattedInstruction,
				"JUMPIFNOTEQ %i %i %i ; to %i",
				LUAU_INSN_A(instruction),
				aux,
				offset,
				jumpTo
			);
			result += formattedInstruction;
			break;
		}
		case LOP_JUMPIFNOTLE: {
			int16_t offset = LUAU_INSN_D(instruction);
			pc++;
			uint32_t jumpTo = pc + offset;
			uint32_t aux = code[pc];
			char formattedInstruction[36];
			sprintf_s(
				formattedInstruction,
				"JUMPIFNOTLE %i %i %i ; to %i",
				LUAU_INSN_A(instruction),
				aux,
				offset,
				jumpTo
			);
			result += formattedInstruction;
			break;
		}
		case LOP_JUMPIFNOTLT: {
			int16_t offset = LUAU_INSN_D(instruction);
			pc++;
			uint32_t jumpTo = pc + offset;
			uint32_t aux = code[pc];
			char formattedInstruction[36];
			sprintf_s(
				formattedInstruction,
				"JUMPIFNOTLT %i %i %i ; to %i",
				LUAU_INSN_A(instruction),
				aux,
				offset,
				jumpTo
			);
			result += formattedInstruction;
			break;
		}
		case LOP_ADD: {
			char formattedInstruction[16];
			sprintf_s(
				formattedInstruction,
				"ADD %i %i %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				LUAU_INSN_C(instruction)
			);
			result += formattedInstruction;
			break;
		}
		case LOP_SUB: {
			char formattedInstruction[16];
			sprintf_s(
				formattedInstruction,
				"SUB %i %i %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				LUAU_INSN_C(instruction)
			);
			result += formattedInstruction;
			break;
		}
		case LOP_MUL: {
			char formattedInstruction[16];
			sprintf_s(
				formattedInstruction,
				"MUL %i %i %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				LUAU_INSN_C(instruction)
			);
			result += formattedInstruction;
			break;
		}
		case LOP_DIV: {
			char formattedInstruction[16];
			sprintf_s(
				formattedInstruction,
				"DIV %i %i %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				LUAU_INSN_C(instruction)
			);
			result += formattedInstruction;
			break;
		}
		case LOP_MOD: {
			char formattedInstruction[16];
			sprintf_s(
				formattedInstruction,
				"MOD %i %i %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				LUAU_INSN_C(instruction)
			);
			result += formattedInstruction;
			break;
		}
		case LOP_POW: {
			char formattedInstruction[16];
			sprintf_s(
				formattedInstruction,
				"POW %i %i %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				LUAU_INSN_C(instruction)
			);
			result += formattedInstruction;
			break;
		}
		case LOP_ADDK: {
			uint8_t constantIndex = LUAU_INSN_C(instruction);
			LuaValue& constantValue = k[constantIndex];
			char formattedInstruction[46];
			sprintf_s(
				formattedInstruction,
				"ADDK %i %i %i ; K(%i) = %4.3f",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				constantIndex,
				constantIndex,
				constantValue.value.number
			);
			result += formattedInstruction;
			break;
		}
		case LOP_SUBK: {
			uint8_t constantIndex = LUAU_INSN_C(instruction);
			LuaValue& constantValue = k[constantIndex];
			char formattedInstruction[46];
			sprintf_s(
				formattedInstruction,
				"SUBK %i %i %i ; K(%i) = %4.3f",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				constantIndex,
				constantIndex,
				constantValue.value.number
			);
			result += formattedInstruction;
			break;
		}
		case LOP_MULK: {
			uint8_t constantIndex = LUAU_INSN_C(instruction);
			LuaValue& constantValue = k[constantIndex];
			char formattedInstruction[46];
			sprintf_s(
				formattedInstruction,
				"MULK %i %i %i ; K(%i) = %4.3f",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				constantIndex,
				constantIndex,
				constantValue.value.number
			);
			result += formattedInstruction;
			break;
		}
		case LOP_DIVK: {
			uint8_t constantIndex = LUAU_INSN_C(instruction);
			LuaValue& constantValue = k[constantIndex];
			char formattedInstruction[46];
			sprintf_s(
				formattedInstruction,
				"DIVK %i %i %i ; K(%i) = %4.3f",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				constantIndex,
				constantIndex,
				constantValue.value.number
			);
			result += formattedInstruction;
			break;
		}
		case LOP_MODK: {
			uint8_t constantIndex = LUAU_INSN_C(instruction);
			LuaValue& constantValue = k[constantIndex];
			char formattedInstruction[46];
			sprintf_s(
				formattedInstruction,
				"MODK %i %i %i ; K(%i) = %4.3f",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				constantIndex,
				constantIndex,
				constantValue.value.number
			);
			result += formattedInstruction;
			break;
		}
		case LOP_POWK: {
			uint8_t constantIndex = LUAU_INSN_C(instruction);
			LuaValue& constantValue = k[constantIndex];
			char formattedInstruction[46];
			sprintf_s(
				formattedInstruction,
				"POWK %i %i %i ; K(%i) = %4.3f",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				constantIndex,
				constantIndex,
				constantValue.value.number
			);
			result += formattedInstruction;
			break;
		}
		case LOP_ANDK: {
			uint8_t constantIndex = LUAU_INSN_C(instruction);
			char formattedInstruction[127];
			sprintf_s(
				formattedInstruction,
				"ANDK %i %i %i ; K(%i) = %s",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				constantIndex,
				constantIndex,
				getConstantString(&k[constantIndex]).c_str()
			);
			result += formattedInstruction;
			break;
		}
		case LOP_ORK: {
			uint8_t constantIndex = LUAU_INSN_C(instruction);
			char formattedInstruction[127];
			sprintf_s(
				formattedInstruction,
				"ORK %i %i %i ; K(%i) = %s",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				constantIndex,
				constantIndex,
				getConstantString(&k[constantIndex]).c_str()
			);
			result += formattedInstruction;
			break;
		}
		case LOP_CONCAT: {
			char formattedInstruction[19];
			sprintf_s(
				formattedInstruction,
				"CONCAT %i %i %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				LUAU_INSN_C(instruction)
			);
			result += formattedInstruction;
			break;
		}
		case LOP_NOT: {
			char formattedInstruction[12];
			sprintf_s(
				formattedInstruction,
				"NOT %i %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction)
			);
			result += formattedInstruction;
			break;
		}
		case LOP_MINUS: {
			char formattedInstruction[14];
			sprintf_s(
				formattedInstruction,
				"MINUS %i %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction)
			);
			result += formattedInstruction;
			break;
		}
		case LOP_LENGTH: {
			char formattedInstruction[16];
			sprintf_s(
				formattedInstruction,
				"LENGTH %i %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction)
			);
			result += formattedInstruction;
			break;
		}
		case LOP_NEWTABLE: {
			pc++;
			uint32_t aux = code[pc];
			char formattedInstruction[24];
			sprintf_s(
				formattedInstruction,
				"NEWTABLE %i %i %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				aux
			);
			result += formattedInstruction;
			break;
		}
		case LOP_DUPTABLE: {
			char formattedInstruction[19];
			sprintf_s(
				formattedInstruction,
				"DUPTABLE %i %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_D(instruction)
			);
			result += formattedInstruction;
			break;
		}
		case LOP_SETLIST: {
			uint8_t sourceStart = LUAU_INSN_B(instruction);
			uint8_t argc = LUAU_INSN_C(instruction);
			pc++;
			uint32_t aux = code[pc];
			char formattedInstruction[127];
			sprintf_s(
				formattedInstruction,
				"SETLIST %i %i %i %i ; start at register %i, fill %s values, start at table index %i",
				LUAU_INSN_A(instruction),
				sourceStart,
				argc,
				aux,
				sourceStart,
				argc == 0 ? "MULTRET" : std::to_string(argc - 1).c_str(),
				aux
			);
			result += formattedInstruction;
			break;
		}
		case LOP_FORNPREP: {
			int16_t jumpOffset = LUAU_INSN_D(instruction);
			char formattedInstruction[32];
			sprintf_s(
				formattedInstruction,
				"FORNPREP %i %i ; to %i",
				LUAU_INSN_A(instruction),
				jumpOffset,
				pc + jumpOffset + 1
			);
			result += formattedInstruction;
			break;
		}
		case LOP_FORNLOOP: {
			int16_t jumpOffset = LUAU_INSN_D(instruction);
			char formattedInstruction[32];
			sprintf_s(
				formattedInstruction,
				"FORNLOOP %i %i ; to %i",
				LUAU_INSN_A(instruction),
				jumpOffset,
				pc + jumpOffset + 1
			);
			result += formattedInstruction;
			break;
		}
		case LOP_FORGPREP_INEXT: {
			int16_t jumpOffset = LUAU_INSN_D(instruction);
			char formattedInstruction[40];
			sprintf_s(
				formattedInstruction,
				"FORGPREP_INEXT %i %i ; to %i",
				LUAU_INSN_A(instruction),
				jumpOffset,
				pc + jumpOffset + 1
			);
			result += formattedInstruction;
			break;
		}
		case LOP_FORGLOOP_INEXT: {
			int16_t jumpOffset = LUAU_INSN_D(instruction);
			char formattedInstruction[40];
			sprintf_s(
				formattedInstruction,
				"FORGLOOP_INEXT %i %i ; to %i",
				LUAU_INSN_A(instruction),
				jumpOffset,
				pc + jumpOffset + 1
			);
			result += formattedInstruction;
			break;
		}
		case LOP_FORGPREP_NEXT: {
			int16_t jumpOffset = LUAU_INSN_D(instruction);
			char formattedInstruction[40];
			sprintf_s(
				formattedInstruction,
				"FORGPREP_NEXT %i %i ; to %i",
				LUAU_INSN_A(instruction),
				jumpOffset,
				pc + jumpOffset + 1
			);
			result += formattedInstruction;
			break;
		}
		case LOP_FORGLOOP_NEXT: {
			int16_t jumpOffset = LUAU_INSN_D(instruction);
			char formattedInstruction[40];
			sprintf_s(
				formattedInstruction,
				"FORGLOOP_NEXT %i %i ; to %i",
				LUAU_INSN_A(instruction),
				jumpOffset,
				pc + jumpOffset + 1
			);
			result += formattedInstruction;
			break;
		}
		case LOP_DUPCLOSURE: {
			char formattedInstruction[21];
			sprintf_s(
				formattedInstruction,
				"DUPCLOSURE %i %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_D(instruction)
			);
			result += formattedInstruction;
			break;
		}
		case LOP_PREPVARARGS: {
			result += "PREPVARARGS " + std::to_string(LUAU_INSN_A(instruction));
			break;
		}
		case LOP_FASTCALL: {
			uint8_t jumpOffset = LUAU_INSN_C(instruction);
			char formattedInstruction[30];
			sprintf_s(
				formattedInstruction,
				"FASTCALL %i %i ; to %i",
				LUAU_INSN_A(instruction),
				jumpOffset,
				pc + jumpOffset + 1
			);
			result += formattedInstruction;
			break;
		}
		case LOP_CAPTURE: {
			uint8_t captureTypeId = LUAU_INSN_A(instruction);
			const char* captureTypeString = CAPTURE_TYPES[captureTypeId];
			char formattedInstruction[30];
			sprintf_s(
				formattedInstruction,
				"CAPTURE %i %i ; %s capture",
				captureTypeId,
				LUAU_INSN_B(instruction),
				captureTypeString
			);
			result += formattedInstruction;
			break;
		}
		case LOP_JUMPIFEQK: {
			int16_t offset = LUAU_INSN_D(instruction);
			uint32_t jumpTo = pc + offset;
			pc++;
			uint32_t aux = code[pc];
			char formattedInstruction[127];
			sprintf_s(
				formattedInstruction,
				"JUMPIFEQK %i %i %i ; K(%i) = %s, to %i",
				LUAU_INSN_A(instruction),
				aux,
				offset,
				aux,
				getConstantString(&k[aux]).c_str(),
				jumpTo
			);
			result += formattedInstruction;
			break;
		}
		case LOP_JUMPIFNOTEQK: {
			int16_t offset = LUAU_INSN_D(instruction);
			uint32_t jumpTo = pc + offset;
			pc++;
			uint32_t aux = code[pc];
			char formattedInstruction[127];
			sprintf_s(
				formattedInstruction,
				"JUMPIFNOTEQK %i %i %i ; K(%i) = %s, to %i",
				LUAU_INSN_A(instruction),
				aux,
				offset,
				aux,
				getConstantString(&k[aux]).c_str(),
				jumpTo
			);
			result += formattedInstruction;
			break;
		}
		case LOP_FASTCALL1: {
			uint8_t jumpOffset = LUAU_INSN_C(instruction);
			char formattedInstruction[34];
			sprintf_s(
				formattedInstruction,
				"FASTCALL1 %i %i %i ; jump to %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				jumpOffset,
				pc + jumpOffset + 1
			);
			result += formattedInstruction;
			break;
		}
		case LOP_FASTCALL2: {
			uint8_t jumpOffset = LUAU_INSN_C(instruction);
			pc++;
			uint32_t aux = code[pc];
			char formattedInstruction[38];
			sprintf_s(
				formattedInstruction,
				"FASTCALL2 %i %i %i %i ; jump to %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				aux,
				jumpOffset,
				pc + jumpOffset
			);
			result += formattedInstruction;
			break;
		}
		case LOP_FASTCALL2K: {
			uint8_t jumpOffset = LUAU_INSN_C(instruction);
			pc++;
			uint32_t aux = code[pc];
			char formattedInstruction[127];
			sprintf_s(
				formattedInstruction,
				"FASTCALL2K %i %i %i %i ; K(%i) = %s, jump to %i",
				LUAU_INSN_A(instruction),
				LUAU_INSN_B(instruction),
				aux,
				jumpOffset,
				aux,
				getConstantString(&k[aux]).c_str(),
				pc + jumpOffset
			);
			result += formattedInstruction;
			break;
		}
		default: {
			result += "UNKNOWN";
			break;
		}
		}

		return result;
	}

	std::string disassemble(const char* bytecode, size_t bytecode_size, bool displayLineInfo) {
		std::string output;
		output.reserve(bytecode_size * 6);

		std::vector<Proto*> protoTable = deserialize_bytecode(bytecode);

		for (uint32_t protoId = 0; protoId < protoTable.size(); protoId++) {
			Proto* p = protoTable[protoId];

			char isVarargStringBuffer[3];
			sprintf_s(isVarargStringBuffer, "%0.2X", p->is_vararg);

			std::string header = std::format(R"(; global id: {}
; proto name: {}
; linedefined: {}

; maxstacksize: {}
; numparams: {}
; nups: {}
; is_vararg: {}
{}
; sizecode: {}
; sizek: {}
)",
protoId,
p->debugname,
p->linedefined,
p->maxstacksize,
p->numparams,
p->nups,
isVarargStringBuffer,
p->p.size() > 0 ? listChildProtos(p->p, p->p.size()) : "",
p->code.size(),
p->k.size()
);
			output += header;

			for (size_t i = 0; i < p->code.size(); i++) {
				output += getStringForInstruction(p, i, displayLineInfo) + '\n';
			}
		}

		protoTable.clear();

		return output;
	}

} // namespace LuauDisassembler
