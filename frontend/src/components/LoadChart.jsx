import { ResponsiveContainer, LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, Legend } from 'recharts';

function formatTime(value) {
  const d = new Date(value);
  if (Number.isNaN(d.getTime())) return '';
  return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
}

export default function LoadChart({ data }) {
  if (!data || data.length === 0) {
    return (
      <div className="chart-card">
        <h3>Load Average</h3>
        <div className="chart-empty">No data yet</div>
      </div>
    );
  }

  return (
    <div className="chart-card">
      <h3>Load Average</h3>
      <ResponsiveContainer width="100%" height={220}>
        <LineChart data={data}>
          <CartesianGrid strokeDasharray="3 3" stroke="#2a323e" />
          <XAxis dataKey="collected_at" tickFormatter={formatTime} stroke="#8b96a5" fontSize={12} />
          <YAxis stroke="#8b96a5" fontSize={12} />
          <Tooltip
            labelFormatter={formatTime}
            formatter={(value, name) => [Number(value).toFixed(2), name]}
            contentStyle={{ background: '#161c24', border: '1px solid #2a323e' }}
          />
          <Legend />
          <Line type="monotone" dataKey="load_avg_1m" name="1m" stroke="#00ff88" dot={false} />
          <Line type="monotone" dataKey="load_avg_5m" name="5m" stroke="#ffaa00" dot={false} />
          <Line type="monotone" dataKey="load_avg_15m" name="15m" stroke="#ff4444" dot={false} />
        </LineChart>
      </ResponsiveContainer>
    </div>
  );
}
