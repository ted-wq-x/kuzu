package io.transwarp.stellardb_booster;

/**
 * Utility functions for BoosterValue of node type.
 */
public class BoosterValueNodeUtil {
    /**
     * Get the internal ID of the node value.
     * @param value: The node value.
     * @return The internal ID of the node value.
     * @throws BoosterObjectRefDestroyedException If the node value has been destroyed.
     */
    public static BoosterInternalID getID(BoosterValue value) throws BoosterObjectRefDestroyedException {
        value.checkNotDestroyed();
        return BoosterNative.node_val_get_id(value);
    }

    /**
     * Get the label name of the node value.
     * @param value: The node value.
     * @return The label name of the node value.
     * @throws BoosterObjectRefDestroyedException If the node value has been destroyed.
     */
    public static String getLabelName(BoosterValue value) throws BoosterObjectRefDestroyedException {
        value.checkNotDestroyed();
        return BoosterNative.node_val_get_label_name(value);
    }

    /**
     * Get the property size of the node value.
     * @param value: The node value.
     * @return The property size of the node value.
     * @throws BoosterObjectRefDestroyedException If the node value has been destroyed.
     */
    public static long getPropertySize(BoosterValue value) throws BoosterObjectRefDestroyedException {
        value.checkNotDestroyed();
        return BoosterNative.node_val_get_property_size(value);
    }

    /**
     * Get the property name at the given index from the given node value.
     * @param value: The node value.
     * @param index: The index of the property.
     * @return The property name at the given index from the given node value.
     * @throws BoosterObjectRefDestroyedException If the node value has been destroyed.
     */
    public static String getPropertyNameAt(BoosterValue value, long index) throws BoosterObjectRefDestroyedException {
        value.checkNotDestroyed();
        return BoosterNative.node_val_get_property_name_at(value, index);
    }

    /**
     * Get the property value at the given index from the given node value.
     * @param value: The node value.
     * @param index: The index of the property.
     * @return The property value at the given index from the given node value.
     * @throws BoosterObjectRefDestroyedException If the node value has been destroyed.
     */
    public static BoosterValue getPropertyValueAt(BoosterValue value, long index) throws BoosterObjectRefDestroyedException {
        value.checkNotDestroyed();
        return BoosterNative.node_val_get_property_value_at(value, index);
    }

    /**
     * Convert the node value to string.
     * @param value: The node value.
     * @return The node value in string format.
     * @throws BoosterObjectRefDestroyedException If the node value has been destroyed.
     */
    public static String toString(BoosterValue value) throws BoosterObjectRefDestroyedException {
        value.checkNotDestroyed();
        return BoosterNative.node_val_to_string(value);
    }
}
