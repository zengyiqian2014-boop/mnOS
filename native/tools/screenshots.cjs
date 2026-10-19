// SPDX-License-Identifier: GPL-3.0-or-later
// Lossless QEMU P6 capture to PNG; build-time Node only, never shipped as runtime.
const fs=require('node:fs'), path=require('node:path'), zlib=require('node:zlib');
const dir=process.argv[2];if(!dir)throw Error('Usage: node screenshots.cjs DIRECTORY');
function crc(bytes){let c=0xffffffff;for(const x of bytes){c^=x;for(let i=0;i<8;i++)c=(c>>>1)^((c&1)?0xedb88320:0);}return (c^0xffffffff)>>>0;}
function chunk(name,body){const n=Buffer.from(name),len=Buffer.alloc(4),sum=Buffer.alloc(4);len.writeUInt32BE(body.length);sum.writeUInt32BE(crc(Buffer.concat([n,body])));return Buffer.concat([len,n,body,sum]);}
for(const name of fs.readdirSync(dir).filter(x=>/^(amd64|arm64)-.*\.ppm$/.test(x))){
 const p=path.join(dir,name),b=fs.readFileSync(p);let at=0;
 const token=()=>{while(b[at]===32||b[at]===10||b[at]===13)at++;if(b[at]===35){while(b[at]!==10&&at<b.length)at++;return token();}const start=at;while(at<b.length&&b[at]!==32&&b[at]!==10&&b[at]!==13)at++;return b.subarray(start,at).toString('ascii');};
 if(token()!=='P6')throw Error('Not P6');const w=Number(token()),h=Number(token());if(token()!=='255'||w<1||w>8192||h<1||h>8192)throw Error('Invalid image');at++;
 if(b.length-at!==w*h*3)throw Error('Invalid pixel extent');const rows=Buffer.alloc((w*3+1)*h);
 for(let y=0;y<h;y++)b.copy(rows,y*(w*3+1)+1,at+y*w*3,at+(y+1)*w*3);
 const header=Buffer.alloc(13);header.writeUInt32BE(w,0);header.writeUInt32BE(h,4);header[8]=8;header[9]=2;
 fs.writeFileSync(p.replace(/\.ppm$/,'.png'),Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),chunk('IHDR',header),chunk('IDAT',zlib.deflateSync(rows,{level:9})),chunk('IEND',Buffer.alloc(0))]));
}
