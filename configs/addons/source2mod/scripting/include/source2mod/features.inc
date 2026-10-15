/**
 * Source2Mod SourcePawn API: feature checks (ISource2ModFeatures).
 *
 * Whether a native, a capability, an interface or a library is there before
 * it is used. SourceMod's GetFeatureStatus / RequireFeature (source2mod.inc)
 * take these types too: FeatureType_Native and FeatureType_Capability are
 * the same numbers as Feature_Native and Feature_Capability.
 *
 * Capabilities: "sourcepawn", "geoip" (a GeoLite2 database is open),
 * "clientprefs" (the cookie database is connected), "gamesystems".
 */

#if defined _source2mod_features_included
 #endinput
#endif
#define _source2mod_features_included

enum Source2ModFeatureType
{
	/** A native: the core's, or one a plugin registered (unavailable while that plugin is unloaded). */
	Feature_Native = 0,
	/** A named part of the core that can be missing at runtime. */
	Feature_Capability = 1,
	/** A C++ interface by its versioned name, "ISource2ModMenus005". */
	Feature_Interface = 2,
	/** A library a plugin registered (RegPluginLibrary). */
	Feature_Library = 3,
};

methodmap Source2ModFeatures
{
	/**
	 * FeatureStatus_Available, _Unavailable (known, not usable now) or _Unknown.
	 * The return value is SourceMod's FeatureStatus.
	 */
	public static native any GetStatus(Source2ModFeatureType type, const char[] name);

	public static native bool IsNativeAvailable(const char[] name);

	/** Whether the core (or a plugin) serves this interface at this exact revision. */
	public static native bool IsInterfaceAvailable(const char[] iface);

	public static native bool IsLibraryAvailable(const char[] name);

	public static native bool HasCapability(const char[] name);
}
