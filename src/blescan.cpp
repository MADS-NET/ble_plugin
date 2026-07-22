/*
  ____                                   _             _
 / ___|  ___  _   _ _ __ ___ ___   _ __ | |_   _  __ _(_)_ __
 \___ \ / _ \| | | | '__/ __/ _ \ | '_ \| | | | |/ _` | | '_ \
  ___) | (_) | |_| | | | (_|  __/ | |_) | | |_| | (_| | | | | |
 |____/ \___/ \__,_|_|  \___\___| | .__/|_|\__,_|\__, |_|_| |_|
                                  |_|            |___/
Continuously scans for BLE advertisements and publishes all
publicly-broadcast information (address, name, RSSI, tx power,
manufacturer data, advertised services) for every visible device.
*/
// Mandatory included headers
#include <source.hpp>
#include <filter.hpp>
#include <nlohmann/json.hpp>
#include <pugg/Kernel.h>

// other includes as needed here
#include <simpleble/SimpleBLE.h>
#include <company_ids.hpp>
#include <atomic>
#include <csignal>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <unordered_map>

#ifdef __APPLE__
// SimpleBLE's macOS backend calls into Objective-C for several Peripheral
// accessors (e.g. identifier()/address() message-send into CBPeripheral and
// return autoreleased NSStrings, bridged to std::string). A normal Cocoa app
// drains its autorelease pool once per run-loop turn; this plugin is a bare
// C++ program with no run loop, so on the thread that polls these accessors
// nothing would ever drain the pool and every autoreleased object piles up
// forever. Confirmed by measuring RSS: without draining, memory climbed
// linearly and indefinitely; wrapping the accessor-heavy work below in a
// pool made it plateau. These two calls are the C ABI entry points that
// `@autoreleasepool { ... }` compiles down to, usable from plain C++
// (a .cpp file can't use the @autoreleasepool keyword directly).
extern "C" {
void *objc_autoreleasePoolPush(void);
void objc_autoreleasePoolPop(void *ctxt);
}
struct ScopedAutoreleasePool {
  void *pool = objc_autoreleasePoolPush();
  ~ScopedAutoreleasePool() { objc_autoreleasePoolPop(pool); }
};
#endif

// Load the namespaces
using namespace std;
using json = nlohmann::json;
using namespace SimpleBLE;
using namespace std::chrono_literals;


// Plugin class. This shall be the only part that needs to be modified,
// implementing the actual functionality
class BleScanPlugin : public Source<json> {

public:
  BleScanPlugin() = default;

  ~BleScanPlugin() override {
    try {
      if (_adapter.initialized() && _adapter.scan_is_active()) {
        _adapter.scan_stop();
      }
    } catch (...) {
      // never let an exception escape a destructor
    }
  }

  // Typically, no need to change this
  string kind() override { return "ble_scan"; }

  return_type get_output(json &out, vector<unsigned char> *blob = nullptr) override {
    out.clear();

    // Self-healing: (re)start the scan if it is not currently active. Rate
    // limited so a persistently unavailable adapter does not get hammered.
    if (!_adapter.initialized() || !_adapter.scan_is_active()) {
      auto now = chrono::steady_clock::now();
      if (now - _last_restart_attempt < 1000ms) {
        return return_type::retry;
      }
      _last_restart_attempt = now;
      try {
        if (!_params["silent"])
          cerr << "[blescan] scan not active, restarting..." << endl;
        start_scan();
      } catch (exception &e) {
        if (!_params["silent"])
          cerr << "[blescan] failed to (re)start scan: " << e.what() << endl;
        return return_type::retry;
      }
    }

    // Periodically rebuild the adapter/scan session even when healthy:
    // SimpleBLE never forgets a peripheral it has seen for the life of the
    // Adapter object, so a long-running scan otherwise accumulates memory
    // without bound regardless of what we do with our own bookkeeping below.
    long long adapter_refresh = _params.value("adapter_refresh", 600LL);
    if (adapter_refresh > 0 &&
        chrono::steady_clock::now() - _last_adapter_refresh >= chrono::seconds(adapter_refresh)) {
      try {
        if (!_params["silent"])
          cerr << "[blescan] periodic adapter refresh (bounding SimpleBLE's internal peripheral cache)" << endl;
        start_scan();
      } catch (exception &e) {
        if (!_params["silent"])
          cerr << "[blescan] periodic adapter refresh failed: " << e.what() << endl;
      }
    }

    if (_params.value("only_when_changed", false) && !_changed.load()) {
      return return_type::retry; // no new advertisement since the last publish
    }

    long long max_age_ms = _params.value("max_age_ms", 30000LL);
    auto now = chrono::steady_clock::now();
    json devices = json::array();

    {
#ifdef __APPLE__
      // Draining per-peripheral Objective-C accessor calls below; see the
      // ScopedAutoreleasePool note near the top of this file for why.
      ScopedAutoreleasePool arp;
#endif
      for (auto &peripheral : _adapter.scan_get_results()) {
        try {
          string address = peripheral.address();
          long long age_ms;
          {
            lock_guard<mutex> lock(_last_seen_mutex);
            auto it = _last_seen.find(address);
            if (it == _last_seen.end())
              continue; // not yet timestamped by our callback; appears next tick
            age_ms = chrono::duration_cast<chrono::milliseconds>(now - it->second).count();
            if (max_age_ms > 0 && age_ms > max_age_ms) {
              _last_seen.erase(it); // stop tracking a device that's gone quiet
              continue;             // ...and prune it from this tick's output
            }
          }
          if (_params.value("filter_empty_manufacturer", false) && peripheral.manufacturer_data().empty())
            continue; // filter out devices that do not advertise manufacturer data

          devices.push_back(peripheral_to_json(peripheral, age_ms));
        } catch (exception &e) {
          if (!_params["silent"])
            cerr << "[blescan] skipping device: " << e.what() << endl;
        }
      }
    }

    out["devices"] = devices;
    out["count"] = devices.size();
    if (!_agent_id.empty()) out["agent_id"] = _agent_id;
    _changed.store(false); // published: only report retry again once something new arrives
    return return_type::success;
  }

  void set_params(const json &params) override {
    Source::set_params(params);

    _params["adapter"] = 0;        // index into Adapter::get_adapters()
    _params["max_age_ms"] = 30000; // drop a device if unseen for this long; 0 = never prune
    _params["silent"] = false;     // suppress diagnostic logging to stderr
    _params["filter_empty_manufacturer"] = false; // filter out devices that do not advertise manufacturer data
    _params["only_when_changed"] = false; // only publish when the output changes
    _params["adapter_refresh"] = 600; // rebuild the adapter/scan session this often to bound SimpleBLE's own peripheral cache; 0 = never

    _params.merge_patch(params);

    start_scan();
  }

  map<string, string> info() override {
    map<string, string> result{
        {"Adapter index", to_string(_params.value("adapter", 0))},
        {"Max age (ms)", to_string(_params.value("max_age_ms", 30000))},
    };
    try {
      if (_adapter.initialized())
        result["Adapter"] = _adapter.identifier() + " [" + _adapter.address() + "]";
    } catch (...) {
      // adapter not queryable yet; omit silently
    }
    return result;
  };

private:
  void start_scan() {
#ifdef __APPLE__
    // Called far less often than get_output()'s per-tick loop, but still
    // touches Objective-C accessors (Adapter::get_adapters() etc.) on our
    // own thread; see the ScopedAutoreleasePool note near the top of this
    // file for why that needs draining. RAII so it's still popped if any of
    // the throw statements below fire.
    ScopedAutoreleasePool arp;
#endif

    // Defensive: stop any scan already running on the current adapter before
    // replacing it, so a re-invocation never orphans a background session.
    if (_adapter.initialized() && _adapter.scan_is_active()) {
      _adapter.scan_stop();
    }

    vector<Adapter> adapters;
    try {
      adapters = Adapter::get_adapters();
    } catch (exception &e) {
      throw runtime_error("Failed to enumerate BLE adapters: "s + e.what());
    }
    if (adapters.empty())
      throw runtime_error("No BLE adapters found on this system");

    int index;
    try {
      index = _params.at("adapter").get<int>();
    } catch (json::exception &e) {
      throw runtime_error("Invalid 'adapter' parameter (expected an integer index): "s + e.what());
    }
    if (index < 0 || static_cast<size_t>(index) >= adapters.size())
      throw runtime_error("Adapter index " + to_string(index) + " out of range, only " +
                           to_string(adapters.size()) + " adapter(s) found");

    _adapter = adapters[index];

    {
      lock_guard<mutex> lock(_last_seen_mutex);
      _last_seen.clear();
    }

    _adapter.set_callback_on_scan_start([this]() {
      if (!_params["silent"])
        cerr << "[blescan] scan started on adapter: " << _adapter.identifier() << endl;
    });

    _adapter.set_callback_on_scan_stop([this]() {
      if (!_params["silent"])
        cerr << "[blescan] scan stopped" << endl;
    });

    // Register the same handler for both callbacks: on_scan_found fires once
    // per newly-seen address, on_scan_updated fires on every advertisement
    // after that. Both cases just need to refresh the last-seen timestamp.
    // NOTE: must capture [this], not [&] - this function returns immediately
    // while scanning continues in the background, so [&] would dangle.
    auto on_advertisement = [this](Peripheral peripheral) {
      try {
        touch(peripheral.address());
      } catch (...) {
        // never let an exception escape a SimpleBLE backend callback
      }
    };
    _adapter.set_callback_on_scan_found(on_advertisement);
    _adapter.set_callback_on_scan_updated(on_advertisement);

    _adapter.scan_start();
    _last_adapter_refresh = chrono::steady_clock::now();
  }

  void touch(const string &address) {
    {
      lock_guard<mutex> lock(_last_seen_mutex);
      _last_seen[address] = chrono::steady_clock::now();
    }
    _changed.store(true); // observed by get_output() to decide only_when_changed retries
  }

  // Bluetooth SIG company identifier -> manufacturer name, parsed once from
  // the CMake-embedded company_ids.json (see company_ids.hpp.in).
  static const unordered_map<uint16_t, string> &company_names() {
    static const unordered_map<uint16_t, string> table = [] {
      unordered_map<uint16_t, string> m;
      try {
        for (auto &entry : json::parse(kCompanyIdsJson))
          m[entry.at("code").get<uint16_t>()] = entry.at("name").get<string>();
      } catch (exception &) {
        // malformed/missing table: fall back to an empty map, names omitted
      }
      return m;
    }();
    return table;
  }

  json peripheral_to_json(Peripheral &p, long long age_ms) {
    json d;
    d["name"] = p.identifier();
    d["rssi"] = p.rssi();
    d["address"] = p.address();

    int16_t tx = p.tx_power();
    d["tx_power"] = (tx == INT16_MIN) ? json(nullptr) : json(tx);

    d["connectable"] = p.is_connectable();

    switch (p.address_type()) {
      case BluetoothAddressType::PUBLIC: d["address_type"] = "public"; break;
      case BluetoothAddressType::RANDOM: d["address_type"] = "random"; break;
      case BluetoothAddressType::UNSPECIFIED:
      default: d["address_type"] = "unspecified"; break;
    }

    json manufacturer_data = json::object();
    auto &names = company_names();
    for (auto &entry : p.manufacturer_data()) {
      char key[5];
      snprintf(key, sizeof(key), "%04x", entry.first);
      auto name_it = names.find(entry.first);
      manufacturer_data["id"] = key;
      manufacturer_data["name"] = name_it != names.end() ? json(name_it->second) : json(nullptr);
      manufacturer_data["data"] = entry.second.toHex();
    }
    d["manufacturer_data"] = manufacturer_data;

    json services = json::array();
    for (auto &service : p.services()) {
      services.push_back({{"uuid", service.uuid()}, {"data", service.data().toHex()}});
    }
    d["services"] = services;

    d["age_ms"] = age_ms;
    return d;
  }

  Adapter _adapter;
  mutex _last_seen_mutex;
  unordered_map<string, chrono::steady_clock::time_point> _last_seen;
  chrono::steady_clock::time_point _last_restart_attempt{};
  chrono::steady_clock::time_point _last_adapter_refresh{};
  atomic<bool> _changed{false};
};



/*
  _____ _ _ _                    _             _        
 |  ___(_) | |_ ___ _ __   _ __ | |_   _  __ _(_)_ __   
 | |_  | | | __/ _ \ '__| | '_ \| | | | |/ _` | | '_ \  
 |  _| | | | ||  __/ |    | |_) | | |_| | (_| | | | | | 
 |_|   |_|_|\__\___|_|    | .__/|_|\__,_|\__, |_|_| |_| 
                          |_|            |___/          
Filter the data from the BLE scan plugin, e.g. to only publish certain fields 
or to filter out certain devices. 
*/

class BleFilterPlugin : public Filter<json, json> {

public:
  using Filter::Filter; // inherit constructors

  string kind() override { return "ble_filter"; }

  // Implement the actual functionality here
  // Return types:
  // return_type::success: processing is valid, go to process
  // return_type::retry: skip processing go to next loop
  // return_type::warning: content of _error is tracked with register_event
  // return_type::error: _error is traced, skip process
  // return_type::critical: execution stops
  return_type load_data(json const &input, string topic = "", vector<unsigned char> const *blob = nullptr) override {
    _doc.clear();
    size_t n = 0;
    _doc["num_devices"] = input.value("devices", json::array()).size();
    if (_params.value("layout_ary", true)) {
      _doc["rssi_ary"] = json::array();
    }
    if (_params.value("rssi_map", true)) {
      _doc["rssi_map"] = json::object();
    }
    for (auto &device : input.value("devices", json::array())) {
      if (!_params["id_whitelist"].empty() && find(_params["id_whitelist"].begin(), _params["id_whitelist"].end(), device["manufacturer_data"]["id"]) == _params["id_whitelist"].end()) {
        continue; // skip devices not in the whitelist
      }
      if (!_params["id_blacklist"].empty() && find(_params["id_blacklist"].begin(), _params["id_blacklist"].end(), device["manufacturer_data"]["id"]) != _params["id_blacklist"].end()) {
        continue; // skip devices in the blacklist
      }
      try {
        if (device.value("rssi", -1) >= _params["rssi_min"]) {
          continue; // skip devices with RSSI below the threshold
        }
        if (_params.value("layout_ary", true)) {
          json e = json::object();
          e["address"] = device["address"];
          e["rssi"] = device["rssi"];
          e["name"] = device["manufacturer_data"]["name"];
          e["id"] = device["manufacturer_data"]["id"];
          e["age_ms"] = device["age_ms"];
          _doc["rssi_ary"].push_back(e);
        }
        if (_params.value("layout_map", true)) {
          _doc["rssi_map"][device["address"]] = device["rssi"];
        }
      } catch (exception &e) {
        if (!_params["silent"])
          cerr << "[ble_filter] skipping device: " << e.what() << endl;
        continue; 
      }
      n++;
    }
    _doc["num_filtered_devices"] = n;
    return return_type::success;
  }

  // Return types:
  // return_type::success: result is published
  // return_type::retry: don't publish, go to next loop
  // return_type::warning: content of _error is added to result befor publishing
  // return_type::error: _error is traced via register_event, don't publish
  // return_type::critical: execution stops
  return_type process(json &out, vector<unsigned char> *blob = nullptr) override {
    out.clear();
    if (_params.value("layout_ary", true)) {
      out["rssi_ary"] = _doc["rssi_ary"];
    }
    if (_params.value("layout_map", true)) {
      out["rssi_map"] = _doc["rssi_map"];
    }
    out["num_devices"] = _doc["num_devices"];
    out["num_filtered_devices"] = _doc["num_filtered_devices"];

    // load the data as necessary and set the fields of the json out variable

    // This sets the agent_id field in the output json object, only when it is
    // not empty
    if (!_agent_id.empty()) out["agent_id"] = _agent_id;
    return return_type::success;
  }
  
  void set_params(const json &params) override {
    Filter::set_params(params);

    _params["rssi_min"] = -50; // minimum RSSI to include a device
    _params["silent"] = true; // suppress diagnostic logging to stderr
    _params["layout_ary"] = true;
    _params["layout_map"] = true;
    _params["id_whitelist"] = json::array(); // list of device addresses to include, empty = all
    _params["id_blacklist"] = json::array(); // list of device addresses to exclude, empty = none

    _params.merge_patch(params);
    if (!_params["id_whitelist"].empty() && !_params["id_blacklist"].empty()) {
      throw runtime_error("Cannot specify both id_whitelist and id_blacklist");
    }
      
  }

  // Implement this method if you want to provide additional information
  map<string, string> info() override {     
    return {
      {"rssi_min", to_string(_params["rssi_min"].get<int>())},
      {"silent", _params["silent"].get<bool>() ? "true" : "false"},
      {"layout_ary", _params["layout_ary"].get<bool>() ? "true" : "false"},
      {"layout_map", _params["layout_map"].get<bool>() ? "true" : "false"},
      {"id_whitelist", _params["id_whitelist"].dump()},
      {"id_blacklist", _params["id_blacklist"].dump()}
    };
  };

private:
  // Define the fields that are used to store internal resources
  json _doc = json::object();
};


/*
  ____  _             _             _      _
 |  _ \| |_   _  __ _(_)_ __     __| |_ __(_)_   _____ _ __
 | |_) | | | | |/ _` | | '_ \   / _` | '__| \ \ / / _ \ '__|
 |  __/| | |_| | (_| | | | | | | (_| | |  | |\ V /  __/ |
 |_|   |_|\__,_|\__, |_|_| |_|  \__,_|_|  |_| \_/ \___|_|
                |___/
Enable the class as plugin
*/
MADS_REGISTER_PLUGINS(BleScanPlugin, BleFilterPlugin);


/*
                  _
  _ __ ___   __ _(_)_ __
 | '_ ` _ \ / _` | | '_ \
 | | | | | | (_| | | | | |
 |_| |_| |_|\__,_|_|_| |_|

For testing purposes, when directly executing the plugin
*/

static bool running = true;

int main(int argc, char const *argv[]) {
  BleScanPlugin plugin;
  json output, params;

  params["adapter"] = 0;
  params["max_age_ms"] = 30000;
  params["silent"] = false;
  params["filter_empty_manufacturer"] = true;
  params["only_when_changed"] = true;

  plugin.set_params(params);
  for (auto &kv : plugin.info())
    cerr << kv.first << ": " << kv.second << endl;

  signal(SIGINT, [](int) { running = false; });
  while (running) {
    return_type rt = plugin.get_output(output);
    if (rt == return_type::success)
      cout << "devices: " << output.value("count", 0) << " -> " << output.dump(2) << endl;
    else
      cerr << "[main] get_output: retry" << endl;

    this_thread::sleep_for(100ms);
  }
  cout << "Exiting..." << endl;
  return 0;
}
