// Official artist upload, streamed by the visible SoundCloud player.
// No track bytes are copied into this repository. Load only after a user click.
export const TRACK_START = 12.06;
export const FILM_DURATION = 24;
let api;
function loadApi() {
  if (window.SC?.Widget) return Promise.resolve(window.SC);
  if (api) return api;
  api = new Promise((resolve, reject) => {
    const script = document.createElement('script');
    const timer = setTimeout(() => reject(new Error('音乐服务连接超时。可以继续观看无声介绍。')), 15000);
    script.src = 'https://w.soundcloud.com/player/api.js';
    script.onload = () => { clearTimeout(timer); window.SC?.Widget ? resolve(window.SC) : reject(new Error('音乐播放器未能启动。')); };
    script.onerror = () => { clearTimeout(timer); reject(new Error('暂时无法连接音乐服务。可以继续观看无声介绍。')); };
    document.head.append(script);
  }).catch(error => { api = null; throw error; });
  return api;
}
export async function connectMusic(container, { onPosition, onState, onError }) {
  const SC = await loadApi();
  const iframe = document.createElement('iframe');
  iframe.title = 'Terror Jr — 3 Strikes · SoundCloud';
  iframe.allow = 'autoplay'; iframe.height = '166'; iframe.width = '100%';
  iframe.src = 'https://w.soundcloud.com/player/?url=https%3A%2F%2Fapi.soundcloud.com%2Ftracks%2F256591463&color=%239ce0c8&auto_play=false&hide_related=true&show_comments=false&show_reposts=false&show_teaser=false';
  container.replaceChildren(iframe);
  const widget = SC.Widget(iframe);
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('音乐加载超时。可直接打开原曲，或继续无声播放。')), 20000);
    widget.bind(SC.Widget.Events.READY, () => { clearTimeout(timer); resolve(); });
    widget.bind(SC.Widget.Events.ERROR, () => { clearTimeout(timer); reject(new Error('音乐服务暂不可用。')); onError(); });
  });
  widget.bind(SC.Widget.Events.PLAY_PROGRESS, event => onPosition(event.currentPosition / 1000));
  widget.bind(SC.Widget.Events.PLAY, () => onState('playing'));
  widget.bind(SC.Widget.Events.PAUSE, () => onState('paused'));
  widget.bind(SC.Widget.Events.FINISH, () => onState('paused'));
  return {
    play(progress = 0) { widget.seekTo((TRACK_START + progress * FILM_DURATION) * 1000); widget.play(); },
    pause() { widget.pause(); },
  };
}
