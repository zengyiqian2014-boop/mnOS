export const clamp = value => Math.max(0, Math.min(1, value));
export const ease = value => { const t=clamp(value);return t*t*(3-2*t); };
// Cubic Hermite interpolation with one shared tangent at each camera key.
// Velocity is continuous across shots, instead of stopping at every keyframe.
export function sampleTrack(keys, progress, field, component) {
  const p=clamp(progress);
  const get = key => component === undefined ? key[field] : key[field][component];
  let right=1;
  while (right < keys.length-1 && p > keys[right].p) right++;
  const left=right-1, a=keys[left],b=keys[right], span=b.p-a.p;
  const tangent = i => i===0 || i===keys.length-1 ? 0 : (get(keys[i+1])-get(keys[i-1]))/(keys[i+1].p-keys[i-1].p);
  const t=(p-a.p)/span,t2=t*t,t3=t2*t;
  return (2*t3-3*t2+1)*get(a)+(t3-2*t2+t)*span*tangent(left)+(-2*t3+3*t2)*get(b)+(t3-t2)*span*tangent(right);
}
