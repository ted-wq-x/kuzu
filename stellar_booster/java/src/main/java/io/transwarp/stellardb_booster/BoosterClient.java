package io.transwarp.stellardb_booster;

import java.util.Iterator;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;

public class BoosterClient {
    public static String DB_PATH = "booster.db.path";
    //bytes ,0表示0.8*物理内存
    private static final String DB_BUFFER_POOL_SIZE = "booster.db.buffer.pool.size";
    private static final String DB_COMPRESSION = "booster.db.compression";
    private static final String DB_CPU_AFFINITY = "booster.db.cpu.affinity";
    private static final String DB_LRU_CACHE_SIZE = "booster.db.lru.cache.size";
    private static final String DB_READ_ONLY = "booster.db.readonly";
    private static final String DB_EXEC_THREAD_NUM = "booster.db.exec.thread.num";

    private static final ConcurrentHashMap<String, BoosterClient> instances = new ConcurrentHashMap<String, BoosterClient>();

    public static int concurrency = -1;
    public static boolean curReadOnly = false;
    private static boolean preReadOnly = false;

    private static boolean isMemMode = System.getProperty(DB_PATH, "").isEmpty();

    public static class BoosterOperationResult {
        private String info;
        private boolean isSuccess;

        public BoosterOperationResult(String info, boolean isSuccess) {
            this.info = info;
            this.isSuccess = isSuccess;
        }

        public String getInfo() {
            return info;
        }

        public boolean isSuccess() {
            return isSuccess;
        }

    }

    public static Map<String, Integer> show() {
        ConcurrentHashMap<String, Integer> retMap = new ConcurrentHashMap<>();
        for (Map.Entry<String, BoosterClient> entry :
                instances.entrySet()) {
            retMap.put(entry.getKey(), entry.getValue().refCount.get());
        }
        return retMap;
    }

    public static BoosterClient getInstance(String graphName) throws BoosterObjectRefDestroyedException {
        if (preReadOnly != curReadOnly) {
            synchronized (BoosterClient.class) {
                finalizeAll();
                instances.clear();
            }
            preReadOnly = curReadOnly;

        }
        BoosterClient instance = instances.get(graphName);

        if (instance == null) {
            synchronized (BoosterClient.class) {
                if (instance == null) {
                    if (isMemMode && instances.size() > 1) {
                        throw new RuntimeException("In mem mode,only support 1 database,If you want multi databases,please set quark server jvm options:-D" + DB_PATH);
                    }
                    instance = new BoosterClient(graphName, curReadOnly);
                    instances.put(graphName, instance);
                }
            }
        }
        instance.refCount.addAndGet(1);
        return instance;
    }

    public static BoosterOperationResult destroyInstance(String graphName) throws BoosterObjectRefDestroyedException {
        BoosterClient instance = instances.get(graphName);
        if (instance != null) {
            int refCount = instance.refCount.decrementAndGet();
            if (refCount == 0) {
                synchronized (BoosterClient.class) {
                    if (instance.refCount.get() == 0) {
                        instances.remove(graphName);
                        instance.clear();
                        return new BoosterOperationResult("destroy $graphName's instance success", true);
                    } else {
                        return new BoosterOperationResult("$graphName's instance ref:" + refCount, false);
                    }
                }
            } else {
                return new BoosterOperationResult("$graphName's instance ref:" + refCount, false);
            }

        }
        return new BoosterOperationResult("$graphName's instance not exists'", false);
    }

    public static void destroyInstanceForce(String graphName) throws BoosterObjectRefDestroyedException {
        BoosterClient instance = instances.get(graphName);
        if (instance != null) {
            synchronized (BoosterClient.class) {
                instances.remove(graphName);
                instance.clear();
            }
        }
    }

    public static void finalizeAll() throws BoosterObjectRefDestroyedException {
        for (BoosterClient boosterClient : instances.values()) {
            boosterClient.finalize();
        }
    }

    public void finalize() throws BoosterObjectRefDestroyedException {
        this.database.close();
    }

    public final String graphName;
    private final BoosterDatabase database;
    private final AtomicInteger refCount = new AtomicInteger(0);

    private BoosterClient(String graphName, boolean readOnly) {
        this.graphName = graphName;
        String path = System.getProperty(DB_PATH, "");
        long poolSize = Long.parseLong(System.getProperty(DB_BUFFER_POOL_SIZE, "-1"));
        boolean enableCompression = Boolean.parseBoolean(System.getProperty(DB_COMPRESSION, "true"));
        boolean enableCpuAffinity = Boolean.parseBoolean(System.getProperty(DB_CPU_AFFINITY, "false"));
        int lruCacheSize = Integer.parseInt(System.getProperty(DB_LRU_CACHE_SIZE, "-1"));
        boolean _readOnly = Boolean.parseBoolean(System.getProperty(DB_READ_ONLY, "false"));
        if (_readOnly) {
            readOnly = true;
        }
        this.database = new BoosterDatabase(path + "/" + graphName, poolSize, enableCompression, readOnly, -1L, enableCpuAffinity, lruCacheSize);

    }

    public boolean isDestroyed() {
        return refCount.get() == 0;
    }

    public void clear() throws BoosterObjectRefDestroyedException {
        refCount.set(0);
        database.close();
    }

    public BoosterConnection createConnection() throws BoosterObjectRefDestroyedException {
        assertDB();
        BoosterConnection connection = new BoosterConnection(database);
        int execThreadNum = Integer.parseInt(System.getProperty(DB_EXEC_THREAD_NUM, "-1"));
        if (execThreadNum != -1) {
            connection.setMaxNumThreadForExec(execThreadNum);
        }
        return connection;
    }

    private void assertDB() {
        // should not happen
        assert database != null;
    }

}
