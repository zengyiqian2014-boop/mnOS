import { connectMusic, TRACK_START, FILM_DURATION } from './film-audio.mjs';
const clamp = (n, low = 0, high = 1) => Math.min(high, Math.max(low, n));
const ease = n => { n = clamp(n); return n * n * (3 - 2 * n); };
export function frameAt(progress) {
  const p = clamp(progress);
  return { progress:p, chapter:p<.32?0:p<.65?1:2,
    opacity:[1-ease((p-.28)/.08),ease((p-.28)/.08)*(1-ease((p-.61)/.08)),ease((p-.61)/.08)],
    kernel:ease(p/.3),desktop:ease((p-.3)/.32),guest:ease((p-.64)/.32) };
}
export function mountFilm({ isPaused }) {
  const section = document.querySelector('.scroll-film');
  if (!section) return { refresh() {} };
  const scenes=[...section.querySelectorAll('.film-scene')];
  const chapters=[...section.querySelectorAll('[data-chapter]')];
  const play=section.querySelector('.film-play'), musicButton=section.querySelector('.film-music');
  const audioPanel=section.querySelector('.film-audio'), audioStatus=section.querySelector('.film-audio-status');
  const canvas=section.querySelector('.film-canvas');
  let progress=0,playing=false,frame=0,last=0,mode='silent';
  let world,worldPromise,worldFailed=false,music,musicPromise,musicWanted=false,mediaTime=0,mediaStamp=0;
  const staticMode=()=>isPaused()||innerHeight<500;
  function geometry(){return{top:section.getBoundingClientRect().top+scrollY-64,range:Math.max(1,section.offsetHeight-innerHeight)};}
  function visible(){const r=section.getBoundingClientRect();return r.top<innerHeight&&r.bottom>64;}
  async function loadWorld(){
    if(world||worldPromise||worldFailed||staticMode()||!visible())return;
    worldPromise=import('./film-scene.mjs').then(m=>m.createFilmScene(canvas)).then(result=>{
      if(staticMode()){result.dispose();return;}
      world=result;section.classList.add('film-webgl');section.dataset.renderer='webgl';draw(progress);
    }).catch(()=>{worldFailed=true;section.dataset.renderer='fallback';section.classList.remove('film-webgl');}).finally(()=>{worldPromise=null;});
  }
  function draw(value){
    progress=clamp(value);const f=frameAt(progress);
    section.dataset.chapter=String(f.chapter);section.dataset.progress=progress.toFixed(5);
    section.style.setProperty('--film-progress',progress.toFixed(5));
    section.style.setProperty('--intro-opacity',(1-ease((progress-.06)/.1)).toFixed(5));
    section.style.setProperty('--copy-opacity',ease((progress-.12)/.08).toFixed(5));
    // Let the screen occupy the close-up without type across its interface.
    const closeUp = ease((progress-.37)/.045) * (1-ease((progress-.53)/.04));
    section.style.setProperty('--camera-copy-opacity',(ease((progress-.12)/.08)*(1-closeUp)).toFixed(5));
    section.style.setProperty('--kernel-turn',(-36+f.kernel*20)+'deg');
    section.style.setProperty('--kernel-lift',(26+f.kernel*70)+'px');
    section.style.setProperty('--kernel-scale',String(.9+f.kernel*.16));
    section.style.setProperty('--screen-tilt',(14*(1-f.desktop))+'deg');
    section.style.setProperty('--screen-scale',String(.82+f.desktop*.18));
    section.style.setProperty('--guest-open',f.guest.toFixed(4));
    scenes.forEach((scene,i)=>{
      const alpha=f.opacity[i];scene.style.setProperty('--scene-opacity',alpha.toFixed(4));
      scene.style.setProperty('--scene-shift',((1-alpha)*35)+'px');
      scene.setAttribute('aria-hidden',String(i!==f.chapter));scene.inert=i!==f.chapter;
    });
    chapters.forEach((button,i)=>{button.setAttribute('aria-pressed',String(i===f.chapter));button.style.setProperty('--chapter-progress',clamp((progress-i/3)*3).toFixed(4));});
    if(world&&visible()&&!staticMode())world.render(progress);
  }
  function stop(){
    playing=false;mode='silent';musicWanted=false;cancelAnimationFrame(frame);frame=0;
    music?.pause();play.setAttribute('aria-pressed','false');play.innerHTML='播放介绍 <span>↗</span>';
    musicButton.textContent='音乐版 · 3 Strikes ↗';
  }
  function tick(stamp){
    if(!playing||staticMode()||document.hidden){stop();return;}
    if(mode==='music'){
      // Never outrun buffered audio: extrapolate at most 100 ms between widget events.
      const seconds=mediaTime+Math.min(.1,Math.max(0,(stamp-mediaStamp)/1000));
      draw((seconds-TRACK_START)/FILM_DURATION);
    }else{const delta=Math.min(.06,(stamp-last)/1000);draw(progress+delta/FILM_DURATION);}
    last=stamp;const{top,range}=geometry();scrollTo({top:top+range*progress,behavior:'instant'});
    if(progress>=1)stop();else frame=requestAnimationFrame(tick);
  }
  function begin(kind){
    if(staticMode()||document.hidden)return;
    playing=true;mode=kind;last=performance.now();
    play.setAttribute('aria-pressed','true');play.innerHTML='暂停介绍 <span>Ⅱ</span>';
    cancelAnimationFrame(frame);frame=requestAnimationFrame(tick);
  }
  function refresh(){
    section.classList.toggle('film-static',staticMode());
    if(staticMode()){
      stop();world?.dispose();world=undefined;section.dataset.renderer='static';section.classList.remove('film-webgl');
      scenes.forEach(scene=>{scene.removeAttribute('aria-hidden');scene.inert=false;});return;
    }
    void loadWorld();
    if(!playing){const{top,range}=geometry();draw((scrollY-top)/range);}
  }
  let queued=false;
  addEventListener('scroll',()=>{if(playing||queued)return;queued=true;requestAnimationFrame(()=>{queued=false;refresh();});},{passive:true});
  addEventListener('resize',refresh,{passive:true});
  for(const type of ['wheel','touchstart'])addEventListener(type,event=>{if(!audioPanel.contains(event.target))stop();},{passive:true});
  addEventListener('keydown',event=>{if(['Escape','PageDown','PageUp','ArrowDown','ArrowUp','Home','End'].includes(event.key))stop();});
  document.addEventListener('visibilitychange',()=>{if(document.hidden)stop();});
  canvas.addEventListener('webglcontextlost',event=>{
    event.preventDefault();stop();world?.dispose();world=undefined;worldFailed=true;
    section.classList.remove('film-webgl');section.dataset.renderer='fallback';
  });
  play.addEventListener('click',()=>{if(playing){stop();return;}if(staticMode())return;if(progress>=.98)draw(0);begin('silent');});
  chapters.forEach((button,i)=>button.addEventListener('click',()=>{stop();const{top,range}=geometry();scrollTo({top:top+range*[.22,.43,.73][i],behavior:'instant'});refresh();}));
  musicButton.addEventListener('click',async()=>{
    if(mode==='music'&&playing){stop();return;}
    stop();if(staticMode())return;
    musicWanted=true;audioPanel.hidden=false;musicButton.setAttribute('aria-expanded','true');
    musicButton.textContent='连接音乐…';audioStatus.textContent='原曲约 00:12 起。点击播放器也可开始；画面跟随音乐。';
    try{
      if(!musicPromise)musicPromise=connectMusic(section.querySelector('.film-audio-player'),{
        onPosition(seconds){if(!musicWanted)return;mediaTime=seconds;mediaStamp=performance.now();if(mode==='music'&&seconds>=TRACK_START+FILM_DURATION)stop();},
        onState(state){
          if(state==='playing'){
            if(!musicWanted||staticMode()||document.hidden){music?.pause();return;}
            musicButton.textContent='暂停音乐版';begin('music');
          }else if(mode==='music'){playing=false;cancelAnimationFrame(frame);frame=0;play.setAttribute('aria-pressed','false');play.innerHTML='播放介绍 <span>↗</span>';musicButton.textContent='音乐版 · 3 Strikes ↗';}
        },onError(){stop();audioStatus.textContent='音乐服务暂不可用；可继续观看无声介绍。';}
      }).then(value=>{music=value;return value;}).catch(error=>{musicPromise=null;throw error;});
      const player=await musicPromise;
      if(!musicWanted||staticMode()||document.hidden)return;
      draw(0);mediaTime=TRACK_START;mediaStamp=performance.now();player.play(0);
      musicButton.textContent='在播放器中开始 ↗';
    }catch(error){stop();audioStatus.textContent=error.message;}
  });
  section.querySelector('.film-audio-close').addEventListener('click',()=>{stop();audioPanel.hidden=true;musicButton.setAttribute('aria-expanded','false');musicButton.focus();});
  section.classList.add('film-ready');refresh();
  return{refresh};
}
