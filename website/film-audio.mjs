// Native audio: same-origin files or explicitly configured HTTPS CDN mirrors.
// No third-party player, tracking SDK, or unverified music URL is loaded.
export const TRACK_START = 12.06;
export const FILM_DURATION = 24;
export function readMusicConfig(value, base) {
  if (!value || !Array.isArray(value.sources)) throw new Error('Invalid music configuration');
  const start = value.start ?? TRACK_START, duration = value.duration ?? FILM_DURATION;
  if (!Number.isFinite(start) || start < 0 || !Number.isFinite(duration) || duration < 1 || duration > 120)
    throw new Error('Invalid music timing');
  const sources = value.sources.slice(0, 3).map(source => {
    if (typeof source !== 'string' || !source.trim()) throw new Error('Invalid music URL');
    const url = new URL(source, base);
    if (url.username || url.password || !['https:','http:'].includes(url.protocol) || (url.protocol !== 'https:' && url.origin !== new URL(base).origin))
      throw new Error('Music sources must use HTTPS or the same origin');
    return url.href;
  });
  return {start, duration, sources, title: String(value.title || ''), artist: String(value.artist || '')};
}
export async function connectMusic(audio, onState) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 2500);
  let config;
  try {
    const response = await fetch(new URL('./media/music.json', import.meta.url), {signal: controller.signal, credentials:'omit'});
    if (!response.ok) throw new Error('Music configuration unavailable');
    config = readMusicConfig(await response.json(), import.meta.url);
  } finally { clearTimeout(timer); }
  if (!config.sources.length) { onState('unconfigured'); return null; }
  let index = 0, pending, generation = 0, stalled = false, destroyed = false;
  audio.preload = 'auto';
  audio.volume = .65;
  const events = [];
  function listen(name, callback) { audio.addEventListener(name, callback); events.push([name, callback]); }
  function prepare() {
    if (pending) return pending;
    pending = (async () => {
      for (; index < config.sources.length && !destroyed; index++) {
        try {
          await new Promise((resolve, reject) => {
            const timeout = setTimeout(() => done(new Error('Music load timeout')), 5000);
            const ready = () => done();
            const fail = () => done(new Error('Music source unavailable'));
            function done(error) {
              clearTimeout(timeout); audio.removeEventListener('loadeddata', ready); audio.removeEventListener('error', fail);
              error ? reject(error) : resolve();
            }
            audio.addEventListener('loadeddata', ready, {once:true});
            audio.addEventListener('error', fail, {once:true});
            audio.src = config.sources[index]; audio.load();
          });
          if (!Number.isFinite(audio.duration) || audio.duration < config.start + config.duration - .1)
            throw new Error('The audio does not contain the configured introduction');
          return;
        } catch (error) { if (index + 1 === config.sources.length) throw error; }
      }
      throw new Error('Music stopped');
    })();
    return pending;
  }
  listen('playing', () => { stalled=false; onState('playing'); });
  listen('waiting', () => { if(!audio.paused){stalled=true; onState('buffering');} });
  listen('pause', () => onState('paused'));
  listen('ended', () => onState('ended'));
  listen('error', () => onState('unavailable'));
  function seekTo(progress) {
    const position=config.start+Math.max(0,Math.min(.999,progress))*config.duration;
    let seekable=false;
    for(let i=0;i<audio.seekable.length;i++)if(position>=audio.seekable.start(i)&&position<=audio.seekable.end(i))seekable=true;
    if(!seekable){audio.pause();onState('unavailable');return false;}
    audio.currentTime=position;return true;
  }
  // Loading never blocks the visual introduction. Catch rejection immediately.
  const ready = prepare().catch(() => { onState('unavailable'); return false; });
  return {
    config,
    async play(progress=0) {
      const request = ++generation;
      if (await ready === false || request !== generation || destroyed) return false;
      const current = typeof progress === 'function' ? progress() : progress;
      if(!seekTo(current))return false;
      try { await audio.play(); return request === generation; }
      catch (error) { if(request === generation && !destroyed) onState(error.name === 'NotAllowedError' ? 'blocked' : 'unavailable'); return false; }
    },
    pause() { generation++; audio.pause(); },
    seek(progress) { return audio.readyState>0&&seekTo(progress); },
    time() { return stalled || audio.paused || audio.readyState < 3 ? null : (audio.currentTime-config.start)/config.duration; },
    dispose() { destroyed=true;generation++;audio.pause();for(const [name,fn] of events)audio.removeEventListener(name,fn);audio.removeAttribute('src');audio.load(); },
  };
}
