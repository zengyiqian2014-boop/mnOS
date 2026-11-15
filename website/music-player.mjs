// Use the provider's supported player, including its controls and attribution.
// Do not extract its preview URL or present a preview as a licensed soundtrack.
export const MUSIC_PROVIDERS = {
  soundcloud: {
    name:'SoundCloud',
    embed:'https://w.soundcloud.com/player/?url=https%3A%2F%2Fapi.soundcloud.com%2Ftracks%2F256591463&color=%239ce0c8&auto_play=false&hide_related=true&show_comments=false&show_reposts=false&show_teaser=false',
    listen:'https://soundcloud.com/terrorjr/3-strikes',
    description:'SoundCloud · Terror Jr 发布的原曲',
  },
  apple: {
    name:'Apple Music',
    embed:'https://embed.music.apple.com/us/album/3-strikes/1165457525?i=1165457668&theme=dark',
    listen:'https://music.apple.com/us/song/1165457668',
    description:'Apple Music 官方试听 · provided courtesy of iTunes',
  },
};

export function mountMusicPlayer() {
  const dialog = document.querySelector('#music-player');
  if (!dialog) return;
  const slot = dialog.querySelector('.music-player-frame');
  const status = dialog.querySelector('.music-player-status');
  const select = dialog.querySelector('#music-provider');
  const link = dialog.querySelector('.music-player-links a');
  const triggers = [...document.querySelectorAll('[data-music-player]')];
  let opener, frame, timer;

  function load() {
    const provider=MUSIC_PROVIDERS[select.value]||MUSIC_PROVIDERS.soundcloud;
    clearTimeout(timer);
    slot.replaceChildren();
    status.textContent = `正在连接 ${provider.name}…`;
    dialog.querySelector('#music-player-description').textContent=provider.description;
    link.href=provider.listen;link.textContent=`在 ${provider.name} 中打开 ↗`;
    frame = document.createElement('iframe');
    frame.title = `Terror Jr — 3 Strikes · ${provider.name}`;
    frame.allow = 'autoplay; encrypted-media';
    frame.referrerPolicy = 'strict-origin-when-cross-origin';
    frame.src = provider.embed;
    const current=frame;
    frame.addEventListener('load', () => {
      if(frame!==current||!dialog.open)return;
      clearTimeout(timer);
      status.textContent = '点击播放器中的播放按钮。关闭此窗口会停止试听。';
    }, {once:true});
    frame.addEventListener('error', () => {
      if(frame!==current||!dialog.open)return;
      clearTimeout(timer);
      status.textContent = '播放器未能加载。可以切换来源、重试或打开原曲页面。';
    }, {once:true});
    timer = setTimeout(() => {
      status.textContent = '连接时间较长。可以切换播放来源，或打开下方的原曲页面。';
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
  select.addEventListener('change', () => {if(dialog.open)load();});
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
