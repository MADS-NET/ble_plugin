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



---