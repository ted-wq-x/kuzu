package io.transwarp.stellardb_booster;

/**
 * Utility functions for BoosterValue of list type.
 */
public class BoosterValueListUtil {
    /**
     * Get the size of the list value.
     * @param value: The list value.
     * @return The size of the list value.
     * @throws BoosterObjectRefDestroyedException If the list value has been destroyed.
     */
    public static long getListSize(BoosterValue value) throws BoosterObjectRefDestroyedException {
        value.checkNotDestroyed();
        return BoosterNative.value_get_list_size(value);
    }

    /**
     * Get the element at the given index from the given list value.
     * @param value: The list value.
     * @param index: The index of the element.
     * @return The element at the given index from the given list value.
     * @throws BoosterObjectRefDestroyedException If the list value has been destroyed.
     */
    public static BoosterValue getListElement(BoosterValue value, long index) throws BoosterObjectRefDestroyedException {
        value.checkNotDestroyed();
        return BoosterNative.value_get_list_element(value, index);
    }
}
