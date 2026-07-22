#pragma once

#include "../scripts/osiris_common.h"

#include <cstdint>
#include <memory>

class OsirisBridgeClient {
public:
  static std::unique_ptr<OsirisBridgeClient> Start(const char *module_path);
  ~OsirisBridgeClient();

  bool Initialize(tOSIRISModuleInit *init);
  void Shutdown();
  int GetGOScriptID(const char *name, ubyte is_door);
  int GetTriggerScriptID(int room, int face);
  int GetCOScriptList(int **handles, int **ids);
  void *CreateInstance(int id);
  void DestroyInstance(int id, void *instance);
  short CallInstanceEvent(int id, void *instance, int event, tOSIRISEventInfo *data);
  int SaveRestoreState(void *file, ubyte saving);
  void ForgetMemoryForScript(const tOSIRISSCRIPTID *script);

  bool IsAlive() const;
  static bool IsWin32Module(const char *path);

private:
  OsirisBridgeClient();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
