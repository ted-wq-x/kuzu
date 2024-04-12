package io.transwarp.stellardb_booster;

/**
 * QuerySummary stores the execution time, plan, compiling time and query options of a query.
 */
public class BoosterQuerySummary {

    double parTime;
    double cmpTime;
    double exeTime;

    /**
     * Construct a new query summary.
     * @param cmpTime: The compiling time of the query.
     * @param exeTime: The execution time of the query.
     */
    public BoosterQuerySummary(double cmpTime, double exeTime, double parTime) {
        this.cmpTime = cmpTime;
        this.exeTime = exeTime;
        this.parTime = parTime;
    }

    /**
     * Get the compiling time of the query.
     * @return The compiling time of the query.
     */
    public double getCompilingTime() {
        return cmpTime;
    }

    /**
     * Get the execution time of the query.
     * @return The execution time of the query.
     */
    public double getExecutionTime() {
        return exeTime;
    }

    public double getParsingTime() {
        return parTime;
    }

    @Override
    public String toString() {
        return "[" +
                "parTime=" + parTime +
                ", cmpTime=" + cmpTime +
                ", exeTime=" + exeTime +
                ']';
    }
}
