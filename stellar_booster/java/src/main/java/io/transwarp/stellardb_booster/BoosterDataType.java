package io.transwarp.stellardb_booster;

/**
* BoosterDataType is the booster internal representation of data types.
*/
public class BoosterDataType {
    long dt_ref;
    boolean destroyed = false;

    /**
    * Create a non-nested BoosterDataType object from its internal ID.
    * @param id: the booster internal representation of data type IDs.
    */
    public BoosterDataType(BoosterDataTypeID id) {
        dt_ref = BoosterNative.data_type_create(id, null, 0);
    }

    public BoosterDataType
            (BoosterDataTypeID id, BoosterDataType child_type, long fixed_num_elements_in_list) {
        dt_ref = BoosterNative.data_type_create(id, child_type, fixed_num_elements_in_list);
    }

    /**
     * Checks if the database instance has been destroyed.
     * @throws BoosterObjectRefDestroyedException If the data type instance has been destroyed.
    */
    private void checkNotDestroyed() throws BoosterObjectRefDestroyedException {
        if (destroyed)
            throw new BoosterObjectRefDestroyedException("BoosterDataType has been destroyed.");
    }

    /**
    * Finalize.
    * @throws BoosterObjectRefDestroyedException If the data type instance has been destroyed.
    */
    @Override
    protected void finalize() throws BoosterObjectRefDestroyedException {
        destroy();
    }

    /**
    * Destroy the data type instance.
    * @throws BoosterObjectRefDestroyedException If the data type instance has been destroyed.
    */
    public void destroy() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        BoosterNative.data_type_destroy(this);
        destroyed = true;
    }

    /**
    * Clone the data type instance.
    * @return The cloned data type instance.
    */
    public BoosterDataType clone() {
        if (destroyed)
            return null;
        else
            return BoosterNative.data_type_clone(this);
    }

    /**
    * Returns true if the given data type is equal to the other data type, false otherwise.
    * @param other The other data type to compare with.
    * @return If the given data type is equal to the other data type or not.
    * @throws BoosterObjectRefDestroyedException If the data type instance has been destroyed.
    */
    public boolean equals(BoosterDataType other) throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        return BoosterNative.data_type_equals(this, other);
    }

    /**
    * Returns the enum internal id of the given data type.
    * @return The enum internal id of the given data type.
    * @throws BoosterObjectRefDestroyedException If the data type instance has been destroyed.
    */
    public BoosterDataTypeID getID() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        return BoosterNative.data_type_get_id(this);
    }

    /**
    * Returns the child type of the given data type.
    * @return The child type of the given data type.
    * @throws BoosterObjectRefDestroyedException If the data type instance has been destroyed.
    */
    public BoosterDataType getChildType() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        return BoosterNative.data_type_get_child_type(this);
    }

    /**
    * Returns the fixed number of elements in the list of the given data type.
    * @return The fixed number of elements in the list of the given data type.
    * @throws BoosterObjectRefDestroyedException If the data type instance has been destroyed.
    */
    public long getFixedNumElementsInList() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        return BoosterNative.data_type_get_fixed_num_elements_in_list(this);
    }

}
