# Win32 Osiris compatibility host

`OsirisHost32.exe` allows the Win64 engine to load legacy 32-bit Osiris level
and game modules out of process. The Win64 engine detects an i386 PE module,
starts the host without a window, and forwards exports and supported engine
callbacks over a private named pipe.

The protocol never passes process-local pointers directly. Script instances,
files, and auto-managed memory use logical tokens. Auto-managed memory is
mirrored at event boundaries so engine save data and the pointer visible to a
Win32 script remain coherent. Nested requests are supported because a script
callback can synchronously call back into the engine.

Build both architectures. The Win64 build produces `PiccuEngine.exe`; the
Win32 build produces `osiris_bridge/OsirisHost32.exe`. Ship the host beside the
Win64 executable.

The bridge fails closed when a legacy module invokes a callback that has no
typed wire representation. Canned cinematics are supported. The general
`Cine_Start` API is intentionally not yet supported because its structure can
contain a Win32 function pointer; it needs a dedicated reverse-callback
adapter. Other unexercised callbacks may likewise require additional typed
marshalling.

`OsirisBridgeTestLevel` builds the same synthetic level module for Win32 and
Win64. It exercises initialization, timers, mission flags, `msafe` access,
event and module serialization, and auto-managed memory. For an end-to-end
mission, package either DLL as `BridgeTest.dll` with a test mine named
`BridgeTest.d3l` and the supplied `test_mission/bridge_test.msn` (renamed to
match the mission archive's 8.3-compatible basename).
