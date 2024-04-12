const thisScript = import.meta.url;

const canvas = document.querySelector("canvas");
const ctx = canvas.getContext("2d");

function resolveScript(uri) {
  if (!/^\.{0,2}\//.test(uri)) {
    uri = "./"+uri;
  }
  return new URL(uri, thisScript).toString();
}

async function startSimulation() {
  // Load the wasm module
  const response = await fetch(resolveScript("../dinnye.wasm"));
  let memory;
  const instanceAndModule = await WebAssembly.instantiateStreaming(response, { env: {
    setMemorySize(bytes) {
      if (bytes) {
        let delta = bytes - memory.buffer.byteLength;
        if (delta > 0) {
          const pages = (delta + 0xFFFF) >> 16;
          console.log("Growing by ", pages*64);
          memory.grow(pages);
        }
        console.log("kbytesRequired: ", bytes+1023 >> 10, " overall available: ", memory.buffer.byteLength+1023 >> 10);
      }
      return memory.buffer.byteLength;
    },
    dumpInt(val) {
      console.log(val);
    },
    dumpQuadInt(a, b, c, d) {
      console.log(a, b, c, d);
    },
  }});
  const instance = instanceAndModule.instance;
  memory = instance.exports.memory;
  const init = instance.exports.init;
  const simulate = instance.exports.simulate;
  const worldSizeX = instance.exports.getWorldSizeX();
  const worldSizeY = instance.exports.getWorldSizeY();
  const getNumFruits = instance.exports.getNumFruits;

  function drawFruits(addr) {
    const numFloats = getNumFruits() * 8;
    let dv = new DataView(memory.buffer, addr, numFloats * 4);
    let floats = new Float32Array(memory.buffer, addr, numFloats);
    ctx.clearRect(0, 0, ctx.canvas.width, ctx.canvas.height);
    const scale = 720/Math.max(worldSizeX, worldSizeY);
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    for (let offset = 0; offset < numFloats; offset += 8) {
      let sizeIndex = dv.getUint32(offset*4 + 7*4, true);
      let rotation = dv.getUint32(offset*4 + 6*4, true) & 65535;
      ctx.strokeStyle = `hsl(${sizeIndex*30}deg 100% 50%)`;
      ctx.fillStyle = `hsl(${sizeIndex*30}deg 100% 50%)`;
      const x = floats[offset]*scale;
      const y = floats[offset + 1]*scale;
      const radius = floats[offset + 4]*scale;
      const fontSize = sizeIndex > 1 ? sizeIndex * 2 + 10 : 0;
      ctx.save();
      ctx.translate(x, y);
      ctx.rotate(rotation / 32768 * Math.PI);
      if (fontSize) {
        let s = (sizeIndex+1).toString();
        ctx.font = `${fontSize}px sans-serif`;
        ctx.fillText(s, 0, 0);
      }
      ctx.beginPath();
      ctx.arc(0, 0, radius, 0, 2 * Math.PI);
      ctx.moveTo(0, -fontSize * 0.5);
      ctx.lineTo(0, -radius);
      ctx.stroke();
      ctx.restore();
    }
  }

  let frame;
  frame = () => {
    drawFruits(simulate(Math.random()*Number.MAX_SAFE_INTEGER|0));
    requestAnimationFrame(frame);
  };

  drawFruits(init(Math.random()*Number.MAX_SAFE_INTEGER|0));
  console.log(new Float32Array(memory.buffer, instance.exports.radii.value, 11));
  frame();
  return instance;
}

startSimulation();
