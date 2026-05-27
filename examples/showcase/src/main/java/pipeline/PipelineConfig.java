package pipeline;

/**
 * PipelineConfig -- configuration for the data pipeline.
 * Referenced by types.topo via std::import.
 */
public class PipelineConfig {
    public int maxBatchSize;
    public int numWorkers;
    public int bufferSizeKb;
    public boolean enableCompression;
}
