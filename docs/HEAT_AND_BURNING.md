# Photon heat and burning (Phase 4, in progress)

Status: the models below are implemented. The single-item and single-room validation against NIST and UL data is under way. Results are recorded at the end of this file.

## Solids

| Piece | Model | Code |
| --- | --- | --- |
| Furniture and other solid fuel cells | 1D implicit conduction into each exposed cell. 8 nodes from 0.5 mm to 9 mm thick (25 mm in total), adiabatic at the back. The surface temperature is extrapolated from the first node. | `Solver::conductSlab` |
| Upholstery | Foam in an item that has a fabric part is covered by 1.5 mm of that fabric. The cover's conductivity, heat capacity and kinetics apply to the outer nodes until it has burned away. | `discretize.cpp`, `conductSlab`, `pyrolyzeSlab` |
| Walls, ceilings, floors | 1D implicit conduction through every intact layer, with 1 to 3 nodes per layer depending on thickness. Layers given as an R-value (batts, stud cavities) conduct through that R. Each face's absorbed energy is its boundary flux. | `Solver::barriersStep` |
| Porous fuel (needles, floor coverings) | Lumped node, with absorbing and emitting area capped at what a cell can intercept (needles shade each other). | `solidsStep`, `radiation.cpp` |

## Pyrolysis

- **Kinetics:** one first-order Arrhenius reaction per material in every slab node. It is built the way the FDS User Guide builds it from TGA data: the peak mass-loss temperature (`pyrolysis_temp`) and width (`pyrolysis_range`, default 80 K) at 5 K/min heating:
  - r_p = 2 beta / dT
  - E/R = e r_p Tp^2 / beta
  - k(T) = e r_p exp((E/R)(1/Tp - 1/T))
- **Heat of reaction:** the reaction absorbs the material's heat of gasification.
- **Receding surface:** when the surface node is spent, the profile shifts inward and the surface recedes into the block.
- **Thin and porous fuels:** these keep the heat-of-gasification model at the pyrolysis temperature.

## Flame on a surface (sub-grid, MODELED)

A 20 cm cell cannot resolve a flame sheet, so:

- **Flame heat flux:** a flaming cell next to a surface heats it as a turbulent flame would. The flame is taken at 1200 K with emissivity 0.3 and h = 15 W/m2K, which gives about 30–50 kW/m2. The heating scales with the cell's heat release; full coverage is at 1.2 MW/m3.
- **Burning fraction:** each fuel cell has a flaming fraction that starts as a roughly 100 cm2 patch. It grows at the material's spread rate, which is faster upward and speeds up under radiant heating above 15 kW/m2.
- **Ignition of unburned surface:** it ignites when it reaches the pyrolysis temperature, or when a touching flame has delivered the energy needed to heat its surface layer.

## Gas-phase combustion

- **Ignition:**
  - a pilot flame;
  - a burning neighbor;
  - the local fuel mixture's own auto-ignition temperature, which is carried as a transported tracer and mass-weighted over the burnable off-gases in `data/gases.txt`.
- **Extinction:** uses the FDS-style critical flame temperature (1600 K). Fuel burns only if it, plus the air carrying the oxygen it needs, would reach 1600 K. For ambient gas this gives a limiting O2 of about 12% by mass.
- **Radiant loss:** the radiative fraction (0.35) leaves the flame at once. Radiation carries it to the surfaces.

## Radiation

- **Method:** ray-traced finite-angle radiation, with 32 directions over the full sphere from every surface element.
- **Bands:** four wavelength bands, 0–2.7, 2.7–8, 8–14 (the thermal camera band) and above 14 um.
  - Soot absorbs as kappa = 5.5 fv / lambda.
  - H2O and CO2 absorb with per-band coefficients per atmosphere (PROTOTYPE; verify against RADCAL).
  - Emission in each band is the band's Planck fraction.
- **Flame cells:** they emit at least chi_r q''' (the FDS flame correction), with a 1400 K spectrum.
- **Reference level:** everything is relative to ambient, so a room at ambient exchanges nothing.

## Verification (photon_tests)

| Test | Checks |
| --- | --- |
| slab_conduction_semi_infinite | Constant flux into a thick solid: surface temperature within 5% of 2 q sqrt(t / (pi k rho c)) |
| pyrolysis_peak_at_reference_temperature | Heated at 5 K/min, mass loss peaks within 15 K of the material's reference temperature |
| extinction_limiting_oxygen | The limiting O2 mass fraction is 10–14%; hot vitiated gas still burns |

## Validation runs (cases/tree_living.case, cases/living_sofa.case)

| Case | Photon | Measured |
| --- | --- | --- |
| Dry Christmas tree, 2 kW flame | 7.7 MW at 10 s (after the shading fix) | 7.4 MW at 31 s, NIST TN 2131; dry Scotch pine room flashover within 40 s |
| Living-room sofa, 2 kW flame (before upholstery covers) | catches at 30 s, 10 MW at 60 s, chokes in the closed house by 100 s | UL modern room: flashover at 3:20–4:45 |
