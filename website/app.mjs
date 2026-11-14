import { RAW_CATALOG, REPOSITORY, readCatalog, findImage, formatBytes } from './catalog.mjs';

const $ = selector => document.querySelector(selector);
const $$ = selector => [...document.querySelectorAll(selector)];
const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)');
const root = document.documentElement;
// Each page owns exactly one system's ISO selector. Linux simulation on the
// native page must never switch a user to an independent Linux installation.
const edition = document.body.dataset.system === 'linux' ? 'linux' : 'native';
let userPaused = false;
try { userPaused = localStorage.getItem('mnos-motion') === 'paused'; } catch { /* Storage is optional. */ }
const isPaused = () => reducedMotion.matches || userPaused;
let toastTimer;
function toast(message) {
  $('#toast').textContent = message;
  $('#toast').classList.add('visible');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => $('#toast').classList.remove('visible'), 2800);
}

async function copy(text, message) {
  try {
    if (navigator.clipboard && window.isSecureContext) await navigator.clipboard.writeText(text);
    else {
      const field = document.createElement('textarea');
      field.value = text;
      field.setAttribute('readonly', '');
      field.style.cssText = 'position:fixed;left:-9999px';
      document.body.append(field);
      const previous = document.activeElement;
      field.select();
      const done = document.execCommand('copy');
      field.remove();
      previous?.focus();
      if (!done) throw new Error('Clipboard unavailable');
    }
    toast(message);
  } catch { toast('浏览器未允许复制，请选中文字手动复制。'); }
}

// Enhance, rather than hide, content when JavaScript or motion is unavailable.
if ('IntersectionObserver' in window) {
  root.classList.add('motion-ready');
  const reveals = new IntersectionObserver(entries => {
    for (const entry of entries) if (entry.isIntersecting) {
      entry.target.classList.add('is-visible');
      reveals.unobserve(entry.target);
    }
  }, { threshold: 0.08, rootMargin: '0px 0px -15px 0px' });
  $$('.reveal').forEach(element => reveals.observe(element));
}

const stage = $('.desktop-stage');
const frame = $('.desktop-frame');
let scrollQueued = false;
function updateScroll() {
  scrollQueued = false;
  const distance = Math.max(1, root.scrollHeight - innerHeight);
  $('.reading-progress').style.transform = `scaleX(${Math.min(1, Math.max(0, scrollY / distance))})`;
  if (stage && frame && !isPaused() && innerWidth > 760) {
    const rect = $('#experience').getBoundingClientRect();
    const progress = Math.min(1, Math.max(0, (innerHeight * 0.4 - rect.top) / (innerHeight * 0.8)));
    stage.style.setProperty('--desktop-scale', (0.88 + progress * 0.12).toFixed(3));
    frame.style.setProperty('--desktop-tilt', `${(10 * (1 - progress)).toFixed(2)}deg`);
  }
}
addEventListener('scroll', () => {
  if (!scrollQueued) { scrollQueued = true; requestAnimationFrame(updateScroll); }
}, { passive: true });
addEventListener('resize', updateScroll, { passive: true });
updateScroll();

const commands = edition === 'linux' ? {
  install: { command: 'sudo mnos apt install nano nmap', comment: '# 通过 Linux Edition 的 APT 安装软件', explanation: '保留 APT 的依赖解析和安装确认。普通应用正常运行无需 root，修改系统软件需要 sudo。', help: 'help -cn apt' },
  components: { command: 'sudo mnos full --without code', comment: '# 安装 Full 组件，本次跳过 code', explanation: '--without 后的包本次不安装。依赖冲突会停止，已安装的软件不会因此卸载。', help: 'help -cn packages' },
  update: { command: 'sudo mnos update', comment: '# 先验证签名版本索引，再更新 mnOS 组件', explanation: '从统一资源入口读取版本，并由 APT 校验和安装更新。组件回退需要明确选择已有版本。', help: 'help -cn updates' },
} : {
  info: { command: 'kernel info', comment: '# 在自研 mnOS 中查看原生内核信息', explanation: '这条命令在 mnOS 原生终端运行。Linux 客体中的命令在单独的模拟窗口内执行。', help: 'kernel help -cn' },
  start: { command: 'linux start', comment: '# 请求开启系统内的 Linux 模拟环境', explanation: '默认关闭。用户确认后启动 Linux 客体；暂停、恢复和停止可通过 linux 命令或设置控制。', help: 'help -cn linux' },
  help: { command: 'help -cn linux', comment: '# 阅读 Linux 模拟环境的中文帮助', explanation: '查看模拟环境的开启、暂停、恢复、关闭方式，以及当前支持的客体程序范围。', help: 'help -cn' },
};
let currentCommand = edition === 'linux' ? commands.install : commands.info;
$$('[data-command]').forEach(button => button.addEventListener('click', () => {
  currentCommand = commands[button.dataset.command];
  $$('[data-command]').forEach(other => other.setAttribute('aria-pressed', String(other === button)));
  $('#command-code').textContent = currentCommand.command;
  $('#command-comment').textContent = currentCommand.comment;
  $('#command-explanation').textContent = currentCommand.explanation;
  $('.terminal-help code').textContent = currentCommand.help;
}));
$('#copy-command').addEventListener('click', () => copy(currentCommand.command, '命令已复制'));

let catalog;
let selectedImage = null;
const downloadButton = $('#download-button');
const guidance = {
  native: '此 ISO 安装自研 mnOS。Linux 仅在模拟环境内运行；当前支持部分控制台程序。',
  linux: '此 ISO 安装独立的 Linux 系统。Firefox、VS Code、APT 与 CTF 工具在 Linux 系统内运行。',
};
function renderDownload() {
  const architecture = $('#download-arch').value;
  const flavor = $('#download-flavor').value;
  selectedImage = findImage(catalog, edition, architecture, flavor);
  $('#download-name').textContent = edition === 'native' ? 'mnOS 自研系统' : 'Linux 独立系统';
  $('#result-arch').textContent = architecture.toUpperCase();
  $('#download-guidance').textContent = guidance[edition];
  $('#checksum-details').hidden = !selectedImage;
  if (selectedImage) {
    $('#download-version').textContent = `${selectedImage.version} · ${flavor === 'lite' ? 'Lite' : 'Full'}`;
    $('#result-size').textContent = formatBytes(selectedImage.bytes);
    downloadButton.disabled = false;
    downloadButton.setAttribute('aria-disabled', 'false');
    downloadButton.innerHTML = '下载安装镜像 <svg class="icon"><use href="#icon-download"/></svg>';
    $('#download-notes').href = selectedImage.release;
    $('#checksum-value').textContent = selectedImage.sha256;
  } else {
    $('#download-version').textContent = catalog ? '此版本组合暂未发布。请切换配置，或查看资源目录。' : '资源目录暂时无法读取，请从下方完整目录下载。';
    $('#result-size').textContent = '—';
    downloadButton.disabled = true;
    downloadButton.setAttribute('aria-disabled', 'true');
    downloadButton.textContent = catalog ? '暂未发布' : '目录暂不可用';
    $('#download-notes').href = `${REPOSITORY}/tree/resources`;
    $('#checksum-value').textContent = '';
  }
}
$('#download-arch').addEventListener('change', renderDownload);
$('#download-flavor').addEventListener('change', renderDownload);
const consentDialog = $('#download-consent');
const consentAgree = $('#consent-agree');
const consentDownload = $('#consent-download');
let pendingImage = null;
downloadButton.addEventListener('click', () => {
  if (!selectedImage) return;
  // Freeze the exact selection: a live catalog refresh must not change the
  // image to which the user is agreeing while the dialog is open.
  pendingImage = { ...selectedImage };
  consentAgree.checked = false;
  consentDownload.disabled = true;
  $('#consent-image').textContent = decodeURIComponent(new URL(pendingImage.url).pathname.split('/').pop());
  consentDialog.showModal();
});
consentAgree.addEventListener('change', () => { consentDownload.disabled = !consentAgree.checked; });
$('#consent-cancel').addEventListener('click', () => consentDialog.close());
consentDialog.addEventListener('close', () => {
  pendingImage = null;
  consentAgree.checked = false;
  consentDownload.disabled = true;
  downloadButton.focus({ preventScroll: true });
});
$('#consent-form').addEventListener('submit', event => {
  event.preventDefault();
  if (!consentAgree.checked || !pendingImage) return;
  const url = pendingImage.url;
  consentDownload.disabled = true;
  consentDialog.close();
  window.location.assign(url);
});
$('#copy-checksum').addEventListener('click', () => { if (selectedImage) copy(selectedImage.sha256, 'SHA-256 校验值已复制'); });

async function fetchCatalog(url) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 6000);
  try {
    const response = await fetch(url, { signal: controller.signal, credentials: 'omit', cache: 'no-cache' });
    if (!response.ok) throw new Error('Catalog unavailable');
    const text = await response.text();
    if (text.length > 8 * 1024 * 1024) throw new Error('Catalog too large');
    return readCatalog(JSON.parse(text));
  } finally { clearTimeout(timeout); }
}
function catalogStatus(live) {
  const date = catalog.updated?.toLocaleDateString('zh-CN', { year: 'numeric', month: '2-digit', day: '2-digit' }) ?? '未知';
  $('#catalog-status').textContent = live ? `统一资源目录 · 更新于 ${date}` : `使用页面附带的目录 · ${date} · 可通过完整资源入口查看最新发布`;
}
async function loadDownloads() {
  try { catalog = await fetchCatalog('./catalog.json'); catalogStatus(false); renderDownload(); } catch { /* Try the live source too. */ }
  try {
    const latest = await fetchCatalog(RAW_CATALOG);
    if (!catalog || !catalog.updated || (latest.updated && latest.updated >= catalog.updated)) {
      catalog = latest;
      catalogStatus(true);
      renderDownload();
    }
  } catch {
    if (catalog) catalogStatus(false);
    else { $('#catalog-status').textContent = '资源服务暂不可用，请访问完整资源入口。'; renderDownload(); }
  }
}
loadDownloads();
$('#year').textContent = String(new Date().getFullYear());

// A small canvas draws flowing circuit rings. It stops off-screen, when hidden,
// or when motion is disabled, and caps resolution/frame rate on large displays.
const canvas = $('#flow-field');
const ctx = canvas?.getContext('2d', { alpha: true });
let fieldVisible = true;
let fieldFrame = 0;
let fieldWidth = 1;
let fieldHeight = 1;
let lastDraw = 0;
let time = 0;
function resizeField() {
  if (!canvas) return;
  const bounds = canvas.getBoundingClientRect();
  fieldWidth = bounds.width;
  fieldHeight = bounds.height;
  const ratio = Math.min(devicePixelRatio || 1, 1.5);
  canvas.width = Math.max(1, Math.round(fieldWidth * ratio));
  canvas.height = Math.max(1, Math.round(fieldHeight * ratio));
  if (ctx) ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
  drawField(time);
}
function drawField(t) {
  if (!ctx) return;
  ctx.clearRect(0, 0, fieldWidth, fieldHeight);
  const cx = fieldWidth * 0.5;
  const cy = fieldHeight * 0.47;
  const size = Math.min(fieldWidth / 1200, 1.3);
  for (let ring = 0; ring < 26; ring++) {
    const radius = (180 + ring * 17) * Math.max(size, 0.6);
    ctx.beginPath();
    for (let step = 0; step <= 100; step++) {
      const a = step / 100 * Math.PI * 2;
      const wave = Math.sin(a * 3 + ring * .12 + t * .22) * (5 + ring * .3);
      const x = cx + Math.cos(a) * (radius + wave);
      const y = cy + Math.sin(a) * radius * .25 + wave * .8;
      if (!step) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.strokeStyle = `rgba(145, 205, 163, ${.11 - ring * .0028})`;
    ctx.lineWidth = .7;
    ctx.stroke();
  }
  for (let dot = 0; dot < 12; dot++) {
    const a = dot * 2.399 + t * (dot % 2 ? .025 : -.018);
    const radius = (230 + dot * 22) * Math.max(size, .6);
    const x = cx + Math.cos(a) * radius;
    const y = cy + Math.sin(a) * radius * .25;
    const glow = ctx.createRadialGradient(x, y, 0, x, y, 8);
    glow.addColorStop(0, '#c4f4d385');
    glow.addColorStop(.15, '#b1f2d240');
    glow.addColorStop(1, '#b1f2d200');
    ctx.fillStyle = glow;
    ctx.fillRect(x - 8, y - 8, 16, 16);
  }
}
function animateField(stamp) {
  fieldFrame = 0;
  if (!ctx || isPaused() || !fieldVisible || document.hidden) return;
  if (stamp - lastDraw >= 1000 / 30) {
    time += Math.min(stamp - lastDraw, 100) / 1000;
    lastDraw = stamp;
    drawField(time);
  }
  fieldFrame = requestAnimationFrame(animateField);
}
function scheduleField() {
  if (fieldFrame) cancelAnimationFrame(fieldFrame);
  fieldFrame = 0;
  lastDraw = performance.now();
  if (ctx && !isPaused() && fieldVisible && !document.hidden) fieldFrame = requestAnimationFrame(animateField);
}
function setMotion() {
  root.classList.toggle('motion-paused', isPaused());
  $('#motion-toggle').setAttribute('aria-pressed', String(isPaused()));
  $('#motion-toggle').textContent = reducedMotion.matches ? '已跟随系统减少动态效果' : userPaused ? '开启动画' : '暂停动画';
  $('#motion-toggle').disabled = reducedMotion.matches;
  scheduleField();
  updateScroll();
}
$('#motion-toggle').addEventListener('click', () => {
  userPaused = !userPaused;
  try { localStorage.setItem('mnos-motion', userPaused ? 'paused' : 'running'); } catch { /* Optional preference. */ }
  setMotion();
});
reducedMotion.addEventListener('change', setMotion);
document.addEventListener('visibilitychange', scheduleField);
addEventListener('resize', resizeField, { passive: true });
if (canvas && 'IntersectionObserver' in window) new IntersectionObserver(entries => {
  fieldVisible = entries[0].isIntersecting;
  scheduleField();
}, { threshold: 0 }).observe($('.hero'));
$('.hero')?.addEventListener('pointermove', event => {
  if (isPaused() || event.pointerType !== 'mouse') return;
  const x = (event.clientX / innerWidth - .5) * 7;
  const y = (event.clientY / innerHeight - .5) * 4;
  $('.core-chip').style.transform = `rotateX(${52 - y}deg) rotateZ(${-35 + x}deg)`;
}, { passive: true });
$('.hero')?.addEventListener('pointerleave', () => { $('.core-chip').style.transform = ''; });
resizeField();
setMotion();
