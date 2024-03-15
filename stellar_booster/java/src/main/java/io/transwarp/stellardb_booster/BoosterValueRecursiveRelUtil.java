package io.transwarp.stellardb_booster;

/**
 * Utility functions for BoosterValue of recursive_rel type.
 */
public class BoosterValueRecursiveRelUtil {
    /**
     * Get the node list from the given recursive_rel value.
     * @param value: The recursive_rel value.
     * @return The node list from the given recursive_rel value.
     * @throws BoosterObjectRefDestroyedException If the recursive_rel value has been destroyed.
     */
    public static BoosterValue getNodeList(BoosterValue value) throws BoosterObjectRefDestroyedException {
        return BoosterValueStructUtil.getValueByIndex(value, 0);
    }

    /**
     * Get the rel list from the given recursive_rel value.
     * @param value: The recursive_rel value.
     * @return The rel list from the given recursive_rel value.
     * @throws BoosterObjectRefDestroyedException If the recursive_rel value has been destroyed.
     */
    public static BoosterValue getRelList(BoosterValue value) throws BoosterObjectRefDestroyedException {
        return BoosterValueStructUtil.getValueByIndex(value, 1);
    }
}
