// Token lives in a module-level variable (fast, synchronous access for the
// axios interceptor) and is mirrored into sessionStorage so a page refresh
// doesn't force a re-login.
let token = sessionStorage.getItem('fleet_token') || null;

export function setToken(t) {
  token = t;
  if (t) {
    sessionStorage.setItem('fleet_token', t);
  } else {
    sessionStorage.removeItem('fleet_token');
  }
}

export function getToken() {
  return token;
}

export function isAuthenticated() {
  return !!token;
}
