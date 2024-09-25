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
  const addFruit = instance.exports.addFruit;
  const previewFruit = instance.exports.previewFruit;
  const worldSizeX = instance.exports.getWorldSizeX();
  const worldSizeY = instance.exports.getWorldSizeY();
  const scale = 720/Math.max(worldSizeX, worldSizeY);
  const getNumFruits = instance.exports.getNumFruits;
  const newSeed = () => Math.random()*Number.MAX_SAFE_INTEGER|0;

  const genNext = (x = 0, y = 0) => ({
    x: x,
    y: y,
    size: Math.random()*5|0,
    seed: 0,
  });
  let nextToAdd = genNext();

  function drawFruits(addr) {
    const numFloatsPerFruit = 11;
    const numFloats = getNumFruits() * numFloatsPerFruit;
    const previewFruitAddress = previewFruit(nextToAdd.x, 0, nextToAdd.size, nextToAdd.seed);
    const addressedBytes = previewFruitAddress
        ? previewFruitAddress + (numFloatsPerFruit << 2) - addr
        : numFloats * 4;
    let dv = new DataView(memory.buffer, addr, addressedBytes);
    let floats = new Float32Array(memory.buffer, addr, addressedBytes >> 2);
    ctx.clearRect(0, 0, ctx.canvas.width, ctx.canvas.height);
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    const drawFruitAtOffset = offset => {
      let sizeIndex = dv.getUint32(offset*4 + 7*4, true);
      let rotation = dv.getUint32(offset*4 + 6*4, true) & 65535;
      ctx.strokeStyle = `hsl(${sizeIndex*30}deg 100% 50%)`;
      ctx.fillStyle = `hsl(${sizeIndex*30}deg 100% 50%)`;
      const x = floats[offset]*scale;
      const y = floats[offset + 1]*scale;
      const dx = x-floats[offset + 2]*scale;
      const dy = y-floats[offset + 3]*scale;
      const rsx = floats[offset + 8]*scale;
      const rsy = floats[offset + 9]*scale;
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
    };
    for (let offset = 0; offset < numFloats; offset += numFloatsPerFruit) {
      drawFruitAtOffset(offset);
    }
    if (previewFruitAddress >= addr) {
      drawFruitAtOffset((previewFruitAddress - addr) >> 2);
    }
  }

  let frame;
  frame = () => {
    drawFruits(simulate(newSeed()));
    requestAnimationFrame(frame);
  };

  drawFruits(init(newSeed()));
  console.log(new Float32Array(memory.buffer, instance.exports.radii.value, 11));
  frame();
  
  canvas.addEventListener("click", e => {
    let x = e.clientX / scale
    let y = e.clientY / scale;
    addFruit(x, 0, nextToAdd.size, nextToAdd.seed);
    nextToAdd = genNext(x, y);
  });
  canvas.addEventListener("pointermove", e => {
    if (!e.isPrimary) return;
    let x = e.clientX / scale
    let y = e.clientY / scale;
    nextToAdd.x = x;
    nextToAdd.y = y;
  });

  return instance;
}

startSimulation();
