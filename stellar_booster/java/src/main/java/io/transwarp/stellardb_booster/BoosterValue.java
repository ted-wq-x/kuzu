package io.transwarp.stellardb_booster;

/**
 * BoosterValue can hold data of different types.
 */
public class BoosterValue {
    long v_ref;
    boolean destroyed = false;
    boolean isOwnedByCPP = false;

    /**
     * Construct a BoosterValue from a val.
     * @throws BoosterObjectRefDestroyedException If the BoosterValue has been destroyed.
     */
    public <T> BoosterValue(T val) throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        v_ref = BoosterNative.value_create_value(val);
    }

    /**
     * Create a null BoosterValue.
     * @return The null BoosterValue.
     */
    public static BoosterValue createNull() {
        return BoosterNative.value_create_null();
    }

    /**
     * Create a null BoosterValue with the given data type.
     * @param data_type: The data type of the null BoosterValue.
     */
    public static BoosterValue createNullWithDataType(BoosterDataType data_type) {
        return BoosterNative.value_create_null_with_data_type(data_type);
    }

    /**
     * Create a default BoosterValue with the given data type.
     * @param data_type: The data type of the default BoosterValue.
     * @return The default BoosterValue.
     */
    public static BoosterValue createDefault(BoosterDataType data_type) {
        return BoosterNative.value_create_default(data_type);
    }

    /**
     * Check if the BoosterValue has been destroyed.
     * @throws BoosterObjectRefDestroyedException If the BoosterValue has been destroyed.
     */
    public void checkNotDestroyed() throws BoosterObjectRefDestroyedException {
        if (destroyed)
            throw new BoosterObjectRefDestroyedException("BoosterValue has been destroyed.");
    }

    /**
     * Finalize.
     * @throws BoosterObjectRefDestroyedException If the BoosterValue has been destroyed.
     */
    @Override
    protected void finalize() throws BoosterObjectRefDestroyedException {
        destroy();
    }

    public boolean isOwnedByCPP() {
        return isOwnedByCPP;
    }

    /**
     * Destroy the BoosterValue.
     * @throws BoosterObjectRefDestroyedException If the BoosterValue has been destroyed.
     */
    public void destroy() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        if (!isOwnedByCPP) {
            BoosterNative.value_destroy(this);
            destroyed = true;
        }
    }

    /**
     * Check if the BoosterValue is null.
     * @return True if the BoosterValue is null, false otherwise.
     * @throws BoosterObjectRefDestroyedException If the BoosterValue has been destroyed.
     */
    public boolean isNull() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        return BoosterNative.value_is_null(this);
    }

    /**
     * Set the BoosterValue to null.
     * @param flag: True if the BoosterValue is set to null, false otherwise.
     * @throws BoosterObjectRefDestroyedException If the BoosterValue has been destroyed.
     */
    public void setNull(boolean flag) throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        BoosterNative.value_set_null(this, flag);
    }

    /**
     * Copy the BoosterValue from another BoosterValue.
     * @param other: The BoosterValue to copy from.
     * @throws BoosterObjectRefDestroyedException If the BoosterValue has been destroyed.
     */
    public void copy(BoosterValue other) throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        BoosterNative.value_copy(this, other);
    }

    /**
     * Clone the BoosterValue.
     * @return The cloned BoosterValue.
     */
    public BoosterValue clone() {
        if (destroyed)
            return null;
        else
            return BoosterNative.value_clone(this);
    }

    /**
     * Get the actual value from the BoosterValue.
     * @return The value of the given type.
     * @throws BoosterObjectRefDestroyedException If the BoosterValue has been destroyed.
     */
    public <T> T getValue() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        return BoosterNative.value_get_value(this);
    }

    /**
     * Get the data type of the BoosterValue.
     * @return The data type of the BoosterValue.
     * @throws BoosterObjectRefDestroyedException If the BoosterValue has been destroyed.
     */
    public BoosterDataType getDataType() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        return BoosterNative.value_get_data_type(this);
    }

    /**
     * Convert the BoosterValue to string.
     * @return The current value in string format.
     */
    public String toString() {
        if (destroyed)
            return "BoosterValue has been destroyed.";
        else
            return BoosterNative.value_to_string(this);
    }
}
