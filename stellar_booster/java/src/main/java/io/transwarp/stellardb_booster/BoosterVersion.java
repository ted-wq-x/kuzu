package io.transwarp.stellardb_booster;

/**
 * BoosterVersion is a class to get the version of the Kùzu.
 */
public class BoosterVersion {

    /**
     * Get the version of the Kùzu.
     * @return The version of the Kùzu.
     */
    public static String getVersion() {
        return BoosterNative.get_version();
    }

    /**
     * Get the storage version of the Kùzu.
     * @return The storage version of the Kùzu.
     */
    public static long getStorageVersion() {
        return BoosterNative.get_storage_version();
    }
}
