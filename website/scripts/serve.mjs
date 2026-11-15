import {createServer} from 'node:http';
import {readFile} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root=path.resolve(process.env.MNOS_SITE_DIR||fileURLToPath(new URL('../',import.meta.url)))+path.sep;
const types={'.html':'text/html; charset=utf-8','.css':'text/css; charset=utf-8','.js':'text/javascript; charset=utf-8','.mjs':'text/javascript; charset=utf-8','.json':'application/json','.svg':'image/svg+xml','.png':'image/png','.webm':'video/webm','.mp4':'video/mp4'};
const server=createServer(async(request,response)=>{
 try{
  const pathname=decodeURIComponent(new URL(request.url,'http://localhost').pathname);
  const file=path.resolve(root,`.${pathname.endsWith('/')?`${pathname}index.html`:pathname}`);
  if(!file.startsWith(root))throw new Error('Invalid path');
  const content=await readFile(file);
  const headers={'Content-Type':types[path.extname(file)]||'application/octet-stream','Cache-Control':'no-store','Accept-Ranges':'bytes'};
  if(request.headers.range){
   const match=/^bytes=(\d+)-(\d*)$/.exec(request.headers.range);
   const start=match?Number(match[1]):NaN,end=match&&match[2]?Number(match[2]):content.length-1;
   if(!Number.isSafeInteger(start)||!Number.isSafeInteger(end)||start<0||start>end||end>=content.length){response.writeHead(416,{'Content-Range':`bytes */${content.length}`});response.end();return;}
   response.writeHead(206,{...headers,'Content-Range':`bytes ${start}-${end}/${content.length}`,'Content-Length':end-start+1});response.end(request.method==='HEAD'?undefined:content.subarray(start,end+1));return;
  }
  response.writeHead(200,{...headers,'Content-Length':content.length});response.end(request.method==='HEAD'?undefined:content);
 }catch{response.writeHead(404);response.end('Not found');}
});
server.listen(4173,'127.0.0.1',()=>console.log('mnOS website: http://127.0.0.1:4173'));
