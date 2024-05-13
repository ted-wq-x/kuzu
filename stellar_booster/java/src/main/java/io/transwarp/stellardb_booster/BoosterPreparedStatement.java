package io.transwarp.stellardb_booster;

/**
 * BoosterPreparedStatement is a parameterized query which can avoid planning the same query for repeated execution.
 */
public class BoosterPreparedStatement {
    long ps_ref;
    boolean destroyed = false;

    /**
     * Check if the prepared statement has been destroyed.
     * @throws BoosterObjectRefDestroyedException If the prepared statement has been destroyed.
     */
    private void checkNotDestroyed() throws BoosterObjectRefDestroyedException {
        if (destroyed)
            throw new BoosterObjectRefDestroyedException("BoosterPreparedStatement has been destroyed.");
    }

    /**
    * Finalize.
    * @throws BoosterObjectRefDestroyedException If the prepared statement has been destroyed.
    */
    @Override
    protected void finalize() throws BoosterObjectRefDestroyedException {
        destroy();
    }

    /**
     * Destroy the prepared statement.
     * @throws BoosterObjectRefDestroyedException If the prepared statement has been destroyed.
     */
    public void destroy() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        BoosterNative.prepared_statement_destroy(this);
        destroyed = true;
    }

    /**
     * Check if the query is prepared successfully or not.
     * @return The query is prepared successfully or not.
     * @throws BoosterObjectRefDestroyedException If the prepared statement has been destroyed.
     */
    public boolean isSuccess() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        return BoosterNative.prepared_statement_is_success(this);
    }

    /**
     * Get the error message if the query is not prepared successfully.
     * @return The error message if the query is not prepared successfully.
     * @throws BoosterObjectRefDestroyedException If the prepared statement has been destroyed.
     */
    public String getErrorMessage() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        return BoosterNative.prepared_statement_get_error_message(this);
    }

}
