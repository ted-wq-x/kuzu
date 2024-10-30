package io.transwarp.stellardb_booster;

/**
 * Utility functions for Value of map type.
 */
public class BoosterValueMapUtil {

    private static BoosterValue getMapKeyOrValue(BoosterValue value, long index, boolean isKey) throws BoosterObjectRefDestroyedException {
        value.checkNotDestroyed();
        if (index < 0 || index >= getNumFields(value)) {
            return null;
        }
        BoosterValue structValue = BoosterNative.value_get_list_element(value, index);
        BoosterValue keyOrValue = BoosterNative.value_get_list_element(structValue, isKey ? 0 : 1);
        structValue.close();
        return keyOrValue;
    }

    /**
     * Get the number of fields of the map value.
     *
     * @param value: The map value.
     * @return The number of fields of the map value.
     * @throws BoosterObjectRefDestroyedException If the map value has been destroyed.
     */
    public static long getNumFields(BoosterValue value) throws BoosterObjectRefDestroyedException {
        value.checkNotDestroyed();
        return BoosterNative.value_get_list_size(value);
    }


    /**
     * Get the key from the given map value by the given index.
     *
     * @param value: The map value.
     * @param index: The index of the key.
     * @return The key from the given map value by the given index.
     * @throws BoosterObjectRefDestroyedException If the map value has been destroyed.
     */
    public static BoosterValue getKey(BoosterValue value, long index) throws BoosterObjectRefDestroyedException {
        return getMapKeyOrValue(value, index, true);
    }

    /**
     * Get the value from the given map value by the given index.
     *
     * @param value: The map value.
     * @param index: The index of the value.
     * @return The value from the given map value by the given index.
     * @throws BoosterObjectRefDestroyedException If the map value has been destroyed.
     */
    public static BoosterValue getValue(BoosterValue value, long index) throws BoosterObjectRefDestroyedException {
        return getMapKeyOrValue(value, index, false);
    }
}
