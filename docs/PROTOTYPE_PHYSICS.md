# Phase 1 prototype physics

Everything in the Phase 1 solver is a placeholder for the reference models in the handoff document. This page lists every simplification, so none of them is mistaken for the real model. Each later phase replaces one of them and checks the result against FDS.

| Area | Prototype model | Replaced by |
| --- | --- | --- |
| Grid | Uniform 20 cm cubes, single precision. Walls and ceilings are thin barriers on cell faces. | Phase 3: staggered LES grid at 5-10 cm (reference) and about 20 cm (realtime) |
| Gas flow | Incompressible projection with a heat-release expansion source (div u = q R / (P0 cp)). Buoyancy is g (1 - T0/T). Semi-Lagrangian velocity advection, no turbulence model. | Phase 3: low-Mach Navier-Stokes, LES, per-space background pressure |
| Pressure | Red-black SOR with a fixed 40 iterations and a warm start. It is not converged, so sealed rooms are only approximately consistent. | Phase 3: fast Poisson solver chosen by benchmark |
| Leakage | Intact barriers keep a fixed open-area fraction (`leakage` in assemblies.txt). Attic vents come from IRC R806.2 net free area. | Phase 3: leakage network |
| Scalars | First-order upwind transport of T, O2, fuel, soot, aerosol and soot potential. It is numerically diffusive. | Phase 3 |
| Fuel gas | One generic fuel gas with 25 MJ/kg. Each material's off-gas is converted to that fuel by its heat of combustion, and its soot yield is carried as a separate "soot potential" scalar. | Phase 4: one tracked gas per material (R10) |
| Combustion | Mixing-limited, with tau = 0.1 s. Gas ignites from a pilot, from a lit neighbor, or above 800 K. Extinction uses a limiting O2 that falls linearly from 0.15 at ambient to 0 at 1500 K. Heat of combustion per O2 is 13.1 MJ/kg (Huggett). | Phase 4: per-fuel ignition limits and extinction |
| Radiation | **No radiation transport.** 30% of flame heat is shared among visible solid surfaces within 2 cells by solid angle, and the rest is lost. Gas-to-surface exchange uses a local grey emissivity from soot. Flashover from radiation off a hot layer therefore does not happen. Spread is by flame contact and convection only. | Phase 4: banded radiative transport |
| Solids | One lumped surface node per cell (a thin surface layer), with no in-depth conduction. Pyrolysis follows the heat of gasification: heat above the pyrolysis temperature turns into off-gas. Char stays behind and oxidizes slowly. A cell is removed from the grid once its volatiles are gone. | Phase 4: 1-D in-depth conduction, Arrhenius kinetics, moisture |
| Barriers | Two nodes, one on each exposed face, coupled through the R-value of the layers between them. Combustible layers pyrolyze. Non-combustible layers (gypsum, glass, insulation) fail after `fail_time` above `fail_temp`. When every layer has failed, the face opens. | Phase 4: through-thickness model. Phase 5: thermal-stress glass breakage |
| Ignition sources | A small gas burner of the chosen power and duration that always burns while it is on. The strengths are placeholders. | Phase 6: cited source models (SFPE, NFPA 921) |
| Contents | Boxes of one material per cell. Masses are estimates. | Phase 6: catalog checked against NIST TN 2303 |
| Smoke rendering | Extinction is 8700 m2/kg for soot (Mulholland & Croarkin) and 3000 m2/kg for aerosol (estimate). Flame emission scales with the heat release rate, and hot smoke glows by temperature. | Phase 7: optical properties from computed species |

Known artefacts:

- Results are sensitive to small changes, such as when a window falls out. That is partly real fire behavior and partly the coarse grid.
- A closed house can starve the fire of oxygen. Opening a door or window, or waiting for a window to fail, brings it back.
- Smoke can appear half a cell through a wall in the viewer. This comes from texture interpolation only; the solver does not leak.

## Owner direction: an object-based house

The house will be made of real objects, not cell boxes or zero-thickness faces. Each drywall sheet, sheathing panel, stud, joist, truss member, door slab, window and furniture part becomes an object with its own geometry, material, and thermal and burn state. The solver couples each object to the gas grid at its real surfaces, and burn-through removes the actual piece. Walls get real thickness and stud cavities, which the concealed-space model needs. The framing members already exist as objects in the scene. Planned in Phase 2 (object scene format), Phase 4 (per-object solid model) and Phase 5 (the generator emits objects).

## Structure and collapse (first version)

Every object has its real mass (density times true volume, or the catalogued mass for furniture). A support graph records what holds each piece up: sheets are fastened to the framing they touch, framing bears on framing that reaches lower, and everything else rests on what is directly below it. Weight is summed down that graph. A piece's integrity is read from the fire solver: the consumed fraction of its cells or of its assembly layer. A piece fails when it is consumed (integrity below 0.15) or overloaded. Prototype strength is 400 times its own weight for intact framing and 60 times for other pieces, scaled by integrity^1.5. Anything left with no standing support falls under gravity, lands on what is below, and stays as debris. Falling panels open holes in the fire grid, fallen ceiling sheets remove that layer, and falling framing carries its fuel to where it lands. **Not yet:** beam bending and sag, gusset-plate pull-out, load sharing, lateral stability, or impact effects. These come with the Phase 12 structural model, checked against the UL floor and roof collapse tests.
