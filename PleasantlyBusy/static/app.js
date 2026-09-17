"use strict";

const form = document.querySelector("#settings");
const fields = document.querySelector("#mix-fields");
const player = document.querySelector("#player");
const status = document.querySelector("#status");
const download = document.querySelector("#download");
const generate = document.querySelector("#generate");
const presets = [...document.querySelectorAll(".preset")];
let currentUrl = null;
let renderedSettings = null;

function settings() {
  return Object.fromEntries([...new FormData(form)].map(([key, value]) => [key, Number(value)]));
}

function updateLabels() {
  form.querySelectorAll('input[type="range"]').forEach((input) => {
    const value = input.name === "tempo" ? `${input.value} BPM` : `${Math.round(input.value * 100)}%`;
    document.getElementById(`${input.name}-value`).textContent = value;
    input.setAttribute("aria-valuetext", value);
  });
  presets.forEach((button) => button.setAttribute("aria-pressed",
    String(Number(button.dataset.complexity) === Number(form.elements.complexity.value))));
}

function markChanged() {
  updateLabels();
  status.classList.remove("error");
  if (renderedSettings) {
    status.textContent = "Settings changed. Generate again to update your sound; the player and download still use your previous mix.";
  }
}

form.addEventListener("input", markChanged);
presets.forEach((button) => button.addEventListener("click", () => {
  form.elements.complexity.value = button.dataset.complexity;
  markChanged();
}));
document.querySelector("#shuffle").addEventListener("click", () => {
  const seed = new Uint32Array(1);
  crypto.getRandomValues(seed);
  form.elements.seed.value = seed[0];
  markChanged();
});

const volume = document.querySelector("#listening-volume");
function updateVolume() {
  player.muted = false;
  player.volume = Number(volume.value);
  const label = `${Math.round(Number(volume.value) * 100)}%`;
  document.querySelector("#listening-volume-value").textContent = label;
  volume.setAttribute("aria-valuetext", label);
}
volume.addEventListener("input", updateVolume);
player.addEventListener("volumechange", () => {
  volume.value = player.muted ? 0 : player.volume;
  const label = `${Math.round(Number(volume.value) * 100)}%`;
  document.querySelector("#listening-volume-value").textContent = label;
  volume.setAttribute("aria-valuetext", label);
});
document.querySelector("#repeat").addEventListener("change", (event) => {
  player.loop = event.target.checked;
});

form.addEventListener("submit", async (event) => {
  event.preventDefault();
  if (!form.reportValidity()) return;
  const chosen = settings();
  fields.disabled = true;
  presets.forEach((button) => { button.disabled = true; });
  generate.textContent = "Making your groove…";
  status.classList.remove("error");
  status.textContent = `Generating ${chosen.duration} seconds of sound…`;
  try {
    const response = await fetch("/api/render", {
      method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(chosen),
    });
    if (!response.ok) {
      const error = await response.json();
      throw new Error(error.error || "The render could not be completed.");
    }
    const blob = await response.blob();
    const nextUrl = URL.createObjectURL(blob);
    player.pause();
    player.src = nextUrl;
    player.load();
    download.href = nextUrl;
    download.download = `pleasantly-busy-${chosen.seed}-${Math.round(chosen.complexity * 100)}.wav`;
    download.classList.remove("disabled");
    download.removeAttribute("aria-disabled");
    download.removeAttribute("tabindex");
    if (currentUrl) URL.revokeObjectURL(currentUrl);
    currentUrl = nextUrl;
    renderedSettings = chosen;
    const silent = ["bass", "percussion", "melody", "texture"].every((key) => chosen[key] === 0);
    status.textContent = silent
      ? "All layers are at zero. This mix is silent; raise a layer and generate again to hear it."
      : `${chosen.duration}s · ${chosen.tempo} BPM · ${Math.round(chosen.complexity * 100)}% busyness · seed ${chosen.seed}. Ready when you are—press play.`;
  } catch (error) {
    status.classList.add("error");
    status.textContent = `${error.message} ${renderedSettings ? "Your previous mix is still available." : "Check that the Python studio is running, then try again."}`;
  } finally {
    fields.disabled = false;
    presets.forEach((button) => { button.disabled = false; });
    generate.textContent = "Generate soundscape ↗";
  }
});

updateLabels();
updateVolume();
