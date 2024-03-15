package io.transwarp.stellardb_booster;

/**
* The BoosterDatabase class is the main class of BoosterDB. It manages all database components.
*/
public class BoosterDatabase {
    long db_ref;
    String db_path;
    long buffer_size;
    long max_db_size;
    boolean enableCompression = true;
    boolean destroyed = false;
    boolean readOnly = false;

    /**
     * Creates a database object.
     * @param databasePath: Database path. If the database does not already exist, it will be created.
     */
    public BoosterDatabase(String databasePath) {
        this.db_path = databasePath;
        this.buffer_size = 0;
        this.max_db_size = 0;
        db_ref = BoosterNative.database_init(databasePath, 0, true, false, max_db_size);
    }

    /**
    * Creates a database object.
    * @param databasePath: Database path. If the database does not already exist, it will be created.
    * @param bufferPoolSize: Max size of the buffer pool in bytes.
    * @param enableCompression: Enable compression in storage.
    * @param readOnly: Open the database in READ_ONLY mode.
    * @param maxDBSize: The maximum size of the database in bytes. Note that this is introduced
    * temporarily for now to get around with the default 8TB mmap address space limit some
    * environment.
    */
    public BoosterDatabase(String databasePath, long bufferPoolSize, boolean enableCompression, boolean readOnly, long maxDBSize) {
        this.db_path = databasePath;
        this.buffer_size = bufferPoolSize;
        this.enableCompression = enableCompression;
        this.readOnly = readOnly;
        this.max_db_size = maxDBSize;
        db_ref = BoosterNative.database_init(databasePath, bufferPoolSize, enableCompression, readOnly, maxDBSize);
    }

    /**
    * Sets the logging level of the database instance.
    * @param loggingLevel: New logging level. (Supported logging levels are: 'info', 'debug', 'err').
    */
    public static void setLoggingLevel(String loggingLevel) {
        BoosterNative.database_set_logging_level(loggingLevel);
    }

    /**
    * Checks if the database instance has been destroyed.
    * @throws BoosterObjectRefDestroyedException If the database instance is destroyed.
    */
    private void checkNotDestroyed() throws BoosterObjectRefDestroyedException {
        if (destroyed)
            throw new BoosterObjectRefDestroyedException("BoosterDatabase has been destroyed.");
    }

    /**
    * Finalize.
    * @throws BoosterObjectRefDestroyedException If the database instance has been destroyed.
    */
    @Override
    protected void finalize() throws BoosterObjectRefDestroyedException {
        destroy();
    }

    /**
    * Destroy the database instance.
    * @throws BoosterObjectRefDestroyedException If the database instance has been destroyed.
    */
    public void destroy() throws BoosterObjectRefDestroyedException {
        checkNotDestroyed();
        BoosterNative.database_destroy(this);
        destroyed = true;
    }
}
