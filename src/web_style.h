#pragma once

// Feuille de style commune aux pages web du Tab5 (configuration, mise à jour) :
// mêmes couleurs que l'écran (fond sombre, accent ambre). Tout est intégré : pendant
// la configuration, le téléphone est sur le Wi-Fi du Tab5, sans accès à Internet.
static const char kWebHead[] PROGMEM = R"CSS(<!doctype html><html lang=fr><head><meta charset=utf-8>
<meta name=viewport content='width=device-width,initial-scale=1,viewport-fit=cover'>
<meta name=color-scheme content=dark><style>
:root{--bg:#0e0f12;--card:#1b1d23;--line:#2c2f37;--text:#f4f5f7;--muted:#a6a9b2;--accent:#e8a33d;--ok:#3ecf8e;--err:#ff6b6b}
*{box-sizing:border-box}html{background:var(--bg)}
body{margin:0 auto;max-width:560px;padding:20px 16px 110px;font:16px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;color:var(--text)}
header{display:flex;align-items:center;gap:14px;margin:6px 0 18px}
.logo{width:48px;height:48px;border-radius:14px;background:linear-gradient(135deg,#f0b552,#c97b1c);display:grid;place-items:center;flex:none}
.logo i{width:22px;height:22px;border-radius:50%;border:4px solid #0e0f12;display:block;position:relative}
.logo i:after{content:'';position:absolute;inset:4px;border-radius:50%;background:#0e0f12}
h1{font-size:22px;margin:0}h1+p{margin:2px 0 0;color:var(--muted);font-size:14px}
h2{font-size:17px;margin:0 0 12px;display:flex;align-items:center;gap:10px}
h2 b{width:26px;height:26px;border-radius:50%;background:var(--accent);color:#111;display:grid;place-items:center;font-size:14px}
.card{background:var(--card);border:1px solid var(--line);border-radius:18px;padding:18px;margin:14px 0}
details.card{padding:0}details.card>summary{list-style:none;cursor:pointer;padding:16px 18px;font-weight:600;display:flex;justify-content:space-between}
details.card>summary::-webkit-details-marker{display:none}details.card>summary:after{content:'+';color:var(--muted);font-size:20px;line-height:1}
details[open].card>summary:after{content:'\2013'}details.card>.in{padding:0 18px 18px}
label{display:block;font-weight:600;font-size:14px;margin:14px 0 6px;color:var(--muted)}label:first-child{margin-top:0}
input,select,textarea{width:100%;font:inherit;color:var(--text);background:#111317;border:1px solid var(--line);border-radius:12px;padding:12px 14px;outline:none}
input:focus,select:focus,textarea:focus{border-color:var(--accent)}
textarea{min-height:150px;font-family:ui-monospace,Menlo,monospace;font-size:14px}
small{display:block;color:var(--muted);font-size:13px;margin-top:6px}small b{color:var(--text)}
code{background:#111317;border-radius:6px;padding:1px 6px}
.row{display:flex;gap:12px}.row>div{flex:1;min-width:0}
.nets{display:flex;flex-direction:column;gap:6px;margin-bottom:10px}
.net{display:flex;justify-content:space-between;align-items:center;width:100%;text-align:left;background:#111317;border:1px solid var(--line);color:var(--text);border-radius:12px;padding:12px 14px;font:inherit;cursor:pointer}
.net.on{border-color:var(--accent);box-shadow:0 0 0 1px var(--accent) inset}
.bars{display:inline-flex;gap:2px;align-items:flex-end;height:14px}.bars i{width:4px;background:var(--line);border-radius:1px}
.bars i:nth-child(1){height:5px}.bars i:nth-child(2){height:9px}.bars i:nth-child(3){height:14px}.bars i.f{background:var(--text)}
.pw{position:relative}.pw button{position:absolute;right:6px;top:6px;bottom:6px;border:0;border-radius:9px;background:var(--line);color:var(--text);padding:0 12px;font:inherit;font-size:14px}
.chk{display:flex;gap:10px;align-items:flex-start;font-weight:500;color:var(--text)}.chk input{width:20px;height:20px;margin:2px 0 0;flex:none}
.err{background:#3a1b1e;border:1px solid var(--err);color:#ffd7d7;border-radius:14px;padding:12px 14px}
.okbox{background:#123326;border:1px solid var(--ok);border-radius:14px;padding:12px 14px}
.bar{position:fixed;left:0;right:0;bottom:0;padding:12px 16px calc(12px + env(safe-area-inset-bottom));background:linear-gradient(transparent,var(--bg) 35%)}
.btn{display:block;width:100%;max-width:528px;margin:0 auto;border:0;border-radius:14px;padding:16px;font:600 17px system-ui,sans-serif;background:var(--accent);color:#111;cursor:pointer}
.btn:disabled{opacity:.5}
progress{width:100%;height:14px;margin-top:14px;accent-color:var(--accent)}
</style>)CSS";
