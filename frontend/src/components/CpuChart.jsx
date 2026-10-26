import { ResponsiveContainer, AreaChart, Area, XAxis, YAxis, CartesianGrid, Tooltip } from 'recharts';

function formatTime(value) {
  const d = new Date(value);
  if (Number.isNaN(d.getTime())) return '';
  return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
}

export default function CpuChart({ data }) {
  if (!data || data.length === 0) {
    return (
      <div className="chart-card">
        <h3>CPU Usage</h3>
        <div className="chart-empty">No data yet</div>
      </div>
    );
  }

  return (
    <div className="chart-card">
      <h3>CPU Usage</h3>
      <ResponsiveContainer width="100%" height={220}>
        <AreaChart data={data}>
          <CartesianGrid strokeDasharray="3 3" stroke="#2a323e" />
          <XAxis dataKey="collected_at" tickFormatter={formatTime} stroke="#8b96a5" fontSize={12} />
          <YAxis
            domain={[0, 100]}
            stroke="#8b96a5"
            fontSize={12}
            label={{ value: 'CPU %', angle: -90, position: 'insideLeft', fill: '#8b96a5' }}
          />
          <Tooltip
            labelFormatter={formatTime}
            formatter={(value) => [`${Number(value).toFixed(1)}%`, 'CPU']}
            contentStyle={{ background: '#161c24', border: '1px solid #2a323e' }}
          />
          <Area type="monotone" dataKey="cpu_percent" stroke="#4a9eff" fill="#4a9eff" fillOpacity={0.25} />
        </AreaChart>
      </ResponsiveContainer>
    </div>
  );
}
