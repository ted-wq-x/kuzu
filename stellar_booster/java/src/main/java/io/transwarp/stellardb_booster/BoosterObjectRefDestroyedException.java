package io.transwarp.stellardb_booster;

/**
 * BoosterObjectRefDestroyedException is thrown when a destroyed object is accessed
 * or destroyed again.
 * @see BoosterConnection#destroy()
 */
public class BoosterObjectRefDestroyedException extends Exception {
    public BoosterObjectRefDestroyedException(String errorMessage) {
        super(errorMessage);
    }
}
