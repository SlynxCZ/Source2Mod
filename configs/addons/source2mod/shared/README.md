# shared

Data that more than one plugin reads.

A plugin reaches this through `ISource2ModPaths::SharedDirectory()`, or the
`SOURCE2MOD_SHARED_DIR()` macro:

```cpp
const std::string mapsCfg = std::string(SOURCE2MOD_SHARED_DIR()) + "/Maps.cfg";
```

Anything only one plugin reads belongs in its own `configs/<PluginName>/`
directory instead, reachable with `PLUGIN_CONFIG_DIR()`.

`GeoLite2-City.mmdb` and `GeoLite2-Country.mmdb` are the GeoIP databases core
reads (core.json's `GeoIPDatabase`, City by default, Country when City is
missing). GeoLite2 data created by MaxMind, available from
https://www.maxmind.com.
