# Data register

This register lists every physical value Photon uses, where it came from, and how good it is. A value marked **verify** has a named source, but the number came from general references and has not yet been checked against that source line by line. A value marked **prototype** is an engineering estimate that a later phase replaces.

## Component sizes (seed of the component library)

| Item | Value used | Source | Status |
| --- | --- | --- | --- |
| 2x4 actual size | 1-1/2 x 3-1/2 in | American Softwood Lumber Standard, NIST PS 20 (dry dressed sizes) | verify |
| 2x8 actual size (headers) | 1-1/2 x 7-1/4 in | PS 20 | verify |
| 1x6 actual thickness (fascia) | 3/4 in | PS 20 | verify |
| Stud spacing | 16 in o.c. | IRC Table R602.3(5) | verify |
| Top plate | double, single bottom plate | IRC R602.3.2 | verify |
| Wall gypsum | 1/2 in | IRC Table R702.3.5 | verify |
| Ceiling gypsum at 24 in o.c. | 1/2 in sag-resistant | IRC Table R702.3.5 | verify |
| Wall sheathing | 7/16 in OSB | IRC Table R602.3(3) | verify |
| Roof sheathing | 7/16 in OSB, span rating 24/16 | IRC Table R503.2.1.1(1) | verify |
| Truss spacing | 24 in o.c., Fink web pattern | Common practice. Trusses are designed per ANSI/TPI 1 | verify |
| Truss plates | 20 gauge (0.036 in) galvanized steel | ANSI/TPI 1 minimum plate gauge | verify |
| Wall insulation | R-13 batt | IRC N1102.1.2 (by climate zone) | verify |
| Attic insulation | R-38 blown fiberglass, about 13 in | IRC N1102.1.2 | verify |
| Attic ventilation | 1 sq ft NFA per 150 sq ft, half at the soffit and half at the ridge | IRC R806.2 | verify |
| Interior door | 1-3/8 in hollow core, 32 x 80 in | WDMA I.S.1A | verify |
| Exterior door | 1-3/4 in, 36 x 80 in | WDMA I.S.1A | verify |
| Door undercut leakage | 3/4 in | Common practice | prototype |
| Window | double pane, 2 x 3 mm glass | ASTM E1300 typical | verify |
| Shingles | laminated asphalt, about 2.5-3 lb/sq ft installed | ASTM D3462 product data | verify |
| Vinyl siding | 0.044 in | ASTM D3679 minimum 0.035 in | verify |

## Material properties

Every value is in `data/materials.txt` next to its source line. The thermal properties come from the SFPE Handbook (5th ed.) appendices, the USDA FPL Wood Handbook and Incropera & DeWitt, and all are marked **verify**. Burning properties (heat of combustion, soot yield, heat of gasification) are taken from Tewarson's chapter in the SFPE Handbook where a matching material exists, also marked **verify**. Values for asphalt shingle, the fabric blend, carpet with pad, every aerosol yield, every surface depth and every fail temperature/time are **prototype** estimates.

## Data still needed (Phase 4 and 6 data jobs)

- Per-material off-gas composition, ignition temperatures and flammability limits (R10).
- Cone calorimeter data (ASTM E1354) for asphalt shingles, vinyl siding and soffit, and carpet with pad.
- Gypsum dehydration and fall-off data for fire exposure.
- Ignition source powers and durations from the SFPE Handbook and NFPA 921.
- Furniture items checked against NIST TN 2303 and the NIST Fire Calorimetry Database.

## Gases (data/gases.txt)

21 species (CO, CO2, H2O, H2, CH4, C2H4, C3H6, benzene, toluene, formaldehyde, acrolein, methanol, acetic acid, HCN, HCl, NH3, TDI, methyl methacrylate, caprolactam, lumped tar). Molar mass, heat of combustion, autoignition temperature, flammability limits and IDLH come from NFPA 325, the CRC Handbook and the NIOSH Pocket Guide, all marked **verify**. The lumped tar species is **prototype**.

## Material off-gas splits

Each material lists what it gives off by mass fraction, for example wood (tar, steam, CO2, CO, acetic acid, methane, methanol, formaldehyde), PVC (HCl about 55%), flexible PU foam (TDI, HCN), nylon (caprolactam) and gypsum (steam from bound water). The splits are **prototype** estimates and a Phase 4 data job will replace them with measured pyrolysis product data per material.
