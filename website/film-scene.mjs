// Original, illustrative device geometry; no Apple models or product imagery.
import * as T from './vendor/three.module.min.js';
import { RoundedBoxGeometry } from './vendor/RoundedBoxGeometry.js';
import { RoomEnvironment } from './vendor/RoomEnvironment.js';

import {clamp,ease as smooth,sampleTrack} from './film-timing.mjs';
const mix = (a, b, t) => a + (b - a) * t;
export async function createFilmScene(canvas) {
  const renderer = new T.WebGLRenderer({ canvas, antialias: true, alpha: true, powerPreference: 'low-power' });
  renderer.setPixelRatio(Math.min(devicePixelRatio, innerWidth < 760 ? 1.25 : 1.5));
  renderer.transmissionResolutionScale = .5;
  renderer.setClearColor(0x03060b, 0);
  renderer.outputColorSpace = T.SRGBColorSpace;
  renderer.toneMapping = T.ACESFilmicToneMapping;
  renderer.toneMappingExposure = .95;
  const scene = new T.Scene();
  const camera = new T.PerspectiveCamera(38, 1, .1, 100);
  const pmrem = new T.PMREMGenerator(renderer);
  const room = new RoomEnvironment();
  const environment = pmrem.fromScene(room, .03);
  scene.environment = environment.texture;
  room.dispose(); pmrem.dispose();
  scene.add(new T.HemisphereLight(0xdde9ff, 0x131422, .65));
  const key = new T.DirectionalLight(0xe2eaff, 5.5); key.position.set(-4, 7, 6); scene.add(key);
  const rim = new T.DirectionalLight(0x88ffe1, 3.4); rim.position.set(6, 2, -4); scene.add(rim);
  const fill = new T.DirectionalLight(0x919eff, 2); fill.position.set(-6, 1, -4); scene.add(fill);
  const materials = [];
  const metal = (color, roughness = .25, metalness = .9) => {
    const m = new T.MeshStandardMaterial({ color, roughness, metalness }); materials.push(m); return m;
  };
  const graphite = metal(0x20252d, .3), edge = metal(0x717784, .21), black = metal(0x080b10, .5, .25);
  const glass = metal(0x080b10, .12, .5);
  const liquid = new T.MeshPhysicalMaterial({color:0xe1f3ff,metalness:0,roughness:.025,transmission:.98,thickness:.25,ior:1.45,clearcoat:1,clearcoatRoughness:.06,attenuationColor:0x8bd8f0,attenuationDistance:3,envMapIntensity:.75});
  materials.push(liquid);
  const geometries = [];
  function rounded(parent, w, h, d, r, material, x = 0, y = 0, z = 0) {
    const geometry = new RoundedBoxGeometry(w, h, d, 3, r); geometries.push(geometry);
    const mesh = new T.Mesh(geometry, material); mesh.position.set(x, y, z); parent.add(mesh); return mesh;
  }
  const device = new T.Group(); scene.add(device);
  rounded(device, 6.85, .19, 4.6, .07, graphite, 0, -.8, 0);
  rounded(device, 6.79, .025, 4.54, .012, edge, 0, -.701, 0);
  rounded(device, 6.75, .026, 4.5, .012, graphite, 0, -.68, 0);
  rounded(device, 5.69, .018, 2.22, .008, black, 0, -.656, -.72);
  rounded(device, 2.47, .012, 1.11, .005, edge, 0, -.66, 1.14);
  rounded(device, 2.44, .013, 1.08, .005, graphite, 0, -.65, 1.14);
  const keyGeometry = new RoundedBoxGeometry(.36, .045, .35, 2, .018); geometries.push(keyGeometry);
  const keys = new T.InstancedMesh(keyGeometry, black, 70); const matrix = new T.Matrix4();
  for (let row = 0; row < 5; row++) for (let col = 0; col < 14; col++) {
    matrix.makeTranslation(-2.61 + col * .4, -.619, -1.6 + row * .42); keys.setMatrixAt(row * 14 + col, matrix);
  }
  device.add(keys);
  rounded(device, 2.8, .047, .29, .018, black, 0, -.615, .44);
  for (const x of [-3.06, 3.06]) rounded(device, .22, .01, 2.08, .004, black, x, -.657, -.73);
  for (const x of [-3.428, 3.428]) for (let i = 0; i < 2; i++) rounded(device, .012, .063, .3, .004, black, x, -.795, -.6 - i * .6);
  const hinge = new T.Group(); hinge.position.set(0, -.61, -2.19); device.add(hinge);
  rounded(hinge, 6.84, 4.18, .12, .055, edge, 0, 2.04, 0);
  rounded(hinge, 6.79, 4.13, .121, .055, graphite, 0, 2.04, 0);
  rounded(hinge, 6.62, 3.93, .025, .012, glass, 0, 2.08, .07);
  const textures = [];
  let disposed = false, width = 0, height = 0;
  const loader = new T.TextureLoader();
  async function texture(path) {
    const t = await loader.loadAsync(new URL(path, import.meta.url).href);
    t.colorSpace = T.SRGBColorSpace; t.anisotropy = Math.min(4, renderer.capabilities.getMaxAnisotropy()); textures.push(t); return t;
  }
  const images = await Promise.allSettled([
    texture('./assets/native-home.png'), texture('./assets/native-editor.png'), texture('./assets/native-desktop.png'),
  ]);
  if (images.some(result => result.status === 'rejected')) {
    dispose();
    throw new Error('The desktop images could not be loaded.');
  }
  const [desktop, editor, terminal] = images.map(result => result.value);
  function image(parent, map, w, h, x, y, z) {
    const geometry = new T.PlaneGeometry(w, h); geometries.push(geometry);
    const material = new T.MeshBasicMaterial({ map, toneMapped: false }); materials.push(material);
    const mesh = new T.Mesh(geometry, material); mesh.position.set(x, y, z); parent.add(mesh); return mesh;
  }
  const screen = image(hinge, desktop, 6.38, 3.589, 0, 2.1, .087);
  rounded(hinge, .045, .045, .015, .007, black, 0, 4.035, .085);
  function label(text, color = '#eaf6f1', size = 96, width = 768, height = 256) {
    const c = document.createElement('canvas'); c.width = width; c.height = height;
    const g = c.getContext('2d'); g.fillStyle = color; g.font = '600 ' + size + 'px Arial, sans-serif';
    g.textAlign = 'center'; g.textBaseline = 'middle'; g.fillText(text, width / 2, height / 2);
    const t = new T.CanvasTexture(c); t.colorSpace = T.SRGBColorSpace; textures.push(t); return t;
  }
  const backLogo = image(hinge, label('mnOS'), 1.4, .47, 0, 2.1, -.064);
  backLogo.rotation.y = Math.PI; backLogo.material.transparent = true;
  const layers = new T.Group(); scene.add(layers);
  const cards = [];
  for (const [i, map] of [editor, terminal, desktop].entries()) {
    const card = new T.Group(); layers.add(card); cards.push(card);
    rounded(card, 6.68, 3.93, .24, .12, liquid);
    rounded(card, 6.31, 3.59, .025, .012, black, 0, 0, .126);
    image(card, map, 6.27, 3.527, 0, 0, .142);
  }
  const chip = new T.Group(); layers.add(chip);
  rounded(chip, 2.15, 2.15, .32, .14, liquid);
  rounded(chip, 1.77, 1.77, .025, .012, black, 0, 0, .17);
  const chipLabel = image(chip, label('mnOS'), 1.35, .45, 0, .12, .19); chipLabel.material.transparent = true;
  const chipSub = image(chip, label('NATIVE', '#95b7b2', 65), 1.1, .367, 0, -.32, .19); chipSub.material.transparent = true;
  const pinGeometry = new T.BoxGeometry(.055,.10,.04); geometries.push(pinGeometry);
  const pins = new T.InstancedMesh(pinGeometry, edge, 56);
  let pin=0;
  for(let i=0;i<14;i++)for(const side of [-1,1]){
    matrix.makeTranslation(-.78+i*.12,side*.93,0);pins.setMatrixAt(pin++,matrix);
    matrix.makeRotationZ(Math.PI/2);matrix.setPosition(side*.93,-.78+i*.12,0);pins.setMatrixAt(pin++,matrix);
  }
  chip.add(pins);
  // Soft contact shadow, generated only once; all motion uses a real camera.
  const shadowCanvas = document.createElement('canvas'); shadowCanvas.width = shadowCanvas.height = 128;
  const g = shadowCanvas.getContext('2d'); const gradient = g.createRadialGradient(64,64,5,64,64,64);
  gradient.addColorStop(0,'rgba(0,0,0,.85)'); gradient.addColorStop(1,'rgba(0,0,0,0)'); g.fillStyle=gradient;g.fillRect(0,0,128,128);
  const shadowMap = new T.CanvasTexture(shadowCanvas); textures.push(shadowMap);
  const shadow = image(scene, shadowMap, 13, 8, 0, -2.28, 0); shadow.material.transparent = true; shadow.rotation.x = -Math.PI / 2;
  const shots = [
    {p:0, camera:[8,2.2,10.8], target:[0,.1,0], turn:-.45, tilt:-.05, lid:1.47},
    {p:.18, camera:[7,3.1,10.8], target:[0,.5,-.2], turn:-.18, tilt:0, lid:.14},
    {p:.34, camera:[1.6,3.3,10.6], target:[0,.9,-.5], turn:0, tilt:0, lid:-.12},
    {p:.50, camera:[4.6,2.7,11.4], target:[0,.65,-.4], turn:0, tilt:0, lid:-.12},
    {p:.68, camera:[-4.6,2.8,12], target:[0,.55,-.5], turn:0, tilt:0, lid:-.12},
    {p:.84, camera:[-5,2.6,13], target:[0,.65,-.2], turn:.1, tilt:.02, lid:-.12},
    {p:1, camera:[4.4,2.6,12], target:[0,.65,0], turn:-.2, tilt:.02, lid:-.12},
  ];
  const target = new T.Vector3();
  function render(progress) {
    if (disposed) return;
    const p = clamp(progress), w = canvas.clientWidth, h = canvas.clientHeight;
    if (!w || !h) return;
    if (w !== width || h !== height) { width=w;height=h;renderer.setSize(w,h,false);camera.aspect=w/h;camera.updateProjectionMatrix(); }
    const portrait=w/h<.9;
    for(let i=0;i<3;i++){camera.position.setComponent(i,sampleTrack(shots,p,'camera',i));target.setComponent(i,sampleTrack(shots,p,'target',i));}
    if(portrait) {
      camera.position.sub(target).multiplyScalar(1.2/(w/h)).add(target);
      // Keep the physical scene above the phone's caption and controls.
      camera.setViewOffset(w,h,0,h*.09,w,h);
    } else {
      camera.setViewOffset(w,h,-w*.13,0,w,h);
    }
    camera.lookAt(target);
    device.rotation.set(0,sampleTrack(shots,p,'turn'),sampleTrack(shots,p,'tilt'));
    hinge.rotation.x=sampleTrack(shots,p,'lid');
    const reveal=smooth((p-.36)/.23)*(1-smooth((p-.80)/.20));
    device.position.y=-5*reveal;device.scale.setScalar(1-.98*reveal);
    device.visible=reveal<.999;
    layers.visible=reveal>.001;
    screen.material.color.setScalar(.75+.25*smooth((p-.05)/.15));
    if(layers.visible){
      layers.rotation.set(-.08*reveal,-.12*reveal,-.025*reveal);
      layers.position.set(0,mix(1.45,.75,reveal),mix(-2.44,-.8,reveal));
      cards.forEach((card,i)=>{
        card.scale.setScalar(.96+.04*reveal);
        card.position.set((i-1)*reveal*.32,(i-1)*reveal*.38,(1-i)*reveal*2);
        card.rotation.y=(i-1)*reveal*.08;
      });
      chip.position.set((portrait?-2.35:-3.6)*reveal,-1.5*reveal,2.6*reveal);chip.rotation.set(.05,.2*reveal,-.08);
      chip.scale.setScalar(reveal*(portrait?.68:1));
    }
    rim.intensity=2.7+.35*Math.sin(p*Math.PI*4);
    fill.intensity=1.8+.3*Math.sin(p*Math.PI*2);
    renderer.render(scene,camera);
  }
  function reduceQuality(){renderer.setPixelRatio(1);renderer.setSize(width,height,false);}
  function dispose(){disposed=true;for(const t of textures)t.dispose();for(const m of materials)m.dispose();for(const g of geometries)g.dispose();environment.dispose();renderer.dispose();}
  return {render,dispose,reduceQuality};
}
