package io.transwarp.stellardb_booster;

/**
 * BoosterInternalID type which stores the table_id and offset of a node/rel.
 */
public class BoosterInternalID {
    public long tableId;
    public long offset;

    /**
     * Create a BoosterInternalID from the given table_id and offset.
     * @param tableId: The table_id of the node/rel.
     * @param offset: The offset of the node/rel.
     */
    public BoosterInternalID(long tableId, long offset) {
        this.tableId = tableId;
        this.offset = offset;
    }
}
