#pragma once

#include <Arduino.h>
#include <WebServer.h>

// Dashboard do AmbiSense: HTML, CSS e JavaScript puros (sem bibliotecas externas).
// O texto fica na flash, dividido em 4 trechos entre os valores que mudam:
//   intervalo de atualização salvo (ms) | uso do FATFS (KB) | tamanho do FATFS (KB)
// A página é enviada em pedaços de 1 KB (Transfer-Encoding: chunked), sem montar
// uma String grande na RAM e sem depender de um único envio longo.

static const char DASHBOARD_PART_1[] PROGMEM = R"HTML(
<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AmbiSense</title>
<style>
  :root {
    --bg: #edf4fb;
    --card: #fff;
    --line: #d8e6f3;
    --text: #10233d;
    --muted: #59718e;
    --accent: #0873cb;
    --title: #32699b;
    --ok: #08733d;
    --ok-bg: #e8f8ef;
    --ok-line: #94dfb5;
    --warn: #9a5b00;
    --warn-bg: #fff6df;
    --warn-line: #f0cd78;
    --bad: #b4232c;
    --bad-bg: #ffeaeb;
    --bad-line: #f1a5aa;
    --bar: #e1ebf5;
    --grid: #e3ecf5;
    --c1: #0873cb;
    --c2: #e07a10;
    --c3: #1a9e5c;
    --shadow: 0 8px 24px #17395b14
  }

  body.night {
    --bg: #07111f;
    --card: #122842;
    --line: #294c70;
    --text: #edf4ff;
    --muted: #afc4d9;
    --accent: #3b9bf0;
    --title: #83c8ff;
    --ok: #72e5a3;
    --ok-bg: #103b29;
    --ok-line: #21774c;
    --warn: #ffd166;
    --warn-bg: #433312;
    --warn-line: #886918;
    --bad: #ffabb2;
    --bad-bg: #4a1e25;
    --bad-line: #9d3742;
    --bar: #29415c;
    --grid: #254563;
    --c1: #58b0ff;
    --c2: #ffb454;
    --c3: #5fd18f;
    --shadow: 0 8px 24px #00000066
  }

  * {
    box-sizing: border-box
  }

  body {
    margin: 0;
    padding: 20px;
    font: 14px/1.45 "Segoe UI", system-ui, Arial, sans-serif;
    color: var(--text);
    background: var(--bg)
  }

  main {
    max-width: 1180px;
    margin: auto
  }

  h1,
  h2,
  p {
    margin: 0
  }

  h2 {
    margin: 28px 0 10px;
    font-size: 13px;
    letter-spacing: 1.2px;
    text-transform: uppercase;
    color: var(--title)
  }

  .card {
    background: var(--card);
    border: 1px solid var(--line);
    border-radius: 16px;
    padding: 16px;
    box-shadow: var(--shadow)
  }

  .top {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 14px;
    flex-wrap: wrap
  }

  .top h1 {
    font-size: 28px;
    letter-spacing: -.8px
  }

  .sub,
  .lbl,
  .note {
    color: var(--muted)
  }

  .actions {
    display: flex;
    gap: 8px;
    flex-wrap: wrap;
    align-items: center
  }

  button,
  select,
  input {
    font: inherit;
    min-height: 40px;
    padding: 8px 12px;
    border-radius: 10px;
    border: 1px solid var(--line);
    background: var(--card);
    color: var(--text)
  }

  button {
    background: var(--accent);
    color: #fff;
    border: 0;
    font-weight: 700;
    cursor: pointer;
    touch-action: manipulation
  }

  body.night button {
    color: #06182c
  }

  button.ghost,
  body.night button.ghost {
    background: var(--card);
    color: var(--text);
    border: 1px solid var(--line)
  }

  button.danger,
  body.night button.danger {
    background: #b4232c;
    color: #fff
  }

  .ok {
    --st: var(--ok);
    --st-bg: var(--ok-bg);
    --st-line: var(--ok-line)
  }

  .warn {
    --st: var(--warn);
    --st-bg: var(--warn-bg);
    --st-line: var(--warn-line)
  }

  .bad {
    --st: var(--bad);
    --st-bg: var(--bad-bg);
    --st-line: var(--bad-line)
  }

  .na {
    --st: var(--muted);
    --st-bg: transparent;
    --st-line: var(--line)
  }

  .pill {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    padding: 4px 12px;
    border-radius: 99px;
    font-size: 13px;
    font-weight: 700;
    color: var(--st, var(--muted));
    background: var(--st-bg, transparent);
    border: 1px solid var(--st-line, var(--line))
  }

  .pill::before {
    content: "";
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: currentColor
  }

  .hero {
    display: grid;
    grid-template-columns: 2fr 1fr;
    gap: 12px;
    margin-top: 14px
  }

  .clockcard {
    background: linear-gradient(135deg, #0b579d, #1683d8);
    color: #fff;
    border: 0
  }

  .clock {
    font-size: 40px;
    font-weight: 800;
    letter-spacing: -1.5px;
    margin: 6px 0 10px
  }

  .chips {
    display: flex;
    gap: 8px;
    flex-wrap: wrap;
    font-size: 12px
  }

  .chips span {
    background: #ffffff26;
    border: 1px solid #ffffff44;
    border-radius: 99px;
    padding: 4px 10px
  }

  .val2 {
    font-size: 20px;
    font-weight: 800;
    margin: 4px 0 12px
  }

  .list {
    display: grid;
    gap: 8px
  }

  .sl {
    padding: 10px 12px;
    border-radius: 10px;
    font-weight: 700;
    color: var(--st);
    background: var(--st-bg);
    border: 1px solid var(--st-line)
  }

  .bme {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    padding: 0;
    border: 0;
    background: none
  }

  .cell {
    padding: 8px 10px;
    border-radius: 8px;
    font-weight: 700;
    color: var(--st);
    background: var(--st-bg);
    border: 1px solid var(--st-line)
  }

  .grid {
    display: grid;
    gap: 12px;
    grid-template-columns: repeat(auto-fit, minmax(min(100%, 210px), 1fr))
  }

  .metric {
    border-left: 5px solid var(--st, var(--line))
  }

  .val {
    font-size: 30px;
    font-weight: 800;
    letter-spacing: -1px;
    margin: 8px 0
  }

  .val small {
    font-size: 14px;
    font-weight: 600;
    letter-spacing: 0;
    color: var(--muted)
  }

  .bar {
    height: 8px;
    border-radius: 99px;
    background: var(--bar);
    overflow: hidden
  }

  .fill {
    height: 100%;
    width: 0;
    border-radius: 99px;
    background: var(--st, var(--accent));
    transition: width .4s, background .3s
  }

  .scale {
    display: flex;
    justify-content: space-between;
    margin-top: 4px;
    font-size: 11px;
    color: var(--muted)
  }

  .charts {
    display: grid;
    gap: 12px;
    grid-template-columns: repeat(auto-fit, minmax(min(100%, 420px), 1fr))
  }

  .chart {
    position: relative;
    padding: 12px 14px 8px
  }

  .chead {
    display: flex;
    align-items: baseline;
    gap: 10px;
    flex-wrap: wrap;
    margin-bottom: 4px
  }

  .chead .now {
    margin-left: auto;
    font-weight: 800
  }

  .legend {
    font-size: 12px;
    color: var(--muted)
  }

  .legend i {
    display: inline-block;
    width: 10px;
    height: 10px;
    border-radius: 50%;
    margin: 0 4px 0 8px;
    vertical-align: middle
  }

  .legend .c1 {
    background: var(--c1)
  }

  .legend .c2 {
    background: var(--c2)
  }

  .legend .c3 {
    background: var(--c3)
  }

  canvas {
    display: block;
    width: 100%;
    height: 170px;
    touch-action: pan-y
  }

  .tip {
    position: absolute;
    top: 34px;
    display: none;
    pointer-events: none;
    background: var(--card);
    border: 1px solid var(--line);
    border-radius: 8px;
    padding: 6px 9px;
    font-size: 12px;
    box-shadow: var(--shadow);
    white-space: nowrap
  }

  .refs {
    display: grid;
    gap: 12px;
    grid-template-columns: repeat(auto-fit, minmax(min(100%, 300px), 1fr))
  }

  .note {
    line-height: 1.6
  }

  .note b {
    color: var(--title)
  }

  .stor form {
    display: flex;
    gap: 8px;
    flex-wrap: wrap;
    align-items: center;
    margin-top: 12px
  }

  a {
    color: var(--accent);
    font-weight: 700
  }

  .metric.alarmed {
    border-color: var(--bad);
    box-shadow: 0 0 0 2px var(--bad-line)
  }

  .metric.alarmed .lbl::after {
    content: " · ALARME";
    color: var(--bad);
    font-weight: 800
  }

  .alarm {
    margin-top: 12px;
    font-size: 12px
  }

  .alarm [hidden] {
    display: none !important
  }

  .alarm label {
    display: flex;
    flex-direction: column;
    gap: 3px;
    color: var(--muted);
    font-weight: 600
  }

  .alarm select {
    width: 100%;
    min-height: 36px;
    padding: 6px 8px;
    font-size: 13px
  }

  .alarm .row {
    display: flex;
    gap: 8px;
    flex-wrap: wrap;
    align-items: flex-end;
    margin-top: 8px
  }

  .alarm .row label {
    font-weight: 400
  }

  .alarm input[type=number] {
    width: 92px;
    min-height: 36px;
    padding: 6px 8px
  }

  .alarm button {
    min-height: 36px;
    padding: 6px 12px
  }

  .wins {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    align-items: center;
    margin: 0 0 10px
  }

  .wins button {
    min-height: 36px;
    padding: 6px 14px;
    border-radius: 999px
  }

  .wins button.on,
  body.night .wins button.on {
    background: var(--accent);
    color: #fff;
    border-color: var(--accent)
  }

  body.night .wins button.on {
    color: #06182c
  }

  .alarm .msg {
    margin-top: 6px;
    color: var(--muted);
    min-height: 1em
  }

  @media(max-width:700px) {
    body {
      padding: 12px
    }

    .hero {
      grid-template-columns: 1fr
    }

    .clock {
      font-size: 32px
    }

    .top h1 {
      font-size: 24px
    }

    .val {
      font-size: 26px
    }
  }
</style>
</head>
<body>
<script>
  try {
    var t = localStorage.getItem('ambisense-tema');
    if (t === 'night' || (t === null && matchMedia('(prefers-color-scheme: dark)').matches)) document.body.classList.add('night')
  } catch (e) {}
</script>
<main>
  <header class="top">
    <div>
      <h1>AmbiSense</h1>
      <p class="sub">Monitoramento ambiental da caixa de sensores</p>
    </div>
    <div class="actions">
      <span class="pill na" id="conn">Conectando</span>
      <button id="tema" class="ghost" type="button">Modo noturno</button>
    </div>
  </header>

  <section class="hero">
    <article class="card clockcard">
      <div>Hora atual</div>
      <div class="clock" id="clock">--/--/---- --:--:--</div>
      <div class="chips" id="clockMeta"></div>
    </article>
    <article class="card">
      <div class="lbl">Estado geral</div>
      <div class="val2"><span class="pill na" id="overall">Aguardando</span></div>
      <div class="lbl">Última leitura gravada</div>
      <div class="val2" id="updated">--</div>
    </article>
  </section>

  <h2>Status atual</h2>
  <section class="card list">
    <div class="sl na" id="ntcStatus">NTC — aguardando leitura.</div>
    <div class="sl bme na" id="bmeStatus"><span class="cell na">BME680 — aguardando leitura.</span></div>
    <div class="sl na" id="co2Status">MH-Z19E — aguardando leitura.</div>
    <div class="sl na" id="mqStatus">MQ A0 — aguardando leitura.</div>
  </section>

  <h2>Leituras</h2>
  <section class="grid">
    <article class="card metric na" id="c-ntc">
      <div class="lbl">NTC — Temperatura</div>
      <div class="val" id="v-ntc">--</div>
      <div class="bar"><div class="fill" id="b-ntc"></div></div>
      <div class="scale"><span>0 °C</span><span>50 °C</span></div>
    </article>
    <article class="card metric na" id="c-temp">
      <div class="lbl">BME680 — Temperatura</div>
      <div class="val" id="v-temp">--</div>
      <div class="bar"><div class="fill" id="b-temp"></div></div>
      <div class="scale"><span>0 °C</span><span>50 °C</span></div>
    </article>
    <article class="card metric na" id="c-hum">
      <div class="lbl">BME680 — Umidade relativa</div>
      <div class="val" id="v-hum">--</div>
      <div class="bar"><div class="fill" id="b-hum"></div></div>
      <div class="scale"><span>0%</span><span>100%</span></div>
    </article>
    <article class="card metric na" id="c-press">
      <div class="lbl">BME680 — Patm</div>
      <div class="val" id="v-press">--</div>
      <div class="bar"><div class="fill" id="b-press"></div></div>
      <div class="scale"><span>900 hPa</span><span>1100 hPa</span></div>
    </article>
    <article class="card metric na" id="c-gas">
      <div class="lbl">BME680 — Rgás</div>
      <div class="val" id="v-gas">--</div>
      <div class="bar"><div class="fill" id="b-gas"></div></div>
      <div class="scale"><span>0 kΩ</span><span>200 kΩ</span></div>
    </article>
    <article class="card metric na" id="c-co2">
      <div class="lbl">MH-Z19E — CO₂</div>
      <div class="val" id="v-co2">--</div>
      <div class="bar"><div class="fill" id="b-co2"></div></div>
      <div class="scale"><span>400 ppm</span><span>5000 ppm</span></div>
    </article>
    <article class="card metric na" id="c-mq">
      <div class="lbl">MQ — A0 (analógico)</div>
      <div class="val" id="v-mq">--</div>
      <div class="bar"><div class="fill" id="b-mq"></div></div>
      <div class="scale"><span>0</span><span>4095</span></div>
    </article>
    <article class="card metric na" id="c-do">
      <div class="lbl">MQ — D0 (digital)</div>
      <div class="val" id="v-do" style="font-size:22px">--</div>
    </article>
    <article class="card metric na" id="c-relay">
      <div class="lbl">Exaustão forçada</div>
      <div class="val" id="v-relay" style="font-size:22px">--</div>
    </article>
  </section>

  <h2>Gráficos</h2>
  <div class="wins" id="wins" role="group" aria-label="Janela de tempo dos gráficos">
    <button type="button" class="ghost on" data-n="300">10 min</button>
    <button type="button" class="ghost" data-n="1800">1 h</button>
    <button type="button" class="ghost" data-n="10800">6 h</button>
    <button type="button" class="ghost" data-n="21600">12 h</button>
    <button type="button" class="ghost" data-n="43200">24 h</button>
    <span class="note" id="winInfo"></span>
  </div>
  <section class="charts" id="charts"></section>
  <p class="note" style="margin-top:8px">Os gráficos mostram as leituras recebidas por este painel desde que a página foi aberta, mais as últimas leituras gravadas na placa. Passe o mouse ou o dedo sobre o gráfico para ver os valores.</p>

  <h2>Referências</h2>
  <section class="refs">
    <article class="card note">
      <b>NTC — Temperatura</b><br>
      Fria: abaixo de 18 °C<br>
      Amena: 18 a 30 °C<br>
      Quente: acima de 30 °C
    </article>
    <article class="card note">
      <b>BME680 — Temperatura</b><br>
      Fria: abaixo de 18 °C<br>
      Amena: 18 a 28 °C<br>
      Quente: acima de 28 °C<br>
      <i>O BME680 aquece a própria leitura de temperatura; compare com o NTC.</i>
    </article>
    <article class="card note">
      <b>BME680 — Umidade relativa (UR)</b><br>
      Emergência: abaixo de 12%<br>
      Alerta: 12 a 20%<br>
      Atenção: 21 a 30%<br>
      Ar seco: 31 a 39%<br>
      Ideal: 40 a 70%<br>
      Muito úmido: acima de 70%<br>
      <i>Classificação da Defesa Civil.</i>
    </article>
    <article class="card note">
      <b>BME680 — Patm</b><br>
      Patm: pressão atmosférica absoluta, medida no local, sem correção para o nível do mar.
    </article>
    <article class="card note">
      <b>BME680 — Rgás</b><br>
      Rgás: resistência do sensor de gás (VOC).<br>
      Normal: 50 kΩ ou mais<br>
      Atenção: 20 a 49,9 kΩ<br>
      Elevado: abaixo de 20 kΩ
    </article>
    <article class="card note">
      <b>MH-Z19E — CO₂</b><br>
      Normal: abaixo de 800 ppm<br>
      Atenção: 800 a 999 ppm<br>
      Necessário ventilar: 1000 a 1499 ppm<br>
      Alto: 1500 a 1999 ppm<br>
      Muito alto: 2000 a 2999 ppm<br>
      Crítico: 3000 a 3999 ppm<br>
      Emergência: 4000 a 5000 ppm<br>
      <i>Nos primeiros minutos após ligar o sensor ainda está aquecendo.</i>
    </article>
    <article class="card note">
      <b>MQ — Leitura analógica A0</b><br>
      Ar normal: 0 a 250<br>
      Atenção: 251 a 500<br>
      Ar alterado: 501 a 1000<br>
      Poluição alta: 1001 a 1799<br>
      Alarme: 1800 ou mais<br>
      <i>É uma escala relativa do ADC; não representa ppm.</i>
    </article>
    <article class="card note">
      <b>MQ — D0 (digital)</b><br>
      Normal: D0 = 1, gás abaixo do limite do módulo<br>
      Alarme: D0 = 0, gás acima do limite do módulo<br>
      <i>O limite é ajustado no potenciômetro do módulo; o firmware não o conhece.</i>
    </article>
    <article class="card note">
      <b>Sensação térmica</b><br>
      Calculada com a temperatura do NTC e a umidade do BME680.<br>
      Fórmula de Steadman, sem vento.
    </article>
    <article class="card note">
      <b>Alarme sonoro</b><br>
      Em cada cartão, escolha uma faixa na lista "Alarme sonoro".<br>
      "Personalizada…" permite digitar o mínimo e o máximo.<br>
      A escolha é gravada na placa na hora.<br>
      O buzzer toca 3 bipes longos, uma vez, depois de 3 leituras seguidas fora da faixa.<br>
      Só toca de novo depois que a leitura voltar à faixa por 5 leituras.<br>
      Nos primeiros 2 minutos após ligar, os alarmes ficam suspensos.
    </article>
  </section>

  <section class="card stor" style="margin-top:24px">
    <b>Armazenamento interno</b>
    <p id="storText" class="note"></p>
    <div class="bar" style="margin-top:6px"><div class="fill" id="storBar" style="background:var(--accent)"></div></div>
    <p style="margin-top:10px"><a href="/leituras.csv">Baixar histórico</a> · <a href="/leituras/ultimas">Ver últimas linhas</a></p>
    <form method="post" action="/leituras/formatar" onsubmit="return confirm('Apagar todo o histórico?')">
      <label>Confirmação <input name="confirmacao" placeholder="Digite APAGAR" required autocomplete="off"></label>
      <button class="danger" type="submit">Apagar histórico</button>
    </form>
  </section>
</main>

<script>
  const $ = id => document.getElementById(id);
  const pattern = /^(.*?)\s+MQ2_AO=(\d+)\s+MQ2_DO=\d+\s+MHZ_CO2=([^ ;]+)(?:[; ]+BME_Temp=([\d.]+) C BME_Umid=([\d.]+) % BME_Pressao=([\d.]+) hPa BME_Gas=([\d.]+) kOhm|[; ]+BME680=ERRO)(?:[; ]+TERM_Temp=(-?[\d.]+) C|[; ]+TERMISTOR=ERRO)[; ]+Exaustao_forcada=(\w+)/;
  const refresh = {
    value: '2000'
  };
  const savedInterval = ')HTML";

static const char DASHBOARD_PART_2[] PROGMEM = R"HTML(';
  const storage = {
    used: )HTML";

static const char DASHBOARD_PART_3[] PROGMEM = R"HTML(,
    total: )HTML";

static const char DASHBOARD_PART_4[] PROGMEM = R"HTML(
  };
  const FORM = {
    'Content-Type': 'application/x-www-form-urlencoded'
  };
  let alarmCfg = {};

  function tfetch(url, opt, ms) {
    const c = new AbortController(),
      t = setTimeout(() => c.abort(), ms || 6000);
    return fetch(url, Object.assign({
      signal: c.signal
    }, opt || {})).finally(() => clearTimeout(t))
  }

  /* ---------- utilidades ---------- */
  const toNum = v => {
    const n = parseFloat(v);
    return Number.isFinite(n) ? n : null
  };
  const mean = (rows, i) => {
    const v = rows.map(r => toNum(r[i])).filter(x => x !== null);
    return v.length ? v.reduce((a, b) => a + b, 0) / v.length : null
  };
  const rank = {
    na: 0,
    ok: 0,
    warn: 1,
    bad: 2
  };
  const worst = list => list.reduce((a, b) => rank[b] > rank[a] ? b : a, 'ok');
  const fmtTs = t => /^\d{4}-\d\d-\d\d /.test(t) ? t.slice(8, 10) + '/' + t.slice(5, 7) + '/' + t.slice(0, 4) + ' ' + t.slice(11) : 'sem horário';
  const hm = t => /^\d{4}-\d\d-\d\d /.test(t) ? t.slice(11) : '';

  /* ---------- classificação das faixas ---------- */
  function gasLimits() {
    return {
      normal: 50,
      attention: 20
    }
  }
  const sCo2 = v => v < 800 ? ['NORMAL', 'ok'] : v < 1000 ? ['ATENCAO', 'warn'] : v < 1500 ? ['VENTILAR', 'bad'] : v < 2000 ? ['ALTO', 'bad'] : v < 3000 ? ['MUITO ALTO', 'bad'] : v < 4000 ? ['CRITICO', 'bad'] : ['EMERGENCIA', 'bad'];
  const sMq = v => v <= 250 ? ['AR NORMAL', 'ok'] : v <= 500 ? ['ATENCAO', 'warn'] : v <= 1000 ? ['AR ALTERADO', 'warn'] : v < 1800 ? ['POLUICAO ALTA', 'bad'] : ['ALARME', 'bad'];
  const sTemp = v => v < 18 ? ['Caixa: fria.', 'warn'] : v <= 28 ? ['Caixa: amena.', 'ok'] : ['Caixa: quente.', 'warn'];
  const sHum = h => h < 12 ? ['Umidade relativa: emergência.', 'bad'] : h <= 20 ? ['Umidade relativa: alerta.', 'bad'] : h <= 30 ? ['Umidade relativa: atenção.', 'warn'] : h < 40 ? ['Ar seco (moderado).', 'warn'] : h <= 70 ? ['Umidade relativa ideal.', 'ok'] : ['Ar muito úmido.', 'warn'];
  const sGas = v => {
    const l = gasLimits();
    return v >= l.normal ? ['VOC/gás normal.', 'ok'] : v >= l.attention ? ['VOC/gás em atenção.', 'warn'] : ['VOC/gás elevado.', 'bad']
  };
  const sNtc = v => v < 18 ? ['passagem do ar fria.', 'warn'] : v <= 30 ? ['passagem do ar amena.', 'ok'] : ['passagem do ar quente.', 'warn'];

  /* ---------- atualização da tela ---------- */
  function setMetric(key, v, dec, unit, pct, state) {
    $('c-' + key).className = 'card metric ' + (v === null ? 'na' : state) + (alarmCfg[key] && alarmCfg[key].active ? ' alarmed' : '');
    $('v-' + key).innerHTML = v === null ? '--' : v.toFixed(dec) + (unit ? '<small> ' + unit + '</small>' : '');
    $('b-' + key).style.width = (v === null ? 0 : Math.max(0, Math.min(100, pct))) + '%';
  }

  function setLine(id, text, state) {
    const e = $(id);
    e.textContent = text;
    e.className = 'sl ' + state
  }

  function setConn(state, text) {
    const e = $('conn');
    e.textContent = text;
    e.className = 'pill ' + state
  }

  function setBme(temp, hum, gas) {
    const box = $('bmeStatus');
    box.innerHTML = '';
    if (temp === null || hum === null || gas === null) {
      box.className = 'sl bme bad';
      const c = document.createElement('span');
      c.className = 'cell bad';
      c.textContent = 'CAIXA — BME680 com falha ou sem resposta.';
      box.appendChild(c);
      return 'bad';
    }
    const parts = [sTemp(temp), sHum(hum), sGas(gas)];
    const main = worst(parts.map(p => p[1]));
    box.className = 'sl bme na';
    [
      ['CAIXA', main]
    ].concat(parts).forEach(p => {
      const c = document.createElement('span');
      c.className = 'cell ' + p[1];
      c.textContent = p[0];
      box.appendChild(c)
    });
    return main;
  }

  let failCount = 0,
    busy = false,
    prevStamp = '',
    changedAt = Date.now();
  async function update() {
    if (busy || document.hidden) return;
    busy = true;
    try {
      const seconds = Math.max(1, Math.round(+refresh.value / 1000)),
        lines = Math.max(1, Math.ceil(seconds / 2));
      const data = await (await tfetch('/api/leituras/historico?limite=' + lines)).json();
      const rows = data.lines.map(x => x.match(pattern)).filter(Boolean);
      if (!rows.length) {
        $('updated').textContent = data.lines.length ? 'Formato não reconhecido' : 'Histórico vazio ou indisponível';
        setConn('warn', 'Sem leituras');
        return;
      }
      failCount = 0;
      pushRows(rows);
      const last = rows[rows.length - 1];
      const mq = mean(rows, 2),
        co2 = mean(rows, 3),
        temp = mean(rows, 4),
        hum = mean(rows, 5),
        press = mean(rows, 6),
        gas = mean(rows, 7),
        ntc = mean(rows, 8);
      const states = [];

      let st = co2 === null ? 'bad' : sCo2(co2)[1];
      setMetric('co2', co2, 0, 'ppm', (co2 - 400) * 100 / (5000 - 400), st);
      states.push(st);
      if (co2 === null) setLine('co2Status', 'MH-Z19E — sensor com falha ou sem resposta.', 'bad');
      else setLine('co2Status', 'MH-Z19E — ' + sCo2(co2)[0], st);

      st = mq === null ? 'warn' : sMq(mq)[1];
      setMetric('mq', mq, 0, '', mq / 40.95, st);
      states.push(st);
      setLine('mqStatus', mq === null ? 'MQ A0 — leitura indisponível.' : 'MQ A0 — ' + sMq(mq)[0], st);

      setMetric('temp', temp, 1, '°C', temp / .5, temp === null ? 'na' : sTemp(temp)[1]);
      setMetric('hum', hum, 1, '%UR', hum, hum === null ? 'na' : sHum(hum)[1]);
      setMetric('press', press, 1, 'hPa', (press - 900) / 2, 'ok');
      setMetric('gas', gas, 1, 'kΩ', gas / 2, gas === null ? 'na' : sGas(gas)[1]);
      states.push(setBme(temp, hum, gas));

      st = ntc === null ? 'bad' : sNtc(ntc)[1];
      setMetric('ntc', ntc, 1, '°C', ntc / .5, st);
      states.push(st);
      setLine('ntcStatus', ntc === null ? 'NTC — sensor com falha ou sem resposta.' : 'NTC — ' + sNtc(ntc)[0], st);

      const on = last[9] === 'ON';
      const dg = doTrig(last);
      $('c-do').className = 'card metric ' + (dg === null ? 'na' : dg ? 'warn' : 'ok') + (alarmCfg['do'] && alarmCfg['do'].active ? ' alarmed' : '');
      $('v-do').textContent = dg === null ? '--' : dg ? 'ACIONADA' : 'NORMAL';
      $('c-relay').className = 'card metric ' + (on ? 'ok' : 'warn');
      $('v-relay').textContent = on ? 'ATIVA' : 'DESLIGADA';

      const overall = worst(states);
      const ov = $('overall');
      ov.className = 'pill ' + overall;
      ov.textContent = overall === 'ok' ? 'Ambiente normal' : overall === 'warn' ? 'Atenção' : 'Alerta';

      $('updated').textContent = fmtTs(last[1]);
      if (/^\d{4}-/.test(last[1])) {
        if (last[1] !== prevStamp) {
          prevStamp = last[1];
          changedAt = Date.now()
        }
        const stale = Date.now() - changedAt > 15000;
        setConn(stale ? 'warn' : 'ok', stale ? 'Sem novas leituras' : 'Conectado');
      } else setConn('ok', 'Conectado');
      drawAll();
    } catch (e) {
      // Só avisa depois de 3 falhas seguidas; falhas isoladas são ignoradas.
      if (++failCount >= 3) setConn('bad', 'Sem resposta da placa');
    } finally {
      busy = false
    }
  }

  function schedule() {
    clearInterval(window.dashTimer);
    window.dashTimer = setInterval(update, +refresh.value);
    update()
  }

  /* ---------- gráficos (canvas, sem bibliotecas) ---------- */
  let MAXPTS = 300;
  const SER = {
    t: [],
    co2: [],
    mq: [],
    temp: [],
    ntc: [],
    hum: [],
    gas: [],
    press: [],
    fl: [],
    dg: []
  };
  // Saída digital do MQ-2: nível baixo (0) = acionada; 1 = normal. Retorna 1 (acionada), 0 ou null.
  const doTrig = r => {
    const m = /MQ2_DO=(\d)/.exec(r.input || '');
    return m ? (m[1] === '0' ? 1 : 0) : null
  };
  // Sensação térmica (Steadman, sem vento). Base: NTC; a umidade do BME680 é
  // convertida para a temperatura do NTC (mesmo vapor, outra temperatura).
  const sat = T => Math.exp(17.27 * T / (237.7 + T));

  function feel(t, h, n) {
    const T = n !== null ? n : t;
    if (T === null || h === null) return null;
    let rh = n !== null && t !== null ? h * sat(t) / sat(n) : h;
    rh = Math.min(100, Math.max(0, rh));
    return T + 0.33 * (rh / 100 * 6.105 * sat(T)) - 4;
  }
  let lastStamp = '';
  let WIN = 300,
    winBusy = false;
  const tsec = s => Date.parse(s.replace(' ', 'T')) / 1000;

  let lastLine = '';

  function pushRows(rows, force) {
    if (!force && rows[rows.length - 1][1] < lastStamp) {
      for (const k in SER) SER[k] = [];
      lastStamp = ''
    }
    // Janelas longas (1 h a 24 h): o gráfico tem pontos espaçados; um ponto novo
    // só entra quando passa o espaçamento da janela (WIN leituras de 2 s / pontos).
    const gap = WIN > 300 && !force ? (WIN / MAXPTS) * 2 : 0;
    for (const r of rows) {
      const ts = r[1];
      // Carga completa de uma janela (force): mantém a ordem gravada, mesmo que
      // linhas antigas tenham horário fora de ordem (gravadas em UTC no boot).
      if (/^\d{4}-\d\d-\d\d /.test(ts)) {
        if (!force && ts <= lastStamp) continue;
        if (gap && SER.t.length && tsec(ts) - tsec(SER.t[SER.t.length - 1]) < gap) continue;
        lastStamp = ts;
      } else {
        // Sem hora (RTC sem hora válida e sem NTP): não dá para ordenar por horário,
        // então só descarta a repetição da mesma linha.
        if (!force && r.input === lastLine) continue;
      }
      lastLine = r.input;
      SER.t.push(ts);
      SER.mq.push(toNum(r[2]));
      SER.co2.push(toNum(r[3]));
      SER.temp.push(toNum(r[4]));
      SER.hum.push(toNum(r[5]));
      SER.press.push(toNum(r[6]));
      SER.gas.push(toNum(r[7]));
      SER.ntc.push(toNum(r[8]));
      SER.fl.push(feel(toNum(r[4]), toNum(r[5]), toNum(r[8])));
      SER.dg.push(doTrig(r));
    }
    trimSeries();
  }
  async function loadWindow() {
    if (winBusy) return;
    winBusy = true;
    const n = WIN;
    $('winInfo').textContent = 'Carregando…';
    try {
      const d = await (await tfetch('/api/leituras/historico?limite=' + n + '&pontos=600', null, 60000)).json();
      if (n !== WIN) return;
      const rows = d.lines.map(x => x.match(pattern)).filter(Boolean);
      for (const k in SER) SER[k] = [];
      lastStamp = '';
      if (rows.length) pushRows(rows, true);
      drawAll();
      $('winInfo').textContent = rows.length ? windowNote() : 'Sem leituras nessa janela.';
    } catch (e) {
      $('winInfo').textContent = 'A placa não respondeu; tentando de novo.'
    } finally {
      winBusy = false
    }
  }
  // Intervalo com que o gráfico da janela escolhida recebe um ponto novo.
  function windowNote() {
    const s = WIN > 300 ? (WIN / MAXPTS) * 2 : Math.max(1, Math.round(+refresh.value / 1000));
    const t = s < 60 ? Math.round(s) + ' s' : Math.round(s / 60) + ' min';
    return 'Atualiza a cada ' + t + '.';
  }

  function setWindow(n) {
    WIN = n;
    MAXPTS = n > 300 ? 600 : Math.max(2, n);
    document.querySelectorAll('#wins button').forEach(b => b.classList.toggle('on', +b.dataset.n === n));
    loadWindow();
  }
  document.querySelectorAll('#wins button').forEach(b => b.onclick = () => setWindow(+b.dataset.n));
  $('winInfo').textContent = windowNote();
  setInterval(() => {
    if (WIN > 300 && !document.hidden) loadWindow()
  }, 60000);

  function trimSeries() {
    const extra = SER.t.length - MAXPTS;
    if (extra > 0)
      for (const k in SER) SER[k].splice(0, extra)
  }

  function prependRows(rows) {
    const first = SER.t.length ? SER.t[0] : '~';
    const add = rows.filter(r => /^\d{4}-\d\d-\d\d /.test(r[1]) && r[1] < first);
    if (!add.length) return;
    const cols = {
      mq: 2,
      co2: 3,
      temp: 4,
      hum: 5,
      press: 6,
      gas: 7,
      ntc: 8
    };
    SER.t = add.map(r => r[1]).concat(SER.t);
    for (const k in cols) SER[k] = add.map(r => toNum(r[cols[k]])).concat(SER[k]);
    SER.fl = add.map(r => feel(toNum(r[4]), toNum(r[5]), toNum(r[8]))).concat(SER.fl);
    SER.dg = add.map(r => doTrig(r)).concat(SER.dg);
    if (lastStamp === '') lastStamp = SER.t[SER.t.length - 1];
    trimSeries();
  }
  const CHARTS = [{
    id: 'temp',
    title: 'Temperatura',
    unit: '°C',
    dec: 1,
    span: 2,
    s: [
      ['temp', 'BME680', 1],
      ['ntc', 'NTC', 2],
      ['fl', 'Sensação térmica', 3]
    ],
    lim: () => [
      [18, '18'],
      [28, '28']
    ]
  }, {
    id: 'hum',
    title: 'BME680 — Umidade relativa',
    unit: '%',
    dec: 1,
    span: 10,
    s: [
      ['hum', 'UR', 1]
    ],
    lim: () => [
      [40, '40'],
      [70, '70']
    ]
  }, {
    id: 'press',
    title: 'BME680 — Patm',
    unit: 'hPa',
    dec: 1,
    span: 2,
    s: [
      ['press', 'Patm', 1]
    ],
    lim: () => []
  }, {
    id: 'gas',
    title: 'BME680 — Rgás',
    unit: 'kΩ',
    dec: 1,
    span: 10,
    s: [
      ['gas', 'Rgás', 1]
    ],
    lim: () => {
      const l = gasLimits();
      return [
        [l.normal, 'normal'],
        [l.attention, 'atenção']
      ]
    }
  }, {
    id: 'co2',
    title: 'MH-Z19E — CO₂',
    unit: 'ppm',
    dec: 0,
    span: 100,
    s: [
      ['co2', 'CO₂', 1]
    ],
    lim: () => [
      [800, 'normal/atenção'],
      [1000, 'ventilar'],
      [1500, 'alto'],
      [2000, 'muito alto'],
      [3000, 'crítico'],
      [4000, 'emergência'],
      [5000, 'limite do sensor']
    ]
  }, {
    id: 'mq',
    title: 'MQ — A0 (analógico)',
    unit: '',
    dec: 0,
    span: 50,
    s: [
      ['mq', 'Gases (analógico)', 1]
    ],
    lim: () => [
      [250, 'normal/atenção'],
      [500, 'ar alterado'],
      [1000, 'poluição alta'],
      [1800, 'alarme']
    ]
  }, {
    id: 'do',
    title: 'MQ-2 — Gases (digital)',
    unit: '',
    dec: 0,
    span: 1,
    s: [
      ['dg', 'Gases (digital)', 1]
    ],
    lim: () => [],
    ticks: [
      [0, 'Normal'],
      [1, 'Acionada']
    ],
    fmt: v => v ? 'Acionada' : 'Normal'
  }];
  const PADL = 46,
    PADR = 10,
    PADT = 8,
    PADB = 22;
  let hoverQueued = -1;
  for (const ch of CHARTS) {
    const d = document.createElement('article');
    d.className = 'card chart';
    d.innerHTML = '<div class="chead"><b>' + ch.title + '</b>' + (ch.s.length > 1 ? '<span class="legend">' + ch.s.map(s => '<i class="c' + s[2] + '"></i>' + s[1]).join('') + '</span>' : '') + '<span class="now" id="now-' + ch.id + '">--</span></div><canvas></canvas><div class="tip"></div>';
    $('charts').appendChild(d);
    ch.cv = d.querySelector('canvas');
    ch.tip = d.querySelector('.tip');
    ch.hover = -1;
    ch.cv.addEventListener('pointermove', e => {
      const n = SER.t.length;
      if (!n) return;
      const r = ch.cv.getBoundingClientRect(),
        pw = Math.max(1, r.width - PADL - PADR);
      // Todos os gráficos compartilham o mesmo eixo de tempo: o mesmo índice
      // vale para todos, então o cursor aparece no mesmo instante em cada um.
      const idx = Math.max(0, Math.min(n - 1, Math.round((e.clientX - r.left - PADL) / pw * (n - 1))));
      if (hoverQueued === idx) return;
      hoverQueued = idx;
      requestAnimationFrame(() => {
        CHARTS.forEach(c => {
          c.hover = idx;
          draw(c)
        })
      });
    });
    ch.cv.addEventListener('pointerleave', () => {
      hoverQueued = -1;
      CHARTS.forEach(c => {
        c.hover = -1;
        c.tip.style.display = 'none';
        draw(c)
      })
    });
  }

  function draw(ch) {
    const cv = ch.cv,
      w = cv.clientWidth,
      h = cv.clientHeight;
    if (!w || !h) return;
    const dpr = window.devicePixelRatio || 1;
    if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(h * dpr)) {
      cv.width = Math.round(w * dpr);
      cv.height = Math.round(h * dpr)
    }
    const g = cv.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);
    const css = getComputedStyle(document.body),
      grid = css.getPropertyValue('--grid').trim(),
      muted = css.getPropertyValue('--muted').trim();
    const cols = [null, css.getPropertyValue('--c1').trim(), css.getPropertyValue('--c2').trim(), css.getPropertyValue('--c3').trim()];
    const n = SER.t.length,
      pw = w - PADL - PADR,
      ph = h - PADT - PADB;
    g.font = '11px "Segoe UI",system-ui,Arial';
    g.textBaseline = 'middle';
    let min = Infinity,
      max = -Infinity;
    for (const s of ch.s)
      for (const v of SER[s[0]])
        if (v !== null) {
          if (v < min) min = v;
          if (v > max) max = v
        }
    // Inclui os limites configurados na escala para que as linhas tracejadas
    // continuem visíveis mesmo quando a série atual não alcança uma faixa.
    for (const L of ch.lim()) {
      if (Number.isFinite(L[0])) {
        if (L[0] < min) min = L[0];
        if (L[0] > max) max = L[0];
      }
    }
    if (!isFinite(min)) {
      g.fillStyle = muted;
      g.textAlign = 'center';
      g.fillText('Sem dados válidos', w / 2, h / 2);
      $('now-' + ch.id).textContent = '--';
      return
    }
    if (max - min < ch.span) {
      const c = (max + min) / 2;
      min = c - ch.span / 2;
      max = c + ch.span / 2
    }
    const pad = (max - min) * .08;
    min -= pad;
    max += pad;
    if (ch.ticks) {
      min = -.25;
      max = 1.25
    }
    const X = i => PADL + (n > 1 ? i / (n - 1) : .5) * pw,
      Y = v => PADT + (1 - (v - min) / (max - min)) * ph;
    g.lineWidth = 1;
    g.strokeStyle = grid;
    g.fillStyle = muted;
    g.textAlign = 'right';
    const ticks = ch.ticks || Array.from({
      length: 4
    }, (_, k) => {
      const v = min + (max - min) * k / 3;
      return [v, v.toFixed(ch.dec)]
    });
    for (const t of ticks) {
      const y = Y(t[0]);
      g.beginPath();
      g.moveTo(PADL, y);
      g.lineTo(w - PADR, y);
      g.stroke();
      g.fillText(t[1], PADL - 6, y)
    }
    g.setLineDash([5, 4]);
    g.textAlign = 'left';
    g.strokeStyle = muted;
    for (const L of ch.lim()) {
      if (L[0] > min && L[0] < max) {
        const y = Y(L[0]);
        g.beginPath();
        g.moveTo(PADL, y);
        g.lineTo(w - PADR, y);
        g.stroke();
        g.fillText(L[1], PADL + 4, y - 7)
      }
    }
    g.setLineDash([]);
    g.textAlign = 'left';
    g.fillText(hm(SER.t[0]), PADL, h - 8);
    g.textAlign = 'right';
    g.fillText(hm(SER.t[n - 1]), w - PADR, h - 8);
    g.lineWidth = 2;
    g.lineJoin = 'round';
    for (const s of ch.s) {
      g.strokeStyle = cols[s[2]];
      g.beginPath();
      let pen = false;
      SER[s[0]].forEach((v, i) => {
        if (v === null) {
          pen = false;
          return
        }
        if (pen) g.lineTo(X(i), Y(v));
        else g.moveTo(X(i), Y(v));
        pen = true
      });
      g.stroke();
    }
    const main = SER[ch.s[0][0]];
    let lastV = null;
    for (let i = main.length - 1; i >= 0; i--)
      if (main[i] !== null) {
        lastV = main[i];
        break
      }
    $('now-' + ch.id).textContent = lastV === null ? '--' : ch.fmt ? ch.fmt(lastV) : lastV.toFixed(ch.dec) + (ch.unit ? ' ' + ch.unit : '');
    if (ch.hover >= 0 && ch.hover < n) {
      const i = ch.hover,
        x = X(i);
      g.strokeStyle = muted;
      g.lineWidth = 1;
      g.beginPath();
      g.moveTo(x, PADT);
      g.lineTo(x, PADT + ph);
      g.stroke();
      for (const s of ch.s) {
        const v = SER[s[0]][i];
        if (v !== null) {
          g.fillStyle = cols[s[2]];
          g.beginPath();
          g.arc(x, Y(v), 4, 0, 6.3);
          g.fill()
        }
      }
      ch.tip.innerHTML = '<b>' + fmtTs(SER.t[i]) + '</b><br>' + ch.s.map(s => {
        const v = SER[s[0]][i];
        return s[1] + ': ' + (v === null ? '--' : ch.fmt ? ch.fmt(v) : v.toFixed(ch.dec) + (ch.unit ? ' ' + ch.unit : ''))
      }).join('<br>');
      ch.tip.style.display = 'block';
      const tw = ch.tip.offsetWidth,
        left = x > w / 2 ? x - tw - 10 : x + 10;
      ch.tip.style.left = (left + 14) + 'px';
    }
  }

  function drawAll() {
    CHARTS.forEach(draw)
  }
  addEventListener('resize', () => requestAnimationFrame(drawAll));

  /* ---------- relógio e comandos ---------- */

  let clockBase = null;

  function parseClock(t) {
    const m = t.match(/^(\d\d)\/(\d\d)\/(\d{4}) (\d\d):(\d\d):(\d\d)$/);
    return m ? Date.UTC(+m[3], +m[2] - 1, +m[1], +m[4], +m[5], +m[6]) : null
  }

  function renderClock() {
    if (!clockBase) return;
    const d = new Date(clockBase.ms + (performance.now() - clockBase.at)),
      p = n => String(n).padStart(2, '0');
    $('clock').textContent = p(d.getUTCDate()) + '/' + p(d.getUTCMonth() + 1) + '/' + d.getUTCFullYear() + ' ' + p(d.getUTCHours()) + ':' + p(d.getUTCMinutes()) + ':' + p(d.getUTCSeconds());
  }
  async function updateClock() {
    if (document.hidden) return;
    try {
      const d = await (await tfetch('/api/relogio')).json();
      if (!d.valid) {
        const box = $('clockMeta');
        box.innerHTML = '';
        ['Sem hora: aguardando NTP (RTC: ' + (d.rtc || '?') + ')'].forEach(t => {
          const s = document.createElement('span');
          s.textContent = t;
          box.appendChild(s)
        });
        setTimeout(updateClock, 3000);
        return
      }
      const ms = parseClock(d.time);
      if (ms !== null) {
        clockBase = {
          ms: ms,
          at: performance.now()
        };
        renderClock()
      } else $('clock').textContent = d.time;
      const box = $('clockMeta');
      box.innerHTML = '';
      ['Fonte: ' + d.source, 'RTC: ' + (d.rtc || '?'), d.timezone, 'Horário de verão: ' + d.dst, 'NTP: ' + d.ntp].forEach(t => {
        const s = document.createElement('span');
        s.textContent = t;
        box.appendChild(s)
      });
    } catch (e) {}
  }

  $('tema').onclick = () => {
    const night = document.body.classList.toggle('night');
    $('tema').textContent = night ? 'Modo claro' : 'Modo noturno';
    try {
      localStorage.setItem('ambisense-tema', night ? 'night' : 'day')
    } catch (e) {}
    drawAll();
  };
  $('tema').textContent = document.body.classList.contains('night') ? 'Modo claro' : 'Modo noturno';

  /* ---------- alarmes sonoros por cartão ---------- */
  // Cada faixa pronta é "mínimo|máximo" (vazio = sem limite daquele lado).
  const ALARMS = {
    co2: {
      u: 'ppm',
      p: [
        ['Atenção (a partir de 800 ppm)', '|800'],
        ['Necessário ventilar (a partir de 1000 ppm)', '|1000'],
        ['Alto (a partir de 1500 ppm)', '|1500'],
        ['Muito alto (a partir de 2000 ppm)', '|2000'],
        ['Crítico (a partir de 3000 ppm)', '|3000'],
        ['Emergência (a partir de 4000 ppm)', '|4000']
      ]
    },
    mq: {
      u: '',
      p: [
        ['Atenção (a partir de 250)', '|250'],
        ['Ar alterado (a partir de 500)', '|500'],
        ['Poluição alta (a partir de 1000)', '|1000'],
        ['Alarme (a partir de 1800)', '|1800']
      ]
    },
    temp: {
      u: '°C',
      p: [
        ['Muito fria (abaixo de 14 °C)', '14|'],
        ['Fria (abaixo de 19 °C)', '19|'],
        ['Quente (acima de 26 °C)', '|26'],
        ['Muito quente (acima de 30 °C)', '|30'],
        ['Fora do conforto (19–26 °C)', '19|26']
      ]
    },
    ntc: {
      u: '°C',
      p: [
        ['Muito fria (abaixo de 14 °C)', '14|'],
        ['Fria (abaixo de 19 °C)', '19|'],
        ['Quente (acima de 26 °C)', '|26'],
        ['Muito quente (acima de 30 °C)', '|30'],
        ['Fora do conforto (19–26 °C)', '19|26']
      ]
    },
    hum: {
      u: '%',
      p: [
        ['Ar seco (abaixo de 40%)', '40|'],
        ['Atenção (abaixo de 30%)', '30|'],
        ['Alerta (abaixo de 20%)', '20|'],
        ['Emergência (abaixo de 12%)', '12|'],
        ['Muito úmido (acima de 70%)', '|70'],
        ['Fora do ideal (40–70%)', '40|70']
      ]
    },
    gas: {
      u: 'kΩ',
      p: [
        ['Abaixo de 50 kΩ (atenção)', '50|'],
        ['Abaixo de 20 kΩ (elevado)', '20|']
      ]
    },
    do: {
      u: '',
      p: [
        ['Acionada (DO = 0)', '|0']
      ]
    },
    press: {
      u: 'hPa',
      p: [
        ['Abaixo de 1000 hPa', '1000|'],
        ['Acima de 1025 hPa', '|1025'],
        ['Fora de 980–1030 hPa', '980|1030']
      ]
    }
  };
  const alarmBox = k => $('al-' + k);
  for (const k in ALARMS) {
    const A = ALARMS[k],
      d = document.createElement('div');
    d.className = 'alarm';
    d.id = 'al-' + k;
    const un = A.u ? ' (' + A.u + ')' : '';
    d.innerHTML = '<label>Alarme sonoro<select class="a-sel"><option value="off">Desligado</option>' + A.p.map(x => '<option value="' + x[1] + '">' + x[0] + '</option>').join('') + '<option value="custom">Personalizada…</option></select></label>' +
      '<div class="row a-custom" hidden><label>Mínimo' + un + '<input type="number" step="any" class="a-min" placeholder="sem limite"></label><label>Máximo' + un + '<input type="number" step="any" class="a-max" placeholder="sem limite"></label><button type="button" class="a-save">Salvar</button></div>' +
      '<div class="msg"></div>';
    $('c-' + k).appendChild(d);
    d.querySelector('.a-sel').onchange = () => onAlarmSelect(k);
    d.querySelector('.a-save').onclick = () => postAlarm(k, 1, d.querySelector('.a-min').value, d.querySelector('.a-max').value);
  }
  const alarmKey = a => (a.min === null ? '' : +a.min) + '|' + (a.max === null ? '' : +a.max);

  function applyAlarms(list, force) {
    list.forEach(a => {
      alarmCfg[a.id] = a;
      const box = alarmBox(a.id);
      if (!box) return;
      $('c-' + a.id).classList.toggle('alarmed', !!a.active);
      if (!force && box.contains(document.activeElement)) return;
      const sel = box.querySelector('.a-sel'),
        custom = box.querySelector('.a-custom');
      box.querySelector('.a-min').value = a.min === null ? '' : a.min;
      box.querySelector('.a-max').value = a.max === null ? '' : a.max;
      if (!a.enabled) sel.value = 'off';
      else {
        const key = alarmKey(a);
        sel.value = Array.from(sel.options).some(o => o.value === key) ? key : 'custom';
      }
      custom.hidden = sel.value !== 'custom';
    });
  }
  async function loadAlarms(force) {
    try {
      applyAlarms((await (await tfetch('/api/alertas')).json()).alerts, force)
    } catch (e) {}
  }
  async function postAlarm(k, enabled, min, max) {
    const box = alarmBox(k),
      msg = box.querySelector('.msg');
    const body = 'id=' + k + '&enabled=' + enabled + '&min=' + encodeURIComponent(min) + '&max=' + encodeURIComponent(max);
    msg.textContent = 'Salvando...';
    try {
      const r = await tfetch('/api/alertas', {
        method: 'POST',
        headers: FORM,
        body: body
      }, 8000);
      if (!r.ok) {
        msg.textContent = await r.text();
        return
      }
      applyAlarms((await r.json()).alerts, true);
      msg.textContent = enabled ? 'Alarme gravado na placa.' : 'Alarme desligado.';
    } catch (e) {
      msg.textContent = 'Sem resposta da placa'
    }
  }

  function onAlarmSelect(k) {
    const box = alarmBox(k),
      v = box.querySelector('.a-sel').value,
      cfg = alarmCfg[k] || {
        min: null,
        max: null
      };
    if (v === 'custom') {
      box.querySelector('.a-custom').hidden = false;
      box.querySelector('.msg').textContent = 'Digite o mínimo e/ou o máximo e toque em Salvar.';
      return
    }
    box.querySelector('.a-custom').hidden = true;
    if (v === 'off') {
      postAlarm(k, 0, cfg.min === null ? '' : cfg.min, cfg.max === null ? '' : cfg.max);
      return
    }
    const lim = v.split('|');
    postAlarm(k, 1, lim[0], lim[1]);
  }
  setInterval(() => {
    if (!document.hidden) loadAlarms(false)
  }, 15000);

  /* ---------- armazenamento e inicialização ---------- */
  (function() {
    const pct = storage.total > 0 ? Math.round(storage.used * 100 / storage.total) : 0;
    $('storText').textContent = storage.total > 0 ? 'Uso: ' + storage.used + ' KB de ' + storage.total + ' KB (' + pct + '%)' : 'Armazenamento indisponível: o FATFS não foi montado.';
    $('storBar').style.width = pct + '%';
  })();
  async function prefill() {
    try {
      const d = await (await tfetch('/api/leituras/historico?limite=40', null, 15000)).json();
      const rows = d.lines.map(x => x.match(pattern)).filter(Boolean);
      if (rows.length) {
        prependRows(rows);
        drawAll()
      }
    } catch (e) {}
  }
  schedule();
  setTimeout(updateClock, 800);
  setTimeout(() => loadAlarms(true), 2000);
  setTimeout(prefill, 6000);
  setInterval(updateClock, 30000);
  setInterval(renderClock, 1000);
  document.addEventListener('visibilitychange', () => {
    if (!document.hidden) {
      update();
      updateClock()
    }
  });
</script>
</body>
</html>
)HTML";

inline void sendDashboardPage(WebServer &server, uint32_t refreshIntervalMs, size_t usedBytes,
                              size_t totalBytes) {

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");

  const auto sendPart = [&server](const char *part) {
    const size_t length = strlen(part);

    for (size_t offset = 0; offset < length; offset += 2048) {

      const size_t remaining = length - offset;

      server.sendContent(part + offset, remaining < 2048 ? remaining : 2048);
    }
  };

  sendPart(DASHBOARD_PART_1);
  server.sendContent(String(refreshIntervalMs));
  sendPart(DASHBOARD_PART_2);
  server.sendContent(String(usedBytes / 1024));
  sendPart(DASHBOARD_PART_3);
  server.sendContent(String(totalBytes / 1024));
  sendPart(DASHBOARD_PART_4);

  // Chunk vazio encerra a resposta.
  server.sendContent(String());
}
