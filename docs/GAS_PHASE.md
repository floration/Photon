# Photon gas phase (Phase 3)

Status: implemented and verified against exact answers. Not yet compared with FDS (needs the FDS install). Burning, radiation and solids are still the Phase 1 prototype until Phase 4.

Code: `src/sim/gasphase.cpp` (flow), `src/sim/state.cpp` (save and load), `src/sim/devices.h` (measurements), `src/sim/fds_export.cpp` (FDS input writer), `src/app/cli_case.cpp` (case runner).

## Equations

The model family is the same as FDS: low Mach number, large eddy simulation, staggered uniform grid.

| Quantity | How it is solved |
| --- | --- |
| Density | Ideal gas law at the background pressure of the cell's pressure zone: rho = P_zone / (R T) |
| Divergence | div u = q / (rho cp T) + (added gas volume) / V - (1 / (gamma P)) dP_zone/dt |
| Momentum | du/dt + (u . grad) u + grad H = g (rho0 - rho) / rho + p~ grad(1/rho) + nu_t lap u |
| Pressure | Poisson equation for H, solved each step by conjugate gradient with a wall-aware multigrid preconditioner (pressure_mg.cpp) |
| Turbulence | Deardorff model: nu_t = 0.1 dx sqrt(k_sgs), with k_sgs from the resolved and test-filtered velocity |
| Heat | Molecular and turbulent conduction (Pr_t = 0.5) into the cell heat budget |
| Species | O2, fuel gas, CO2, H2O, CO, soot, pyrolysis aerosol; N2 is the remainder; turbulent diffusion (Sc_t = 0.5) |
| Zones | Gas cells connected through open faces form a zone. A zone touching the open boundary is at ambient pressure. A closed zone has its own pressure, which rises with heat and falls through its leaks. |
| Leakage | Each crack face (open fraction below 5%) flows by the orifice equation, u = sqrt(2 dp / rho). The zone pressure is solved implicitly against all its leaks each step. |
| Boundaries | The sides and top are open at ambient pressure, with zero-gradient outflow. The ground is a wall. Optional wind enters through the windward sides with a power-law profile. |
| Burners | Prescribed fuel-gas release over a rectangle, with a t-squared ramp. |

## Numerics

- **Time stepping:** forward Euler, operator split. The time step is limited by:
  - the Courant number (0.8 realtime, 0.5 reference)
  - the diffusion limit
  - the expansion rate (no cell may push out more than a quarter of its gas in one step)
- **Momentum advection:** second-order upwind with the van Leer limiter. Neighbors across walls use free slip.
- **Scalar transport:** second-order Superbee-limited face values, mass-weighted.
  - The face density comes from the face temperature (rho_f = P / (R T_f)), so rho_f T_f = P / R on every face.
  - As a result, the equation-of-state density after transport equals the transported mass. Temperature mixes by mass (energy conserving), not by volume.
  - The new value is bounded by the cell's own old value and its neighbors' old values.
- **Pressure solve:** warm-started from the previous step. It stops when the largest divergence error is below the profile tolerance: 0.02 per second for realtime, 0.002 for reference.
  - The preconditioner is connected-aggregation multigrid. Each coarse unknown is one connected piece of a 2x2x2 block, so cells on opposite sides of a wall or closed door are never merged.
  - In the model home with a 2.8 MW fire it converges in 11–12 iterations. Plain Jacobi preconditioning stopped at its 80-iteration cap without converging.
- **Combustion products:** the generic fuel is CH1.7O0.4.
  - Per kg of fuel: 1.95 kg O2 consumed, 2.19 kg CO2 and 0.761 kg H2O produced.
  - CO yield is 0.02, and the soot yield comes from each material.
  - Carbon and oxygen balances are corrected for the soot and CO formed.

## Profiles

| | Realtime | Reference |
| --- | --- | --- |
| Cell | 20 cm | 10 cm |
| Outside air around / above the house | 1.6 m / 2.6 m | 2.0 m / 4.0 m |
| Courant limit | 0.8 | 0.5 |
| Pressure tolerance | 0.02 1/s, at most 80 iterations | 0.002 1/s, at most 400 iterations |

## Differences from FDS (known, deliberate for now)

- **Time integration:** FDS uses a second-order predictor-corrector. Photon uses forward Euler with limited upwind schemes, which is more diffusive.
- **Temperature and density:** FDS transports density and gets temperature from it. Photon transports temperature and species with the face-density construction above. Both conserve mass to the accuracy of the pressure solve.
- **Pressure solver:** FDS uses an exact FFT solver. Photon's iterative solve leaves a small divergence error, set by the profile tolerance.
- **Gas properties:** cp is constant at 1100 J/(kg K), and the mixture molar mass is air's. FDS uses temperature- and composition-dependent properties.
- **Combustion:** a mixing time of 0.1 s, with a simple critical-flame-temperature extinction rule (prototype, Phase 4). FDS uses an EDC mixing time from the LES fields.
- **Walls:** walls use free slip in the momentum equation. FDS uses a wall function.
- **Leakage:** the leakage areas are effective leakage areas, with the discharge coefficient folded in.

## Verification (photon_tests)

| Test | Checks |
| --- | --- |
| still_air_stays_still | No fire: velocity below 1 mm/s, no temperature drift |
| sealed_room_pressure_rise | Sealed, adiabatic room: pressure rise within 8% of (gamma - 1) Q / V, and nothing escapes |
| fuel_mass_balance | Fuel released = burned + in grid within 5% (measured: about 3%) |
| doorway_two_way_flow | Burner room: inflow low, outflow high in the doorway, and a hot upper layer |
| resume_matches_uninterrupted | Save, continue, then reload and continue: bit-identical. A state refuses a different grid. |
| wind_flows_around_building | Inflow matches the power-law profile, the field stays bounded, and the closed room stays still |

## Code-to-code (FDS)

```
photon_cli --case cases/room_burner.case --export-fds out/room_burner_rt.fds
photon_cli --case cases/room_burner.case
```

The first command writes the FDS input on the same grid. The second writes `out/room_burner_rt_devc.csv`, with the same device IDs FDS uses. The case is a 100 kW gas burner in an ISO 9705 size room, with adiabatic walls, no radiation and no leakage, so only the gas phase is compared.

First Photon result for that case (realtime, mean of 60 to 120 s):

- **Layer:** interface at 1.0 m; upper layer 146 °C (layer method), with thermocouples reaching 180 °C near the ceiling.
- **Doorway flow:** inflow 0.7 m/s low, outflow 1.3 m/s high, neutral plane at about 1.15 m.
- **Energy check:** the doorway outflow carries about 0.57 kg/s at about 160 K above ambient, which is roughly 100 kW and matches the burner.

## Save and load

- **Format:** versioned state files (`STATE_VERSION`) that hold every gas field, zone pressure, solid and barrier state, burner, ignition and event, plus the run's time and wind.
- **Refusals:** a file saved with a different profile, grid or scene is refused with a message.
- **Compression:** not yet. A realtime house state is about 30 MB.
