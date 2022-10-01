#include <string>
#include <iostream>

#include "disassembler/disassembler.hpp"
#include "config.hpp"

#include "websocketpp/server.hpp"
#include "websocketpp/config/asio_no_tls.hpp"

using server = websocketpp::server<websocketpp::config::asio>;

int main(int argc, char* argv[]) {
	uint16_t port = DISASSEMBLER_DEFAULT_SERVER_PORT;

	if (argc > 1) { // Check for arguments
		std::string flag = std::string(argv[1]);
		if ((flag == "-p" || flag == "--port") && argc == 3) {
			port = std::stoi(std::string(argv[2]), nullptr, 10);
			if (!port) return 1;
		}
	}

	std::cout << "Starting server on port " << port << '\n';

	server s;

	// Set logging settings
	s.set_access_channels(websocketpp::log::alevel::all);
	s.clear_access_channels(websocketpp::log::alevel::frame_payload);

	// Initialize ASIO
	s.init_asio();
	s.set_reuse_addr(true);

	// Register our message handler
	s.set_message_handler([&](websocketpp::connection_hdl hdl, server::message_ptr msg) {
		const std::string& payload = msg->get_payload();
		websocketpp::frame::opcode::value opcode = msg->get_opcode();

		// Some client websocket interfaces don't support sending binary data, like Synapse X, so they are Base64 encoded
		// We can tell if a message is meant to be binary or text based on the message's opcode
		if (opcode == websocketpp::frame::opcode::binary) {
			std::string disassembly = LuauDisassembler::disassemble(payload.c_str(), payload.size(), false);
			s.send(hdl, disassembly, websocketpp::frame::opcode::text);
		} else if (opcode == websocketpp::frame::opcode::text) {
			std::string decoded = websocketpp::base64_decode(payload);
			std::string disassembly = LuauDisassembler::disassemble(decoded.c_str(), decoded.size(), false);
			s.send(hdl, disassembly, websocketpp::frame::opcode::text);
		}
	});

	// Listen on port defined in config.h
	s.listen(port);

	// Queues a connection accept operation
	s.start_accept();

	// Start the Asio io_service run loop
	s.run();

	std::cin.get();
}