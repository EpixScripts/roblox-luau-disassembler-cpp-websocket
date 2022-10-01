local WebSocket = WebSocket or (syn and syn.websocket)
assert(WebSocket, "Disassembler requires WebSocket library")

local DisassemblerSocket = WebSocket.connect("ws://localhost:5395") -- Change if using a different host

-- Since the synapse websocket library doesn't support raw binary data,
-- we need to first encode in Base64 before sending the data over the wire.
-- The WebSocket protocol states that a connection must be closed if invalid
-- UTF-8 is sent with the text opcode, so we need to be careful to not send invalid data.
local isSynapse = identifyexecutor and string.find(identifyexecutor(), "^Synapse") ~= nil

getgenv().disassemble = function(bytecode)
	assert(type(bytecode) == "string", "Argument #1 to disassemble must be a string")

	if isSynapse then
		local encodedBytecode = syn.crypt.base64.encode(bytecode)
		DisassemblerSocket:Send(encodedBytecode)
	else
		DisassemblerSocket:Send(bytecode)
	end

	return DisassemblerSocket.OnMessage:Wait()
end