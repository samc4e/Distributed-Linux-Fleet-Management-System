import { Link, useLocation, useNavigate } from 'react-router-dom';
import { logout } from '../api';
import { setToken } from '../auth';

export default function Navbar() {
  const location = useLocation();
  const navigate = useNavigate();

  const handleLogout = async () => {
    try {
      await logout();
    } catch (err) {
      // Ignore - the local token is cleared regardless below.
    }
    setToken(null);
    navigate('/login');
  };

  return (
    <div className="navbar">
      <div className="navbar-left">
        <span className="navbar-brand">FleetManager</span>
        <div className="navbar-links">
          <Link to="/dashboard" className={location.pathname === '/dashboard' ? 'active' : ''}>
            Dashboard
          </Link>
          <Link to="/nodes" className={location.pathname.startsWith('/nodes') ? 'active' : ''}>
            Nodes
          </Link>
        </div>
      </div>
      <button className="secondary" onClick={handleLogout}>
        Logout
      </button>
    </div>
  );
}
