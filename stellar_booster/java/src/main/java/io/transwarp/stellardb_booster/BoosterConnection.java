package io.transwarp.stellardb_booster;

import java.util.Map;

/**
 * BoosterConnection is used to interact with a BoosterDatabase instance. Each BoosterConnection is thread-safe. Multiple
 * connections can connect to the same BoosterDatabase instance in a multi-threaded environment.
 */
public class BoosterConnection {

    long conn_ref;
    boolean destroyed = false;

    /**
    * Creates a connection to the database.
    * @param db: BoosterDatabase instance.
    */
    public BoosterConnection(BoosterDatabase db) {
        if (db == null)
            throw new AssertionError("Cannot create connection, database is null.");
        conn_ref = BoosterNative.connection_init(db);
    }

    /**
    * Check if the connection has been destroyed.
    * @throws BoosterObjectRefDestroyedException If the connection has been destroyed.
    */
    private void checkNotDestroyed() throws BoosterObjectRefDestroyedException {
        if (destroyed)
            throw new BoosterObjectRefDestroyedException("BoosterConnection has been destroyed.");
    }

    /**
    * Finalize.
    * @throws BoosterObjectRefDestroyedException If the connection has been destroyed.
    */
    @Override
    protected void finalize() throws BoosterObjectRefDestroyedException {
        destroy();
    }

    /**
    * Destroy the connection.
    * @throws BoosterObjectRefDestroyedException If the connection has been destroyed.
    */
    public void destroy() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        BoosterNative.connection_destroy(this);
        destroyed = true;
    }

    /**
    * Return the maximum number of threads used for execution in the current connection.
    * @return The maximum number of threads used for execution in the current connection.
    * @throws BoosterObjectRefDestroyedException If the connection has been destroyed.
    */
    public long getMaxNumThreadForExec() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        return BoosterNative.connection_get_max_num_thread_for_exec(this);
    }

    /**
    * Sets the maximum number of threads to use for execution in the current connection.
    * @param numThreads: The maximum number of threads to use for execution in the current connection
    * @throws BoosterObjectRefDestroyedException If the connection has been destroyed.
    */
    public void setMaxNumThreadForExec(long numThreads) throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        BoosterNative.connection_set_max_num_thread_for_exec(this, numThreads);
    }

    /**
    * Executes the given query and returns the result.
    * @param queryStr: The query to execute.
    * @return The result of the query.
    * @throws BoosterObjectRefDestroyedException If the connection has been destroyed.
    */
    public BoosterQueryResult query(String queryStr) throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        return BoosterNative.connection_query(this, queryStr);
    }

    /**
    * Prepares the given query and returns the prepared statement.
    * @param queryStr: The query to prepare.
    * @return The prepared statement.
    * @throws BoosterObjectRefDestroyedException If the connection has been destroyed.
    */
    public BoosterPreparedStatement prepare(String queryStr) throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        return BoosterNative.connection_prepare(this, queryStr);
    }

    /**
    * Executes the given prepared statement with args and returns the result.
    * @param ps: The prepared statement to execute.
    * @param m: The parameter pack where each arg is a std::pair with the first element being parameter name and second
    * element being parameter value
    * @return The result of the query.
    * @throws BoosterObjectRefDestroyedException If the connection has been destroyed.
    */
    public BoosterQueryResult execute(BoosterPreparedStatement ps, Map<String, BoosterValue> m) throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        return BoosterNative.connection_execute(this, ps, m);
    }

    /**
    * Interrupts all queries currently executed within this connection.
    * @throws BoosterObjectRefDestroyedException If the connection has been destroyed.
    */
    public void interrupt() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        BoosterNative.connection_interrupt(this);
    }

    /**
    * Sets the query timeout value of the current connection. A value of zero (the default) disables the timeout.
    * @param timeoutInMs: The query timeout value in milliseconds.
    * @throws BoosterObjectRefDestroyedException If the connection has been destroyed.
    */
    public void setQueryTimeout(long timeoutInMs) throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        BoosterNative.connection_set_query_timeout(this, timeoutInMs);
    }
}
