// Fernansicht von Laser-Dilettant (planung.md 3.41): fragt jede Sekunde den Zustand ab und schickt Befehle.
// Alles läuft über /api/... mit dem Sitzungs-Cookie; die Seite selbst enthält keine Geheimnisse.
'use strict';
const $ = (s) => document.querySelector(s);

let status = null;          // letzte Antwort von /api/status
let offline = false;        // das Programm auf dem Rechner ist nicht erreichbar
let loggedIn = false;
let touched = { spd: 0, pwr: 0 };   // wann der Regler zuletzt angefasst wurde (dann nicht überschreiben)

const zeit = (s) => {
  s = Math.max(0, Math.round(s));
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), x = s % 60;
  return (h ? h + ':' + String(m).padStart(2, '0') : m) + ':' + String(x).padStart(2, '0');
};
function snack(text) {
  const e = $('#snackbar');
  e.textContent = text;
  e.classList.add('zeigen');
  clearTimeout(snack.t);
  snack.t = setTimeout(() => e.classList.remove('zeigen'), 2600);
}

async function api(path, body) {
  const options = { credentials: 'same-origin', cache: 'no-store' };
  if (body !== undefined) {
    options.method = 'POST';
    options.headers = { 'Content-Type': 'application/json', 'X-LD-Request': '1' };
    options.body = JSON.stringify(body);
  }
  const r = await fetch(path, options);
  let data = {};
  try { data = await r.json(); } catch (e) { /* leere Antwort */ }
  return { status: r.status, data };
}

function zeigeAnmeldung(show) {
  if (show) { vorschauRevision = null; $('#vorschau').removeAttribute('src'); }
  loggedIn = !show;
  $('#ansichtLogin').hidden = !show;
  $('#ansichtMain').hidden = show;
  $('#btnLogout').hidden = show;
  $('#verbindung').hidden = show;
  if (show) $('#pw').focus();
}

// ---- Zeichnen ----
function zeichne() {
  if (!loggedIn || !status) return;
  const klassen = { lauf: '', pause: 'pause', alarm: 'alarm', fertig: 'fertig', aus: 'aus', leer: 'aus' };
  const namen = { lauf: 'Läuft', pause: 'Pause', alarm: 'ALARM', fertig: 'Fertig', aus: 'Keine Verbindung zum Laser', leer: 'Kein Auftrag' };
  const state = offline ? 'aus' : status.state;
  const aktiv = !offline && (state === 'lauf' || state === 'pause');

  $('#verbindung').textContent = offline ? 'Programm nicht erreichbar' : (status.state === 'aus' ? 'Laser getrennt' : 'Verbunden');
  $('#verbindung').classList.toggle('aus', offline || status.state === 'aus');
  $('#zustand').className = 'zustand ' + klassen[state];
  $('#zustand').textContent = offline ? 'Keine Verbindung zum Programm' : namen[state];
  $('#balken').className = 'balken ' + klassen[state];
  const p = Math.max(0, Math.min(100, status.percent || 0));
  $('#pct').textContent = Math.floor(p);
  $('#fuell').style.width = p + '%';
  $('#balken').setAttribute('aria-valuenow', Math.floor(p));
  const hatAuftrag = state !== 'leer' && !(state === 'aus' && !status.runSeconds);
  $('#tLauf').textContent = hatAuftrag ? zeit(status.runSeconds) : '–';
  $('#tSchaetz').textContent = hatAuftrag && status.estimateSeconds ? zeit(status.estimateSeconds) : '–';
  $('#tRest').textContent = aktiv ? zeit(status.remainingSeconds) : '–';

  const hinweise = {
    alarm: '<b>' + esc(status.message || 'Alarm') + '</b> Bitte am Laser nachsehen; der Nullpunkt muss neu geprüft werden.',
    aus: '<b>Keine Verbindung zum Laser.</b> Der Laser kann noch einige Zeilen zu Ende fahren. Bei Gefahr den Laser ausschalten.',
    pause: 'Der Auftrag ist angehalten. Der Laser ist aus.',
    fertig: 'Der Auftrag ist fertig. Gesamtlaufzeit ' + zeit(status.runSeconds) + '.',
  };
  $('#band').innerHTML = offline
    ? 'Das Programm auf dem Rechner antwortet nicht. Es wird weiter versucht.'
    : (hinweise[status.state] || '');

  const frei = !offline && status.control;
  $('#sperrHinweis').hidden = frei || offline;
  const pb = $('#btnPause');
  pb.querySelector('use').setAttribute('href', state === 'pause' ? '#i-play_arrow' : '#i-pause');
  $('#pauseText').textContent = state === 'pause' ? 'Weiter' : 'Pause';
  pb.className = 'btn ' + (state === 'pause' ? 'ok' : '');
  pb.disabled = !aktiv || !frei;
  $('#btnAbbruch').disabled = !aktiv || !frei;
  $('#liveBox').classList.toggle('gesperrt', !aktiv || !frei);

  // Bild des Gebrannten
  $('#vorschau').hidden = !status.preview;
  $('#vorschauLeer').hidden = !!status.preview;
  holeVorschau();

  // Regler folgen dem Programm, außer der Bediener hat sie gerade angefasst
  reglerSetzen();
  reglerText();
}
// Das Bild des Gebrannten: alle "previewSeconds" Sekunden holen, aber nur, wenn es sich geändert hat und die Seite sichtbar ist.
let vorschauRevision = null, vorschauZeit = 0, vorschauLaedt = false;
function holeVorschau() {
  if (!status || !status.preview || document.hidden || vorschauLaedt || offline) return;
  if (status.previewRevision === vorschauRevision) return;
  const jetzt = Date.now();
  if (jetzt - vorschauZeit < (status.previewSeconds || 3) * 1000) return;
  vorschauLaedt = true;
  vorschauZeit = jetzt;
  const revision = status.previewRevision;
  const bild = new Image();   // erst laden, dann tauschen: kein Flackern
  bild.onload = () => { $('#vorschau').src = bild.src; vorschauRevision = revision; vorschauLaedt = false; };
  bild.onerror = () => { vorschauLaedt = false; };
  bild.src = '/api/preview?r=' + revision + '&t=' + jetzt;
}
function esc(s) { return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c])); }

// Die Regler zeigen dieselben Werte und Bereiche wie die Regler im Programm: Speed in mm/min (oder mm/s, wie dort eingestellt),
// Leistung in Prozent der Maschine. Zum Programm gehen Prozent des Startwerts (wie bei GRBL). Bei NC-Aufträgen und ohne bekannten
// Startwert (zum Beispiel beim Rahmen) gibt es nur Prozent, wie im Programm.
function skala(id) {
  const s = status || {};
  const hatBasis = s.speedBase != null;
  const spd = id === 'spd';
  if (!hatBasis) return { min: 10, max: spd ? 200 : 100, step: 1, einheit: '%', basis: 100, dez: 0, gross: 10, klein: 1, faktor: 1 };
  // Bereich und Wert kommen direkt von den Reglern im Programm
  if (s.percentOnly) {
    return { min: spd ? s.speedMin : s.powerMin, max: spd ? s.speedMax : s.powerMax, step: 1, einheit: '%', basis: 100, dez: 0, gross: 10, klein: 1, faktor: 1 };
  }
  if (spd) {
    const f = s.speedFactor || 1, perSek = f !== 1;
    return { min: s.speedMin * f, max: s.speedMax * f, step: perSek ? 0.1 : 1, einheit: s.speedUnit || 'mm/min', basis: s.speedBase * f,
             dez: perSek ? 1 : 0, gross: perSek ? 5 : 100, klein: perSek ? 1 : 10, faktor: f };
  }
  return { min: s.powerMin, max: s.powerMax, step: 1, einheit: '%', basis: s.powerBase, dez: 0, gross: 10, klein: 1, faktor: 1 };
}
const prozentVon = (k, wert) => Math.max(10, Math.min(200, Math.round(wert * 100 / k.basis)));   // wie livePercent im Programm
const wertVon = (k, prozent) => k.basis * prozent / 100;
const runde = (k, wert) => Number(wert.toFixed(k.dez));

function reglerAufbau() {
  // Bereich und Knöpfe der Regler zum aktuellen Auftrag einstellen
  for (const id of ['spd', 'pwr']) {
    const k = skala(id), eingabe = $('#' + id);
    eingabe.min = k.min; eingabe.max = k.max; eingabe.step = k.step;
    const werte = [-k.gross, -k.klein, k.klein, k.gross];
    document.querySelectorAll('.segmente[data-fuer="' + id + '"] button').forEach((knopf, i) => {
      knopf.dataset.d = werte[i];
      knopf.textContent = (werte[i] > 0 ? '+' : '−') + String(Math.abs(werte[i])).replace('.', ',');
    });
  }
}
function reglerText() {
  const ks = skala('spd'), kp = skala('pwr');
  $('#spdInfo').textContent = runde(ks, +$('#spd').value).toFixed(ks.dez).replace('.', ',') + ' ' + ks.einheit;
  $('#pwrInfo').textContent = runde(kp, +$('#pwr').value) + ' %';
}
function reglerSetzen() {
  // Regler auf die Werte des Programms stellen, außer der Bediener hat sie gerade angefasst
  const jetzt = Date.now();
  reglerAufbau();
  const ks = skala('spd'), kp = skala('pwr');
  const direkt = status.speedValue != null;   // Wert direkt von den Reglern im Programm
  if (jetzt - touched.spd > 4000) $('#spd').value = runde(ks, direkt ? status.speedValue * ks.faktor : wertVon(ks, status.feedPercent));
  if (jetzt - touched.pwr > 4000) $('#pwr').value = runde(kp, direkt ? status.powerValue : wertVon(kp, status.powerPercent));
}

// ---- Ton, wenn der Auftrag fertig ist ----
// Browser erlauben Ton erst nach einer Berührung der Seite; darum wird der Ton beim ersten Tippen oder Klicken vorbereitet.
// Der Ton kommt nur, solange die Seite geöffnet ist (Handys halten Seiten im Hintergrund oft an: Dafür gibt es ntfy).
let tonAn = true;
try { tonAn = localStorage.getItem('ld-ton') !== '0'; } catch (e) { /* ohne Speicher: an */ }
let audio = null;
function tonVorbereiten() {
  try {
    if (!audio) { const Ctx = window.AudioContext || window.webkitAudioContext; if (Ctx) audio = new Ctx(); }
    if (audio && audio.state === 'suspended') audio.resume();
  } catch (e) { /* kein Ton möglich */ }
}
for (const art of ['pointerdown', 'keydown', 'touchstart']) document.addEventListener(art, tonVorbereiten, { passive: true });
function spieleTon() {
  tonVorbereiten();
  if (!audio || audio.state !== 'running') return false;
  // drei aufsteigende Töne (A5, D6, A6), je 0,28 s, mit weichem Anfang und Ende
  const t0 = audio.currentTime + 0.05;
  [880, 1174.66, 1760].forEach((hz, i) => {
    const osz = audio.createOscillator(), pegel = audio.createGain();
    osz.type = 'sine';
    osz.frequency.value = hz;
    const start = t0 + i * 0.3;
    pegel.gain.setValueAtTime(0.0001, start);
    pegel.gain.exponentialRampToValueAtTime(0.35, start + 0.03);
    pegel.gain.exponentialRampToValueAtTime(0.0001, start + 0.28);
    osz.connect(pegel).connect(audio.destination);
    osz.start(start);
    osz.stop(start + 0.3);
  });
  return true;
}
function tonKnopf() {
  const k = $('#btnTon');
  k.textContent = 'Ton bei Fertig: ' + (tonAn ? 'an' : 'aus');
  k.setAttribute('aria-pressed', tonAn);
}
$('#btnTon').onclick = () => {
  tonAn = !tonAn;
  try { localStorage.setItem('ld-ton', tonAn ? '1' : '0'); } catch (e) { /* egal */ }
  tonKnopf();
  if (tonAn) spieleTon();   // zur Probe, und weil der Tipp den Ton freischaltet
};
tonKnopf();

// Wechsel von "läuft/Pause" zu "fertig": Ton und Hinweis im Titel des Tabs (auch sichtbar, wenn die Seite im Hintergrund ist)
let letzterZustand = null;
function zustandGewechselt() {
  const jetzt = status ? status.state : null;
  const war = letzterZustand;
  letzterZustand = jetzt;
  if (jetzt === 'fertig' && (war === 'lauf' || war === 'pause')) {
    document.title = '\u2714 Fertig \u2013 Laser-Dilettant';
    if (tonAn) spieleTon();
  } else if (jetzt !== 'fertig') {
    document.title = 'Laser-Dilettant';
  }
}

// ---- Abfragen ----
async function abfrage() {
  if (!loggedIn) return;
  try {
    const r = await api('/api/status');
    if (r.status === 401) { zeigeAnmeldung(true); return; }
    if (r.status === 200) { status = r.data; offline = false; zustandGewechselt(); }
  } catch (e) {
    offline = true;
  }
  zeichne();
}
function planeAbfrage() {
  // im Hintergrund (Tab nicht sichtbar) seltener fragen
  setTimeout(async () => { await abfrage(); planeAbfrage(); }, document.hidden ? 5000 : 1000);
}

// ---- Befehle ----
async function befehl(name, wert) {
  try {
    const r = await api('/api/command', { command: name, value: wert });
    if (r.status === 401) { zeigeAnmeldung(true); return; }
    if (r.status !== 200) snack(r.data.error || 'Der Befehl wurde abgelehnt.');
  } catch (e) {
    snack('Das Programm antwortet nicht.');
  }
  abfrage();
}
function frage(titel, text, jaText, jaKlasse, aktion) {
  $('#dlgTitel').textContent = titel;
  $('#dlgText').textContent = text;
  const ja = $('#dlgJa');
  ja.textContent = jaText;
  ja.className = 'btn ' + jaKlasse;
  $('#dlg').showModal();
  $('#dlgNein').onclick = () => $('#dlg').close();
  ja.onclick = () => { $('#dlg').close(); aktion(); };
}
$('#btnPause').onclick = () => {
  if (!status) return;
  if (status.state === 'lauf') befehl('pause', 0);
  else if (status.state === 'pause') {
    frage('Weiter brennen?', 'Hat sich am Laser nichts verändert? Das Material darf nicht verrutscht sein. Der Laser schaltet sich beim Weiterfahren wieder ein.',
      'Weiter', 'ok', () => befehl('resume', 0));
  }
};
$('#btnAbbruch').onclick = () => frage('Auftrag abbrechen?',
  'Der Laser wird sofort ausgeschaltet. Der Auftrag lässt sich nicht fortsetzen, und die Position ist danach unsicher (Nullpunkt am Laser prüfen).',
  'Abbrechen', 'gefahr', () => befehl('cancel', 0));

// Regler: Schieber (beim Loslassen senden) und Knöpfe
const reglerBefehl = { spd: 'feed', pwr: 'power' };
function sendeRegler(id) {
  befehl(reglerBefehl[id], prozentVon(skala(id), +$('#' + id).value));
}
for (const id of ['spd', 'pwr']) {
  $('#' + id).addEventListener('input', () => { touched[id] = Date.now(); reglerText(); });
  $('#' + id).addEventListener('change', () => { touched[id] = Date.now(); sendeRegler(id); });
}
document.querySelectorAll('.segmente[data-fuer]').forEach((box) => {
  const id = box.dataset.fuer, eingabe = $('#' + id);
  box.addEventListener('click', (e) => {
    const d = e.target.dataset.d;
    if (d === undefined) return;
    const k = skala(id);
    eingabe.value = Math.min(k.max, Math.max(k.min, +eingabe.value + +d));
    touched[id] = Date.now();
    reglerText();
    sendeRegler(id);
  });
});

// ---- Anmeldung, Menü, Design ----
async function anmelden() {
  const pw = $('#pw').value;
  $('#loginFehler').textContent = '';
  try {
    const r = await api('/api/login', { password: pw });
    if (r.status === 200) {
      $('#pw').value = '';
      zeigeAnmeldung(false);
      await abfrage();
    } else {
      $('#loginFehler').textContent = r.data.error || 'Anmeldung fehlgeschlagen.';
    }
  } catch (e) {
    $('#loginFehler').textContent = 'Das Programm auf dem Rechner ist nicht erreichbar.';
  }
}
$('#btnLogin').onclick = anmelden;
$('#pw').addEventListener('keydown', (e) => { if (e.key === 'Enter') anmelden(); });
$('#btnLogout').onclick = async () => { menue(false); try { await api('/api/logout', {}); } catch (e) { /* egal */ } status = null; zeigeAnmeldung(true); };

const schublade = $('#schublade'), scrim = $('#scrim');
function menue(offen) {
  schublade.classList.toggle('offen', offen);
  scrim.classList.toggle('offen', offen);
  $('#btnMenue').setAttribute('aria-expanded', offen);
}
$('#btnMenue').onclick = () => menue(!schublade.classList.contains('offen'));
scrim.onclick = () => menue(false);
document.addEventListener('keydown', (e) => { if (e.key === 'Escape') menue(false); });

function istDunkel() {
  const t = document.documentElement.dataset.theme;
  return t === 'dark' || (!t && matchMedia('(prefers-color-scheme: dark)').matches);
}
function logo() { document.querySelectorAll('.logoBild').forEach((i) => { i.src = istDunkel() ? '/logo-dunkel.svg' : '/logo.svg'; }); }
$('#btnTheme').onclick = () => {
  const neu = istDunkel() ? 'light' : 'dark';
  document.documentElement.dataset.theme = neu;
  try { localStorage.setItem('ld-theme', neu); } catch (e) { /* ohne Speicher geht es auch */ }
  logo();
};
try { const t = localStorage.getItem('ld-theme'); if (t) document.documentElement.dataset.theme = t; } catch (e) { /* egal */ }
logo();

// ---- Start: ist schon eine Sitzung da? ----
(async () => {
  zeigeAnmeldung(true);
  try {
    const r = await api('/api/status');
    if (r.status === 200) { status = r.data; zeigeAnmeldung(false); zeichne(); }
  } catch (e) { $('#loginFehler').textContent = 'Das Programm auf dem Rechner ist nicht erreichbar.'; }
  planeAbfrage();
})();
