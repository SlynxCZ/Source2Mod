# shared

Data that more than one plugin reads.

A plugin reaches this through `ISource2ModPaths::SharedDirectory()`, or the
`SOURCE2MOD_SHARED_DIR()` macro:

```cpp
const std::string mapsCfg = std::string(SOURCE2MOD_SHARED_DIR()) + "/Maps.cfg";
```

Anything only one plugin reads belongs in its own `configs/<PluginName>/`
directory instead, reachable with `PLUGIN_CONFIG_DIR()`.
