import test from 'node:test';
import assert from 'node:assert/strict';
import {sampleTrack} from '../film-timing.mjs';
import {frameAt} from '../film.mjs';
test('camera velocity remains continuous across changing direction and unequal shot lengths',()=>{
 const keys=[{p:0,position:8},{p:.18,position:7},{p:.34,position:1.6},{p:.5,position:4.6},{p:.68,position:-4.6},{p:1,position:4.4}];
 const e=1e-6;
 for(const key of keys.slice(1,-1)){
  const left=(sampleTrack(keys,key.p,'position')-sampleTrack(keys,key.p-e,'position'))/e;
  const right=(sampleTrack(keys,key.p+e,'position')-sampleTrack(keys,key.p,'position'))/e;
  assert.ok(Math.abs(left-right)<.01,`Velocity discontinuity at ${key.p}`);
 }
 assert.equal(sampleTrack(keys,-1,'position'),8);assert.equal(sampleTrack(keys,2,'position'),4.4);
});
test('chapter crossfades retain readable content without opacity jumps',()=>{
 for(let p=0;p<=1;p+=.001){const f=frameAt(p);assert.ok(Math.abs(f.opacity.reduce((a,b)=>a+b,0)-1)<1e-8);}
});
