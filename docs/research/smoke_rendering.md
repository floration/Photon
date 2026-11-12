# Real-time smoke rendering for Photon

Research note, November 2026. Scope: physically driven rendering of the solver's soot, pyrolysis aerosol, temperature and heat-release fields in raw C++20 / OpenGL 3.3, RTX 3090 Ti target with a 2015-GPU floor (GTX 960 / R9 380 class: no compute shaders, no hardware ray tracing).

Ground rule for everything below: **the solver fields define the optical coefficients, and the coefficients define color and opacity**. Color is never painted on. The renderer may add sub-grid *structure*, but only in ways that keep the cell-mean mass and the cell-mean optical depth what the solver says.

---

## 1. Participating-media rendering basics

### 1.1 Radiative transfer along a view ray

For each wavelength (in practice each RGB channel) the radiance reaching the eye is ([PBRT v4, ch. 11 and 14](https://pbr-book.org/4ed/Volume_Scattering); [Fong et al., *Production Volume Rendering*, SIGGRAPH 2017](https://graphics.pixar.com/library/ProductionVolumeRendering/paper.pdf)):

```
L(x_0) = T(0,D) L_surface + ∫_0^D T(0,t) [ σ_a(t) L_e(t) + σ_s(t) L_in(t) ] dt
T(a,b) = exp( -∫_a^b σ_t(s) ds ),   σ_t = σ_a + σ_s,   ω = σ_s / σ_t   (single-scattering albedo)
L_in(x, ω_o) = ∫ p(ω_o, ω_i) L(x, ω_i) dω_i
```

With mass densities from the solver (ρ_soot, ρ_aero in kg/m³) and mass-specific coefficients k (m²/kg):

```
σ_t(λ) = ρ_soot k_soot(λ) + ρ_aero k_aero(λ)
σ_s(λ) = ρ_soot k_soot(λ) ω_soot(λ) + ρ_aero k_aero(λ) ω_aero(λ)
```

This one change is the heart of the plan: the *mixture* albedo and the *mixture* spectral extinction then fall out of the two densities, and white/grey/brown/black appear without a hand-tuned color blend.

### 1.2 Energy-conserving step integration

The naive step `col += tr * a * scat` is biased for thick steps. Hillaire's analytic per-step integral assumes constant coefficients within a step ([Hillaire, *Physically Based and Unified Volumetric Rendering in Frostbite*, SIGGRAPH 2015](https://www.ea.com/news/physically-based-unified-volumetric-rendering-in-frostbite), [slides](https://www.slideshare.net/DICEStudio/physically-based-and-unified-volumetric-rendering-in-frostbite)):

```
S      = σ_s · L_in + σ_a · L_e            (source term, per channel)
Sint   = (S - S · exp(-σ_t Δ)) / σ_t        (integrated over the step)
col   += tr · Sint;   tr *= exp(-σ_t Δ)
```

Dense soot reaches σ_t of 10–80 m⁻¹ (ρ_soot 1–10 g/m³), so a 10 cm step has an optical depth of 1–8. At that depth the naive form is visibly wrong, and this fix costs nothing.

### 1.3 Phase functions

Henyey–Greenstein: `p(θ) = (1 - g²) / (4π (1 + g² - 2g cosθ)^{3/2})`. A two-lobe blend `p = w·HG(g1) + (1-w)·HG(g2)` with g2 < 0 gives the backscatter (silver-lining) lobe, as used for clouds by [Schneider & Vos (Horizon Zero Dawn, 2015)](https://www.advances.realtimerendering.com/s2015/The%20Real-time%20Volumetric%20Cloudscapes%20of%20Horizon%20-%20Zero%20Dawn%20-%20ARTR.pdf) and Frostbite ([Hillaire 2016](https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/s2016_pbs_frostbite_sky_clouds.pdf)).

Suggested values (estimates, see section 2 for the physics):

| Medium | Basis | g (visible) | Back lobe |
|---|---|---|---|
| Flaming soot (fractal aggregates, ~30–50 nm primaries, aggregates 0.1–1 µm) | [Köylü & Faeth 1994](https://deepblue.lib.umich.edu/bitstream/2027.42/30068/1/0000438.pdf): forward scattering ~100× wide-angle, RDG regime | 0.5–0.7 | none needed (low albedo) |
| Pyrolysis / smoldering aerosol (liquid tar droplets, 0.1–1 µm, Mie regime) | Mie theory for sub-micron, weakly absorbing spheres | 0.6–0.75 | w≈0.85, g2≈-0.2 |

Start with g_soot = 0.6 and g_aero = 0.7, mixing the phase functions weighted by σ_s of each species (not by mass). Because soot albedo is low, its g matters far less than aerosol g.

### 1.4 Multiple scattering in real time

Multiple scattering matters only for high-albedo smoke (light pyrolysis smoke, ω ≈ 0.8–0.95). For black soot (ω ≈ 0.2) single scattering is close to the whole story. Usable approximations:

- **Octave approximation** ([Wrenninge, Kulla & Lundqvist, *Oz: The Great and Volumetric*, 2013](https://fpsunflower.github.io/ckulla/data/oz_volumes.pdf), also in Hillaire 2016): `L_ms = Σ_{i=0}^{N-1} b^i · σ_s · T(σ_t · a^i) · p(θ, g · c^i)` with a, b, c ≈ 0.5 and N = 2–3. It costs one extra exp per octave on the shadow optical depth you already have. Scale b by the local albedo (b = 0.5·ω) so black smoke receives almost no extra light. That keeps the approximation tied to the physics.
- **Ambient/room light via optical-depth cones.** Build a mip chain of the extinction volume and take 3–6 cone taps upward and sideways to estimate how much "room light" reaches each sample. This is a volumetric ambient-occlusion term; see the multiple-scattering LUT idea in [Hillaire, *A Scalable and Production Ready Sky and Atmosphere*, EGSR 2020](https://sebh.github.io/publications/egsr2020.pdf).

---

## 2. Optical properties of fire smoke

### 2.1 Mass extinction

- **Flaming, well-ventilated soot:** σ_m = 8.7 ± 1.1 m²/g, nearly universal across fuels and fire sizes ([Mulholland & Johnsson, NIST, 1999](https://firedoc.nist.gov/article/JHcyXYQBWEcjUZEY8eM6); Mulholland & Croarkin, *Specific Extinction Coefficient of Flame Generated Smoke*, Fire and Materials 24(5):227–230, 2000). Mulholland & Choi give 8.3 ± 1.0 m²/g at 632.8 nm. FDS and CFAST use 8700 m²/kg, which Photon already uses. This is a **632.8 nm (red He-Ne)** value.
- **Smoldering / pyrolysis smoke:** roughly half the flaming value. Seader & Einhorn (1977), as summarized in [Mulholland, *Smoke Production and Properties*, SFPE Handbook](https://www.globalspec.com/reference/76873/203279/section-two-fire-dynamics), give about 4.4 m²/g for smoldering against about 7.6 m²/g for flaming. [Patterson et al. 1986](https://www.frames.gov/documents/smoke/serdp/patterson_et_al_1986.pdf) also find that smoldering values are about a factor of two below flaming ones. **Recommendation:** replace the prototype 3000 m²/kg aerosol coefficient with 4400 m²/kg, and flag it as fuel-dependent. *(Check the 4.4 figure against the handbook table before treating it as final.)*
- **Underventilated (post-flashover) smoke** has a lower σ_m per gram of total aerosol, because it carries more organic, non-absorbing material ([Widmann et al., Combust. Flame 134:119, 2003](https://www.nist.gov/node/588811): extinction decreases as the equivalence ratio rises from 1 to 3). Photon's two-species split models this automatically: more aerosol means lower specific extinction and higher albedo.

### 2.2 Wavelength dependence

For soot in the small-particle regime, the dimensionless extinction constant K_e links σ_m to wavelength:

```
σ_m(λ) = K_e / (ρ_soot,material · λ),   ρ ≈ 1.8 g/cm³,   K_e ≈ 8–10 (visible / near-IR)
```

K_e is measured by Dobbins, Mulholland & Bryner (1994) and by Widmann and Zhu/Choi/Mulholland at NIST ([Widmann et al. 2003 instrument paper and IR study](https://www.nist.gov/node/595006)). So **soot extinction scales roughly as 1/λ**. Normalizing to 8700 m²/kg at 633 nm gives about **9500 (R, 610 nm), 10500 (G, 550 nm), 12500 (B, 465 nm) m²/kg**. Light transmitted through soot therefore reddens, which is why fires and sun look orange through smoke and light sources dim toward red-brown. The pyrolysis aerosol is weakly wavelength-dependent in scattering. Its organic ("brown carbon") fraction absorbs mostly in the blue and UV, which gives yellow-brown tones ([ACP 2016 smoke study](https://acp.copernicus.org/articles/16/13491/2016/)).

### 2.3 Single-scattering albedo

| Species | ω (visible) | Source |
|---|---|---|
| Flaming soot | 0.18–0.30 (scattering/absorption ratio 0.22–0.41 at 514.5 nm) | [Köylü & Faeth 1994](https://deepblue.lib.umich.edu/bitstream/2027.42/30068/1/0000438.pdf); [Krishnan, Lin & Faeth 2000](https://firedoc.nist.gov/article/kncyXYQBWEcjUZEY8-Sj) |
| Fresh soot, NIST lab | 0.5–0.6 at 405 nm (σ_m 12–16 m²/g) | [Radney et al. 2014](https://www.nist.gov/node/1195326) |
| Smoldering / pyrolysis aerosol | 0.85–0.95+ (fuel-dependent; biomass ranges 0.37–0.95 at 405 nm) | [NOAA smoke study](https://repository.library.noaa.gov/view/noaa/51342), [ACP 2016](https://acp.copernicus.org/articles/16/13491/2016/) |

Suggested per-channel start values: ω_soot = (0.25, 0.22, 0.20) RGB; ω_aero = (0.95, 0.93, 0.85) RGB. The lower blue albedo of the aerosol is the brown-carbon absorption, and it is the physical source of yellowish and brown smoke.

### 2.4 Composition and the colors firefighters read

The appearance follows directly from the soot-to-aerosol extinction ratio r = σ_t,soot / σ_t,total:

- **White** (r < 0.1): early pyrolysis or smoldering. High ω, so the smoke scatters most of the light and looks bright and milky. Steam also looks white, but Photon does not model water vapour separately.
- **Grey** (r ≈ 0.2–0.5): mixed flaming and pyrolysis.
- **Brown / yellow-brown**: high aerosol fraction with blue-absorbing organics (ω_B < ω_R), plus 1/λ reddening of light that has passed through some soot. This is the "unburned fuel, heavy pyrolysis" warning sign.
- **Black** (r > 0.7): soot-rich, underventilated, well-developed fire. Low ω, so the smoke absorbs and stays dark even when lit.

Photon's current `lightFrac = aero / (3 soot + aero)` with fixed albedo colors is a heuristic version of this. Replacing it with the per-species σ/ω of section 1.1 makes the color a computed result rather than a tuned one.

**Validation hook:** FDS defines visibility as S = C/K, with C = 3 for reflecting objects and 8 for light-emitting signs ([FDS manuals](https://pages.nist.gov/fds-smv/manuals.html)). A test scene with a lit exit sign and a reflecting wall at known K should lose contrast at about those distances under correct exposure.

---

## 3. Real-time techniques used in production

- **Froxels** ([Wronski, SIGGRAPH 2014](https://www.advances.realtimerendering.com/s2014/wronski/bwronski_volumetric_fog_siggraph2014.pdf); [Hillaire 2015](https://www.ea.com/news/physically-based-unified-volumetric-rendering-in-frostbite); [UE Volumetric Fog](https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-fog-in-unreal-engine)): a camera-aligned 3D grid (e.g. 160×90×64) is filled with σ and lighting, then integrated front to back once. Lights are evaluated once per froxel, not once per ray sample. It is a good fit when many lights cover large thin media. For Photon's bounded house, direct marching of the solver grid is simpler. Froxels become worthwhile once torches, fire clusters and transparent particles must all interact.
- **Precomputed light/shadow volumes:** store per-voxel transmittance to each light (or summed irradiance) in a 3D texture, so the march does one tap instead of an inner shadow loop. NVIDIA Flow does self-shadowing on its sparse grid ([NVIDIA Flow docs](https://docs.nvidia.com/gameworks/content/artisttools/flow.htm)). [GVDB](https://github.com/NVIDIA/gvdb-voxels) ([Hoetzlein, HPG 2016](https://diglib.eg.org/handle/10.2312/hpg20161197)) shows sparse-brick traversal, which Photon does not need at 230k cells. [EmberGen](https://cgpress.org/archives/embergen-real-time-volumetric-fluid-simulation.html) and [UE Heterogeneous Volumes](https://dev.epicgames.com/documentation/en-us/unreal-engine/heterogeneous-volumes-in-unreal-engine) both march dense or sparse grids with precomputed shadow volumes.
- **Horizon Zero Dawn / Nubis** ([2015](https://www.advances.realtimerendering.com/s2015/The%20Real-time%20Volumetric%20Cloudscapes%20of%20Horizon%20-%20Zero%20Dawn%20-%20ARTR.pdf), [Nubis Cubed 2023](https://advances.realtimerendering.com/s2023/Nubis%20Cubed%20(Advances%202023).pdf)): render at quarter resolution with temporal reprojection (one pixel in 4×4 updated per frame), use the octave multiple-scattering approximation, and use adaptive step size (large steps in empty space, fine steps in cloud).
- **Blue-noise jitter + temporal accumulation:** replace `hash(gl_FragCoord)` with [spatiotemporal blue noise](https://research.nvidia.com/publication/2022-07_spatiotemporal-blue-noise-masks) (free masks in NVIDIA's STBN repository) for the ray-start offset. This allows 2–4× fewer steps with the residual noise removed by a history buffer that is reprojected with the previous view-projection matrix and neighbourhood-clamped. All of this is GL 3.3 (FBO ping-pong).
- **Half-resolution march + depth-aware upsample:** march at ½ or ¼ resolution into RGBA16F (in-scatter, transmittance) and composite with a bilateral upsample keyed on scene depth. This gives about a 4× saving and is the main lever for 2015 GPUs.

### 3.1 Detail beyond the 20 cm grid without faking density

Options, best first for Photon:

1. **Curl-noise domain displacement** ([Bridson, Hourihan & Nordenstam 2007](https://www.dneg.com/wp-content/uploads/2022/11/2007.pdf)): sample the *solver* texture at `p + A·curlNoise(p, t)` instead of multiplying σ by noise. Curl fields are divergence-free, so the displacement moves smoke around without creating or destroying mass in the continuum limit. This is the cleanest way to honour "density from physics". Keep |A| ≲ 0.5–1 cell.
2. **Flow-advected noise:** translate the noise domain by the resolved velocity, using `p - u(p)·φ` with two phases offset by half a period and cross-faded (the "flow map" / advected-texture technique). The billows then move *with* the computed flow rather than a fixed upward drift. This needs the velocity field.
3. **Kolmogorov-scaled amplitude, as in wavelet turbulence** ([Kim, Thürey, James & Gross 2008](https://www.cs.cornell.edu/~tedkim/wturb)): scale the detail amplitude by the local sub-grid kinetic energy (k_sgs from the LES model, or |∇×u|·Δ as a proxy). Use per-octave amplitude ∝ 2^(-5/6·i) for an inertial-range spectrum. Quiet stratified layers then stay smooth, and the turbulent plume above the fire gets busy. A full wavelet-turbulence upres on the CPU is possible, but too costly per frame here.

**Problem in the current shader:** `sigma = sigma0 * mix(0.25, 1.75, smokeNoise(p))` keeps mean σ only if E[noise] = 0.5, which smoothstep of summed value noise does not guarantee. Even with mean σ preserved, Jensen's inequality (mean of exp(−σΔ) is greater than exp(−mean σΔ)) makes noisy smoke **more transparent than the solver says**, by up to tens of percent at high optical depth. That understates the danger. Either switch to displacement (option 1), or restrict multiplicative noise to amplitudes ≤ ±20% and fade it out where the cell optical depth σ·0.2 m > 1.

---

## 4. Fire lighting inside smoke

### 4.1 Emission from hot soot

Soot emits as an absorber in local thermodynamic equilibrium: `L_e(λ) = σ_a(λ) · B_λ(T)`, with σ_a = σ_t(1−ω) from section 1.1, so its emissivity follows 1/λ as well. Precompute a 1D LUT (T from 300 to 2000 K, 256 texels, RGB float) by integrating B_λ(T)·λ⁻¹ against the CIE 1931 matching functions and converting to linear sRGB. In the shader, `emit = σ_a,soot_ref · LUT(T)`. This replaces `glowColor()` and makes glow strength scale with soot density, so hot soot-laden layers glow dull red at 800 K and clean hot air does not glow at all. The range runs over about 6 orders of magnitude from 700 to 1500 K, so Photon needs an HDR target (RGBA16F) plus exposure/tonemapping. A 20 cm cell's mean T underestimates peak flame-sheet temperatures, so the flame body itself may still use the HRR-driven model. Keep that labelled as the one display-model component.

### 4.2 Smoke lit by flames

Treat clustered burning cells (HRR > threshold) as point or volume lights with radiant power `P_rad = χ_r · Q̇` (χ_r ≈ 0.3 for sooty fuels) and color from the flame-temperature LUT. Light reaching a sample is `P_rad/(4π r²) · T(sample→light)`. The missing piece in the current shader is the **transmittance to the fire**: smoke between the flame and a sample must shadow it, or the upper layer glows unrealistically. On GL 3.3, compute a *fire-irradiance volume* at grid resolution. Render one fragment pass per z-slice into a 3D RGB16F texture (`glFramebufferTextureLayer`), and in each texel march about 8–16 steps toward each of the top-N fire clusters. Do this once per solver step, not per frame.

### 4.3 Indoor ambient and the firefighter's torch

Indoors there is no sun. Light comes from windows/doors (area lights), the fire, and **the helmet or hand torch**. The torch is the most training-relevant source: a high-albedo smoke layer backscatters it into a white wall that destroys visibility, while black smoke simply swallows it. For a light co-located with the eye, the shadow ray equals the view ray, so single scattering is nearly free:

```
L_torch += ∫ T(0,t)² · σ_s(t) · p(θ≈π) · I(spot cone) / t² dt
```

Use the same running `tr` (squared). Here g matters: forward-peaked aerosol (g = 0.7) backscatters little per unit σ_s, which is the physical reason why a torch beam is visible sideways but glare-y head-on.

---

## 5. Recommended plan for Photon (value per effort)

Cost figures are rough estimates for 1920×1080, about 150 march steps through the house, measured against today's shader. They are not benchmarks.

| # | Step | Shader changes | Solver / CPU data | Cost |
|---|---|---|---|---|
| 1 | **Physical coefficients** | Per-channel `k_soot = (9.5, 10.5, 12.5)e3`, `k_aero = 4.4e3` (verify), ω per species (section 2.3); σ_t, σ_s per RGB channel; per-species phase blend; delete `lightFrac`/albedo-color blend | None (soot, aero already present) | ~0; the biggest gain in correctness |
| 2 | **Energy-conserving integration + HDR** | Hillaire step integral; render volume into RGBA16F, tonemap at end | None | ~0–3% |
| 3 | **Fix detail bias** | Replace multiplicative fbm with curl-noise displacement, or clamp amplitude ≤ 20% and fade in thick cells (section 3.1) | None now | ~same as current noise |
| 4 | **Emission LUT** | `L_e = σ_a · B(T)` via 1D RGB LUT; remove `glowColor` | T already present; CPU builds LUT once | <1% |
| 5 | **Torch light** | Co-located spot term using `tr²` | Torch pose/cone uniforms | ~2–5% |
| 6 | **Light/irradiance volumes** | Replace 4-tap sun loop and the unshadowed fire-light loop with 1–2 taps of precomputed volumes; add octave multiple scattering (N=2, b = 0.5ω) | GPU slice passes per solver step: sun/window transmittance + fire irradiance (top-N HRR clusters from CPU) | Net *saving* per frame (removes 4+N_lights taps per step); ~1–2 ms per solver update on a 2015 GPU |
| 7 | **Blue noise + half-res + temporal reprojection** | STBN offset; ½-res march; bilateral upsample; history buffer with neighbourhood clamp | Previous view-projection matrix | 3–6× faster; enables the 2015 floor |
| 8 | **Flow-tied detail** | Two-phase velocity-advected curl displacement; amplitude ∝ k_sgs^(1/2), octaves ∝ 2^(-5/6 i) | **Velocity field** (second 3D texture RGB16F, ~1.4 MB at 230k cells) and **sub-grid TKE** or vorticity magnitude (1 channel) | +10–20% march cost (2–3 noise evaluations per sample, near-camera only) |
| 9 | **Ambient cones** (optional) | 3–6 mip taps of an extinction mip chain for room/window ambient | `glGenerateMipmap` on an extinction texture | +5–10% |

Practical notes:

- **Texture format.** The volume is `GL_RGBA32F` today. Converting on the CPU to RGBA16F that holds σ_soot and σ_aero in m⁻¹ (not kg/m³: half floats lose precision below about 6e-5) together with T and HRR halves bandwidth and filters on every 2015 GPU.
- **Shadow reach.** The current 4 × 0.35 m sun shadow sees only 1.4 m, so a dense hot layer cannot shade the room beneath it. Step 6 removes that limitation.
- **Quality tiers.** On the 3090 Ti use full resolution, Δ = 5 cm and octave multiple scattering. On 2015 hardware use ½–¼ resolution, Δ = 10–15 cm and temporal accumulation. The coefficients are identical in both tiers, so smoke color and density stay identical across hardware and only noise and sharpness change.
- **Calibration.** Add a regression scene (step 1 deliverable): uniform soot at known ρ, exit sign at distance S = 8/K and wall at 3/K. The rendered contrast should cross the visibility threshold at those distances.

## Sources

- PBRT v4, Volume Scattering: https://pbr-book.org/4ed/Volume_Scattering
- Fong et al., Production Volume Rendering (2017): https://graphics.pixar.com/library/ProductionVolumeRendering/paper.pdf
- Hillaire, Frostbite volumetrics (2015): https://www.ea.com/news/physically-based-unified-volumetric-rendering-in-frostbite and https://www.slideshare.net/DICEStudio/physically-based-and-unified-volumetric-rendering-in-frostbite
- Hillaire, Frostbite sky and clouds (2016): https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/s2016_pbs_frostbite_sky_clouds.pdf
- Hillaire, sky/atmosphere EGSR 2020: https://sebh.github.io/publications/egsr2020.pdf
- Schneider & Vos, Horizon Zero Dawn clouds (2015): https://www.advances.realtimerendering.com/s2015/The%20Real-time%20Volumetric%20Cloudscapes%20of%20Horizon%20-%20Zero%20Dawn%20-%20ARTR.pdf ; Nubis Cubed (2023): https://advances.realtimerendering.com/s2023/Nubis%20Cubed%20(Advances%202023).pdf
- Wronski, Volumetric Fog (2014): https://www.advances.realtimerendering.com/s2014/wronski/bwronski_volumetric_fog_siggraph2014.pdf
- Unreal Engine Volumetric Fog: https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-fog-in-unreal-engine ; Heterogeneous Volumes: https://dev.epicgames.com/documentation/en-us/unreal-engine/heterogeneous-volumes-in-unreal-engine
- NVIDIA Flow: https://docs.nvidia.com/gameworks/content/artisttools/flow.htm ; GVDB: https://github.com/NVIDIA/gvdb-voxels , https://diglib.eg.org/handle/10.2312/hpg20161197
- EmberGen overview: https://cgpress.org/archives/embergen-real-time-volumetric-fluid-simulation.html
- Wolfe et al., Spatiotemporal Blue Noise Masks (2022): https://research.nvidia.com/publication/2022-07_spatiotemporal-blue-noise-masks
- Wrenninge, Kulla & Lundqvist, Oz volumes (2013): https://fpsunflower.github.io/ckulla/data/oz_volumes.pdf
- Bridson et al., Curl-Noise (2007): https://www.dneg.com/wp-content/uploads/2022/11/2007.pdf
- Kim et al., Wavelet Turbulence (2008): https://www.cs.cornell.edu/~tedkim/wturb
- Mulholland & Johnsson, 8.7 m²/g (NIST 1999): https://firedoc.nist.gov/article/JHcyXYQBWEcjUZEY8eM6
- Mulholland, Smoke Production and Properties, SFPE Handbook (5th ed. section 2): https://www.globalspec.com/reference/76873/203279/section-two-fire-dynamics
- Widmann et al., post-flame soot extinction (2003): https://www.nist.gov/node/588811 , https://www.nist.gov/node/595006
- Köylü & Faeth (1994): https://deepblue.lib.umich.edu/bitstream/2027.42/30068/1/0000438.pdf ; Krishnan, Lin & Faeth (2000): https://firedoc.nist.gov/article/kncyXYQBWEcjUZEY8-Sj
- Radney et al. soot albedo (2014): https://www.nist.gov/node/1195326
- Patterson et al., smoldering vs flaming (1986): https://www.frames.gov/documents/smoke/serdp/patterson_et_al_1986.pdf
- Biomass smoke albedo: https://repository.library.noaa.gov/view/noaa/51342 ; https://acp.copernicus.org/articles/16/13491/2016/
- FDS manuals (visibility S = C/K, soot 8700 m²/kg): https://pages.nist.gov/fds-smv/manuals.html
