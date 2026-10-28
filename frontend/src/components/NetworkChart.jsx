import { ResponsiveContainer, LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, Legend } from 'recharts';

function formatTime(value) {
  const d = new Date(value);
  if (Number.isNaN(d.getTime())) return '';
  return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
}

function formatBytes(value) {
  if (value == null) return '0 B/s';
  const units = ['B/s', 'KB/s', 'MB/s', 'GB/s'];
  let v = value;
  let i = 0;
  while (v >= 1024 && i < units.length - 1) {
    v /= 1024;
    i += 1;
  }
  return `${v.toFixed(1)} ${units[i]}`;
}

export default function NetworkChart({ data }) {
  if (!data || data.length === 0) {
    return (
      <div className="chart-card">
        <h3>Network Traffic</h3>
        <div className="chart-empty">No data yet</div>
      </div>
    );
  }

  return (
    <div className="chart-card">
      <h3>Network Traffic</h3>
      <ResponsiveContainer width="100%" height={220}>
        <LineChart data={data}>
          <CartesianGrid strokeDasharray="3 3" stroke="#2a323e" />
          <XAxis dataKey="collected_at" tickFormatter={formatTime} stroke="#8b96a5" fontSize={12} />
          <YAxis stroke="#8b96a5" fontSize={12} tickFormatter={formatBytes} />
          <Tooltip
            labelFormatter={formatTime}
            formatter={(value, name) => [formatBytes(value), name]}
            contentStyle={{ background: '#161c24', border: '1px solid #2a323e' }}
          />
          <Legend />
          <Line type="monotone" dataKey="net_rx_rate" name="RX bytes/s" stroke="#4a9eff" dot={false} />
          <Line type="monotone" dataKey="net_tx_rate" name="TX bytes/s" stroke="#ffaa00" dot={false} />
        </LineChart>
      </ResponsiveContainer>
    </div>
  );
}
