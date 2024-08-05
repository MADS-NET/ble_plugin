# ble_plugin plugin for MADS

This is a Source plugin for [MADS](https://github.com/MADS-NET/MADS). 

This plugin reads raw data published by a BLE peripheral device and sends it to the MADS framework. the plugin is based on the [SimpleBLE](https://github.com/OpenBluetoothToolbox/SimpleBLE), which is automatically downloaded and compiled.

*Required MADS version: 1.0.2.*


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


## INI settings

The plugin supports the following settings in the INI file:

```ini
[ble_plugin]
peripheral = "Arduino" # The name of the peripheral device
characteristics = [] # a list of 128 bits valid UUID of the characteristics to be read
```

All settings are optional; if omitted, the default values are used.


## Executable demo

The demo connects to a BLE peripheral named "Arduino" and fetches all the available characteristics as `uint32_t` values.



---