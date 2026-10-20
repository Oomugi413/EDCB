#include "EpgTimerSrv/EpgTimerSrv/stdafx.h"
#include "EpgTimerSrv/EpgTimerSrv/EpgTimerSrvSetting.h"
#include "Common/EpgTimerUtil.h"
#include "Common/TimeUtil.h"
#include "Common/ThreadUtil.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <set>

// Suppress persistent debug logging from the real INI utility.
void AddDebugLogNoNewline(const wchar_t*, bool) {}

// No tuner, server, network, or persistence object is constructed.
class CEpgDBManager {
public:
    vector<EPGDB_EVENT_INFO> events;
    template<class Callback>
    void SearchEpg(const EPGDB_SEARCH_KEY_INFO*, int, LONGLONG, LONGLONG,
                   void*, Callback callback) const {
        for (const auto& event : events) callback(&event, nullptr);
    }
};
template<class T> struct Rules {
    map<DWORD, T> data;
    const map<DWORD, T>& GetMap() const { return data; }
};
struct ReservationSink {
    vector<RESERVE_DATA> input, changed;
    vector<DWORD> deleted;
    vector<RESERVE_DATA> GetReserveDataAll() const { return input; }
    void DelReserveData(const vector<DWORD>& ids) { deleted = ids; }
    bool ChgReserveData(const vector<RESERVE_DATA>& values) { changed = values; return true; }
};
class CEpgTimerSrvMain {
public:
    CEpgTimerSrvSetting::SETTING setting{};
    recursive_mutex_ settingLock;
    ReservationSink reserveManager;
    CEpgDBManager epgDB;
    Rules<EPG_AUTO_ADD_DATA> epgAutoAdd;
    Rules<MANUAL_AUTO_ADD_DATA> manualAutoAdd;
    vector<RESERVE_DATA>& PreChgReserveData(vector<RESERVE_DATA>& values) { return values; }
    bool SyncChangeAutoAddReserveData(const vector<EPG_AUTO_ADD_DATA>&,
                                     const vector<MANUAL_AUTO_ADD_DATA>&);
};
static LONGLONG fakeNow;
static int nowCalls;
static LONGLONG TestNow() { ++nowCalls; return fakeNow; }
#define GetNowI64Time TestNow
#include "production.inc"
#undef GetNowI64Time

static int checks;
static void require(bool ok, const string& name) {
    ++checks;
    if (!ok) throw std::runtime_error("FAIL: " + name);
}
static string iniPath;
static CEpgTimerSrvSetting::SETTING load(const string& text) {
    { std::ofstream file(iniPath); file << "[SET]\n" << text;
      if (!file) throw std::runtime_error("fixture write failed"); }
    return CEpgTimerSrvSetting::LoadSetting(wstring(iniPath.begin(), iniPath.end()).c_str());
}
static void settingsTests() {
    for (const string& value : {"", "-1", "bad", "5x", "1.5", "2147483648",
                                "9999999999999999999999999999", "+", "-"}) {
        require(load("CautionOnRecMarginMin=" + value + "\n").cautionOnRecMarginMin == 5,
                "invalid minutes: " + value);
    }
    for (const auto& item : vector<pair<string, int>>{{"0", 0}, {"1", 1}, {"10", 10},
                                 {"  +12  ", 12}, {"2147483647", INT_MAX}}) {
        require(load("CautionOnRecMarginMin=" + item.first + "\n").cautionOnRecMarginMin == item.second,
                "valid minutes: " + item.first);
    }
    for (const auto& item : vector<pair<string, bool>>{{"0", false}, {" 0 ", false},
         {"1", true}, {"-1", true}, {"-2147483648", true}, {"2147483647", true},
         {"", true}, {"bad", true}, {"0x", true}, {"2147483648", true}}) {
        require(load("CautionOnRecChange=" + item.first + "\n").cautionOnRecChange == item.second,
                "boolean: " + item.first);
    }
    for (int minutes : {5, 1, 0}) {
        require(load("CautionOnRecMarginMin=" + std::to_string(minutes)).cautionOnRecMarginMin == minutes,
                "repeated LoadSetting");
    }
    const auto defaults = load("");
    require(defaults.cautionOnRecChange && defaults.cautionOnRecMarginMin == 5, "removed keys default");
    require(!defaults.syncResAutoAddChange && !defaults.syncResAutoAddChgNewRes, "sync defaults unchanged");
}

struct Fixture {
    CEpgTimerSrvMain sys;
    EPG_AUTO_ADD_DATA epg{};
    MANUAL_AUTO_ADD_DATA manual{};
    bool isEpg;
    Fixture(bool epgMode, DWORD duration = 1800) : isEpg(epgMode) {
        sys.setting = load("");
        sys.setting.syncResAutoAddChange = true;
        sys.setting.syncResAutoAddChgNewRes = true;
        RESERVE_DATA r{};
        r.reserveID = 1; r.comment = L"auto"; r.title = L"old";
        r.startTime = {2026, 9, 1, 7, 12, 0, 0, 0};
        r.durationSecond = duration; r.eventID = epgMode ? 100 : 0xFFFF;
        r.recSetting.useMargineFlag = 1; r.recSetting.priority = 1;
        sys.reserveManager.input.push_back(r);
        epg.dataID = manual.dataID = 1;
        epg.recSetting = manual.recSetting = r.recSetting;
        manual.dayOfWeekFlag = 127; manual.durationSecond = duration;
        manual.startTime = 12 * 3600; manual.title = L"new";
        sys.epgAutoAdd.data[1] = epg;
        sys.manualAutoAdd.data[1] = manual;
        if (epgMode) sys.manualAutoAdd.data.clear(); else sys.epgAutoAdd.data.clear();
        EPGDB_EVENT_INFO event{};
        event.original_network_id = r.originalNetworkID;
        event.transport_stream_id = r.transportStreamID;
        event.service_id = r.serviceID;
        event.durationSec = duration; event.freeCAFlag = 0;
        event.StartTimeFlag = event.DurationFlag = 1;
        event.event_id = r.eventID; event.start_time = r.startTime;
        sys.epgDB.events.push_back(event);
        setting().priority = 4;
    }
    REC_SETTING_DATA& setting() { return isEpg ? epg.recSetting : manual.recSetting; }
    void run(LONGLONG secondsBeforeNoon, LONGLONG tickOffset = 0) {
        fakeNow = ConvertI64Time(sys.reserveManager.input[0].startTime) - secondsBeforeNoon * I64_1SEC + tickOffset;
        nowCalls = 0;
        require(sys.SyncChangeAutoAddReserveData(isEpg ? vector<EPG_AUTO_ADD_DATA>{epg} : vector<EPG_AUTO_ADD_DATA>{},
                         isEpg ? vector<MANUAL_AUTO_ADD_DATA>{} : vector<MANUAL_AUTO_ADD_DATA>{manual}), "sync return");
        require(nowCalls == (sys.setting.syncResAutoAddChange && sys.setting.syncResAutoAddChgNewRes ? 1 : 0), "single clock snapshot");
    }
    void outcome(bool deleted, const string& name) {
        if (sys.reserveManager.deleted != (deleted ? vector<DWORD>{1} : vector<DWORD>{})) {
            std::cerr << "mode=" << isEpg << " deleted=" << sys.reserveManager.deleted.size()
                      << " changed=" << sys.reserveManager.changed.size()
                      << " delta=" << (CalcReserveStartTime(sys.reserveManager.input[0], sys.setting.startMargin) - fakeNow) / I64_1SEC
                      << " minutes=" << sys.setting.cautionOnRecMarginMin << '\n';
        }
        require(sys.reserveManager.deleted == (deleted ? vector<DWORD>{1} : vector<DWORD>{}), name + " deletion IDs");
        require(sys.reserveManager.changed.size() == (deleted ? 0U : 1U), name + " change retained");
        if (!deleted) {
            require(sys.reserveManager.changed[0].reserveID == 1 && sys.reserveManager.changed[0].recSetting.priority == 4,
                    name + " ID/settings retained");
            if (!isEpg) require(sys.reserveManager.changed[0].title == L"new", name + " program title");
        }
    }
};

static void timeTests(bool epg) {
    struct Case { const char* name; int before; bool del; int margin = 0; int common = 0;
        int flag = 1; DWORD duration = 1800; bool caution = true; int minutes = 5; int old = 0; };
    const Case cases[] = {
        {"T01",301,true}, {"T02",300,false}, {"T03",299,false},
        {"T04",61,true,0,0,1,1800,false,10}, {"T05",60,false,0,0,1,1800,false,10},
        {"T06a",1,true,0,0,1,1800,true,0}, {"T06b",0,false,0,0,1,1800,true,0},
        {"T07",600,false,0,0,1,1800,true,10}, {"T08",90,false,120},
        {"T09",90,false,0,120,0}, {"T10",301,true,0,600}, {"T11",301,true,600,0,0},
        {"T12a",181,true,-120}, {"T12b",180,false,-120},
        {"T13a",241,true,-120,0,1,60}, {"T13b",240,false,-120,0,1,60},
        {"T14a",301,true,0,0,1,0}, {"T14b",300,false,0,0,1,0},
        {"T16",330,false,600}, {"T17",330,true,0,0,1,1800,true,5,600},
        {"T18",330,false,0,600,0}, {"T19",-1860,false},
        {"T20",301,false,0,0,1,1800,true,INT_MAX},
        {"large-duration",301,true,INT_MIN,0,1,0xFFFFFFFF},
        {"zero-duration-negative",300,false,INT_MIN,0,1,0}
    };
    for (const auto& c : cases) {
        Fixture f(epg, c.duration);
        f.setting().startMargine = c.margin; f.setting().useMargineFlag = c.flag;
        f.sys.setting.startMargin = c.common;
        f.sys.setting.cautionOnRecChange = c.caution; f.sys.setting.cautionOnRecMarginMin = c.minutes;
        f.sys.reserveManager.input[0].recSetting.startMargine = c.old;
        f.run(c.before); f.outcome(c.del, c.name);
    }
    for (int end : {-600, 600}) {
        Fixture f(epg); f.setting().endMargine = end; f.run(300); f.outcome(false, "T15");
    }
    for (int tick : {-1, 0, 1}) {
        Fixture f(epg); f.run(300, tick); f.outcome(tick < 0, "100ns boundary");
    }
}
static void integrationTests(bool epg) {
    for (const auto& config : vector<pair<string, bool>>{{"CautionOnRecMarginMin=5\n", false},
         {"CautionOnRecMarginMin=1\n", true}, {"CautionOnRecMarginMin=0\n", true}, {"", false}}) {
      Fixture f(epg);
      f.sys.setting = load(config.first + "SyncResAutoAddChange=1\nSyncResAutoAddChgNewRes=1\n");
      f.run(120); f.outcome(config.second, "INI to sync decision");
    }
    { Fixture f(epg); f.setting().startMargine = 120; f.run(90); f.outcome(false, "I01/I02"); }
    { Fixture f(epg); auto r = f.sys.reserveManager.input[0]; r.reserveID = 2;
      r.startTime.wDay += 7; f.sys.reserveManager.input.push_back(r);
      auto event = f.sys.epgDB.events[0]; event.start_time = r.startTime;
      f.sys.epgDB.events.push_back(event); f.run(300);
      require(f.sys.reserveManager.deleted == vector<DWORD>{2}, "I03 mixed deletion");
      require(f.sys.reserveManager.changed.size() == 1 && f.sys.reserveManager.changed[0].reserveID == 1, "I03 retained"); }
    { Fixture f(epg); f.sys.reserveManager.input[0].recSetting.recMode = 7;
      f.run(900); f.outcome(false, "I04"); require(f.sys.reserveManager.changed[0].recSetting.IsNoRec(), "I04 disabled retained"); }
    { Fixture f(epg); f.setting().recMode = 7; f.run(900); f.outcome(false, "I05"); }
    { Fixture f(epg); f.sys.setting.syncResAutoAddChange = false; f.run(900);
      require(f.sys.reserveManager.deleted.empty() && f.sys.reserveManager.changed.empty(), "I06 sync off"); }
    { Fixture f(epg); f.sys.setting.syncResAutoAddChgNewRes = false; f.sys.setting.syncResAutoAddChgKeepRecTag = true;
      f.sys.reserveManager.input[0].recSetting.batFilePath = L"old*tag"; f.setting().batFilePath = L"new*other";
      f.run(900); f.outcome(false, "I07"); require(f.sys.reserveManager.changed[0].recSetting.batFilePath == L"new*tag", "I07 tag"); }
    { Fixture f(epg); if (epg) { auto r = f.epg; r.dataID = 2; f.sys.epgAutoAdd.data[2] = r; }
      else { auto r = f.manual; r.dataID = 2; f.sys.manualAutoAdd.data[2] = r; }
      f.run(900); f.outcome(false, "I08 other enabled rule"); }
    for (auto comment : {L"", L"auto$"}) {
      Fixture f(epg); f.sys.reserveManager.input[0].comment = comment; f.run(900);
      require(f.sys.reserveManager.deleted.empty() && f.sys.reserveManager.changed.empty(), "I09 detached"); }
    for (bool separate : {false, true}) {
      Fixture f(epg); f.sys.setting.separateFixedTuners = separate; f.setting().tunerID = 2;
      f.run(300); f.outcome(false, "I10 old tuner matching");
      require(f.sys.reserveManager.changed[0].recSetting.tunerID == 2, "I10 tuner changed"); }
    { Fixture f(epg); if (epg) f.epg.searchInfo.andKey = L"^!{999}"; else f.manual.dayOfWeekFlag = 0;
      f.run(900); require(f.sys.reserveManager.changed.empty() && f.sys.reserveManager.deleted.empty(), "I11 disabled rule"); }
    for (bool deletion : {false, true}) {
      Fixture f(epg);
      fakeNow = ConvertI64Time(f.sys.reserveManager.input[0].startTime) - (deletion ? 900 : 300) * I64_1SEC;
      auto secondEpg = f.epg; auto secondManual = f.manual;
      secondEpg.dataID = secondManual.dataID = 2;
      secondEpg.recSetting.priority = secondManual.recSetting.priority = 2;
      if (epg) f.sys.epgAutoAdd.data[2] = secondEpg; else f.sys.manualAutoAdd.data[2] = secondManual;
      require(f.sys.SyncChangeAutoAddReserveData(epg ? vector<EPG_AUTO_ADD_DATA>{f.epg, secondEpg} : vector<EPG_AUTO_ADD_DATA>{},
              epg ? vector<MANUAL_AUTO_ADD_DATA>{} : vector<MANUAL_AUTO_ADD_DATA>{f.manual, secondManual}), "I13 batch return");
      f.outcome(deletion, "I13 unique IDs and first settings win");
    }
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("INI fixture path required");
        iniPath = argv[1]; settingsTests();
        for (bool epg : {false, true}) { timeTests(epg); integrationTests(epg); }
        std::cout << "PASS: " << checks << " checks (real sync body/helpers, real INI reader, UBSan)\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
