import StatusBadge from './StatusBadge';

function relativeTime(dateString) {
  if (!dateString) return 'Never';
  const then = new Date(dateString).getTime();
  if (Number.isNaN(then)) return 'Never';
  const seconds = Math.floor((Date.now() - then) / 1000);
  if (seconds < 5) return 'just now';
  if (seconds < 60) return `${seconds}s ago`;
  const minutes = Math.floor(seconds / 60);
  if (minutes < 60) return `${minutes}m ago`;
  const hours = Math.floor(minutes / 60);
  if (hours < 24) return `${hours}h ago`;
  const days = Math.floor(hours / 24);
  return `${days}d ago`;
}

export default function NodeCard({ node, onClick }) {
  const memPct =
    node.mem_total_bytes && node.mem_used_bytes != null
      ? ((node.mem_used_bytes / node.mem_total_bytes) * 100).toFixed(1)
      : null;

  return (
    <div className="node-card" onClick={onClick}>
      <div className="node-card-header">
        <span className="hostname">{node.hostname}</span>
        <StatusBadge status={node.status} />
      </div>
      <div className="ip">{node.ip_address}</div>
      <div className="node-card-metrics">
        <span>CPU</span>
        <strong>{node.cpu_percent != null ? `${node.cpu_percent.toFixed(1)}%` : 'N/A'}</strong>
      </div>
      <div className="node-card-metrics">
        <span>Memory</span>
        <strong>{memPct != null ? `${memPct}%` : 'N/A'}</strong>
      </div>
      <div className="last-seen">Last seen: {relativeTime(node.last_seen)}</div>
    </div>
  );
}
