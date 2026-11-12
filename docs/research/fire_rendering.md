# Real-time fire and flame rendering for Photon

Scope: how to make Photon's flames look like real compartment fire (flame tongues, rollover, flashover) while every visible quantity stays driven by the solver fields (HRR q''' in W/m3, gas temperature T, soot density, velocity). This note complements `smoke_rendering.md` (smoke optics, energy-conserving integration, multiple scattering) and does not repeat it.

State of the current code (`src/render/renderer.cpp`, `VOL_FS`), relevant to what follows:

- The volume texture is `RGBA32F` = (soot, aerosol, T, q'''). Velocity is not uploaded.
- Flame emission is `flameColor(k) * shape * drive * 6.0`, where `drive` is a hand-scaled mix of q''' and a 1000-1600 K smoothstep. The color ramp is hand-made. `glowColor(T)` adds a second ad-hoc incandescence term.
- Emission is added as `emit * ds`. It is not tied to the soot absorption coefficient, so a thick flame gets brighter without limit instead of saturating at blackbody radiance.
- `flameNoise` drifts straight up (`-uTime*3.2` in z) whatever the local flow. Rollover tongues run horizontally along the ceiling jet, so this noise moves the wrong way exactly where it matters most.
- The scene color target is `GL_RGBA8` (line ~392). There is no HDR buffer, exposure, tone map or bloom, so any physically scaled emission clips to flat colors.
- Fire point lights (`uLights[8]`) light the meshes with `1/(1+r^2)` and no occlusion, so firelight leaks through walls.

---

## 1. Physics of flame appearance

### 1.1 Soot incandescence is what you see

The yellow-orange light from building fires is thermal emission from soot particles at roughly 1100-1500 K. It is not light from the combustion gases themselves. Soot is close to a grey/Rayleigh absorber, so each point emits

    j_lambda = kappa_lambda * B_lambda(T)          (Kirchhoff, local thermodynamic equilibrium)
    B_lambda(T) = 2 h c^2 / lambda^5 / (exp(h c / (lambda k T)) - 1)      (Planck)

The Rayleigh-limit soot spectral absorption coefficient is `kappa_lambda = C0 * f_v / lambda`, where f_v is the soot volume fraction and C0 is about 5-7 ([Sandia Fuego absorptivity model](https://www.sandia.gov/files/sierra/Fuego_Users_5_22/theory/mathmodels/absorptivity.html); [Modest, soot cloud emittance appendix](https://thermalradiation.net/webappendix/webappendixDSoot2.html)). Integrating over Planck gives the Planck-mean coefficient

    kappa_P = 3.72 * C0 * f_v * T / C2   ~=  1800 * f_v * T   [1/m]   (C0 = 7, C2 = 0.014388 m K)

Cross-check against Photon's smoke constant. With f_v = 1e-6 and soot density about 1800 kg/m3, the mass concentration is 1.8e-3 kg/m3. The FDS mass extinction of 8700 m2/kg ([FDS Technical Reference Guide](https://pages.nist.gov/fds-smv/manuals.html)) then gives sigma ~= 15.7 1/m, and the Rayleigh formula at 550 nm gives about 12.7 1/m. The smoke that the renderer already darkens is the same soot that must glow when hot. That one link explains most of what rollover looks like: a black layer that lights up from inside as it heats and ignites.

**Why flames look yellow-orange.** Wien's law, `lambda_max = 2898 um K / T`, puts the peak of 1300 K soot at about 2.2 um, deep in the infrared. The visible part is the rising short-wavelength tail, where red exceeds green and green exceeds blue, so the eye sees orange to yellow. The 1/lambda factor in kappa shifts soot emission slightly bluer than a true blackbody.

**Steepness.** In the visible, d ln B / d ln T = x e^x/(e^x - 1) with x = hc/(lambda k T). At 580 nm and 1300 K, x ~= 19, so visible brightness scales roughly as T^19. A 10% temperature error changes brightness about 6x. This is why a hand-made ramp cannot look right, and why the temperature used for emission matters more than anything else.

### 1.2 Blue regions

Blue light comes from chemiluminescence of excited CH* (strongest band at 431 nm, about 80% of CH emission). Green Swan bands of C2* appear in rich regions ([core.ac.uk review](https://core.ac.uk/download/96748447.pdf); [colorSpec "blue flame" vignette](https://ftp.kaist.ac.kr/CRAN/web/packages/colorSpec/vignettes/blueflame.pdf)). This emission is weak. It shows only where soot has not yet formed: at the base of flames near the fuel surface, on premixed or lean fronts, and in the pale, dancing "ghosting" flames of under-ventilated compartments. Anywhere soot is present, its incandescence drowns the blue.

### 1.3 Flame emissivity versus soot

For a path L through flame, the emissivity is `eps = 1 - exp(-kappa L)`. With kappa of order 1 1/m (f_v ~ 1e-6 at 1300 K), a 20 cm cell has optical depth of about 0.2-0.5 and a 1 m flame has emissivity of about 0.6-0.9. Small flames are therefore translucent with glowing edges, and large flames read as nearly opaque bright surfaces, capped at blackbody radiance. The ray marcher has to reproduce this saturation (section 2.4).

### 1.4 Flame height, regions and intermittency

**Heskestad mean flame height** (50% intermittency), Q in kW, D in m ([firedoc.nist.gov](https://firedoc.nist.gov/article/53cyXYQBWEcjUZEYz8q-); summary with NFPA 92 context in [this plume calculator](https://homo-deus-lab.pages.dev/lab/fire-engineering/fire-plume/)):

    L = 0.235 Q^(2/5) - 1.02 D

Worked examples: a 500 kW chair with D = 1 m gives L ~= 1.8 m. A 1 MW fire gives L ~= 2.7 m, so flames impinge on a 2.4 m ceiling and extend horizontally beneath it, which is the start of the visual rollover sequence.

**McCaffrey regions** ([NBSIR 79-1910](https://nvlpubs.nist.gov/nistpubs/Legacy/IR/nbsir79-1910.pdf)), in terms of z/Q^(2/5) (m/kW^(2/5)):

- Continuous flame, below about 0.08: centerline temperature rise roughly constant (about 800 K); velocity grows as z^(1/2).
- Intermittent flame, about 0.08-0.20: the flame pulsates (about 3 Hz in his burners); temperature rise falls roughly as (z/Q^(2/5))^-1; velocity roughly constant.
- Plume, above about 0.20: flame-free; temperature rise falls as (z/Q^(2/5))^(-5/3).

**Intermittency I(z)** is the fraction of time flame is present at a point. It is 1 in the continuous zone and 0 in the plume, and the visible flame height is where I = 0.5. At 20 cm resolution the solver gives time- and space-averaged fields, so in sub-grid terms a cell with moderate q''' means "flame is here part of the time". The renderer should draw flame at that point for that fraction of the time. Section 3.3 turns this into a shader rule.

**Puffing frequency**: f ~= 1.5/sqrt(D) Hz (Cetegen and Ahmed 1993; coefficients between 1.5 and 1.68 appear in the literature) ([vortex-dynamics revisit, arXiv 1803.10400](https://arxiv.org/pdf/1803.10400); [Moreno-Boza et al. on puffing onset](https://e-archivo.uc3m.es/entities/publication/2613ed72-8032-4a3b-8b16-695d50b6183a/full)). A 1 m fire puffs at about 1.5 Hz and a 0.3 m wastebasket at about 2.7 Hz. A 20 cm grid can partly resolve puffing for D >= 1 m (5 or more cells). Measure it in the solver output before adding any procedural pulsation, so that it is not counted twice.

### 1.5 Rollover and flashover, defined physically

Flashover is usually defined as an upper-layer temperature of about 600 C together with a floor heat flux of 20 kW/m2 ([41 CFR 102-80.145](https://www.law.cornell.edu/cfr/text/41/102-80.145); ASTM E603). Rollover is ignition of the unburnt fuel-rich gas layer under the ceiling, seen as flame tongues running through and ahead of the smoke layer. In solver terms, both are **HRR appearing in the hot upper layer away from the fuel item**, moving with the ceiling-jet velocity. The cues a firefighter recognizes are:

1. A thick, dark, turbulent layer banking down.
2. Isolated tongues of flame, sometimes bluish or pale where soot is low, rolling horizontally along the layer interface.
3. The whole layer lighting up orange, then the room filling with flame and flames venting from openings.

Every one of these follows from (a) coupled soot absorption and emission, (b) HRR-driven sub-grid flames, and (c) noise advected by the real velocity. None of them needs scripting.

---

## 2. From temperature and soot to displayed color

### 2.1 Planck -> CIE XYZ -> sRGB lookup table

Build a 1D texture once at startup on the CPU (microseconds):

    for T in [500 K, 3000 K], 512 entries:
        XYZ(T) = sum over lambda = 380..780 nm step 5 of  B_lambda(T) * (1/lambda) * [xbar, ybar, zbar](lambda) * dlambda
        rgb_lin(T) = M_XYZ->sRGB * XYZ(T) * 683 lm/W * scale

Use the [Wyman/Sloan/Shirley analytic CIE 1931 fits](https://research.nvidia.com/labs/rtr/publication/wyman2013simple/) so that no data tables are needed (about 10 lines, no library). Use the standard D65 matrix ([Lindbloom](http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html)): rows (3.2406, -1.5372, -0.4986), (-0.9689, 1.8758, 0.0415), (0.0557, -0.2040, 1.0570). Store absolute linear radiance (luminance in cd/m2 in Y) as `GL_RGB16F` or `GL_RGB32F`. The (1/lambda) factor is the soot spectral weighting; drop it to get a pure blackbody. Below about 1200 K the blackbody chromaticity falls slightly outside the sRGB gamut, so G and B can go slightly negative. Clamp at zero, or desaturate toward the same-luminance grey. Reference tables to check against: [Charity's blackbody colors](http://www.vendian.org/mncharity/dir3/blackbody/) and [Blender Cycles' blackbody fit](https://projects.blender.org/archive/blender-archive/raw/commit/1dfd82d2bcdd28e6230878d643c19b93a31cb4cd/intern/cycles/render/blackbody.cpp).

Nguyen, Fedkiw and Jensen render fire this way, spectrally through XYZ, and then apply **von Kries chromatic adaptation** to the flame's own white point. An observer adapted to firelight sees the hottest regions as near white rather than orange ([Nguyen et al. 2002](https://graphics.stanford.edu/papers/fire-sg02/)). For Photon, use partial adaptation (for example 30-50% toward the flame white point) only when fire dominates the frame, driven by the same auto-exposure state.

### 2.2 Brightness range and exposure

Real flames have luminance of roughly 10^3-10^5 cd/m2 (a candle flame is about 10^4). A smoke-filled room lit by the fire is about 1-100 cd/m2, so the ratio is 3-5 orders of magnitude. This requires:

1. An **HDR target** (`GL_RGBA16F`) for the mesh pass and the volume composite.
2. **Exposure** from the log-average luminance of the frame ([Reinhard et al. 2002](https://www.cs.utah.edu/docs/techreports/2002/pdf/UUCS-02-001.pdf)), measured over a downsampled buffer with the top 5-10% brightest pixels excluded, so that the flames do not set the exposure. Adapt with a 1-2 s time constant so the eye/camera "adapts" when flashover floods the room.
3. A **per-channel filmic tone map**: [Hable](http://filmicworlds.com/blog/filmic-tonemapping-operators/), [Narkowicz's ACES fit](https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/), or AgX ([darktable AgX docs](https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/agx/)).

Per-channel compression is what makes bright orange go to yellow and then to white. Red saturates first, then green catches up, and the core becomes yellow-white while the cooler edges stay orange-red. This "path to white" matches film and eye. A luminance-only tone map keeps cores saturated orange and looks fake. AgX gives the most controlled path to white with the least hue skew. Do not clamp before tone mapping, and keep exposure tied to the scene, not to the flame, so the room does not wash out.

### 2.3 Bloom and glare

Glare around bright flames is an important part of how "bright" reads on an LDR display. Use the [Jimenez Call of Duty: AW pyramid](https://iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/) (13-tap downsample, 3x3 tent upsample, 5-6 mips, about 4% energy mix, no threshold, and a 1/(1+luma) weight on the first downsample to stop flicker from fireflies). A straightforward OpenGL walk-through is [LearnOpenGL Phys. Based Bloom](https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom). Haloing of flames seen through smoke is real scattering and comes from the volume itself (smoke note §4.2).

### 2.4 Coupled emission-absorption step

Replace `col += tr * (a*scat + emit*ds)` with the energy-conserving form ([Hillaire, Frostbite 2015](https://www.ea.com/news/physically-based-unified-volumetric-rendering-in-frostbite); smoke note §1.2):

```glsl
vec3  kAbs   = kappaVis * vec3(0.90, 1.0, 1.18);      // soot ~1/lambda: R,G,B at 610,550,465 nm
vec3  sigT   = kAbs + sigmaScat;                        // per-channel extinction
vec3  stepT  = exp(-sigT * ds);
vec3  S      = kAbs * Lbb(Temit) + sigmaScat * inScatter;   // emission + scattering source
col += tr * S * (1.0 - stepT) / max(sigT, 1e-6);
tr  *= stepT;                                           // tr becomes vec3
```

`Lbb` is the LUT from 2.1. In an optically thick flame, the radiance converges to `Lbb(T)` (blackbody saturation) instead of growing with depth. Per-channel transmittance also makes fire seen through smoke look redder, which is physically correct and something firefighters recognize. This change removes `flameColor`, `glowColor` and the `drive*6.0` constant. Hot smoke at 600-800 K glows a faint dull red, just above the Draper point of about 800 K, and the same code handles it.

---

## 3. Real-time and film techniques

### 3.1 Survey

- **Nguyen, Fedkiw, Jensen 2002** ([project page](https://graphics.stanford.edu/papers/fire-sg02/)): a level-set reaction front gives the blue core, and the products carry temperature that drives blackbody emission with absorption and scattering. Rendering is spectral, with chromatic adaptation. This is the template for Photon's color pipeline.
- **GPU Gems 3, ch. 30** ([Crane, Llamas, Tariq](https://developer.nvidia.com/gpugems/gpugems3/part-v-physics-simulation/chapter-30-real-time-simulation-and-rendering-3d-fluids)): a GPU ray-marched fire with a reaction coordinate mapped through a 1D blackbody texture. It is close to Photon's architecture.
- **NVIDIA Flow** ([docs](https://docs.nvidia.com/gameworks/content/artisttools/flow.htm)): a sparse voxel grid with per-voxel combustion. Temperature and smoke feed buoyancy and appearance, and the same grid is used for self-shadowing. It confirms the approach of field-driven emission plus self-shadowing on the simulation grid.
- **UE Niagara Fluids** ([fluid simulation overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/fluid-simulation-in-unreal-engine---overview); [reference](https://dev.epicgames.com/documentation/unreal-engine/niagara-fluids-reference-in-unreal-engine)): the 3D Gas template stores density, temperature and velocity per cell, and temperature sets fire color through a "Black Body" render mode ([forum note](https://forums.unrealengine.com/t/tutorial-niagara-fluids-exploring-the-3d-gas-emitter/888748/24)).
- **EmberGen** ([JangaFX](https://jangafx.com/software/embergen)): real-time sparse-grid sims, color-gradient mapping on density and temperature, and a "semi-physically based" renderer ([CG Channel](https://www.cgchannel.com/?p=131092)). It is mostly used to bake flipbooks for games.
- **Production film** ([Pixar Production Volume Rendering notes](https://graphics.pixar.com/library/ProductionVolumeRendering/index.html)): blackbody emission from temperature, scaled by a density or "flame" channel, with heavy sub-grid upres.
- **Sub-grid detail**: [Wavelet Turbulence](https://www.cs.cornell.edu/~tedkim/wturb) (Kim et al. 2008) synthesizes missing high frequencies with an energy estimate from the coarse velocity. [Curl noise](https://history.siggraph.org/?p=115355) (Bridson et al. 2007) gives divergence-free procedural velocity. [Horvath and Geiger 2009](https://history.siggraph.org/?p=108799) refine a coarse fire simulation in high-resolution view-aligned 2D slices (film quality, too expensive for Photon).

**How games do rollover and flashover.** I found no public technical write-up of a game or training simulator that derives rollover from simulated fields. Commercial titles and VR trainers use authored flipbooks, emissive sheet meshes under the ceiling and scripted state changes. Published VR trainer work lists flashover and backdraft as future work ([TAMU FIT 2023](https://people.tamu.edu/~ed.rojas/src/pdfs/FIT2023.pdf)), or drives smoke from FDS while approximating flames ([Frontiers review](https://www.frontiersin.org/journals/robotics-and-ai/articles/10.3389/frobt.2019.00101/full)). Fire departments still say VR does not replace a flash-container burn ([Fire Engineering, Cosumnes FD](https://www.fireengineering.com/apparatus-equipment/tech-zone/how-the-cosumnes-fire-department-incorporates-virtual-reality-into-its-fire-academy-training/)). Field-driven rollover would be a real differentiator for Photon.

### 3.2 Velocity-advected sub-grid noise

Upload velocity as a second 3D texture (`GL_RGB16F`; 3 floats per cell, trivial bandwidth). Advect the noise lookup with the local flow using the two-phase flow-map trick ([Vlachos, Water Flow in Portal 2, SIGGRAPH 2010](https://advances.realtimerendering.com/s2010/index.html)):

```glsl
vec3  u   = texture(uVel, uvw).xyz;                    // m/s, solver velocity
float ph0 = fract(uTime / P), ph1 = fract(uTime / P + 0.5);   // P ~ 0.5-1 s
float n0  = noise3(p * f - u * ph0 * f + off0);
float n1  = noise3(p * f - u * ph1 * f + off1);
float n   = mix(n0, n1, abs(2.0 * ph0 - 1.0));         // cross-fade hides the reset
```

Add a curl-noise domain warp, with amplitude scaled by a turbulence estimate (|u| or |curl u| from the velocity texture, following the Kolmogorov idea in wavelet turbulence: sub-grid velocity ~ u_grid * (k/k_grid)^(-1/3)). Tongues then curl and stretch with the real flow: upward in the plume, horizontally along the ceiling jet, and out of doorways at the neutral plane.

Use a **precomputed tileable 3D noise texture** (64^3 `RGBA8`: value noise in one channel and a curl vector in three, about 1 MB, generated at startup). Three octaves of ALU hash noise (24 hashes) in a 600-step loop then become 2-3 texture fetches, which is probably a net speed-up.

### 3.3 Intermittency-calibrated flame presence

This is the rule that keeps the sub-grid detail honest:

1. Intermittency from the fields: `I = clamp(qppp / q_ref, 0, 1)`, with q_ref about 0.5-1 MW/m3. Tune q_ref once so that the rendered 50%-presence height matches Heskestad for a reference burner. (Smokeview's default HRRPUV flame cutoff is around 200 kW/m3, a useful lower bound.)
2. Remap the noise to an approximately uniform CDF (rank-normalize it once when baking the texture), so that `P(n < I) = I`. Flame is present at a sample when `n < I`. The time-averaged flame presence then equals the solver's value, and the noise only decides *where* within the cell and *when*.
3. Emission temperature of the sub-grid flame: `T_f = mix(1100 K, 1500 K, I)`. Cores near the fuel are yellow-white and ragged tips are orange-red, matching McCaffrey's temperature fall-off through the intermittent zone. Use `max(T_f, T_cell)`.
4. Emission strength from the radiant fraction, as FDS does in flame cells ([FDS Tech Guide, radiation chapter](https://pages.nist.gov/fds-smv/manuals.html)). With chi_r ~= 0.3-0.4 of HRR radiated:

       kappa_flame = chi_r * qppp / (4 * sigma_SB * T_f^4 * I)      (inside flame samples)
       kappa_vis   = kappa_soot(rho_soot) + kappa_flame              (feeds 2.4)

   The 1/I factor conserves the cell's mean radiated power. The flame brightness is then tied to computed HRR in W/m3, not to an artistic multiplier.
5. Blue: add `q''' * c_blue * vec3(0.15, 0.25, 1.0)` (CH* at 431 nm mapped to sRGB, then scaled down) weighted by `exp(-rho_soot / rho_ref)`. It appears only where soot is low: flame bases and ghosting flames in a fuel-rich layer.

---

## 4. Fire lighting the scene

1. **Clustered fire lights from HRR.** Group burning cells (q''' > threshold) into K <= 8 clusters, for example per fuel item and per ceiling region, using connected components or k-means on the CPU at the solver rate. For each cluster: position = q'''-weighted centroid; radiant power `P_r = chi_r * sum(q''' dV)` [W]; color = `Lbb(T_f)` normalized; radius = cluster extent (use it as a sphere light to soften the falloff). Irradiance on a surface follows the fire-engineering point-source model, `E = P_r cos(theta) / (4 pi r^2)`, which is valid beyond about 2 flame diameters. Replace the current `1/(1+r^2)` with this.
2. **Coherent flicker.** Modulate each cluster's intensity with the same sub-grid noise, integrated cheaply over 8-16 samples of the cluster's cells per frame, so light and visible flames flicker together near f ~ 1.5/sqrt(D). Uncorrelated light flicker is noticeably fake.
3. **Occlusion (no leaks).** Upload the solver's solid mask as an `R8` 3D texture. In the mesh fragment shader, march from the surface point toward each light through the 20 cm grid (DDA or fixed steps, at most about 40 fetches) and stop on a solid cell. Walls are at least one cell thick, so this is enough to block leaks. The same march accumulates smoke optical depth, giving `E *= exp(-tau)`, so firelight dims and reddens through smoke. If the per-pixel cost is too high, precompute a **fire-light grid** on the CPU instead: per cell, the visibility-weighted irradiance and dominant direction from each cluster (cells x K x about 50 steps; at about 56k cells this is cheap at 2-5 Hz). Sample it trilinearly for surfaces and for smoke in-scattering. Cube shadow maps per cluster (6 passes, 256^2) are the conventional alternative and give sharper shadows.
4. **Light from the volume and bounce.** For glow within the smoke layer and room-scale bounce, use a light-propagation-volume-style pass ([Kaplanyan and Dachsbacher 2010](https://www.cg.tuwien.ac.at/sites/default/files/talk/3289/Kaplanyan.pdf)) on the solver grid: inject `kappa * B` emission, propagate 2-band SH for a few iterations, and block on solids and smoke. If Photon later gains a radiation solver (discrete ordinates, as in FDS), its incident radiation field G is exactly this grid and should replace it. VPLs ([Keller, Instant Radiosity 1997](https://doi.org/10.1145/258734.258769)) and [clustered shading](https://www.cse.chalmers.se/~uffe/clustered_shading_preprint.pdf) only become worthwhile if there are more than about 16 lights. A simple per-room bounce term (mean firelight irradiance x wall albedo, added as ambient) captures most of the ceiling-glow effect.

---

## 5. Embers and sparks

- **Spawn** at a rate proportional to the local q''' on burning cellulosic fuels and in the upper layer during flashover (venting firebrands), and in bursts on ignition and collapse events.
- **Motion**: `dv/dt = (u(x) - v)/tau_p + g`, with `tau_p = rho_p d^2 / (18 mu)` (Stokes) for small sparks, and quadratic drag `0.5 rho C_d A |u-v|(u-v)/m` for larger brands. u is the solver velocity sampled trilinearly on the CPU. Embers then rise with the plume, travel along the ceiling jet and leave through openings.
- **Thermal state**: lumped cooling, `m c dT/dt = -h A (T - T_gas) - eps sigma A (T^4 - T_gas^4)`. Color and brightness come from the same `Lbb(T)` LUT, so particles stay consistent with the flames. Kill a particle when T falls below about 800 K (no visible glow) or it hits a solid.
- **Render**: GL_POINTS expanded to camera-facing quads, stretched along screen-space velocity x shutter (about 1/60 s) for streaks, additive into the HDR buffer before bloom, and depth-tested. 2-5k particles on the CPU cost little.

---

## 6. Recommended plan for Photon (value per effort)

| # | Change | Shader / code | Extra solver data | Cost |
|---|---|---|---|---|
| 1 | **HDR + exposure + filmic tone map + bloom** | Scene FBO `RGBA16F`; volume composite in HDR; post pass with log-average auto-exposure (bright pixels excluded, 1-2 s adaptation), per-channel AgX/ACES, Jimenez bloom chain | none | Low effort. About 16 MB of targets at 1080p; about 0.5 ms post. **Prerequisite for everything else.** |
| 2 | **Physical color: Planck -> XYZ -> sRGB LUT + coupled emission-absorption** | 1D `RGB16F` LUT built at startup (Wyman CMF fits); §2.4 step; per-channel tr; delete `flameColor`, `glowColor`, `drive*6.0` | none (soot, T, q''' already uploaded) | Low effort, about zero runtime cost. Hot layers glow dull red, thick flames saturate, fire seen through smoke reddens. |
| 3 | **Radiant-fraction flame emission + intermittency-thresholded sub-grid flames** | §3.3 rules; 64^3 baked noise texture replacing `vnoise` | chi_r per fuel (data register); optionally fire base diameter D per item for puffing checks | Low-medium effort; shader likely faster than now. Flame heights now validate against Heskestad. |
| 4 | **Velocity-advected, curl-warped noise** | 2nd 3D texture; two-phase flow-map advection §3.2 | velocity per cell (3 x fp16) | Medium effort, small cost. **This is what makes rollover read as rollover**: tongues run along the ceiling jet and out of doorways. |
| 5 | **Occluded, HRR-clustered, coherently flickering fire lights** | Point-source E = P_r cos/(4 pi r^2); solid-mask march or CPU fire-light grid; smoke attenuation | solid mask (`R8` 3D); optional per-cell irradiance grid | Medium effort. Fixes light leaking through walls. About 1 ms per-pixel march, or CPU grid at 2-5 Hz. |
| 6 | **Smoke in-scattering from the fire-light grid / LPV** | Replace `uLights` loop in `VOL_FS` with a grid fetch | reuses #5 | Low effort after #5. The layer glows from within before and during flashover. |
| 7 | **Embers and sparks** | Particle system, additive HDR quads | velocity (from #4), fuel type | Low-medium effort, negligible cost. |
| 8 | **Blue chemiluminescence + partial chromatic adaptation** | §3.3 item 5; von Kries step tied to exposure | none | Low effort, modest value (flame bases, ghosting flames). |

**Validation (do alongside 3-4):** (a) render a steady burner and check the 50%-presence flame height against Heskestad and the region boundaries against McCaffrey. (b) Record the luminance time series of the flame region and check that its dominant frequency is near 1.5/sqrt(D). (c) Have the owner compare the result side by side with reference compartment-fire videos, for example [UL FSRI](https://fsri.org/) flashover experiments, judging rollover onset, layer color and flame-tongue direction. Keep every constant introduced (q_ref, chi_r, T_f range, C0) in `DATA_REGISTER.md` with its source, because they are physical parameters, not art knobs.

---

## Sources

- Nguyen, Fedkiw, Jensen, *Physically Based Modeling and Animation of Fire*, SIGGRAPH 2002 — https://graphics.stanford.edu/papers/fire-sg02/ ; https://graphics.ucsd.edu/~henrik/papers/fire/
- Crane, Llamas, Tariq, GPU Gems 3 ch. 30 — https://developer.nvidia.com/gpugems/gpugems3/part-v-physics-simulation/chapter-30-real-time-simulation-and-rendering-3d-fluids
- McCaffrey, NBSIR 79-1910 — https://nvlpubs.nist.gov/nistpubs/Legacy/IR/nbsir79-1910.pdf
- Heskestad flame height — https://firedoc.nist.gov/article/53cyXYQBWEcjUZEYz8q- ; https://homo-deus-lab.pages.dev/lab/fire-engineering/fire-plume/
- Puffing: https://arxiv.org/pdf/1803.10400 ; https://e-archivo.uc3m.es/entities/publication/2613ed72-8032-4a3b-8b16-695d50b6183a/full
- Soot radiation — https://www.sandia.gov/files/sierra/Fuego_Users_5_22/theory/mathmodels/absorptivity.html ; https://thermalradiation.net/webappendix/webappendixDSoot2.html
- FDS Technical Reference / User Guides (soot extinction, radiant fraction, Smokeview) — https://pages.nist.gov/fds-smv/manuals.html
- Chemiluminescence — https://core.ac.uk/download/96748447.pdf ; https://ftp.kaist.ac.kr/CRAN/web/packages/colorSpec/vignettes/blueflame.pdf
- Flashover definition — https://www.law.cornell.edu/cfr/text/41/102-80.145
- Wyman, Sloan, Shirley, CIE XYZ fits — https://research.nvidia.com/labs/rtr/publication/wyman2013simple/
- sRGB matrix — http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
- Blackbody tables — http://www.vendian.org/mncharity/dir3/blackbody/ ; https://projects.blender.org/archive/blender-archive/raw/commit/1dfd82d2bcdd28e6230878d643c19b93a31cb4cd/intern/cycles/render/blackbody.cpp
- Reinhard et al. 2002 — https://www.cs.utah.edu/docs/techreports/2002/pdf/UUCS-02-001.pdf
- Hable filmic — http://filmicworlds.com/blog/filmic-tonemapping-operators/ ; Narkowicz ACES — https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/ ; AgX — https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/agx/
- Jimenez bloom — https://iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/ ; https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom
- Hillaire, Frostbite volumetrics — https://www.ea.com/news/physically-based-unified-volumetric-rendering-in-frostbite
- NVIDIA Flow — https://docs.nvidia.com/gameworks/content/artisttools/flow.htm
- Niagara Fluids — https://dev.epicgames.com/documentation/en-us/unreal-engine/fluid-simulation-in-unreal-engine---overview ; https://dev.epicgames.com/documentation/unreal-engine/niagara-fluids-reference-in-unreal-engine ; https://forums.unrealengine.com/t/tutorial-niagara-fluids-exploring-the-3d-gas-emitter/888748/24
- EmberGen — https://jangafx.com/software/embergen ; https://www.cgchannel.com/?p=131092
- Pixar Production Volume Rendering — https://graphics.pixar.com/library/ProductionVolumeRendering/index.html
- Wavelet Turbulence — https://www.cs.cornell.edu/~tedkim/wturb ; Curl noise — https://history.siggraph.org/?p=115355 ; Horvath and Geiger — https://history.siggraph.org/?p=108799
- Vlachos, Water Flow in Portal 2 (flow maps) — https://advances.realtimerendering.com/s2010/index.html
- Kaplanyan and Dachsbacher, Cascaded LPV — https://www.cg.tuwien.ac.at/sites/default/files/talk/3289/Kaplanyan.pdf
- Keller, Instant Radiosity — https://doi.org/10.1145/258734.258769 ; Olsson et al., Clustered shading — https://www.cse.chalmers.se/~uffe/clustered_shading_preprint.pdf
- VR firefighter training — https://people.tamu.edu/~ed.rojas/src/pdfs/FIT2023.pdf ; https://www.frontiersin.org/journals/robotics-and-ai/articles/10.3389/frobt.2019.00101/full ; https://www.fireengineering.com/apparatus-equipment/tech-zone/how-the-cosumnes-fire-department-incorporates-virtual-reality-into-its-fire-academy-training/
- UL FSRI — https://fsri.org/
