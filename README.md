# ble_plugin plugin for MADS

This is a Source plugin for [MADS](https://github.com/MADS-NET/MADS). 

This plugin reads raw data published by a BLE peripheral device and sends it to the MADS framework. the plugin is based on the [SimpleBLE](https://github.com/OpenBluetoothToolbox/SimpleBLE), which is automatically downloaded and compiled.

> This plugin has been updated for MADS v2

## Supported platforms

Currently, the supported platforms are:

* **Linux** 
* **MacOS**
* **Windows**


## Installation

Linux and MacOS:

```bash
cmake -Bbuild -DCMAKE_INSTALL_PREFIX="$(mads -p)"
cmake --build build -j4
sudo cmake --install build
```

Windows:

```powershell
cmake -Bbuild -DCMAKE_INSTALL_PREFIX="$(mads -p)"
cmake --build build --config Release
cmake --install build --config Release
```

## Requirements

On Linux, the plugin requires the following packages to be installed:

```bash
sudo apt install libdbus-1-dev
```

## Services and Characteristics

In BLE, data come organized in *services*, each service having one or more *characteristic*, i.e. individual value. The settings file must specify the peripheral name and a list of *characteristics* (an array of UUIDs). **If there are multiple characteristics under different services, this might cause issues and is not currently supported.**

If the list of characteristics is empty (default), then all the characteristics are read.


## Modes of operation

It can operate in two modes, selected with the `subscribe` setting:

1. **polling**: read the data published by the selected characteristics as soon as they are sent (default);
2. **event**: get an immediate notification when a value is written by the device, typically only when it changes.


## Arduino code

The `arduino` folder contains a sketch suitable for testing purposes. It runs on arduino boards with BLE capabilities (as the Arduino R4). It produces readings for analog pins A1 and A2, suitable for polling mode, and for the digital pin D2, suitable for event-based notifications.


## INI settings

The plugin supports the following settings in the INI file:

```ini
[ble_plugin]
peripheral = "Arduino" # The name of the peripheral device
characteristics = [] # a list of 128 bits valid UUID of the characteristics to be read
subscribe = false    # subscribe to notifications (event mode)
list_uuids = false   # on launch, print available UUIDs
```

All settings are optional; if omitted, the default values are used.


## Executable demo

The demo connects to a BLE peripheral named "Arduino" and fetches all the available characteristics as `uint32_t` values.


## `blescan` plugin

Unlike `ble` (which connects to one named peripheral and reads specific
characteristics), `blescan` never connects to anything: it keeps a single BLE
scan running continuously in the background via SimpleBLE, and on every call
publishes, for every currently visible device, all publicly-broadcast
information: address, name, RSSI, advertised TX power, connectability,
address type, manufacturer data, and advertised service UUIDs/data.

### INI settings

```ini
[blescan]
adapter = 0        # index into the list of available BLE adapters (0 = first)
max_age_ms = 30000 # drop a device from the output if unseen for this long; 0 = never drop
silent = false      # suppress diagnostic logging to stderr
filter_empty_manufacturer = false # skip devices that advertise no manufacturer data
only_when_changed = false # only publish (return success) when a new advertisement arrived since the last call; otherwise return retry
adapter_refresh = 600 # seconds between full adapter/scan rebuilds, bounding SimpleBLE's own peripheral cache; 0 = never
```

All settings are optional; if omitted, the default values above are used.

### Output format

Each call publishes a JSON object of the form:

```json
{
  "devices": [
    {
      "address": "aa:bb:cc:dd:ee:ff",
      "name": "My Device",
      "rssi": -62,
      "tx_power": null,
      "connectable": true,
      "address_type": "random",
      "manufacturer_data": { "id": "004c", "name": "Apple, Inc.", "data": "0215..." },
      "services": [ { "uuid": "0000180f-0000-1000-8000-00805f9b34fb", "data": "" } ],
      "age_ms": 42
    }
  ],
  "count": 1
}
```

`tx_power` is `null` when a device does not advertise it. Since scanning
happens continuously in the background and `get_output()` only reads the
already-updated scan state, it is safe to call at high frequency (e.g. 10 Hz
or faster).

### Known limitations

* SimpleBLE never forgets a device once seen, for as long as the underlying
  `Adapter` object lives, even after it goes out of range. `blescan`
  compensates in two ways: its own per-device "last seen" bookkeeping is
  pruned (not just hidden) once `max_age_ms` has elapsed since a device's
  last advertisement (`max_age_ms = 0` disables pruning), and the whole
  adapter/scan session is periodically rebuilt from scratch every
  `adapter_refresh` seconds to also bound SimpleBLE's own internal cache,
  which has no public API for removing individual stale entries
  (`adapter_refresh = 0` disables this).
* On macOS, several `Peripheral`/`Adapter` accessors call into Objective-C
  (e.g. `identifier()`/`address()`) and return autoreleased objects. This
  plugin is a bare C++ process with no run loop to drain the autorelease
  pool automatically, so the per-tick device loop (and the less-frequent
  adapter setup in `start_scan()`) explicitly pushes/pops one via
  `objc_autoreleasePoolPush`/`Pop` — without it, memory grows steadily and
  unboundedly in proportion to (live device count × poll rate), independent
  of any of the pruning above. Confirmed by measuring process RSS with and
  without the pool.
* On Windows, the underlying SimpleBLE backend reads its internal scan
  results without its own lock, which is a known upstream thread-safety gap
  outside this plugin's control.
* Each `manufacturer_data` entry's `name` is resolved from the Bluetooth SIG
  company identifier registry (mirrored via [Nordic Semiconductor's
  bluetooth-numbers-database](https://github.com/NordicSemiconductor/bluetooth-numbers-database),
  fetched once at CMake configure time). It is `null` for codes not yet
  present in that snapshot; re-run `cmake -Bbuild` to refresh it.

---