# Real Fire Reference: Residential Structure Fires

Visual and behavioral reference for the Photon house-fire training simulator. This file holds links and notes only. No video files were downloaded.

Compiled 2026-11-12.

## How to read this file

- **Verification.** Every YouTube link below was checked on 2026-11-12 by fetching the watch page. Each returned playability status `OK`. Title, channel and length come from YouTube metadata. Non-YouTube links returned HTTP 200 unless the entry says otherwise. Fire Engineering pages return 403 to scripted requests but open in a browser.
- **Lengths** are given as m:ss.
- **Timestamps** marked "(FSRI timeline)" or "(publisher description)" are copied from the official video description. They are not from my own viewing. I did not watch the footage. Visual notes come from those descriptions and from the linked reports. Check them by eye before using them for tuning.
- **Source tiers:**
  - **A**: official research body (UL FSRI, NIST, FDNY, ISFSI, a fire department's own channel).
  - **B**: trade press or a training organization republishing research footage (Firehouse, Fire Engineering, LACoFD, Insight Fire Training, Kill the Flashover).
  - **C**: an individual upload. Use it for look only, never for numbers.
- **Prefer the FSRI "Horizontal Ventilation" and "Vertical Ventilation" experiment videos for tuning.** They are real-time, fixed-camera recordings of instrumented burns. Each has a published event timeline and a technical report with temperature, gas and pressure data.

---

## 0. Primary source hubs

| # | Link | Source / tier | What it is |
|---|------|---------------|-----------|
| 0.1 | https://www.youtube.com/@FireSafetyResearchInstitute | UL FSRI YouTube / A | The channel that hosts every FSRI experiment video below. |
| 0.2 | https://www.youtube.com/@NIST | NIST YouTube / A | NIST's official channel: fire research, FDS reconstructions and lectures. |
| 0.3 | https://www.youtube.com/playlist?list=PL386371DD1CBA390F | NIST "Fire Research" playlist / A | Curated NIST fire research videos. |
| 0.4 | https://training.fsri.org | FSRI Fire Safety Academy / A | Free online courses (horizontal and vertical ventilation, attic, basement, fire attack). They embed experiment video synced to temperature data, which is the best combined visual and data reference. Free account needed. |
| 0.5 | https://www.nist.gov/el/fcd | NIST Fire Calorimetry Database / A | Item-burn data: heat release rate (HRR) curves plus video or stills, 50 kW to 20 MW. |
| 0.6 | https://www.nist.gov/el/fcd/experiments | NIST FCD / A | Searchable list of all experiments. |
| 0.7 | https://www.nist.gov/el/fire/national-fire-research-laboratory/videos | NIST NFRL videos / A | 18 NFRL videos, including the upholstered furniture comparison and the blue-light "seeing through fire" video. |
| 0.8 | https://fsri.org/programs/tactical-considerations | UL FSRI / A | Index of the Tactical Considerations series: short videos that each tie one research finding to experiment footage. |
| 0.9 | https://fireinvestigation.fsri.org/ | UL FSRI / A | Fire investigation and ventilation study site, with per-experiment pages, photos, video and data for ranch and colonial houses. Example: https://fireinvestigation.fsri.org/ranch/experiment-2/index.html |
| 0.10 | https://closeyourdoor.org | UL FSRI / A | Hub for the closed-door campaign. |

---

## 1. Full-scale experiments with known conditions (highest value)

### 1A. UL FSRI: legacy vs. modern furnishings, side-by-side room burns

**Report:** Kerber, S., "Analysis of Changing Residential Fire Dynamics and Its Implications on Firefighter Operational Timeframes," *Fire Technology* 48 (2012).
- PDF: https://d1gi3fvbl0xj2a.cloudfront.net/public/2021-07/Analysis_of_Changing_Residential_Fire_Dynamics_and_Its_Implications_on_Firefighter_Operational_Timeframes.pdf
- Landing page: https://fsri.org/resource/analysis-changing-residential-fire-dynamics-and-its-implications-firefighter-operational

**Test conditions:**
- Three pairs of living rooms. Rooms 1–4 were 3.7 x 3.7 m with a 2.4 m ceiling and a 2.4 x 2.1 m front opening. Rooms 5–6 were 4.0 x 5.5 m with a 3.0 x 2.1 m opening.
- Each fire was started with a candle on the sofa.
- Fuel loads: 19.0 kg/m² (modern) vs. 22.9 kg/m² (legacy) in pair 2, and about 11.2 kg/m² for both rooms in pair 3.

| # | Link | Source / tier | Length | What it shows | Timestamps / notes |
|---|------|---------------|--------|---------------|-------------------|
| 1.1 | https://www.youtube.com/watch?v=87hAnxuh1g8 | UL FSRI / A | 2:31 | 2020 high-definition side-by-side burn of a natural-furnished and a synthetic-furnished room. Sharpest current footage of the modern vs. legacy contrast. | Synthetic room flashed over at 4:50. Natural room took more than 30 min (FSRI project page, https://fsri.org/research/new-comparison-natural-and-synthetic-home-furnishings). |
| 1.2 | https://www.youtube.com/watch?v=D7T43OmErmU | UL FSRI / A | 1:19 | The original FSRI "Comparison of Room Furnishing (Natural vs Synthetic)" clip, from the 2009 comparison. | Compressed time. |
| 1.3 | https://fsri.org/resource/new-home-furnishing-comparison-video | UL FSRI / A | n/a | Official resource page for 1.1, with context. | |
| 1.4 | https://www.youtube.com/watch?v=MhBo8_8JNGk | Firehouse (UL footage) / B | 3:52 | Side-by-side modern and legacy fuel compartments with door and window ventilation. **Best single clip for smoke-layer onset and ventilation-induced flashover timing.** | Publisher description: modern room has a visible ceiling smoke layer within 2:00 and is ventilation-limited at 5:00. Door and window are vented at 8:00. Flashover follows 2:15 after venting. Legacy room is ventilation-limited at 20:00 and flashes over 8:30 after venting. |
| 1.5 | https://www.youtube.com/watch?v=H1V4lVkOUmI | Flashpoint Equipment (compilation by "Swedish FireNerd") / C | 3:46 | The UL legacy and modern burns at 10x speed, with **time–temperature curve overlays**. | Useful for matching visual stage to temperature. |
| 1.6 | https://www.youtube.com/watch?v=mulnwTNhV6Q | individual re-upload of UL test / C | 5:32 | The original UL legacy (left) and modern (right) side-by-side. | |
| 1.7 | https://www.youtube.com/watch?v=IEOmSN2LRq0 | StreamwoodFD (fire department re-upload) / B | 5:32 | The same UL demonstration. | |
| 1.8 | https://www.youtube.com/watch?v=CTqizMtS4zU | ATF Fire Research Lab footage (individual upload) / B–C | 3:58 | ATF modern vs. legacy compartment burns with **tenability data**. | Description: modern compartments reach flashover and peak HRR 6–8x faster. |

**Numbers from the Kerber paper** (section 3 below collects all numbers):
- Modern rooms flashed over at 3:40 (Exp 1), 4:45 (Exp 3) and 3:20 (Exp 5).
- Legacy rooms flashed over at 29:30 (Exp 2) and 34:15 (Exp 4). Exp 6 never flashed over and was extinguished at 30:00.
- Exp 3 (modern): fire involved the right third of the sofa at 3:45 and spread to the TV stand at 4:00. The left sofa arm ignited from gas-layer radiation at 4:16. Flames came out the top of the opening at 4:20, and flashover followed at 4:45.
  - **This sequence is a ready-made script for the simulator's growth-to-flashover stage.**

### 1B. UL FSRI horizontal ventilation house experiments (2008 DHS AFG)

**Project:** "Impact of Ventilation on Fire Behavior in Legacy and Contemporary Residential Construction"
- Project page: https://fsri.org/research/impact-ventilation-fire-behavior-legacy-and-contemporary-residential-construction
- 2010 technical report: https://d1gi3fvbl0xj2a.cloudfront.net/public/2021-07/DHS_2008_Grant_Report_Final.pdf
- 2012 summary paper: https://d1gi3fvbl0xj2a.cloudfront.net/public/2021-07/Analysis_of_One_and-_Two_Story_Single_Family_Home_Fire_Dynamics_and_the_Imapct_of_Firefighter_Horizontal_Ventilation.pdf
- Online course: https://training.fsri.org/course/148/impact-of-horizontal-ventilation-on-fire-behavior

**Test conditions:**
- Two houses were built inside the UL Northbrook large fire facility:
  - a one-story ranch (1,200 ft², 3 bedrooms, compartmentalized, 2.4 m ceilings), vented at 8:00;
  - a two-story colonial (297 m² / 3,200 ft², open plan, two-story great room), vented at 10:00.
- Every fire was ignited on the living-room or family-room sofa, with all exterior openings closed.
- In every experiment, water was applied for 10 s with a 379 L/min (100 gpm) fog nozzle set to a straight stream.

All of the following are FSRI channel videos (tier A). They are real-time, and the video length matches the experiment end time. Timelines are FSRI's.

| # | Link | House | Length | Ventilation scenario and timeline (FSRI timeline) |
|---|------|-------|--------|---------------------------------------------|
| 1.9 | https://www.youtube.com/watch?v=kYwo7ofs3Zg | 1-story | 15:28 | **Exp 1.** 0:00 ignition. 8:00 front door open. 12:30–12:40 straight stream into front door. 13:30–13:40 fog stream into front door. 14:15 left half of living-room window opened. Post-flashover by 12:30. |
| 1.10 | https://www.youtube.com/watch?v=E4sZPCkDoV0 | 2-story | 17:55 | **Exp 2.** 10:00 front door open. 16:05–16:15 straight stream into front door. 18:00 end. |
| 1.11 | https://www.youtube.com/watch?v=02sAaLyv0LI | 1-story | 11:30 | **Exp 3.** 8:00 front door open. 8:15 living-room window open. 10:22–10:32 straight stream into front door. Door plus window near the seat of the fire gives the fastest flashover. |
| 1.12 | https://www.youtube.com/watch?v=UpD6kDtexjI | 2-story | 18:30 | **Exp 4.** 10:00 front door open. 10:15 family-room window open. 17:31–17:41 straight stream into family-room window. |
| 1.13 | https://www.youtube.com/watch?v=1Grn7gLgjHI | 1-story | 12:47 | **Exp 5.** 8:00 living-room window open, with no door open. 11:32–11:42 straight stream into the window. 12:45 end. Shows a window-only flow path, with the window acting as both inlet and outlet across a neutral plane. |
| 1.14 | https://www.youtube.com/watch?v=Q8bd7-biSpk | 2-story | 17:30 | **Exp 6.** 10:02 family-room window open, window only. 16:32–16:42 straight stream. |
| 1.15 | https://www.youtube.com/watch?v=SXQ-C67SqKo | 1-story | 16:42 | **Exp 7.** 8:00 front door open. 8:16 bedroom window open, remote from the fire. 15:46–15:56 straight stream into front door. Venting remote from the fire pulls fire toward the uninvolved bedroom. |
| 1.16 | https://www.youtube.com/watch?v=VJCGiUloxxQ | 2-story | 18:30 | **Exp 8.** 10:00 front door open. 10:15 bedroom window open, remote. 17:32–17:42 straight stream into bedroom window. |
| 1.17 | https://www.youtube.com/watch?v=bAjZYy4C9fc | 1-story | 12:20 | **Exp 9**, a repeat of Exp 3. 8:00 door. 8:15 living-room window. 11:12–11:22 straight stream into the living-room window. |
| 1.18 | https://www.youtube.com/watch?v=1d0kGbIK69Y | 2-story | 25:30 | **Exp 10**, a repeat of Exp 4. 10:00 door. 10:15 family-room window. 24:16–24:26 straight stream. Slow growth after venting because the fire did not spread to the second sofa before going ventilation-limited, which is useful for showing variability. |
| 1.19 | https://www.youtube.com/watch?v=G7ys2JWXKYg | 2-story | 16:30 | **Exp 11**, a repeat of Exp 4. 10:00 door. 10:15 family-room window. 15:17–15:27 straight stream. |
| 1.20 | https://www.youtube.com/watch?v=b7UC2gFmmGI | 1-story | 12:17 | **Exp 12**, a repeat of Exp 3. 8:00 door. 8:15 living-room window. 11:09–11:19 straight stream. |
| 1.21 | https://www.youtube.com/watch?v=DIrrmfS1qTU | 2-story | 15:31 | **Exp 13.** 10:00 door. 10:15 **upper** family-room window. 12:28–12:38 straight stream into the upper window. This is the high-vent case in the "venting high vs. low" comparison. |
| 1.22 | https://www.youtube.com/watch?v=kQ1f5ForroU | 1-story | 14:12 | **Exp 14.** 8:00 door, then the living-room window, master bedroom window and other bedroom windows at 15 s intervals. Multiple openings. |
| 1.23 | https://www.youtube.com/watch?v=KCEiXsMQ0Bc | 2-story | 16:01 | **Exp 15.** 10:00 door, then the living-room, den and family-room windows at 15 s intervals. 14:33 fog stream into the family-room window. Multiple openings. |

**Numbers from the summary paper:**
- **One-story house, before ventilation:** bedroom 2 (remote) reached 350 °C at 2.1 m and then fell to about 150 °C as the fire became ventilation-limited. Across all one-story experiments, temperatures at the moment of venting were within 50 °C of each other.
- **Two-story house, before ventilation:** family room at 4.9 m peaked at 325–450 °C between 450 and 550 s, then declined to 240–310 °C just before the 10:00 vent. **Every experiment peaked and declined before ventilation**, which marks a ventilation-limited fire.
- **High vs. low window (two-story):** the high-window vent reached 950 °C at the ceiling and 650 °C at the floor at about 720 s. The low-window vent reached 800 °C at the ceiling and 500 °C at the floor at about 870 s.
  - Air in low and hot gases out high created flashover conditions even in the two-story volume.
  - The room did not cool after the high vent was opened.
- **Tenability:** heat incapacitated occupants before carbon monoxide did. Occupants standing in the fire room or in open bedrooms were incapacitated before the simulated fire-department arrival.

### 1C. UL FSRI vertical ventilation house experiments (2010 DHS AFG)

**Project:** "Effectiveness of Fire Service Vertical Ventilation and Suppression Tactics"
- Update page: https://fsri.org/research-update/study-effectiveness-fire-service-vertical-ventilation-and-suppression-tactics
- Fire-service summary: https://technicalpanels.fsri.org/docs/2010-DHS-FD-Summary.pdf

**Test conditions:**
- 17 experiments in one-story and two-story houses, varying roof hole size (4x4 ft vs. 4x8 ft), roof hole location, door control, and legacy vs. modern fuel.
- Water was applied for about 15 s at about 100 gpm through a 1¾ in. line.

All of the following are FSRI channel videos (tier A), real-time, with FSRI's timelines.

| # | Link | Length | Scenario and timeline (FSRI timeline) |
|---|------|--------|------------------------------|
| 1.24 | https://www.youtube.com/watch?v=ugnfxm3uEoI | 13:04 | **Exp 1.** 8:00 front door. 8:15 living-room window. 11:10 straight stream into living-room window. 12:20 end. Horizontal baseline. |
| 1.25 | https://www.youtube.com/watch?v=CnOxpZ3LHzQ | 16:01 | **Exp 2.** 10:00 front door. 10:15 family-room window. 14:20 straight stream. 15:40 end. |
| 1.26 | https://www.youtube.com/watch?v=Kl8lcKCA8nE | 18:01 | **Exp 3.** 8:00 front door. 8:15 door partly closed (**door control**). 12:05 4x4 ft roof vent. 15:07 front door opened further, and so on. |
| 1.27 | https://www.youtube.com/watch?v=mf18l4zywkI | 21:01 | **Exp 4.** 10:00 door. 10:15 door partly closed (door control). 13:30 4x4 roof vent. 19:00 door fully opened. |
| 1.28 | https://www.youtube.com/watch?v=8zs1SYrBF2E | 13:16 | **Exp 5.** 8:00 front door. 9:45 4x4 roof vent over the fire. 11:15 straight stream into front door. 12:55 end. The summary uses this experiment to show the short coordination window after a roof vent: its Figs. 4.5–4.7 compare 5 s and 60 s after venting. |
| 1.29 | https://www.youtube.com/watch?v=x5Nekqcu4gQ | 16:01 | **Exp 6.** 10:00 door. 11:45 4x4 roof vent. 13:45 straight stream into door. 15:25 end. |
| 1.30 | https://www.youtube.com/watch?v=9Rq-9EOibeU | 13:30 | **Exp 7.** 8:00 door. 9:35 4x4 roof vent. 9:45 4x8 roof vent added. 11:10 stream. Larger hole. |
| 1.31 | https://www.youtube.com/watch?v=TYKl1DV_iYY | 15:33 | **Exp 8.** 10:00 door. 11:15 4x8 roof vent. 13:00 stream into door. 15:05 end. |
| 1.32 | https://www.youtube.com/watch?v=YpMQap_8WkM | 17:30 | **Exp 9.** 8:00 door. 11:00 4x4 vent, remote from the fire. 15:15 bedroom 1 window. 16:30 end. |
| 1.33 | https://www.youtube.com/watch?v=bW2ohRw-btk | 19:31 | **Exp 10.** 10:00 door. 11:30 4x4 vent, remote. 16:35 bedroom 3 window. 17:30 stream. |
| 1.34 | https://www.youtube.com/watch?v=Y5AkvHUZ7hc | 13:00 | **Exp 11.** 6:00 bedroom 1 window. 8:00 door. 9:20 4x4 vent. 10:45 stream. |
| 1.35 | https://www.youtube.com/watch?v=Wyl7wxMqRoI | 16:31 | **Exp 12.** 8:00 family-room window. 10:00 front door. 12:15 4x4 vent. 14:00 straight stream into family-room window. 16:05 end. |
| 1.36 | https://www.youtube.com/watch?v=G2ySMVzF2wU | 28:00 | **Exp 13.** 10:00 door. 13:45 straight stream into front door. 19:00–19:05 fog stream into front door. 20:00 onward, later actions. Suppression variants. |
| 1.37 | https://www.youtube.com/watch?v=RY1k9S3P-Sc | 15:00 | **Exp 14.** 8:35 bedroom 3 window. 10:00 door. 11:00 4x4 vent. 12:30 stream. The summary's "softening the target" example: exterior water on fire showing from the second floor. |
| 1.38 | https://www.youtube.com/watch?v=K6IjZgvaB7k | 20:30 | **Exp 15.** 6:00 living-room window. 9:30 bedroom 1 window. 10:30 stream into living-room window. 18:00 onward. |
| 1.39 | https://www.youtube.com/watch?v=woyw3d7Dw4g | 36:00 | **Exp 16.** 17:00 family-room window. 21:25 additional family-room ignition. 27:00 bedroom 3 window. 28:30 stream. The long pre-vent times are consistent with the study's legacy-fuel runs; confirm in the report. |
| 1.40 | https://www.youtube.com/watch?v=foPlQQJ3uNE | 36:01 | **Exp 17.** 24:00 front door. 24:15 living-room window. 33:30 straight stream. 35:30 end. Very slow development; likely the legacy-fuel comparison (confirm in the report). |

**Findings from the fire-service summary:**
- **Door control:** pulling the door closed to hose width limits air to a ventilation-limited fire and so limits heat release.
- **Vents and the neutral plane:** once vents open, the neutral plane lifts.
- **Delayed response:** ventilation can look positive at first, but "2 minutes later, conditions could become deadly" without water.
- **Roof vent coordination:** a roof vent without water adds oxygen and increases HRR (a "third growth stage").
- **Exterior water:** 15 s of water through a window or door did not worsen conditions in adjacent spaces.

### 1D. UL FSRI acquired-structure and field series

| # | Link | Source / tier | Length | What it shows / notes |
|---|------|---------------|--------|----------------------|
| 1.41 | https://fsri.org/research/study-coordinated-fire-attack-utilizing-acquired-structures | UL FSRI / A | n/a | Coordinated Fire Attack: 40 live burns in acquired structures. 20 experiments in 8 single-family homes in Sidney and Beavercreek Twp., OH. 13 in four 10-unit garden apartments in Cobb County, GA. 7 in a strip mall in Fairborn, OH. Single-family report: https://d1gi3fvbl0xj2a.cloudfront.net/public/2021-07/Coord_Tactics_Single_Family_Homes.pdf |
| 1.42 | https://www.youtube.com/watch?v=QjxCxLOtZCw | UL FSRI / A | 10:01 | "Coordinated Fire Attack – Single Family Residential Structures": overview with real acquired-house exterior smoke and suppression footage. |
| 1.43 | https://fsri.org/resource/coordinated-fire-attack-project-overview | UL FSRI / A | n/a | Project overview video page. Also on Vimeo: https://vimeo.com/388163366 |
| 1.44 | https://www.youtube.com/watch?v=xcdCkfa21GI | Ed Hartin re-upload of FSRI multi-family footage / B | 2:32 | "UL FSRI Multi-Family Experiment 3B": exterior fire control before moving interior. |
| 1.45 | https://fsri.org/research/governors-island-experiments | UL FSRI / A | n/a | Governors Island, NY (2012): FDNY, NIST and UL burned 20 brick-veneer townhouses to study flow path and door control. |
| 1.46 | https://www.youtube.com/watch?v=NCtBkLSKdX0 | UL FSRI / A | 8:27 | "Fire Dynamics in the 21st Century": award-winning Governors Island summary with exterior smoke and flame-out footage. |
| 1.47 | https://www.youtube.com/watch?v=QAAA7ObJ0lU | UL FSRI / A | 18:20 | "Governors Island: Exp 1 – Door Control", full experiment. |
| 1.48 | https://www.youtube.com/watch?v=50BDdHMMbW0 | UL FSRI / A | 21:51 | "Governors Island: Exp 3 – Door Control", full experiment. |
| 1.49 | https://www.youtube.com/watch?v=67Bv0IhGQhc | UL FSRI / A | 19:21 | "Governors Island: Exp 4 – Basement Fire", full experiment. |
| 1.50 | https://www.youtube.com/watch?v=2cy8DbVR3I8 | UL FSRI / A | 17:20 | "Governors Island: Exp 6 – Basement Fire", full experiment. |
| 1.51 | https://www.youtube.com/watch?v=axWirHFKD8U | FDNY official / A | 3:11 | "FDNY Tests Fire Science and Firefighter Procedures" (Governors Island). Description: flashover took about 20 min in the 1980s and about 4 min now, and temperatures can rise from 250 to 1,500 °F in about 10 s. |
| 1.52 | https://fsri.org/research/study-fire-service-residential-home-size-and-search-rescue-operations | UL FSRI / A | n/a | Size-up and search & rescue study: open-plan houses with sofa ignition and window or door search tactics. |
| 1.53 | https://www.youtube.com/watch?v=FflxmqW6Hjg | UL FSRI / A | 1:13:05 | "Residential Size-Up and Search & Rescue: Virtual Visitor Day". Livestream of a full experiment: sofa ignition in an open kitchen and living room, through flashover, to window-initiated search. Long, but complete real-time footage. |
| 1.54 | https://www.youtube.com/watch?v=zRZeFAl5Xzg | UL FSRI / A | 3:50 | "Fire Attack Full-Scale Experiments". 2013 DHS study of interior vs. transitional attack: more than 30 experiments in two 1,600 ft² modern ranch houses with more than 250 instruments per house. Co-authors include Zevotek and Weinschenk. |

**Spartanburg and Chicago: what the sources show**
- **Spartanburg, SC** was a **NIST + ISFSI** series, not UL FSRI.
  - January 2013: eight abandoned houses burned to study ventilation and isolation as ways to delay flashover.
  - May 2014: transitional (exterior) attack burns.
- **Chicago:** I found no UL FSRI acquired-structure *residential* series in Chicago. Chicago FD's research role was:
  - the NIST positive pressure ventilation (PPV) tests in a 16-story Chicago apartment building (1.60);
  - a technical-panel seat on the Coordinated Fire Attack project.
- **Cook County:** the NIST Cook County work was a 2003 high-rise *office* fire investigation, not residential.
- **Dallas:** I could not confirm any NIST Dallas residential test series.

| # | Link | Source / tier | Length | What it shows / notes |
|---|------|---------------|--------|----------------------|
| 1.55 | https://www.nist.gov/node/399111 | NIST / A | n/a | NIST photo and news page for the 2013 Spartanburg live burns in abandoned houses (NIST, ISFSI, SC Fire Academy, Spartanburg FD). |
| 1.56 | https://www.firehouse.com/operations-training/news/10859817/sc-study-may-change-the-way-firefighters-do-business | Firehouse / B | n/a | News report on the Spartanburg series: ventilation, isolation and exterior attack. |
| 1.57 | https://www.youtube.com/watch?v=U4qfVk5hZKk | John Buckman (ISFSI past president) / B | 3:36 | "Spartanburg Burn ISFSI NIST Day 4": exterior and acquired-house footage from the series. |
| 1.58 | https://www.youtube.com/watch?v=IqOZLRiSVeo | Firehouse (NIST footage) / B | 2:04 | "NIST: Nothing Showing Means Nothing": acquired structure with modern furnishings. **Key reference for the "nothing showing" exterior.** Publisher description: 3:31 free-burning, with smoke showing. 4:02 ventilation-limited, with pressure dropping and little or no smoke visible outside. 5:08 front door forced, followed by a rush of air, return to free burning, and flashover. |
| 1.59 | https://www.youtube.com/watch?v=dag7nDCgHLY | Firehouse (NIST acquired-structure footage) / B | 2:20 | "NIST: Hose Stream Comparison": second-floor fire, indirect attack with a straight stream vs. a fog stream. Description: the straight stream off the ceiling caused little movement of heat into the stairwell. The fog stream entrained air and drove a wave of heat and fire gas into the stairwell. |

### 1E. NIST experiments and videos

| # | Link | Source / tier | Length | What it shows / notes |
|---|------|---------------|--------|----------------------|
| 1.60 | https://www.youtube.com/watch?v=4hKw9Q0OXf0 | NIST official / A | 26:53 | "NIST Fire Research: Positive Pressure Ventilation for the Fire Service" (Madrzykowski and Kerber). Experiments in a three-story training building, a 30-story high-rise in Toledo and a 16-story apartment building in Chicago. Includes smoke-flow footage. |
| 1.61 | https://www.youtube.com/watch?v=R3xPHt_WZTk | NIST official / A | 22:41 | "Examining Fire Fighting Tactics under Wind-Driven Conditions (Pt. 3)". Madrzykowski presents multi-camera, **including infrared**, views of lab apartment fires. Report: NIST TN 1618, https://nvlpubs.nist.gov/nistpubs/Legacy/TN/nbstechnicalnote1618.pdf |
| 1.62 | https://www.nist.gov/el/fire-research-division-73300/firegov-fire-service/videos-wind-driven-fires-governors-island | NIST / A | 0:31–1:30 clips | NIST wind-driven fire videos: Governors Island apartments with 9–11 m/s simulated wind, plus lab tests. Lab Test 1 notes hot gas reaching the corridor at 268 s, Test 2 at 169 s and Test 8 at 145 s. The 7-story building report is TN 1629: https://nvlpubs.nist.gov/nistpubs/Legacy/TN/nbstechnicalnote1629.pdf |
| 1.63 | https://www.youtube.com/watch?v=SmM1towHk78 | uploaded by Dan Madrzykowski (NIST/FSRI) / A | 11:02 | "Flashover.mpg": three NIST flashover experiments. (1) A closed multi-room structure at Ft. Indiantown Gap, PA: **the fire runs short of oxygen**, the ventilation-limited signature. (2) A 12x12x8 ft room with one open doorway, burning post-flashover. (3) An open office of 16x20x9.5 ft, showing radiant ignition below the hot layer. |
| 1.64 | https://www.youtube.com/watch?v=w4W82HIzUcc | NIST video re-uploaded by AKFireTraining / B | 4:10 | "NIST Flashover": a dry Scotch pine Christmas tree, a **living room with sofa**, and an office cubicle. |
| 1.65 | https://www.youtube.com/watch?v=IKel2MdPn58 | NIST video re-uploaded / C | 2:46 | "NIST Video illustrating Fire Progression to Flash Over". |
| 1.66 | https://www.youtube.com/watch?v=xIIhKugeYAY | NIST video re-uploaded (the original was at nist.gov/fire/upload/NS_multi.wmv) / C | 1:32 | NIST compartment fire flashover. |
| 1.67 | https://www.youtube.com/watch?v=26A-49Wb2F4 | NIST official / A | 2:17 | "Christmas Tree Fire: Watered Tree vs. Dry Tree". The dry tree's very fast growth and room flashover is the extreme fast-fuel case. |
| 1.68 | https://www.youtube.com/watch?v=pgDbsv62cu8 | NIST official / A | 10:53 | FDS reconstruction of the June 2011 San Francisco hillside house fire (2 line-of-duty deaths, LODD). A wind-driven flow path visualization. |
| 1.69 | https://www.youtube.com/watch?v=ts41eXCopzs | NIST official / A | 38:48 | Charleston Sofa Super Store fire news briefing (2007, 9 LODD). Covers furniture fuel load, rapid spread and window failure. Commercial, but the furniture fire dynamics are relevant. |
| 1.70 | https://www.youtube.com/watch?v=oUWtwjgl8kU | IAFF-TV lecture re-upload (Madrzykowski, NIST) / B | 19:41 | "Part 6 of 7: Live Fire Tests: NIST & UL Research on Fire Behavior". Related parts: Part 2, the modern fire environment (Kerber), https://www.youtube.com/watch?v=FR4FZUkzKHM (30:19); Part 4, case studies, https://www.youtube.com/watch?v=6EZvGqf-wD0 (31:31). |
| 1.71 | https://www.nist.gov/video/upholstered-furniture-fires-untreated-vs-back-coated-fabric | NIST NFRL / A | n/a | Upholstered furniture fire, untreated vs. back-coated fabric. |
| 1.72 | https://www.nist.gov/video/seeing-through-fire-nist-blue-light-imaging-method-vs-normal-illumination | NIST NFRL / A | n/a | Blue-light imaging vs. normal light through flame. Shows how strongly flame luminosity hides geometry. |
| 1.73 | https://www.nist.gov/video/how-nist-captures-great-fire-video-footage-0 | NIST / A | n/a | How NIST films fires: exposure and camera setup. Useful for matching simulator tone-mapping to research footage. |

**Related NIST reports:**
- NIST TN 1953, propane-fueled ventilation experiments (horizontal, vertical, PPV): https://nvlpubs.nist.gov/nistpubs/TechnicalNotes/NIST.TN.1953.pdf
- NISTIR 7315, PPV on a two-story colonial house fire (Kerber): https://nvlpubs.nist.gov/nistpubs/Legacy/IR/nistir7315.pdf
- "Wind Driven Fire Research: Hazard and Tactics" (Madrzykowski and Kerber): https://www.nist.gov/publications/wind-driven-fire-research-hazard-and-tactics

### 1F. NIST Fire Calorimetry Database: single-item burns

**Project:** "Design Fires – Residential and Office Items", 51 experiments with HRR data and a still at ignition. The listing says the fires were recorded on video; open each test page for available media.
- Listing: https://www.nist.gov/el/fcd/design-fires-residential-and-office-items
- Peak HRRs below come from the listing. Base URL for test pages is the listing URL plus `/testNNN`.
- **Trash bins and waste containers are not in this project.** Search https://www.nist.gov/el/fcd/experiments for them.

| # | Link | Item | Peak HRR |
|---|------|------|----------|
| 1.74 | https://www.nist.gov/el/fcd/design-fires-residential-and-office-items/test036 | Sofa B (loveseat with chaise) plus 2 pillows | **5,153 kW** |
| 1.75 | https://www.nist.gov/el/fcd/design-fires-residential-and-office-items/test034 | Sofa C plus 2 pillows | 4,021 kW |
| 1.76 | https://www.nist.gov/el/fcd/design-fires-residential-and-office-items/test029 | Sofa A (loveseat) | 1,984 kW |
| 1.77 | https://www.nist.gov/el/fcd/design-fires-residential-and-office-items/test044 | 12 in. polyurethane foam mattress, no fire barrier, plus pillows | **7,148 kW** |
| 1.78 | https://www.nist.gov/el/fcd/design-fires-residential-and-office-items/test038 | 12 in. polyurethane foam mattress with pillows and bedding | 4,563 kW |
| 1.79 | https://www.nist.gov/el/fcd/design-fires-residential-and-office-items/test039 | 6 in. spring mattress with bedding | 189 kW. Shows how much the bed type matters. |
| 1.80 | https://www.nist.gov/el/fcd/design-fires-residential-and-office-items/test042 | 12 in. foam mattress with fire-retardant cotton scrim | 34 kW. The barrier nearly prevents spread. |
| 1.81 | https://www.nist.gov/el/fcd/design-fires-residential-and-office-items/test027 | Arm chair H | 1,733 kW |
| 1.82 | https://www.nist.gov/el/fcd/design-fires-residential-and-office-items/test022 | Arm chair E | 421 kW |
| 1.83 | https://www.nist.gov/el/fcd/design-fires-residential-and-office-items/test046 | Office chair J | 563 kW |
| 1.84 | https://www.nist.gov/el/fcd/design-fires-residential-and-office-items/test010 | Four D pillows | 192 kW. Pillow tests (Test001–013) range from 20 to 101 kW. Useful for the incipient stage. |
| 1.85 | https://www.nist.gov/el/fcd/fire-barriers-full-scale-chair-mock-ups | Chair mock-ups with fire barriers (11 experiments) | Video with data overlay. Barriers cut peak HRR by up to 64% and delayed the peak by up to 19 min. |
| 1.86 | https://www.nist.gov/programs-projects/functional-barriers-low-flammability-products/low-heat-release-upholstered/full | NIST low-HRR upholstered furniture | Three TB-117 sofas burned in a 3.7 x 3.7 x 2.4 m furnished room (polyester, barrier and cotton covers), with **360° video**. Data DOI: https://doi.org/10.18434/mds2-2314 |

---

## 2. Smoke and fire by stage

### 2.1 Incipient: light white or grey pyrolysis smoke, laminar and lazy

- **Look:** thin white to light-grey smoke. The plume is laminar and rises slowly from a small flame.
  - Modern foam produces visible grey-black smoke within about 60–120 s.
  - Legacy cotton produces clean flame with little smoke for many minutes.
- **References:**
  - 1.4 (Firehouse/UL): legacy fuels "open flame with limited smoke" early on, while the modern room shows a ceiling smoke layer within 2:00.
  - 1.1 (FSRI 2020): first 0:00–1:00 of each side, candle flame spreading to the throw blanket and cushion.
  - NIST FCD pillow tests (1.84), 20–190 kW: small single-item plumes.
  - Kerber Exp 1: slow growth for the first minute as the candle reached the polyester throw and sofa cushion.

### 2.2 Growth: plume to ceiling, ceiling jet, hot layer forming

- **Look:** the flame reaches the ceiling and the ceiling jet spreads radially. A defined hot smoke layer forms in the top 0.6–0.9 m, and the flame tilts toward the opening.
- **References:**
  - Kerber Exp 1: at 2:00 a layer had formed "in the top two to three feet of the room". At 3:00 about half the sofa was involved.
  - 1.63 (NIST Flashover.mpg), office experiment: radiant ignition of materials below the hot gas layer.
  - FSRI horizontal-ventilation videos (1.9–1.23), roughly 0:00–4:00 of each. Interior cameras show layer formation.

### 2.3 Smoke layer descent and banking down

- **Look:** the layer interface drops toward the floor and visibility at standing height is lost. In closed houses the smoke fills the whole volume.
- **References:**
  - FSRI horizontal 1-story experiments (1.9, 1.11, 1.13), about 3:00–8:00 before the 8:00 vent.
  - 1.53, the size-up livestream: long real-time view of an open-plan house filling.
  - 1.74–1.86, FCD room burns: note the layer depth vs. HRR.

### 2.4 Rollover and flameover

- **Look:** flame fingers run along the underside of the smoke layer ahead of the fire, often in waves toward the opening or down a hallway.
- **References:**
  - Kerber Exp 3: flames came out the top of the opening at 4:20, before flashover at 4:45. That is the rollover-to-flashover interval.
  - https://www.youtube.com/watch?v=aV2bjCdqyTY (Fire Cam vendor, tier C, 0:57): "rollover down the hallway" during a vacant-building training fire. Real helmet-camera perspective.
  - https://www.youtube.com/watch?v=__6pEcFWSPI (Fireside Training, tier C, 0:26): "Turbulent Smoke Transitioning to Flashover".
  - https://www.youtube.com/watch?v=3jxwEocR1eA ("Why It Spiked", tier C commentary over NIST footage, 4:30): annotates the hot gas layer, rollover and transition in NIST's furnished-room test. Use it as a visual guide only.

### 2.5 Flashover

- **Physics:** the hot layer reaches about 600 °C and the heat flux at the floor about 20 kW/m², which are the classic criteria. Every exposed fuel ignites almost at once, and flame fills the doorway or window.
- **Timing:**
  - Modern contents: 3:20–4:50 from ignition with an open front (Kerber; FSRI 2020).
  - Legacy contents: 29:30 to more than 30 min.
- **References:**
  - 1.1–1.8, side-by-side burns. Kerber's flashover indicator was ignition of the flooring just inside the opening from flames exiting the top.
  - 1.63–1.67, NIST flashover compilations and the Christmas tree.
  - https://www.youtube.com/watch?v=BtMmymOxdjc (Oak Ridge FD, tier A department, 5:27): flashover demonstration burn.
  - https://www.youtube.com/watch?v=HoXigbw1jEo (Insight Fire Training / Kill the Flashover 2024, tier B, 2:41): 10x10 ft drywall room with one couch, ignition to full room involvement in 3:15. Filmed with a GoPro and a **Bullard QXT thermal imaging camera (TIC)**.
  - https://www.youtube.com/watch?v=k-3UCGGizgc (Fire Hobbyist, tier C, 0:39): flashover in an acquired-structure training burn in Raleigh, NC.
  - https://www.youtube.com/watch?v=OEz70hoxMsI (NYU Fire Research, tier B, 1:05) and https://www.youtube.com/watch?v=xh3XIIyxGJ0 ("Pyrolysis & Flashover", 0:51).

### 2.6 Ventilation-limited fire: dark, turbulent, pulsing smoke from openings

- **Look:** after the fire consumes the room's oxygen, flaming shrinks or disappears and temperatures *drop*. Dense dark smoke pushes from cracks, and openings pulse as the fire "breathes".
  - From outside, the house can look calm ("nothing showing").
  - With a crack open: black, turbulent, high-velocity smoke, Dodson's "black fire".
- **References:**
  - 1.58 (NIST: Nothing Showing Means Nothing), 4:02: ventilation-limited, with little smoke showing.
  - 1.63 (Madrzykowski Flashover.mpg), experiment 1: the fire runs short of oxygen in a closed structure.
  - All FSRI horizontal 2-story videos, about 7:30–10:00: temperatures peaked at 450–550 s and declined before the 10:00 vent.
  - https://www.youtube.com/watch?v=tGTESi1ejA0 (FSRI Tactical Consideration, Kerber, tier A, 2:46): "Fire Development Changes When a Fire Becomes Ventilation Limited".
  - https://www.youtube.com/watch?v=WcmWqz64CzI (FSRI, tier A, 2:19): "Nothing Showing Means Nothing" (C. Bailey). Page: https://fsri.org/resource/tactical-considerations-ep-6-nothing-showing-means-nothing
  - https://www.youtube.com/watch?v=VFo6ATQhJZI (LACoFD, tier A department, 4:39): reading smoke from a house fire. Tailboard Talk segment at 1:31–5:54.
  - https://www.youtube.com/watch?v=5SR6GC-kR8I (UL Xplorlabs, tier A, 0:21): short "Fire Behavior: Ventilation Limited Fire" clip.
  - https://www.youtube.com/watch?v=BrkKkIZenBQ (UL-FSRI and NIST "Ventilation Limited Fires" video, re-upload). Found in search; not metadata-checked.

### 2.7 Flow paths and the neutral plane at doors and windows

- **Look:** at a single opening, smoke exits the upper part and fresh air enters the lower part. The boundary between them is the neutral plane.
  - With an inlet and an outlet, a unidirectional flow path forms. Smoke at the outlet accelerates and darkens.
  - When vents open, the neutral plane lifts (FSRI vertical summary).
- **References:**
  - 1.13 (Horizontal Exp 5) and 1.14 (Exp 6): window-only vent, both in and out at one opening.
  - 1.11 (Exp 3) vs. 1.15 (Exp 7): vent near the seat of the fire vs. remote.
  - 1.21 (Exp 13): high exhaust window.
  - https://www.youtube.com/watch?v=O8s3Q57TZtE (FSRI, tier A, 2:56): "Fire Flows from High Pressure to Low Pressure" (Kerber).
  - https://www.youtube.com/watch?v=7EKBYtRQSW4 (FSRI, tier A, 4:00): "Understanding Flow Paths: Low Intake, High Exhaust". Includes a handout.
  - https://www.youtube.com/watch?v=0P7xDwdwmHc (FSRI, tier A, 4:13): "Forcing the Front Door Must be Thought of as Ventilation".
  - https://www.youtube.com/watch?v=B7k7_F2lowg (FSRI, tier A, 1:59): "Flow Path and Suppression Must be Considered Together" (Zevotek).
  - https://www.youtube.com/watch?v=2yX9hcdR5cI (FSRI, tier A, 1:15): door control experiment.
  - https://www.youtube.com/watch?v=n26mPCY5xEM (FSRI, tier A, 2:41): VEIS. An isolated room with the door closed vs. an open room in the flow path; conditions worsen as soon as the glass breaks.
  - https://www.youtube.com/watch?v=bSP03BE74WA (FSRI, tier A, 5:45): "See the Dramatic Difference a Door Can Make", open vs. closed bedroom.
  - https://www.youtube.com/watch?v=Nu5ICj3LwqE (FSRI, tier A, 3:20): Close Before You Doze live demonstration.
  - https://www.youtube.com/watch?v=lileW0dSJa8 (FSRI, tier A, 0:14): "900 Degree Difference", the temperature behind a closed vs. open door.
  - https://www.youtube.com/watch?v=IbMCRldLu5M (Utah Fire and Rescue Academy, tier A, 4:16): animated flow path.
  - https://www.youtube.com/watch?v=9mhAuok9M_s (Insight Fire Training / Kill the Flashover 2011, tier B, 1:32): door control research burn in a 12x12 bedroom with couch, chair, pallet and straw.
  - https://www.youtube.com/watch?v=uYD8GQmwvcg (Kill the Flashover, tier B, 2:00): TIC view of a door closing that slows growth. https://www.youtube.com/watch?v=gocg2gyUUas (0:52): the same project.

### 2.8 Backdraft and smoke explosion

- **Distinction:**
  - **Backdraft:** air enters an underventilated, fuel-rich, *hot* space and the gases ignite explosively. Warning signs: smoke drawn *back in* at gaps, pulsing, yellow-grey smoke, hot sooty windows, little visible flame.
  - **Smoke explosion** (fire gas ignition): pre-mixed, *cooler* smoke reaches an ignition source, often in voids or remote rooms.
- **References:**
  - https://www.youtube.com/watch?v=WVkiOYvgqFg (NIST official, tier A, 2:37): "Predicting Deadly Backdrafts". Hundreds of NIST experiments on backdraft conditions feeding a machine-learning model.
  - https://www.youtube.com/watch?v=q_7dhW0dmiE (FSRI, tier A, 1:26): FDIC 2025 session invite on smoke explosions, backdrafts and overpressure events (Madrzykowski, Fleischmann, FDNY's Ceriello). Points to current FSRI work.
  - https://www.youtube.com/watch?v=Gqs5HlIxT_w (Chatham County FD official, tier A, 1:07): **real smoke explosion** shortly after interior entry, Aug 2, 2024. Description: black, fuel-rich smoke mixing with air and igniting.
  - https://www.youtube.com/watch?v=UpmUifxQKmw ("Virginia Fire Department" channel, likely Virginia, MN; tier A–B, 0:37): real smoke explosion at a structure fire, Dec 19, 2016.
  - https://www.youtube.com/watch?v=D171Oyttcow (tcassin, tier C, 1:38): smoke explosion at a Waterbury, CT structure fire.
  - https://www.youtube.com/watch?v=kM7pRyEJqes (Elia Clemente, tier C, 0:44): Daniel's Block fire. The storefront glass "dropped out lazily", smoke poured out, and a **real backdraft** followed in a mixed-use building.
  - https://www.youtube.com/watch?v=SnUA04wyHg4 (TheDailyShare / HLN, tier C, 0:58): real house backdraft caught on camera.
  - https://www.youtube.com/watch?v=gVWAwWjw4LU (CBS New York, tier B news, 0:21): firefighter blown back by a backdraft.
  - https://www.youtube.com/watch?v=YQHwcFr4myc (tier C, 0:31): Orange, CA FD transitional-attack training with a smoke explosion (2015).
  - Training props, which show clean backdraft mechanics but not a house:
    - https://www.youtube.com/watch?v=KjAXtsQI_kc (CFBT International, 4:19): Gyeonggi Fire Academy, South Korea.
    - https://www.youtube.com/watch?v=8LxnvlV2URk (0:37): slow-motion backdraft in a wood-fiber box, Netherlands academy. Shows pressure build-up, then ignition.
    - https://www.youtube.com/watch?v=ZyCCWuO0mQo (The Slow Mo Guys, 11:13): 4K slow-motion backdraft with a fire-service prop. Good for fireball shape and timing.
    - https://www.youtube.com/watch?v=e6m8d4VyOTY (0:11): unexpected backdraft in training, South Tyrol.
    - https://www.youtube.com/watch?v=V2tcFCRLrac (0:13): Birmingham Airport backdraft demonstration.
  - https://www.youtube.com/watch?v=S6SrLYLe1dA (Matt Davis, tier B–C education, 10:28): intro to smoke explosions and fire gas ignitions.

### 2.9 After-ventilation flaring: ventilation-induced flashover

- **Look:** after a door or window opens, the fire does *not* respond at once.
  - There is a lag of 1–3 min.
  - The neutral plane rises at first, then the smoke at the opening turns dark and turbulent, and flame appears at the top of the opening.
  - Flashover follows, with flame venting from the window or door.
- **Measured lags:**
  - 2:15 (modern) vs. 8:30 (legacy) after venting (1.4).
  - FSRI: "2 minutes later conditions could become deadly".
- **References:**
  - All horizontal experiment videos (1.9–1.23) from the vent time (8:00 or 10:00) to the water time. The straight stream at the end shows the knockdown look.
  - 1.58 from 5:08: door forced, then flashover.
  - https://www.youtube.com/watch?v=rD-ZdUvMxCw (tier C, 0:14): a fireball erupts from a roof vent hole as it is cut, at a burn-to-learn.
  - https://www.youtube.com/watch?v=AwVvvK2oL3M ("PPV Flashover"). Found in search; not metadata-checked.

### 2.10 Attic, void-space and balloon-frame fires

- **Look:**
  - Smoke pushes from soffits, eaves, gable vents and ridge, often brown to tan (wood pyrolysis) and pressurized.
  - In legacy balloon frame, smoke can appear at the eaves and attic far from a basement or first-floor origin, through open stud channels.
- **Gap:** I found no instrumented experiment specific to balloon frame. The closest are the FSRI attic and knee-wall void experiments, including the Milwaukee acquired-house field tests.
- **References:**
  - FSRI attic final report: https://technicalpanels.fsri.org/docs/Attic-Final-Report-Online.pdf
    - 32 wall tests, 3 eave/wall tests, 8 full-scale attic tests (four 36 x 30 x 8 ft attics).
    - 3 knee-wall field experiments in Milwaukee acquired houses, examining spread through knee walls and the voids above collar ties.
  - https://fsri.org/research-update/attic-fire-experiments-northbrook-il and https://fsri.org/research-update/attic-fire-experiments-visitor-days (FSRI update pages).
  - https://www.youtube.com/watch?v=K8pGUULE3Xc (FSRI, tier A, 7:23): "DHS 2011 Eave Experiment Comparison": three wall and attic constructions exposed to a 100 kW fire.
  - https://www.youtube.com/watch?v=h10CCxkYOVE (FSRI, tier A, 31:37): "Attic Fires: Wall Experiment 1", full real-time burn.
  - https://www.youtube.com/watch?v=2g23gBer1T0 (Fire Engineering, tier B, 6:46): UL attic burn test, Part 1.
  - https://www.youtube.com/watch?v=6RxOoP9OT-I (Bobby Halton, Fire Engineering editor, tier B, 51:49): UL Attic Test 2, full length.
  - https://www.youtube.com/watch?v=0pGD18Ivg78 (FSRI, tier A, 3:02): "Get Water in the Eaves for Attic Fires".
  - https://www.youtube.com/watch?v=zdBDMFHsdnA (FSRI, tier A, 0:54): attic tactic of opening a large hole below the attic.
  - Basement fires, the related below-grade flow path:
    - https://www.youtube.com/watch?v=1_gohY3-O6Q (FSRI, 8:16): wind-driven basement fire that injured 7 Prince George's County firefighters in 2012.
    - https://www.youtube.com/watch?v=3YiLLsWuOxg (FSRI, 3:11): basement flow path exhaust.
    - https://www.youtube.com/watch?v=RNt7DF3vlRw (ISFSI, 14:59): full basement fire training video.

---

## 3. Fire behavior numbers the simulator must reproduce

| Behavior | Measured value | Source |
|----------|----------------|--------|
| Time to flashover, modern living room (open front, candle on sofa) | 3:20, 3:40, 4:45 (three tests); all under 5 min | Kerber 2012 (1A) |
| Time to flashover, legacy living room | 29:30, 34:15; one never flashed over (extinguished at 30:00); at least 700% longer | Kerber 2012 |
| 2020 repeat, synthetic vs. natural room | 4:50 vs. more than 30 min | FSRI project page (1.1) |
| Modern room smoke layer onset | visible ceiling layer within 2:00; layer in the top 2–3 ft at 2:00 | 1.4; Kerber Exp 1 |
| Ventilation-limited onset in closed rooms or houses | about 5:00 modern vs. 20:00 legacy (UL compartment); two-story house peaked at 450–550 s, then declined | 1.4; FSRI horizontal paper |
| Temperatures in a ventilation-limited house before venting | family room at 4.9 m: 325–450 °C peak, 240–310 °C at vent time; remote bedroom 350 °C, falling to about 150 °C | FSRI horizontal paper |
| Flashover after ventilation | 2:15 modern vs. 8:30 legacy (UL compartment); FSRI says about 2 min to deadly conditions without water | 1.4; FSRI vertical summary |
| High vs. low ventilation (two-story) | high window: 950 °C ceiling / 650 °C floor at about 720 s; low window: 800 / 500 °C at about 870 s | FSRI horizontal paper |
| Peak post-flashover temperatures | 800–950 °C at the ceiling and 500–650 °C at the floor in the two-story ventilation cases (above); FDNY quotes a rise from 250 to 1,500 °F in about 10 s. For per-location peaks, read the temperature plots in the 2010 report (6.3); I did not extract them. | FSRI horizontal paper; 1.51 |
| Window failure (furnace exposure) | modern double-glazed: 254, 259, 270, 312 s; legacy single-glazed: 577, 846 s. In room fires the modern panes failed at 3:39–5:55 and the legacy at 6:34–16:00. Failure means 25% or more of the glass area open. | Kerber 2012, Table 5 |
| Interior door burn-through (well-ventilated fire) | hollow oak 5:12, hollow composite 5:15, solid 6-panel 5:02 (through the thin panels); about 5 min of containment | Kerber 2012, Table 6 |
| Floor collapse (unprotected, furnace) | engineered I-joist 6:00 vs. 2x10 dimensional lumber 18:35; protected: 26:43 vs. 79:00 | Kerber 2012 |
| Closed vs. open bedroom door | about a "900 degree" (°F) difference | FSRI (2.7) |
| Item peak HRR | sofa 2.0–5.2 MW; foam mattress 0.9–7.1 MW; spring mattress about 0.2 MW; arm chair 0.4–1.7 MW; office chair about 0.56 MW; pillow 20–190 kW. A commonly cited flashover threshold for a typical room is about 1 MW. | NIST FCD (1F) |
| Door control | partly closing the inlet door slows growth in proportion to the area closed | FSRI vertical summary; 1.26, 1.27 |
| Hot gas reaching the corridor in wind-driven apartment tests | 145–268 s depending on configuration; wind 9–11 m/s | NIST wind-driven page (1.62) |

**Flame from windows:**
- After flashover, burning moves to the vents, because the interior is too fuel-rich to burn.
- In the FSRI vertical summary's flashover stage, flames extend out of the doorway while the inside of the house is fuel-rich.
- **The simulator should place the brightest flame outside the opening, with the interior dark and smoke-logged.**
- Visual references: 1.9–1.23 after vent; 1.46; 1.51.

---

## 4. Helmet-camera and body-camera footage from real fires

**Caveats:**
- No research agency publishes a large public library of real-incident helmet-cam footage.
- Most of the footage below is posted by firefighters, departments or news outlets. The tier is marked on each entry.
- Notes on visibility come from the uploader descriptions. Check them by eye.
- For *calibrated* interior views, prefer the FSRI and NIST research videos with interior cameras and infrared (1.61, 1.62, FSRI course videos).

| # | Link | Source / tier | Length | What is visible (per description) |
|---|------|---------------|--------|----------------------------------|
| 4.1 | https://www.youtube.com/watch?v=Cd43nNGvh00 | WAVY TV 10 (news) / B | 10:04 | Raw helmet-cam of an interior attack at a Smithfield, VA house fire caused by lightning. |
| 4.2 | https://www.youtube.com/watch?v=ACAjq9loXwA | SBCityFire (San Bernardino City FD channel) / A–B | 5:36 | Captain's helmet cam on an interior attack with a **partial roof collapse**. |
| 4.3 | https://www.youtube.com/watch?v=PPJNacsm2zw | Tri-State Fire / B–C | 10:30 | Barboursville, WV, 2022: heavy smoke and flames showing, then **high heat and zero visibility** inside; rapid knockdown. |
| 4.4 | https://www.youtube.com/watch?v=BAUdPazli-A | DeKalb Twenty (DeKalb County Fire Rescue company / Local 1492) / B–C | 11:39 | Interior attack with a brief **Mayday** for a lost nozzleman; fire pushed back. |
| 4.5 | https://www.youtube.com/watch?v=-mdLv67Nips | Firefighter Activity / C | 15:01 | Freeport, NY firefighter's helmet cam at a house fire. |
| 4.6 | https://www.youtube.com/watch?v=yxPZoMGpMOM | Willows Volunteer Fire Department / A | 4:50 | Helmet cam inside a training burn in a condemned house: progression and interior view. |
| 4.7 | https://www.youtube.com/watch?v=-6DQtLj-wOU | Sean Weber (first-due firefighter) / C | 4:58 | First-due engine POV: heavy fire from the front A/B corner and quick knockdown. |
| 4.8 | https://www.youtube.com/watch?v=tu56gUKwQq0 | Firefighter Activity / C | 13:30 | Highland Park FD house fire, FireCam footage. |
| 4.9 | https://www.youtube.com/watch?v=naH6zrxToG4 | BigD3243 / C | 4:15 | Dallas FD Station 5 interior attack, fire cam. |
| 4.10 | https://www.youtube.com/watch?v=F9HC8wPE7sc | Willie Townsend / C | 11:57 | Interior attack on a kitchen fire: high heat and zero visibility. |
| 4.11 | https://www.youtube.com/watch?v=aV2bjCdqyTY | Fire Cam (vendor) / C | 0:57 | Rollover down a hallway, training fire (also 2.4). |
| 4.12 | https://www.youtube.com/watch?v=G-VZrhxL9jA | Fire Cam (vendor) / C | 8:10 | Fully involved vacant house with an exposure problem; exterior helmet cam. |

**Thermal imaging camera references**

| # | Link | Source / tier | Length | What it shows |
|---|------|---------------|--------|---------------|
| 4.13 | https://www.youtube.com/watch?v=W1jUGxLjgak | Kill the Flashover 2013 (SC Fire Academy) / B | 4:00 | Fire-room growth seen through **two ISG E-380 TICs**: one couch burning, one couch exposed. The uploader says to watch the carpet closely toward the end. |
| 4.14 | https://www.youtube.com/watch?v=5Rc6h3kuPFg | Kill the Flashover 2012 Burn 4 / B | 4:33 | Interior TIC view of the burn and attack. The companion visible-light camera is https://www.youtube.com/watch?v=MfFmthBxW4c (4:16). |
| 4.15 | https://www.youtube.com/watch?v=x33WN3m6JNc | Kill the Flashover 2012 Burn 2 / B | 4:13 | TIC view of an exterior-window attack on a kitchen couch fire. |
| 4.16 | https://www.youtube.com/watch?v=gMwGmlzRYxg | Kill the Flashover 2015 Burn 6-2 / B | 8:01 | **Quad view**: two TIC channels, one interior visible camera and one exterior camera, synchronized. Ideal for building simulator split views. |
| 4.17 | https://www.youtube.com/watch?v=NBYLefIXkLQ | Insight Fire Training (Andy Starnes) / B | 1:27 | How to scan with a TIC. |
| 4.18 | https://www.youtube.com/watch?v=jR0Jxro6WmI | Insight Fire Training / B | 3:25 | "Thermal Imaging Camera Not a Thermometer": the limits of the spot temperature. |
| 4.19 | https://www.youtube.com/watch?v=u2XBMZ19WHM | Insight Fire Training / B | 3:11 | The case for TIC use, with LODD context. |
| 4.20 | https://www.youtube.com/watch?v=O1XnGdgQ89A | NLFR Training Bureau (fire department) / A | 1:40 | TIC basics and limitations demonstration. |

**What the simulator's TIC mode must show.** This is standard TIC behavior, consistent with the references above:
- **Sees through smoke, not through barriers.** It sees through smoke (long-wave IR) but not through glass, water films or walls.
- **Surface temperature, not air temperature.** The hot ceiling and layer show bright white, or in color mode yellow, orange or red above the camera's thresholds.
- **Thermal layering.** A cool floor appears dark, which shows the layer.
- **Gain switch.** The camera switches to low-sensitivity mode at about 150 °C or more, and image contrast drops.
- **Saturation.** Flames and very hot surfaces wash out.
- **Spot temperature misleads.** The spot temperature readout misleads in hot gas.

**Visible-light interior conditions in real footage:**
- Visibility is often zero at standing height.
- Light appears as an orange-red diffuse glow through smoke that grows brighter and larger as the fire nears. The flame shape is not visible until close or until the layer lifts.
- The best visibility is at floor level.
- Under water application, visibility drops briefly as steam forms, then improves.

---

## 5. Reading smoke: teaching materials and color meanings

| # | Link | Source / tier | Length | Notes |
|---|------|---------------|--------|-------|
| 5.1 | https://www.fireengineering.com/leadership/david-dodson-the-art-of-reading-smoke/ | Fire Engineering (Dodson) / B | article | Dodson's own article. Opens in a browser; returns 403 to scripted requests. Older companion: https://www.fireengineering.com/firefighting/dave-dodson-the-art-of-reading-smoke/ |
| 5.2 | https://www.youtube.com/watch?v=GQ7B542OSF8 | FireEngineeringBooks (official publisher) / B | 2:12 | Dodson "Art of Reading Smoke" excerpt. The full DVD and book are sold by Fire Engineering. Avoid the unofficial full uploads, which appear to be pirated. |
| 5.3 | https://www.youtube.com/watch?v=vmfx51e0j9o | Phillip Jose / B | 2:31 | "The Art of Reading Smoke: The Next Generation" (FDOIC 2024): first-in video teaching volume, velocity, density and color. |
| 5.4 | https://www.youtube.com/watch?v=jL5ULzpgvxE | ARFF World / C | 7:39 | Summary of the Dodson method. |
| 5.5 | https://www.youtube.com/watch?v=l8CxxwD0Ir8 | CTBS2010 / C | 9:53 | Dave Dodson, "Reading Smoke" lecture clip. |
| 5.6 | https://www.youtube.com/watch?v=cLBtRwJ93aQ | Matt Hinkle (Box Alarm Training) / B–C | 9:36 | Smoke-reading **experiments** plus live-fire footage. |
| 5.7 | https://www.youtube.com/watch?v=31x59Mhu2Ow | Tailboard Tips / C | 1:38 | Summary of smoke colors. |
| 5.8 | https://www.youtube.com/watch?v=fHUjG5Zt1tM | Dale G. Pekel / C | 10:43 | Size-up and reading smoke, including hostile fire event warning signs. |
| 5.9 | https://www.youtube.com/watch?v=VFo6ATQhJZI | LACoFD / A | 4:39 | Reading smoke from a house fire ("Nothing Showing Means Nothing"). |
| 5.10 | https://www.youtube.com/watch?v=ACjtIa5MU5M | UL FSRI / A | 2:04 | "What's So Dangerous About Smoke?": smoke toxicity and the escape window (3 min or less). |

**Dodson's four factors, paraphrased from 5.1:**
- **Volume:** how much fuel is off-gassing. Judge it against the size of the box. A small fire can fill a small house, so volume alone says little.
- **Velocity:** pressure, and its cause.
  - *Heat-driven* smoke keeps its speed as it leaves and slows gradually.
  - *Volume-driven* smoke slows almost at once.
  - **Turbulent** (boiling, agitated) smoke means the room can no longer absorb heat, which is a **flashover warning**. **Laminar** (smooth) smoke means the space is still absorbing heat.
  - Compare like-sized openings. The fastest smoke from the smallest opening points toward the fire.
- **Density:** fuel content and continuity. Thicker smoke is more flammable, more toxic, and gives worse visibility. Dense smoke is the strongest single indicator of a potential flashover, flash fire or smoke explosion.
- **Color:** stage of heating and distance traveled. It identifies the material only for single-fuel fires.

**Smoke color meanings** (Dodson and general fire-service teaching):

| Color | Meaning | Simulator use |
|-------|---------|---------------|
| White, lazy, low velocity | early heating: moisture and light pyrolysis vapors; incipient stage | incipient plume, low opacity, slow rise |
| White with push or velocity | smoke that has traveled a distance and cooled, losing carbon on the way. Can come from a hot, remote fire. **White does not mean safe**. | remote-fire exhaust at eaves and far openings |
| Grey | mix of moisture and carbon. Plastics, painted or finished surfaces, or white smoke picking up soot nearer the fire. | growth and modern-contents fires |
| Tan / brown | unfinished wood (framing) pyrolyzing: the fire has moved into the **structure** (attic, voids, balloon frame). From engineered wood, a collapse warning. | attic, eave and void smoke |
| Black, thin and fast | carbon-rich and hot. Flames are close to the opening. | near the seat of the fire, rollover precursor |
| Black, dense, turbulent, high volume ("black fire") | imminent autoignition or flashover. The smoke itself can ignite and do flame-like damage. | ventilation-limited push, pre-flashover; treat as fire |
| Yellow-grey or yellow-brown, pulsing, drawn back in at gaps | under-ventilated, fuel-rich smoke. Classic **backdraft** warning, along with sooty or crazed windows. | backdraft precursor |

---

## 6. Still-image references for smoke color and fire stages

| # | Link | Source / tier | What it gives |
|---|------|---------------|---------------|
| 6.1 | https://technicalpanels.fsri.org/docs/2010-DHS-FD-Summary.pdf | UL FSRI / A | Exterior and interior photos of instrumented experiments. Fig. 4.5 and 4.6 show the house 5 s vs. 60 s after a roof vent. Fig. 4.21 shows side A smoke 10 s before and after an event. Includes flow-path diagrams for each growth stage (Figs. 4.15–4.19). |
| 6.2 | https://d1gi3fvbl0xj2a.cloudfront.net/public/2021-07/Analysis_of_Changing_Residential_Fire_Dynamics_and_Its_Implications_on_Firefighter_Operational_Timeframes.pdf | Kerber 2012 / A | Time-stamped stills of the modern and legacy rooms, window-failure photos and fire-timeline figures. |
| 6.3 | https://d1gi3fvbl0xj2a.cloudfront.net/public/2021-07/DHS_2008_Grant_Report_Final.pdf | UL FSRI 2010 horizontal report / A | Hundreds of time-stamped exterior and interior stills for each experiment, with temperature, O₂, CO and velocity plots. The primary source for stage-by-stage smoke look vs. data. |
| 6.4 | https://technicalpanels.fsri.org/docs/Attic-Final-Report-Online.pdf | UL FSRI / A | Eave, soffit and attic smoke and flame stills. |
| 6.5 | https://d1gi3fvbl0xj2a.cloudfront.net/public/2021-07/Coord_Tactics_Single_Family_Homes.pdf | UL FSRI / A | Acquired-house exterior smoke photos before and after ventilation and suppression. |
| 6.6 | https://fireinvestigation.fsri.org/ | UL FSRI / A | Per-experiment photo galleries and video stills (ranch and colonial). |
| 6.7 | https://www.nist.gov/el/fcd/design-fires-residential-and-office-items | NIST FCD / A | Ignition stills for 51 items. Each test page adds images or video with HRR. |
| 6.8 | https://www.nist.gov/node/399111 | NIST / A | Spartanburg live-burn photos. |
| 6.9 | https://commons.wikimedia.org/wiki/Category:House_fires | Wikimedia Commons / C (freely licensed) | Real house-fire photos with varied smoke color. Check each image's license. |
| 6.10 | https://commons.wikimedia.org/wiki/Category:Smoke_from_fires | Wikimedia Commons / C | Smoke color and plume reference. |
| 6.11 | https://commons.wikimedia.org/wiki/Category:Flashover | Wikimedia Commons / C | Flashover stills, mostly training-container burns. |
| 6.12 | https://commons.wikimedia.org/wiki/Category:Backdraft | Wikimedia Commons / C | Backdraft stills. |
| 6.13 | https://fsri.org/research/new-comparison-natural-and-synthetic-home-furnishings | UL FSRI / A | Stills and key times from the 2020 natural vs. synthetic comparison. |

---

## 7. Gaps and next steps

1. **FSRI Fire Safety Academy course videos** (0.4) combine footage with synchronized temperature and gas plots. They are the best single source for matching what a stage looks like to its measured conditions. A free login is needed, so I did not catalog the individual lessons.
2. **The NIST FCD lacks waste-bin and trash-can burns** in the residential items project. Search the FCD experiment list, or add a measured bin HRR from the literature.
3. **No balloon-frame-specific instrumented footage was found.** Use the FSRI attic and knee-wall void work (2.10), and NIOSH line-of-duty-death reports on balloon-frame fires for photos. The NIOSH site (cdc.gov/niosh/fire) returns 403 to scripted requests, so I did not verify those links.
4. **Real-incident helmet-cam footage is mostly tier C.** For calibrated interior visibility, rely on the research interior and infrared footage (1.61, 1.62, 4.13–4.16) and treat incident footage as qualitative.
5. **Two links appeared in search results but were not metadata-checked:**
   - https://www.youtube.com/watch?v=BrkKkIZenBQ (UL-FSRI/NIST "Ventilation Limited Fires" video)
   - https://www.youtube.com/watch?v=AwVvvK2oL3M ("PPV Flashover")
