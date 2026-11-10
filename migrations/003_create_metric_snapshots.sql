-- 003_create_metric_snapshots.sql
-- Time-series metric snapshots collected from agents by the PrometheusPoller.

CREATE TABLE metric_snapshots (
    id               BIGSERIAL PRIMARY KEY,
    node_id          UUID NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,
    collected_at     TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    cpu_percent      DOUBLE PRECISION,
    mem_used_bytes   BIGINT,
    mem_total_bytes  BIGINT,
    disk_read_rate   DOUBLE PRECISION,
    disk_write_rate  DOUBLE PRECISION,
    net_rx_rate      DOUBLE PRECISION,
    net_tx_rate      DOUBLE PRECISION,
    load_avg_1m      DOUBLE PRECISION,
    load_avg_5m      DOUBLE PRECISION,
    load_avg_15m     DOUBLE PRECISION
);

CREATE INDEX idx_snapshots_node_time ON metric_snapshots (node_id, collected_at DESC);

-- Retention: keep only 24 hours of snapshots. Since this project uses plain
-- .sql migrations run manually (no scheduler framework), retention is enforced
-- by PrometheusPoller itself, which runs this statement once per poll cycle:
--   DELETE FROM metric_snapshots WHERE collected_at < NOW() - INTERVAL '24 hours';
