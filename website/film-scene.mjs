// Original, illustrative device geometry; no Apple models or product imagery.
import * as T from './vendor/three.module.min.js';
import { RoundedBoxGeometry } from './vendor/RoundedBoxGeometry.js';
import { RoomEnvironment } from './vendor/RoomEnvironment.js';

const clamp = n => Math.max(0, Math.min(1, n));
const smooth = n => { n = clamp(n); return n * n * (3 - 2 * n); };
const mix = (a, b, t) => a + (b - a) * t;
export async function createFilmScene(canvas) {
  const renderer = new T.WebGLRenderer({ canvas, antialias: true, alpha: false, powerPreference: 'low-power' });
  renderer.setPixelRatio(Math.min(devicePixelRatio, 1.7));
  renderer.setClearColor(0x050609);
  renderer.outputColorSpace = T.SRGBColorSpace;
  renderer.toneMapping = T.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.1;
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
  const glass = metal(0x080b10, .12, .5), mint = metal(0x7fdbbe, .22, .7);
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
  for (const [i, map] of [desktop, editor, terminal].entries()) {
    const card = new T.Group(); layers.add(card); cards.push(card);
    rounded(card, 6.42, 3.67, .09, .038, i === 2 ? mint : edge);
    rounded(card, 6.38, 3.63, .095, .038, graphite);
    image(card, map, 6.27, 3.527, 0, 0, .052);
  }
  const chip = new T.Group(); layers.add(chip);
  rounded(chip, 1.85, 1.85, .18, .08, edge);
  rounded(chip, 1.77, 1.77, .185, .07, black);
  const chipLabel = image(chip, label('mnOS'), 1.35, .45, 0, .12, .099); chipLabel.material.transparent = true;
  const chipSub = image(chip, label('NATIVE', '#95b7b2', 65), 1.1, .367, 0, -.32, .1); chipSub.material.transparent = true;
  for (let i = 0; i < 14; i++) for (const side of [-1, 1]) {
    rounded(chip, .06, .10, .055, .008, edge, -.78 + i * .12, side * .93, 0);
    rounded(chip, .10, .06, .055, .008, edge, side * .93, -.78 + i * .12, 0);
  }
  // Soft contact shadow, generated only once; all motion uses a real camera.
  const shadowCanvas = document.createElement('canvas'); shadowCanvas.width = shadowCanvas.height = 128;
  const g = shadowCanvas.getContext('2d'); const gradient = g.createRadialGradient(64,64,5,64,64,64);
  gradient.addColorStop(0,'rgba(0,0,0,.85)'); gradient.addColorStop(1,'rgba(0,0,0,0)'); g.fillStyle=gradient;g.fillRect(0,0,128,128);
  const shadowMap = new T.CanvasTexture(shadowCanvas); textures.push(shadowMap);
  const shadow = image(scene, shadowMap, 13, 8, 0, -2.28, 0); shadow.material.transparent = true; shadow.rotation.x = -Math.PI / 2;
  const shots = [
    {p:0, camera:[9,1.1,9], target:[0,-.5,0], turn:-.48, tilt:-.07, lid:1.49},
    {p:.125, camera:[8,2.3,9.8], target:[0,.1,0], turn:-.25, tilt:-.06, lid:1.15},
    {p:.25, camera:[5.7,3.8,10.2], target:[0,.8,0], turn:-.17, tilt:0, lid:.1},
    {p:.375, camera:[.2,2.4,10], target:[0,1.05,0], turn:0, tilt:0, lid:-.12},
    {p:.49, camera:[0,2.28,3.9], target:[0,1.35,-1.6], turn:0, tilt:0, lid:-.12},
    {p:.50, camera:[0,2.28,3.9], target:[0,1.35,-1.6], turn:0, tilt:0, lid:-.12},
    {p:.625, camera:[7.4,4.5,12.5], target:[0,.2,0], turn:0, tilt:0, lid:-.12},
    {p:.75, camera:[-5.8,3,13.3], target:[0,.3,0], turn:0, tilt:0, lid:-.12},
    {p:.875, camera:[-4,2.5,12], target:[0,.5,0], turn:.25, tilt:.03, lid:-.12},
    {p:1, camera:[4.4,2.3,11.2], target:[0,.65,0], turn:-.2, tilt:.02, lid:-.12},
  ];
  const target = new T.Vector3();
  function render(progress) {
    if (disposed) return;
    const p = clamp(progress), w = canvas.clientWidth, h = canvas.clientHeight;
    if (!w || !h) return;
    if (w !== width || h !== height) { width=w;height=h;renderer.setSize(w,h,false);camera.aspect=w/h;camera.updateProjectionMatrix(); }
    let a=shots[0],b=shots[1];
    for(let i=1;i<shots.length;i++)if(p<=shots[i].p){a=shots[i-1];b=shots[i];break;}
    const t=smooth((p-a.p)/(b.p-a.p));
    const portrait=w/h<.9;
    for(let i=0;i<3;i++){camera.position.setComponent(i,mix(a.camera[i],b.camera[i],t));target.setComponent(i,mix(a.target[i],b.target[i],t));}
    if(portrait) {camera.position.sub(target).multiplyScalar(1.1/(w/h)).add(target);target.y+=.35;}
    camera.lookAt(target);
    device.rotation.set(0,mix(a.turn,b.turn,t),mix(a.tilt,b.tilt,t));
    hinge.rotation.x=mix(a.lid,b.lid,t);
    device.visible=p<.5||p>=.86;
    layers.visible=!device.visible;
    screen.material.color.setScalar(.68+.32*smooth((p-.08)/.2));
    if(layers.visible){
      const spread=smooth((p-.5)/.12);
      layers.rotation.set(-.1, -.18 + smooth((p-.62)/.2)*.25, -.035);
      layers.position.set(0, mix(1.45,.45,spread), mix(-2.44,-1.3,spread));
      cards.forEach((card,i)=>{card.visible=i===0||spread>.025;card.position.set((i-1)*spread*.25,(i-1)*spread*.55,(1-i)*spread*2.3);card.rotation.y=(i-1)*spread*.075;});
      chip.position.set(-3.7*spread,-1.6*spread,3.8*spread);chip.rotation.set(.05,.2*spread,-.08);
      cards[2].position.x+=smooth((p-.68)/.12)*2.1;
      cards[2].position.y+=smooth((p-.68)/.12)*.35;
      cards[2].position.z+=smooth((p-.68)/.12)*6;
    }
    rim.intensity=2.5+Math.pow(1-((p*32)%1),8)*1.1;
    renderer.render(scene,camera);
  }
  function dispose(){disposed=true;for(const t of textures)t.dispose();for(const m of materials)m.dispose();for(const g of geometries)g.dispose();environment.dispose();renderer.dispose();}
  return {render,dispose};
}
