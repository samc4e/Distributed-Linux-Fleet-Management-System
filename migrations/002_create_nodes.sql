-- 002_create_nodes.sql
-- Registry of managed Linux nodes.

CREATE TABLE nodes (
    id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    hostname     VARCHAR(253) NOT NULL,
    ip_address   INET NOT NULL,
    agent_port   INTEGER NOT NULL DEFAULT 9200,
    agent_token  TEXT NOT NULL,
    os_info      JSONB,
    hardware_info JSONB,
    labels       JSONB DEFAULT '{}',
    status       VARCHAR(32) NOT NULL DEFAULT 'pending'
                 CHECK (status IN ('pending','online','offline','error')),
    last_seen    TIMESTAMPTZ,
    onboarded_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_nodes_agent_token ON nodes (agent_token);
CREATE INDEX idx_nodes_status ON nodes (status);
