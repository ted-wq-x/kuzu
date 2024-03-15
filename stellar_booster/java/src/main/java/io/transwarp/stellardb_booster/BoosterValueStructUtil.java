package io.transwarp.stellardb_booster;

/**
 * Utility functions for BoosterValue of struct type.
 */
public class BoosterValueStructUtil {
    /**
     * Get the number of fields of the struct value.
     * @param value: The struct value.
     * @return The number of fields of the struct value.
     * @throws BoosterObjectRefDestroyedException If the struct value has been destroyed.
     */
    public static long getNumFields(BoosterValue value) throws BoosterObjectRefDestroyedException {
        value.checkNotDestroyed();
        return BoosterNative.value_get_list_size(value);
    }

    /**
     * Get the index of the field with the given name from the given struct value.
     * @param value: The struct value.
     * @param fieldName: The name of the field.
     * @return The index of the field with the given name from the given struct value.
     * @throws BoosterObjectRefDestroyedException If the struct value has been destroyed.
     */
    public static long getIndexByFieldName(BoosterValue value, String fieldName) throws BoosterObjectRefDestroyedException {
        value.checkNotDestroyed();
        return BoosterNative.value_get_struct_index(value, fieldName);
    }

    /**
     * Get the name of the field at the given index from the given struct value.
     * @param value: The struct value.
     * @param index: The index of the field.
     * @return The name of the field at the given index from the given struct value.
     * @throws BoosterObjectRefDestroyedException If the struct value has been destroyed.
     */
    public static String getFieldNameByIndex(BoosterValue value, long index) throws BoosterObjectRefDestroyedException {
        value.checkNotDestroyed();
        return BoosterNative.value_get_struct_field_name(value, index);
    }

    /**
     * Get the value of the field with the given name from the given struct value.
     * @param value: The struct value.
     * @param fieldName: The name of the field.
     * @return The value of the field with the given name from the given struct value.
     * @throws BoosterObjectRefDestroyedException If the struct value has been destroyed.
     */
    public static BoosterValue getValueByFieldName(BoosterValue value, String fieldName) throws BoosterObjectRefDestroyedException {
        value.checkNotDestroyed();
        long index = getIndexByFieldName(value, fieldName);
        if (index < 0) {
            return null;
        }
        return getValueByIndex(value, index);
    }

    /**
     * Get the value of the field at the given index from the given struct value.
     * @param value: The struct value.
     * @param index: The index of the field.
     * @return The value of the field at the given index from the given struct value.
     * @throws BoosterObjectRefDestroyedException If the struct value has been destroyed.
     */
    public static BoosterValue getValueByIndex(BoosterValue value, long index) throws BoosterObjectRefDestroyedException {
        value.checkNotDestroyed();
        if (index < 0 || index >= getNumFields(value)) {
            return null;
        }
        return BoosterNative.value_get_list_element(value, index);
    }
}
