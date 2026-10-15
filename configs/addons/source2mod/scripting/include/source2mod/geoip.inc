/**
 * Source2Mod SourcePawn API: GeoIP (ISource2ModGeoIP).
 *
 * Where an IP address (a port is allowed) or a player is, from a MaxMind
 * GeoLite2 database: addons/source2mod/geoip/GeoLite2-City.mmdb by default
 * (core.json "GeoIPDatabase"), not shipped. Without it every lookup returns
 * false; Source2ModFeatures.HasCapability("geoip") says whether it is there.
 *
 * SourceMod's geoip.inc natives are there as well (GeoipCode2, GeoipCountry,
 * GeoipCity, ...), except GeoipCode3.
 */

#if defined _source2mod_geoip_included
 #endinput
#endif
#define _source2mod_geoip_included

#include <source2mod/types>

enum GeoIPField
{
	GeoIP_CountryCode = 0,   /**< "US" */
	GeoIP_Country,           /**< "United States" */
	GeoIP_ContinentCode,     /**< "NA" */
	GeoIP_Continent,         /**< "North America" */
	GeoIP_RegionCode,        /**< "CA" (City database) */
	GeoIP_Region,            /**< "California" (City database) */
	GeoIP_City,              /**< "Mountain View" (City database) */
	GeoIP_Timezone,          /**< "America/Los_Angeles" (City database) */
	GeoIP_PostalCode,        /**< (City database) */
};

methodmap Source2ModGeoIP
{
	/** Whether a database is open. */
	public static native bool IsLoaded();

	/** "GeoLite2-City" / "GeoLite2-Country", "" when none. */
	public static native void GetDatabaseType(char[] buffer, int maxlen);

	/**
	 * One field of an address.
	 *
	 * @param language  For names: "en", "de", "es", "fr", "ja", "pt-BR", "ru", "zh-CN"; English when missing.
	 */
	public static native bool Lookup(const char[] ip, GeoIPField field, char[] buffer, int maxlen, const char[] language = "en");

	/** Lookup() for a player's address. */
	public static native bool LookupPlayer(CPlayerSlot slot, GeoIPField field, char[] buffer, int maxlen, const char[] language = "en");

	/** Latitude and longitude (City database). */
	public static native bool GetLocation(const char[] ip, float &latitude, float &longitude);

	/** Any value of the record by its path, "country/names/de", "subdivisions/0/iso_code". */
	public static native bool LookupPath(const char[] ip, const char[] path, char[] buffer, int maxlen);

	/** A player's IP address without the port; false for an empty slot or a bot. */
	public static native bool GetPlayerIP(CPlayerSlot slot, char[] buffer, int maxlen);
}
