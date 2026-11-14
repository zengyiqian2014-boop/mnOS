// A reversible scroll timeline. Scrolling is never intercepted or locked.
const clamp = (n, low = 0, high = 1) => Math.min(high, Math.max(low, n));
const ease = n => { n = clamp(n); return n * n * (3 - 2 * n); };
export function frameAt(progress) {
  const p = clamp(progress);
  return {
    progress: p,
    chapter: p < .32 ? 0 : p < .65 ? 1 : 2,
    opacity: [1 - ease((p - .28) / .08), ease((p - .28) / .08) * (1 - ease((p - .61) / .08)), ease((p - .61) / .08)],
    kernel: ease(p / .3),
    desktop: ease((p - .3) / .32),
    guest: ease((p - .64) / .32),
  };
}
export function mountFilm({ isPaused }) {
  const section = document.querySelector('.scroll-film');
  if (!section) return { refresh() {} };
  const scenes = [...section.querySelectorAll('.film-scene')];
  const chapters = [...section.querySelectorAll('[data-chapter]')];
  const play = section.querySelector('.film-play');
  let progress = 0, playing = false, frame = 0, last = 0;
  const duration = 20; // Silent introduction. Audio timing requires the chosen recording.
  const staticMode = () => isPaused() || window.innerHeight < 500;
  function geometry() {
    return { top: section.getBoundingClientRect().top + window.scrollY - 64,
      range: Math.max(1, section.offsetHeight - window.innerHeight) };
  }
  function draw(value) {
    progress = clamp(value);
    const f = frameAt(progress);
    section.dataset.chapter = String(f.chapter);
    section.style.setProperty('--film-progress', progress.toFixed(5));
    section.style.setProperty('--kernel-turn', `${-36 + f.kernel * 20}deg`);
    section.style.setProperty('--kernel-lift', `${26 + f.kernel * 70}px`);
    section.style.setProperty('--kernel-scale', `${.9 + f.kernel * .16}`);
    section.style.setProperty('--screen-tilt', `${14 * (1 - f.desktop)}deg`);
    section.style.setProperty('--screen-scale', `${.82 + f.desktop * .18}`);
    section.style.setProperty('--guest-open', f.guest.toFixed(4));
    scenes.forEach((scene, i) => {
      const alpha = f.opacity[i];
      scene.style.setProperty('--scene-opacity', alpha.toFixed(4));
      scene.style.setProperty('--scene-shift', `${(1 - alpha) * 45}px`);
      scene.setAttribute('aria-hidden', String(i !== f.chapter));
      scene.inert = i !== f.chapter;
    });
    chapters.forEach((button, i) => {
      button.setAttribute('aria-pressed', String(i === f.chapter));
      button.style.setProperty('--chapter-progress', clamp((progress - i / 3) * 3).toFixed(4));
    });
  }
  function stop() {
    playing = false;
    cancelAnimationFrame(frame); frame = 0;
    play.setAttribute('aria-pressed', 'false');
    play.innerHTML = '播放介绍 <span>↗</span>';
  }
  function tick(stamp) {
    if (!playing || staticMode() || document.hidden) { stop(); return; }
    const delta = Math.min(.06, (stamp - last) / 1000); last = stamp;
    draw(progress + delta / duration);
    const { top, range } = geometry();
    window.scrollTo({ top: top + range * progress, behavior: 'instant' });
    if (progress >= 1) stop();
    else frame = requestAnimationFrame(tick);
  }
  function refresh() {
    section.classList.toggle('film-static', staticMode());
    if (staticMode()) {
      stop();
      scenes.forEach(scene => { scene.removeAttribute('aria-hidden'); scene.inert = false; });
      return;
    }
    if (!playing) {
      const { top, range } = geometry();
      draw((window.scrollY - top) / range);
    }
  }
  let queued = false;
  addEventListener('scroll', () => {
    if (playing || queued) return;
    queued = true;
    requestAnimationFrame(() => { queued = false; refresh(); });
  }, { passive: true });
  addEventListener('resize', refresh, { passive: true });
  for (const type of ['wheel', 'touchstart']) addEventListener(type, stop, { passive: true });
  addEventListener('keydown', event => {
    if (['Escape', 'PageDown', 'PageUp', 'ArrowDown', 'ArrowUp', 'Home', 'End'].includes(event.key)) stop();
  });
  document.addEventListener('visibilitychange', () => { if (document.hidden) stop(); });
  play.addEventListener('click', () => {
    if (playing) { stop(); return; }
    if (staticMode()) return;
    if (progress >= .98) draw(0);
    playing = true; last = performance.now();
    play.setAttribute('aria-pressed', 'true');
    play.innerHTML = '暂停介绍 <span>Ⅱ</span>';
    frame = requestAnimationFrame(tick);
  });
  chapters.forEach((button, i) => button.addEventListener('click', () => {
    stop();
    const { top, range } = geometry();
    window.scrollTo({ top: top + range * [.12, .47, .85][i], behavior: 'instant' });
    refresh();
  }));
  section.classList.add('film-ready');
  refresh();
  return { refresh };
}
