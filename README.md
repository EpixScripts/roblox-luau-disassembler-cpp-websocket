## Client must support the following:
- `WebSocket` library

### Example usage
```lua
-- Assuming there is a running server and the client initialization script (client/client.lua) has ran
-- This would also need `getscriptbytecode` to exist, to be fed to the disassembler
writefile("output.txt", disassemble(getscriptbytecode(game.Players.LocalPlayer.PlayerScripts.LocalScript)))
```

The host of the server can be changed in `client/client.lua`.

# How to set up a server

## Clone the repository:
Since this project uses `websocketpp` as a submodule, the repository needs to be cloned with the `--recurse-submodules` flag:
```
git clone --recurse-submodules https://github.com/EpixScripts/roblox-luau-disassembler
```

## Configure:
Set your server port in `server/src/config.h`. The default port is `5395`.

You can also set the port when launching the server with the `-p` flag from the command line.

## Install Boost:
Boost is required to build this project because `boost.asio` is a dependency of `websocketpp`. You can get instructions on how to download and install it here:
https://www.boost.org/doc/libs/1_78_0/more/getting_started/index.html

Set the CMake variable `BOOST_ROOT` to where you installed your boost root to so the build can find it.