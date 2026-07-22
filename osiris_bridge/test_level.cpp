#include "osiris_import.h"

#include <cstdint>
#include <cstring>

#ifdef _MSC_VER
#define STDCALL __stdcall
#else
#define STDCALL __attribute__((stdcall))
#endif

namespace {

constexpr uint32_t kSaveMagic = 0x42524944; // "BRID"
int g_intervals = 0;
int g_timer_events = 0;
int g_saved_value = 0;
int *g_managed_value = nullptr;
int g_cinematic_frames = 0;

void BridgeTestCinematicCallback(int type) {
  mprintf(0, "BRIDGE_TEST cinematic-callback type=%d frame=%d\n", type,
          g_cinematic_frames);
  if (type == GCCT_FRAME && ++g_cinematic_frames == 3)
    Cine_Stop();
}

class BridgeTestScript {
public:
  short CallEvent(int event, tOSIRISEventInfo *data) {
    switch (event) {
    case EVT_LEVELSTART: {
      Msn_FlagSet(31, 1);
      const int flag = Msn_FlagGet(31);
      mprintf(0, "BRIDGE_TEST level-start flag=%d difficulty=%d language=%d\n",
              flag, static_cast<int>(Game_GetDiffLevel()), Game_GetLanguage());
      int goal_count = -1;
      LGoal_Value(VF_GET, LGV_I_NUM_GOALS, &goal_count);
      mprintf(0, "BRIDGE_TEST level-goals=%d\n", goal_count);
      tOSIRISTIMER timer{};
      timer.flags = OTF_LEVEL;
      timer.id = 0x4252;
      timer.repeat_count = 0;
      timer.timer_interval = 0.10f;
      const int timer_handle = Scrpt_CreateTimer(&timer);
      mprintf(0, "BRIDGE_TEST timer=%d\n", timer_handle);
      tOSIRISMEMCHUNK chunk{};
      chunk.my_id.type = LEVEL_SCRIPT;
      chunk.id = 7;
      chunk.size = sizeof(*g_managed_value);
      g_managed_value = static_cast<int *>(Scrpt_MemAlloc(&chunk));
      if (g_managed_value)
        *g_managed_value = 1000;

      msafe_struct player{};
      player.slot = 0;
      MSafe_GetValue(MSAFE_OBJECT_PLAYER_HANDLE, &player);
      if (player.objhandle != OBJECT_HANDLE_NONE) {
        MSafe_GetValue(MSAFE_OBJECT_POS, &player);
        const vector position = player.pos;
        MSafe_GetValue(MSAFE_OBJECT_ORIENT, &player);
        matrix orientation = player.orient;
        MSafe_GetValue(MSAFE_OBJECT_ROOMNUM, &player);

        tGameCinematic cinematic{};
        cinematic.flags = GCF_FULLSCREEN | GCF_USEPOINT | GCF_TEXT_NOEFFECT |
                          GCF_LAYOUT_BOTTOM;
        cinematic.target_objhandle = player.objhandle;
        cinematic.position = position;
        cinematic.orient = &orientation;
        cinematic.room = player.roomnum;
        cinematic.max_time_play = 1.0f;
        cinematic.callback = BridgeTestCinematicCallback;
        cinematic.text_display = {0.0f, 0.0f};
        cinematic.track_target = {0.0f, 0.0f};
        cinematic.player_disabled = {0.0f, 1.0f};
        cinematic.in_camera_view = {0.0f, 1.0f};
        cinematic.quick_exit = {1.0f, 1.0f};
        const bool started = Cine_Start(&cinematic, const_cast<char *>(""));
        mprintf(0, "BRIDGE_TEST cinematic-started=%d\n", started ? 1 : 0);
      }
      break;
    }
    case EVT_INTERVAL:
      ++g_intervals;
      if (g_managed_value)
        ++*g_managed_value;
      if (g_intervals == 10) {
        msafe_struct value{};
        value.roomnum = 0;
        MSafe_GetValue(MSAFE_ROOM_FOG_STATE, &value);
        mprintf(0, "BRIDGE_TEST interval=10 fog-state=%d game=%.3f frame=%.6f\n",
                static_cast<int>(value.state), Game_GetTime(), Game_GetFrameTime());
      }
      break;
    case EVT_MEMRESTORE:
      if (data->evt_memrestore.id == 7) {
        g_managed_value = static_cast<int *>(data->evt_memrestore.memory_ptr);
        mprintf(0, "BRIDGE_TEST memory-restore value=%d\n",
                g_managed_value ? *g_managed_value : -1);
      }
      break;
    case EVT_TIMER:
      ++g_timer_events;
      mprintf(0, "BRIDGE_TEST timer-event id=%d count=%d\n", data->evt_timer.id, g_timer_events);
      break;
    case EVT_SAVESTATE:
      File_WriteInt(static_cast<int>(kSaveMagic), data->evt_savestate.fileptr);
      File_WriteInt(g_intervals, data->evt_savestate.fileptr);
      File_WriteInt(g_timer_events, data->evt_savestate.fileptr);
      File_WriteInt(g_saved_value, data->evt_savestate.fileptr);
      mprintf(0, "BRIDGE_TEST save intervals=%d timers=%d\n", g_intervals, g_timer_events);
      break;
    case EVT_RESTORESTATE: {
      const uint32_t magic = static_cast<uint32_t>(File_ReadInt(data->evt_restorestate.fileptr));
      g_intervals = File_ReadInt(data->evt_restorestate.fileptr);
      g_timer_events = File_ReadInt(data->evt_restorestate.fileptr);
      g_saved_value = File_ReadInt(data->evt_restorestate.fileptr);
      mprintf(0, "BRIDGE_TEST restore magic=%08x intervals=%d timers=%d\n",
              magic, g_intervals, g_timer_events);
      break;
    }
    }
    return CONTINUE_CHAIN | CONTINUE_DEFAULT;
  }
};

} // namespace

extern "C" {

char STDCALL InitializeDLL(tOSIRISModuleInit *init) {
  osicommon_Initialize(init);
  if (init->game_checksum != CHECKSUM)
    return 0;
  g_intervals = 0;
  g_timer_events = 0;
  g_saved_value = 0x12345678;
  g_managed_value = nullptr;
  g_cinematic_frames = 0;
  mprintf(0, "BRIDGE_TEST initialize pointer-size=%u\n", static_cast<unsigned>(sizeof(void *)));
  return 1;
}

void STDCALL ShutdownDLL() { mprintf(0, "BRIDGE_TEST shutdown\n"); }
int STDCALL GetGOScriptID(char *, ubyte) { return -1; }
int STDCALL GetTriggerScriptID(int, int) { return -1; }
int STDCALL GetCOScriptList(int **handles, int **ids) {
  *handles = nullptr;
  *ids = nullptr;
  return 0;
}
void *STDCALL CreateInstance(int id) { return id == 0 ? new BridgeTestScript : nullptr; }
void STDCALL DestroyInstance(int id, void *instance) {
  if (id == 0)
    delete static_cast<BridgeTestScript *>(instance);
}
short STDCALL CallInstanceEvent(int id, void *instance, int event, tOSIRISEventInfo *data) {
  return id == 0 && instance ? static_cast<BridgeTestScript *>(instance)->CallEvent(event, data)
                             : CONTINUE_CHAIN | CONTINUE_DEFAULT;
}
int STDCALL SaveRestoreState(void *file, ubyte saving) {
  if (saving) {
    File_WriteInt(static_cast<int>(kSaveMagic), file);
    File_WriteInt(g_saved_value, file);
  } else {
    const uint32_t magic = static_cast<uint32_t>(File_ReadInt(file));
    g_saved_value = File_ReadInt(file);
    mprintf(0, "BRIDGE_TEST module-restore magic=%08x value=%08x\n", magic,
            static_cast<uint32_t>(g_saved_value));
  }
  return static_cast<int>(sizeof(uint32_t) * 2);
}

} // extern "C"
