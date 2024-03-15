package io.transwarp.stellardb_booster;

/**
* FlatTuple stores a vector of values.
*/
public class BoosterFlatTuple {
    long ft_ref;
    boolean destroyed = false;

    /**
     * Check if the flat tuple has been destroyed.
     * @throws BoosterObjectRefDestroyedException If the flat tuple has been destroyed.
     */
    private void checkNotDestroyed() throws BoosterObjectRefDestroyedException {
        if (destroyed)
            throw new BoosterObjectRefDestroyedException("BoosterFlatTuple has been destroyed.");
    }

    /**
     * Finalize.
     * @throws BoosterObjectRefDestroyedException If the flat tuple has been destroyed.
     */
    @Override
    protected void finalize() throws BoosterObjectRefDestroyedException {
        destroy();
    }

    /**
     * Destroy the flat tuple.
     * @throws BoosterObjectRefDestroyedException If the flat tuple has been destroyed.
     */
    public void destroy() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        BoosterNative.flat_tuple_destroy(this);
        destroyed = true;
    }

    /**
     * Get the value at the given index.
     * @param index: The index of the value.
     * @return The value at the given index.
     * @throws BoosterObjectRefDestroyedException If the flat tuple has been destroyed.
     */
    public BoosterValue getValue(long index) throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        return BoosterNative.flat_tuple_get_value(this, index);
    }

    /**
     * Convert the flat tuple to string.
     * @return The string representation of the flat tuple.
     */
    public String toString() {
        if (destroyed)
            return null;
        else
            return BoosterNative.flat_tuple_to_string(this);
    }
}
