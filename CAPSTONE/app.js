// app.js — shared core for every CampusFind AI page.
const API_BASE = "http://localhost:8080/api";

const Auth = {
  get user() {
    try { return JSON.parse(localStorage.getItem("cf_user") || "null"); }
    catch (e) { return null; }
  },
  set user(u) { localStorage.setItem("cf_user", JSON.stringify(u)); },
  logout() { localStorage.removeItem("cf_user"); window.location.href = "index.html"; },
  requireLogin() {
    if (!this.user) window.location.href = "index.html";
    return this.user;
  },
  isAdmin() { return this.user && this.user.role === "ADMIN"; }
};

async function api(path, options = {}) {
  const headers = Object.assign({ "Content-Type": "application/json" }, options.headers || {});
  if (Auth.user) headers["X-User-Id"] = Auth.user.userId;
  const res = await fetch(API_BASE + path, Object.assign({}, options, { headers }));
  let body = null;
  try { body = await res.json(); } catch (e) { /* empty body */ }
  if (!res.ok) {
    const msg = (body && body.error) ? body.error : ("Request failed (" + res.status + ")");
    const err = new Error(msg);
    err.status = res.status;
    err.body = body;
    throw err;
  }
  return body;
}

function toast(message, type = "info") {
  let box = document.querySelector(".toast-container");
  if (!box) {
    box = document.createElement("div");
    box.className = "toast-container";
    document.body.appendChild(box);
  }
  const t = document.createElement("div");
  t.className = "toast " + type;
  t.textContent = message;
  box.appendChild(t);
  setTimeout(() => t.remove(), 3800);
}

function confidenceBadgeClass(conf) {
  if (!conf) return "badge-weak";
  if (conf.includes("VERY STRONG")) return "badge-strong";
  if (conf.includes("STRONG")) return "badge-strong";
  if (conf.includes("POSSIBLE")) return "badge-possible";
  if (conf.includes("WEAK")) return "badge-weak";
  return "badge-unlikely";
}

function statusBadgeClass(status) {
  const s = (status || "").toUpperCase();
  if (s === "ACTIVE") return "badge-active";
  if (s === "MATCHED") return "badge-matched";
  if (s === "RETURNED") return "badge-returned";
  if (s === "PENDING") return "badge-pending";
  if (s === "UNDER REVIEW") return "badge-pending";
  if (s === "APPROVED") return "badge-approved";
  if (s === "REJECTED") return "badge-rejected";
  return "badge-pending";
}

function initials(name) {
  if (!name) return "?";
  return name.split(" ").map(p => p[0]).slice(0, 2).join("").toUpperCase();
}

function scoreRingSVG(score, size = 74) {
  const r = (size - 7) / 2;
  const c = 2 * Math.PI * r;
  const offset = c - (score / 100) * c;
  return `<div class="score-ring" style="width:${size}px;height:${size}px">
    <svg width="${size}" height="${size}">
      <circle class="track" cx="${size/2}" cy="${size/2}" r="${r}"></circle>
      <circle class="fill" cx="${size/2}" cy="${size/2}" r="${r}" stroke-dasharray="${c}" stroke-dashoffset="${offset}"></circle>
    </svg>
    <div class="num">${score}%</div>
  </div>`;
}

const NAV_ITEMS = [
  { href: "dashboard.html", icon: "&#9673;", label: "Dashboard" },
  { href: "report-lost.html", icon: "&#9888;", label: "Report Lost" },
  { href: "register-found.html", icon: "&#9873;", label: "Register Found" },
  { href: "smart-matching.html", icon: "&#10022;", label: "Smart Matching" },
  { href: "search.html", icon: "&#8981;", label: "Search" },
  { href: "claims.html", icon: "&#9993;", label: "Claims" },
  { href: "history.html", icon: "&#8635;", label: "History" },
  { href: "analytics.html", icon: "&#9776;", label: "Analytics" },
];

function renderShell(activeHref, pageTitle, pageSub) {
  const user = Auth.requireLogin();
  if (!user) return;

  const navHtml = NAV_ITEMS.map(item =>
    `<a class="nav-link ${item.href === activeHref ? "active" : ""}" href="${item.href}">
      <span class="ic">${item.icon}</span>${item.label}
    </a>`
  ).join("");

  const adminLink = user.role === "ADMIN"
    ? `<a class="nav-link ${activeHref === "admin.html" ? "active" : ""}" href="admin.html"><span class="ic">&#9878;</span>Admin</a>`
    : "";

  document.body.innerHTML = `
    <div class="app-shell">
      <aside class="sidebar">
        <div class="brand-mark">
          <span class="glyph">C</span>
          <span class="name">CampusFind AI</span>
        </div>
        <div class="tagline">Find · Match · Verify · Return</div>
        <div class="nav-group">
          <div class="nav-label">Workspace</div>
          ${navHtml}
        </div>
        ${adminLink ? `<div class="nav-group"><div class="nav-label">Administration</div>${adminLink}</div>` : ""}
        <div class="sidebar-foot">
          <div class="sidebar-user">
            <div class="dot">${initials(user.name)}</div>
            <div class="who">
              <div class="u-name">${user.name}</div>
              <div class="u-role">${user.role}</div>
            </div>
          </div>
          <a class="logout-link" href="#" id="logoutBtn">Sign out</a>
        </div>
      </aside>
      <div class="main">
        <div class="topbar">
          <div>
            <div class="page-title">${pageTitle}</div>
            <div class="page-sub">${pageSub || ""}</div>
          </div>
          <div class="topbar-actions">
            <a class="bell" href="notifications.html" title="Notifications">&#128276;<span class="badge" id="notifBadge" style="display:none">0</span></a>
          </div>
        </div>
        <div class="content" id="pageContent"></div>
      </div>
    </div>
  `;
  document.getElementById("logoutBtn").addEventListener("click", (e) => { e.preventDefault(); Auth.logout(); });
  loadNotifBadge();
  return document.getElementById("pageContent");
}

async function loadNotifBadge() {
  try {
    const notifs = await api("/notifications?userId=" + encodeURIComponent(Auth.user.userId));
    const unread = notifs.filter(n => !n.isRead).length;
    const badge = document.getElementById("notifBadge");
    if (badge && unread > 0) { badge.style.display = "inline-flex"; badge.textContent = unread; }
  } catch (e) { /* silent */ }
}

// Reads a <input type=file> selection into a base64 data URL for the
// imageBase64 field the backend expects. Resolves to "" if no file chosen.
function readImageAsDataUrl(inputEl) {
  return new Promise((resolve, reject) => {
    const file = inputEl.files && inputEl.files[0];
    if (!file) { resolve(""); return; }
    const reader = new FileReader();
    reader.onload = () => resolve(reader.result);
    reader.onerror = reject;
    reader.readAsDataURL(file);
  });
}

function escapeHtml(str) {
  const div = document.createElement("div");
  div.textContent = str == null ? "" : String(str);
  return div.innerHTML;
}
