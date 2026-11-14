// Use the provider's supported player, including its controls and attribution.
// Do not extract its preview URL or present a preview as a licensed soundtrack.
export const MUSIC_EMBED_URL = 'https://embed.music.apple.com/us/album/3-strikes/1165457525?i=1165457668&theme=dark';

export function mountMusicPlayer() {
  const dialog = document.querySelector('#music-player');
  if (!dialog) return;
  const slot = dialog.querySelector('.music-player-frame');
  const status = dialog.querySelector('.music-player-status');
  const triggers = [...document.querySelectorAll('[data-music-player]')];
  let opener, frame, timer;

  function load() {
    clearTimeout(timer);
    slot.replaceChildren();
    status.textContent = '正在连接 Apple Music…';
    frame = document.createElement('iframe');
    frame.title = 'Terror Jr — 3 Strikes · Apple Music 官方试听';
    frame.allow = 'autoplay; encrypted-media';
    frame.referrerPolicy = 'strict-origin-when-cross-origin';
    frame.src = MUSIC_EMBED_URL;
    frame.addEventListener('load', () => {
      clearTimeout(timer);
      status.textContent = '点击播放器中的播放按钮。关闭此窗口会停止试听。';
    }, {once:true});
    frame.addEventListener('error', () => {
      clearTimeout(timer);
      status.textContent = '播放器未能加载，请重试或打开下方的原曲页面。';
    }, {once:true});
    timer = setTimeout(() => {
      status.textContent = '连接时间较长。可重试，或打开下方的原曲页面。';
    }, 12000);
    slot.append(frame);
  }

  triggers.forEach(trigger => {
    trigger.hidden = false;
    trigger.addEventListener('click', () => {
      opener = trigger;
      dialog.showModal();
      triggers.forEach(button => button.setAttribute('aria-expanded', 'true'));
      load();
    });
  });
  dialog.querySelector('[data-music-close]').addEventListener('click', () => dialog.close());
  dialog.querySelector('[data-music-retry]').addEventListener('click', load);
  dialog.addEventListener('close', () => {
    clearTimeout(timer);
    // Removing the browsing context stops playback, including decoded audio.
    slot.replaceChildren();
    frame = null;
    triggers.forEach(button => button.setAttribute('aria-expanded', 'false'));
    opener?.focus({preventScroll:true});
  });
  dialog.addEventListener('click', event => {
    if (event.target !== dialog) return;
    const box = dialog.getBoundingClientRect();
    if (event.clientX < box.left || event.clientX > box.right || event.clientY < box.top || event.clientY > box.bottom)
      dialog.close();
  });
}
