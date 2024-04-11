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
  const worldSize = instance.exports.getWorldSize();
  const getNumFruits = instance.exports.getNumFruits;

  function drawFruits(addr) {
    const numFloats = getNumFruits() * 6;
    let floats = new Float32Array(memory.buffer, addr, numFloats);
    ctx.strokeStyle = "white";
    ctx.clearRect(0, 0, ctx.canvas.width, ctx.canvas.height);
    const scale = 720/worldSize;
    for (let offset = 0; offset < numFloats; offset += 6) {
      ctx.beginPath();
      ctx.arc(floats[offset]*scale, floats[offset + 1]*scale, floats[offset + 4]*scale*0.85, 0, 2 * Math.PI);
      ctx.stroke();
    }
  }

  let frame;
  frame = () => {
    drawFruits(simulate(Math.random()*Number.MAX_SAFE_INTEGER|0));
    requestAnimationFrame(frame);
  };

  drawFruits(init(Math.random()*Number.MAX_SAFE_INTEGER|0));
  frame();

}

startSimulation();
