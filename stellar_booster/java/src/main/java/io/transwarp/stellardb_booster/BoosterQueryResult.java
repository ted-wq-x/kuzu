package io.transwarp.stellardb_booster;

/**
 * BoosterQueryResult stores the result of a query execution.
 */
public class BoosterQueryResult {
    long qr_ref;
    boolean destroyed = false;

    /**
     * Check if the query result has been destroyed.
     * @throws BoosterObjectRefDestroyedException If the query result has been destroyed.
     */
    private void checkNotDestroyed() throws BoosterObjectRefDestroyedException {
        if (destroyed)
            throw new BoosterObjectRefDestroyedException("BoosterQueryResult has been destroyed.");
    }

    /**
     * Finalize.
     * @throws BoosterObjectRefDestroyedException If the query result has been destroyed.
     */
    @Override
    protected void finalize() throws BoosterObjectRefDestroyedException {
        destroy();
    }

    /**
     * Destroy the query result.
     * @throws BoosterObjectRefDestroyedException If the query result has been destroyed.
     */
    public void destroy() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        BoosterNative.query_result_destroy(this);
        destroyed = true;
    }

    /**
     * Check if the query is executed successfully.
     * @return Query is executed successfully or not.
     * @throws BoosterObjectRefDestroyedException If the query result has been destroyed.
     */
    public boolean isSuccess() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        return BoosterNative.query_result_is_success(this);
    }

    /**
     * Get the error message if any.
     * @return Error message of the query execution if the query fails.
     * @throws BoosterObjectRefDestroyedException If the query result has been destroyed.
     */
    public String getErrorMessage() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        return BoosterNative.query_result_get_error_message(this);
    }

    /**
     * Get the number of columns in the query result.
     * @return The number of columns in the query result.
     * @throws BoosterObjectRefDestroyedException If the query result has been destroyed.
     */
    public long getNumColumns() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        return BoosterNative.query_result_get_num_columns(this);
    }

    /**
     * Get the column name at the given index.
     * @param index: The index of the column.
     * @return The column name at the given index.
     * @throws BoosterObjectRefDestroyedException If the query result has been destroyed.
     */
    public String getColumnName(long index) throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        return BoosterNative.query_result_get_column_name(this, index);
    }

    /**
     * Get the column data type at the given index.
     * @param index: The index of the column.
     * @return The column data type at the given index.
     * @throws BoosterObjectRefDestroyedException If the query result has been destroyed.
     */
    public BoosterDataType getColumnDataType(long index) throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        return BoosterNative.query_result_get_column_data_type(this, index);
    }

    /**
     * Get the number of tuples in the query result.
     * @return The number of tuples in the query result.
     * @throws BoosterObjectRefDestroyedException If the query result has been destroyed.
     */
    public long getNumTuples() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        return BoosterNative.query_result_get_num_tuples(this);
    }

    /**
     * Get the query summary.
     * @return The query summary.
     * @throws BoosterObjectRefDestroyedException If the query result has been destroyed.
     */
    public BoosterQuerySummary getQuerySummary() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        return BoosterNative.query_result_get_query_summary(this);
    }

    /**
     * Return if the query result has next tuple or not.
     * @return Whether there are more tuples to read.
     * @throws BoosterObjectRefDestroyedException If the query result has been destroyed.
     */
    public boolean hasNext() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        return BoosterNative.query_result_has_next(this);
    }

    /**
     * Get the next tuple.
     * @return The next tuple.
     * @throws BoosterObjectRefDestroyedException If the query result has been destroyed.
     */
    public BoosterFlatTuple getNext() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        return BoosterNative.query_result_get_next(this);
    }

    /**
     * Convert the query result to string.
     * @return The string representation of the query result.
     */
    public String toString() {
        if (destroyed)
            return "BoosterQueryResult has been destroyed.";
        else
            return BoosterNative.query_result_to_string(this);
    }

    /**
     * Reset the query result iterator.
     * @throws BoosterObjectRefDestroyedException If the query result has been destroyed.
     */
    public void resetIterator() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        BoosterNative.query_result_reset_iterator(this);
    }
}
