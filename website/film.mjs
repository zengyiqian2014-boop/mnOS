import {clamp,ease} from './film-timing.mjs';
const FILM_DURATION=24;
export function frameAt(value) {
  const p=clamp(value);
  return {progress:p,chapter:p<.32?0:p<.65?1:2,
    opacity:[1-ease((p-.28)/.08),ease((p-.28)/.08)*(1-ease((p-.61)/.08)),ease((p-.61)/.08)]};
}
export function mountFilm({isPaused}) {
  const section=document.querySelector('.scroll-film');
  if(!section)return{refresh(){}};
  const canvas=section.querySelector('.film-canvas'),scenes=[...section.querySelectorAll('.film-scene')];
  const play=section.querySelector('.film-play'),status=section.querySelector('.film-status');
  let world,worldPromise,worldFailed=false;
  let progress=0,playing=false,requested=true,frame=0,last=0,lastUI=-Infinity,lastChapter=-1;
  let ready=false,inView=false;
  let slowFrames=0,qualityReduced=false;
  const staticMode=()=>isPaused()||innerHeight<440;
  const allowed=()=>!staticMode()&&!document.hidden&&inView;
  function draw(value,stamp=performance.now(),force=false) {
    progress=clamp(value);
    if(force||stamp-lastUI>40) {
      lastUI=stamp;const f=frameAt(progress);
      section.dataset.progress=progress.toFixed(5);
      section.style.setProperty('--film-progress',progress.toFixed(5));
      section.style.setProperty('--intro-opacity',(1-ease((progress-.07)/.11)).toFixed(4));
      section.style.setProperty('--camera-copy-opacity',ease((progress-.13)/.09).toFixed(4));
      scenes.forEach((scene,i)=>{
        scene.style.setProperty('--scene-opacity',f.opacity[i].toFixed(4));
        if(f.chapter!==lastChapter){scene.setAttribute('aria-hidden',String(i!==f.chapter));scene.inert=i!==f.chapter;}
      });
      if(f.chapter!==lastChapter){lastChapter=f.chapter;section.dataset.chapter=String(f.chapter);}
    }
    if(world&&allowed())world.render(progress);
  }
  function updateControls() {
    section.classList.toggle('film-playing',playing);
    section.dataset.playing=String(playing);
    play.setAttribute('aria-pressed',String(playing));
    const label=playing?'暂停介绍':progress>=1?'重新播放介绍':'继续介绍';
    play.setAttribute('aria-label',label);play.title=label;
    play.classList.toggle('is-playing',playing);
  }
  function suspend() {
    playing=false;cancelAnimationFrame(frame);frame=0;updateControls();
  }
  function pause() { requested=false;suspend(); }
  function tick(stamp) {
    frame=0;
    if(!playing||!allowed()){suspend();return;}
    const delta=Math.min(.5,Math.max(0,(stamp-last)/1000));last=stamp;
    draw(progress+delta/FILM_DURATION,stamp);
    if(delta>.033)slowFrames++;else slowFrames=Math.max(0,slowFrames-1);
    if(slowFrames>12&&!qualityReduced&&world){world.reduceQuality();qualityReduced=true;}
    if(progress>=1){draw(1,stamp,true);requested=false;suspend();return;}
    frame=requestAnimationFrame(tick);
  }
  function resume() {
    if(!requested||!allowed()||!ready||playing)return;
    playing=true;last=performance.now();updateControls();frame=requestAnimationFrame(tick);
  }
  async function loadWorld() {
    if(world||worldPromise||worldFailed||staticMode()||!inView)return;
    worldPromise=import('./film-scene.mjs').then(module=>module.createFilmScene(canvas)).then(value=>{
      if(staticMode()){value.dispose();return;}
      world=value;section.classList.add('film-webgl');section.dataset.renderer='webgl';
    }).catch(()=>{worldFailed=true;section.dataset.renderer='fallback';}).finally(()=>{
      worldPromise=null;ready=true;draw(progress,performance.now(),true);
      resume();
    });
  }
  function refresh() {
    const rect=section.getBoundingClientRect();
    inView=rect.bottom>innerHeight*.25&&rect.top<innerHeight*.5;
    section.classList.toggle('film-static',staticMode());
    if(staticMode()) {
      suspend();world?.dispose();world=undefined;ready=false;section.classList.remove('film-webgl');section.dataset.renderer='static';
      scenes.forEach(scene=>{scene.removeAttribute('aria-hidden');scene.inert=false;});lastChapter=-1;
      return;
    }
    if(!inView||document.hidden){suspend();return;}
    void loadWorld();
    if(worldFailed){ready=true;resume();}else if(world){draw(progress,performance.now(),true);resume();}
  }
  let queued=false;
  addEventListener('scroll',()=>{if(queued)return;queued=true;requestAnimationFrame(()=>{queued=false;refresh();});},{passive:true});
  addEventListener('resize',refresh,{passive:true});
  document.addEventListener('visibilitychange',refresh);
  addEventListener('keydown',event=>{if(event.key==='Escape')pause();});
  play.addEventListener('click',()=>{
    if(playing){pause();return;}
    if(progress>=1)draw(0,performance.now(),true);
    requested=true;resume();
  });
  canvas.addEventListener('webglcontextlost',event=>{
    event.preventDefault();world?.dispose();world=undefined;worldFailed=true;ready=true;
    section.classList.remove('film-webgl');section.dataset.renderer='fallback';draw(progress,performance.now(),true);
  });
  status.textContent='介绍会自动播放。可用右下角按钮或 Escape 暂停。';
  section.classList.add('film-ready');refresh();
  return{refresh};
}
