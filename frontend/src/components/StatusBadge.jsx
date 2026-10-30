const LABELS = {
  online: 'Online',
  offline: 'Offline',
  pending: 'Pending',
  error: 'Error',
};

export default function StatusBadge({ status }) {
  const key = LABELS[status] ? status : 'offline';
  return (
    <span className="status-badge">
      <span className={`status-dot ${key}`} />
      {LABELS[key]}
    </span>
  );
}
