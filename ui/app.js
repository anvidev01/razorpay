'use strict';

const $ = (id) => document.getElementById(id);
const chat = $('chat'), walEl = $('wal'), verdictEl = $('verdict');
const rupees = (p) => '₹' + (p / 100).toLocaleString('en-IN', { minimumFractionDigits: 2 });

const MANDATE = 'mnd_8f21c4';
const LUNCH = [
  { sku: 'SKU_MEAL_THALI_001', unit_paise: 24000, qty: 1, label: 'Veg thali' },
  { sku: 'SKU_DRINK_LIME_007', unit_paise:  6000, qty: 2, label: 'Fresh lime soda' },
  { sku: 'SKU_SIDE_RAITA_014', unit_paise:  4500, qty: 1, label: 'Boondi raita' },
];
const BLENDER = { sku: 'SKU_APPLIANCE_BLENDER_5', unit_paise: 600000, qty: 1, label: 'NutriPro 900W blender' };

let seen = new Set();

function msg(kind, who, html) {
  const d = document.createElement('div');
  d.className = 'msg ' + kind;
  d.innerHTML = (who ? `<span class="who">${who}</span>` : '') + html;
  chat.appendChild(d);
  chat.scrollTop = chat.scrollHeight;
  return d;
}

function cartHtml(lines) {
  const rows = lines.map(l =>
    `<div><span>${l.label} ${l.qty > 1 ? '× ' + l.qty : ''}</span><span>${rupees(l.unit_paise * l.qty)}</span></div>`
  ).join('');
  const total = lines.reduce((s, l) => s + l.unit_paise * l.qty, 0);
  return `<div class="cart">${rows}<div style="border-top:1px solid rgba(255,255,255,.14);margin-top:6px;padding-top:6px">
    <span>total</span><span>${rupees(total)}</span></div></div>`;
}

async function decide(lines) {
  const body = JSON.stringify({
    mandate_id: MANDATE, merchant: 'swiggy',
    lines: lines.map(({ sku, unit_paise, qty }) => ({ sku, unit_paise, qty })),
  });
  const r = await fetch('/api/decide', { method: 'POST', body });
  const j = await r.json();
  renderVerdict(j);
  await refreshWal();
  return j;
}

function renderVerdict(j) {
  const d = j.decision, allow = d.decision === 'ALLOW';
  const reasons = (d.reasons || []).map(r =>
    `<div class="reason"><code>${r.code}</code><span>${r.detail}</span></div>`).join('');
  const lines = (d.lines || []).map(l =>
    `<div class="l"><span class="pill ${l.ok ? 'ok' : 'no'}">${l.ok ? 'ok' : 'deny'}</span>
       <span class="sku">${l.sku}</span></div>`).join('');
  verdictEl.className = 'verdict ' + (allow ? 'allow' : 'deny');
  verdictEl.innerHTML = `
    <div class="vtop">
      <span class="vbadge">${allow ? 'ALLOW' : 'DENY'}</span>
      <span class="vbits">${d.verdict_hex}</span>
      <span class="vmeta">kernel ${(j.kernel_ns_batched || 0).toFixed(1)} ns<br>
        durable in ${d.commit_us} µs · wal seq ${d.wal_seq}</span>
    </div>
    <div style="font-family:var(--mono);font-size:12.5px;color:var(--muted);margin-top:7px">
      cart total ${rupees(d.cart_total_paise)}</div>
    ${reasons ? `<div class="reasons">${reasons}</div>` : ''}
    ${lines ? `<div class="lines">${lines}</div>` : ''}
    <div class="gate"><b>${d.capability_issued ? 'capability token minted' : 'no capability token'}</b> —
      ${d.capability_issued
        ? 'bound to this cart hash, single use, 60 s TTL'
        : 'this cart physically cannot reach the payment rail'}</div>`;
}

async function refreshWal() {
  const r = await fetch('/api/wal');
  const j = await r.json();
  $('chain').textContent = j.intact ? `chain intact · ${j.records} records` : 'CHAIN BROKEN';
  $('chain').className = 'chip' + (j.intact ? '' : ' bad');
  if (!j.rows.length) { walEl.innerHTML = '<div class="empty">log empty</div>'; return; }
  walEl.innerHTML = j.rows.map(r => {
    const isNew = !seen.has(r.seq);
    const dec = r.type === 'POLICY_DECISION';
    return `<div class="walrow ${dec ? 'dec' : ''} ${isNew ? 'new' : ''}">
      <span class="seq">${r.seq}</span>
      <span class="ty">${r.type}${r.verdict ? ' · ' + r.verdict : ''}</span>
      <span class="hx">${r.hash}</span></div>`;
  }).join('');
  j.rows.forEach(r => seen.add(r.seq));
  walEl.scrollTop = walEl.scrollHeight;
}

// ---- the demo beats ----
$('b1').onclick = async () => {
  $('b1').disabled = true;
  msg('user', null, 'order me lunch, keep it under ₹500');
  msg('sys', null, `mandate ${MANDATE} signed by user · Ed25519 · budget ₹500 · TTL 15 min`);
  $('mandate-state').textContent = 'mandate active · ₹500 cap';
  await new Promise(r => setTimeout(r, 450));
  msg('agent', 'agent', 'Found a thali combo from Saravana Bhavan.' + cartHtml(LUNCH));
  const j = await decide(LUNCH);
  if (j.decision.decision === 'ALLOW') msg('sys', null, '✓ paid — capability token issued, order placed');
  $('b2').disabled = false;
};

$('b2').onclick = async () => {
  $('b2').disabled = true;
  msg('user', null, 'actually, also add something to make smoothies');
  await new Promise(r => setTimeout(r, 450));
  const cart = [...LUNCH, BLENDER];
  msg('agent', 'agent', 'Adding a blender to your order.' + cartHtml(cart));
  const j = await decide(cart);
  const rem = (j.repair.remove || []).join(', ');
  msg('sys', null, `✗ blocked — ${j.decision.reasons.length} policy violations. repair hint: remove ${rem}`);
  msg('push', 'push notification', 'I blocked a ₹6,000 blender your assistant tried to add. Your ₹405 lunch is on the way. Did you actually want the blender?');
  $('b3').disabled = false;
  $('b4').disabled = false;
};

$('b3').onclick = async () => {
  $('b3').disabled = true;
  msg('agent', 'agent', 'That item is outside what you approved. Dropping it and re-submitting the lunch.');
  await new Promise(r => setTimeout(r, 400));
  const j = await decide(LUNCH);
  if (j.decision.decision === 'ALLOW')
    msg('sys', null, '✓ lunch ordered — the user\'s actual goal still completed');
};

$('b4').onclick = async () => {
  $('b4').disabled = true;
  msg('user', null, 'yes, I did want the blender');
  msg('sys', null, 'step-up: user MFA → new mandate mnd_blender signed, appliance category, ₹7,000 cap');
  await new Promise(r => setTimeout(r, 500));
  const intent = {
    mandate_id: 'mnd_blender',
    not_before_ns: (Date.now() - 60000) * 1e6,
    not_after_ns: (Date.now() + 900000) * 1e6,
    total_budget_paise: 700000,
    merchant_allow: ['swiggy'],
    constraints: [{ sku: BLENDER.sku, max_unit_paise: 700000, max_qty: 1 }],
  };
  await fetch('/api/admit', { method: 'POST', body: JSON.stringify(intent) });
  const body = JSON.stringify({
    mandate_id: 'mnd_blender', merchant: 'swiggy',
    lines: [{ sku: BLENDER.sku, unit_paise: BLENDER.unit_paise, qty: 1 }],
  });
  const j = await (await fetch('/api/decide', { method: 'POST', body })).json();
  renderVerdict(j); await refreshWal();
  msg('sys', null, j.decision.decision === 'ALLOW'
    ? '✓ same engine, same rules — now the blender is IN intent, so it is allowed'
    : '✗ still denied');
};

$('reset').onclick = () => location.reload();

refreshWal();
setInterval(refreshWal, 2000);
