package io.transwarp.stellardb_booster;

/**
 * Utility functions for BoosterValue of RDF_VARIANT type.
 */
public class BoosterValueRdfVariantUtil {
    /**
     * Get the data type of the RDF_VARIANT value.
     * @param value: The RDF_VARIANT value.
     * @return The data type of the RDF_VARIANT value.
     * @throws BoosterObjectRefDestroyedException If the RDF_VARIANT value has been destroyed.
     */
    public static BoosterDataType getDataType(BoosterValue value) throws BoosterObjectRefDestroyedException {
       value.checkNotDestroyed();
        return BoosterNative.rdf_variant_get_data_type(value);
    }

    /**
     * Get the value of the RDF_VARIANT value.
     * @param value: The RDF_VARIANT value.
     * @return The value of the RDF_VARIANT value.
     * @throws BoosterObjectRefDestroyedException If the RDF_VARIANT value has been destroyed.
     */
    public static <T> T getValue(BoosterValue value) throws BoosterObjectRefDestroyedException {
        value.checkNotDestroyed();
        return BoosterNative.rdf_variant_get_value(value);
    }
}
