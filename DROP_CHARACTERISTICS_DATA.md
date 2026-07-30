### Drop Characterization & Droplet Mechanics Data

#### 1. Theoretical Droplet Mechanics & Fluid Elasticity (Tate's Law, Poiseuille's Law, & Fluid Capacitance)

##### A. Droplet Detachment (Tate's Law)
The physical size and mass of a falling liquid drop detaching from a nozzle tip are governed by **Tate’s Law**, which balances gravitational force against surface tension:

$$m_{\text{drop}} = \frac{2\pi r \gamma}{g}$$

where:
* $m_{\text{drop}}$ = Mass of the detaching fluid droplet ($\text{kg}$)
* $r$ = Effective outer radius of detachment ($\text{m}$)
* $\gamma$ = Surface tension of the liquid ($\text{N/m}$, water Proxy $\gamma = 0.0728\text{ N/m}$)
* $g$ = Acceleration due to gravity ($9.81\text{ m/s}^2$)

##### Nozzle Geometry Comparison Table:

| Nozzle Configuration | Outer Radius $r$ (mm) | Inner Diameter $D$ (mm) | Theoretical Drop Mass (g) | Empirical Hanging Drop Mass (g) | Compliance with $\pm 0.01\text{g}$ Spec |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Bare Silicone Tubing** | $1.50\text{ mm}$ ($3\text{mm ID}/6\text{mm OD}$) | $3.00\text{ mm}$ | **$0.070\text{ g}$** | $0.050\text{ g} - 0.070\text{ g}$ | **VIOLATED** (1 drop overshoots target) |
| **18-Gauge Needle** | $0.635\text{ mm}$ | $0.84\text{ mm}$ | **$0.030\text{ g}$** | $0.020\text{ g} - 0.025\text{ g}$ | PASS (High resistance: $+400\%$ surge) |
| **16-Gauge Needle (Selected)** | $0.825\text{ mm}$ | $1.19\text{ mm}$ | **$0.038\text{ g}$** | $0.0225\text{ g}$ (burst) | **OPTIMAL** (Balances flow & precision) |

According to **Poiseuille’s Law**, fluidic resistance in a pipe increases inversely with the fourth power of inner radius ($R_{\text{fluid}} \propto \frac{1}{r_{\text{in}}^4}$). Sizing down to an 18G needle causes a **~400% surge in fluidic resistance**, risking stepper motor stalls during high-speed bulk filling of viscous glycerol. Therefore, the **16-Gauge blunt dispensing needle** was selected as the optimal geometric compromise.

##### B. Elastic Strain Energy & Fluid Capacitance ($\text{C}_{\text{fluid}}$)
The elasticity of the silicone tubing acts as a hydraulic capacitor, storing fluid under pressure:

$$C = \frac{\Delta V}{\Delta P} = \frac{\pi D^3 L}{4 E h}$$

where:
* $C$: Volumetric fluid capacitance (compliance), representing the stored volume change per unit pressure change ($\text{m}^3/\text{Pa}$)
* $\Delta V$: Change in internal fluid volume stored due to tube wall expansion ($\text{m}^3$)
* $\Delta P$: Internal fluid backpressure ($\text{Pa}$), governed by Poiseuille’s Law ($R_{\text{fluid}} \propto \frac{\mu L}{D^4}$)
* $D$: Inner diameter of unpressurized tubing ($\text{m}$)
* $L$: Length of the flexible tubing section ($\text{m}$)
* $E$: Young’s Modulus of elasticity of silicone ($\text{Pa}$; softer silicone = lower $E$ = higher capacitance)
* $h$: Tubing wall thickness ($\text{m}$)

**Impact on Dispensing & Control Solution**:
1. **Post-Stop Residual Drip**: Lower Young's Modulus ($E$) or thinner wall thickness ($h$) increases capacitance ($C$), causing the tubing walls to balloon during high-speed fill ($\Delta V = C \cdot \Delta P$). When the motor stops, the elastic strain energy releases, driving a residual post-stop drip.
2. **Active Retraction Mitigation (`DISPENSE_SUCK_BACK`)**: To counter $C \cdot \Delta P$, the firmware executes a dedicated reverse step retraction ($3,200\text{ uSteps}$ for water, $9,600\text{ uSteps}$ for glycerol) to generate negative pressure ($\Delta P_{\text{retract}} < 0$), relieving line expansion and preventing post-dispense dripping.

---

#### 2. Empirical Automated Drop Characterization (`DROP_CAL`) Data

To auto-tune trim pulse budgets live without relying solely on static equations, the firmware executes an automated **Drop Characterization routine (`DROP_CAL`)**. The controller commands sequential **64-microstep inchworm bursts** (`DROP_CAL_BURST_STEPS`) at 1/64 microstepping resolution (equivalent to 1 full step per burst) until liquid accumulates on the nozzle tip and detaches onto the scale.

##### Key Empirical Drop Parameters (`src/config.h` & `drop_characterize.py`):

| Parameter | Value / Metric | Description / Role in Control Logic |
| :--- | :---: | :--- |
| **Default Drop Mass ($m_{\text{drop\_default}}$)** | **$0.0225\text{ g}$** ($22.5\text{ mg}$) | Mean empirical drop mass measured across 15 detachment events per pump channel. |
| **Drop Detachment Threshold** | **$\ge 0.018\text{ g}$** | Scale detection trigger ($\ge 2\text{ LSB}$ at 0.01g resolution) signaling a drop event. |
| **Burst Step Size** | **64 microsteps** | 1 full step increment at 1/64 microstepping resolution ($3500\text{ steps/sec}$ speed). |
| **Burst Settle Delay** | **$800\text{ ms}$** | Scale stabilization window between individual bursts. |
| **Steps per Drop** | **$\sim 1,280$ microsteps** | $\sim 20$ full motor steps required to displace one full $0.0225\text{g}$ drop. |
| **Fine Trim Pulse Limit** | **$0.010\text{ g}$** ($10\text{ mg}$) | Hard pulse cap set to $\sim 0.45 \times m_{\text{drop\_default}}$ to prevent single-pulse overshoots. |
| **Inchworm Threshold** | **$0.045\text{ g}$** ($45\text{ mg}$) | Residual mass threshold ($2 \times m_{\text{drop\_default}}$) where controller switches to burst mode. |

---

#### 3. Ready-to-Copy Report Section: Droplet Mechanics & Fluid Capacitance

```markdown
#### 2.3.2 Droplet Mechanics, Fluid Capacitance, and Empirical Drop Characterization
The physical geometry of the dispensing nozzle and the elastic compliance of the fluidic line directly dictate the absolute dosing accuracy of the system. During initial prototype testing, fluid was dispensed directly from thick-walled bare silicone tubing (6 mm OD, 3 mm ID). Surface tension caused fluid to wet across the flat tube tip, resulting in large hanging droplets weighing 0.050 g to 0.070 g. The mechanics of this detachment failure are governed by Tate's Law:

$$m_{\text{drop}} = \frac{2\pi r \gamma}{g}$$

where nozzle tip outer radius $r = 0.0015\text{ m}$, fluid surface tension $\gamma = 0.0728\text{ N/m}$, and gravitational acceleration $g = 9.81\text{ m/s}^2$ yield a theoretical minimum drop mass of 0.070 g. Because a single hanging droplet detaching at the end of a cycle would immediately exceed the system's strict $\pm 0.01\text{ g}$ absolute tolerance threshold, geometric optimization was required. Integrating a 16-gauge blunt industrial needle tip ($1.65\text{ mm OD}, 1.19\text{ mm ID}, r = 0.000825\text{ m}$) slashes the theoretical droplet mass by nearly half to 0.038 g. Further restricting nozzle geometry to an 18-gauge needle ($1.27\text{ mm OD}, 0.84\text{ mm ID}$) was evaluated, but Poiseuille's Law dictates that fluidic line resistance increases inversely with the fourth power of inner radius ($R_{\text{fluid}} \propto \frac{1}{r_{\text{in}}^4}$). The 18G tip introduced a ~400% surge in fluidic resistance, risking stepper motor stall during high-viscosity bulk filling. Thus, the 16G needle was chosen as the optimal geometric compromise.

In addition to tip geometry, the elastic compliance of the silicone tubing introduces volumetric fluid capacitance ($C$), which governs post-stop liquid drip:

$$C = \frac{\Delta V}{\Delta P} = \frac{\pi D^3 L}{4 E h}$$

where $D$ is inner diameter, $L$ is tube length, $E$ is Young's Modulus of the silicone, and $h$ is wall thickness. High backpressure ($\Delta P$) during bulk filling expands the elastic tube walls, storing excess volume $\Delta V = C \cdot \Delta P$. When the motor stops, the elastic strain energy relaxes, forcing fluid out of the nozzle tip. To eliminate this post-stop drift, the firmware executes a active suck-back retraction sequence (`DISPENSE_SUCK_BACK`) at the end of each cycle ($3,200\text{ uSteps}$ for water, $9,600\text{ uSteps}$ for glycerol), creating negative pressure ($\Delta P_{\text{retract}} < 0$) to collapse tube expansion and lock fluid inside the tip.

To auto-tune dosing logic during operation, the controller executes an automated Drop Characterization routine (`DROP_CAL`). The system actuates 64-microstep inchworm bursts (1 full step at 1/64 microstepping resolution) at 3,500 steps/sec, allowing an 800 ms scale settling window between bursts. A drop event is recorded when scale feedback detects a mass jump $\ge 0.018\text{ g}$. Across 15 consecutive detachment events, the system characterized a mean empirical drop mass of $m_{\text{drop\_default}} = 0.0225\text{ g}$ ($\sim 1,280$ microsteps per drop). The firmware uses this empirical baseline to set a hard fine-trim pulse limit of 0.010 g ($\sim 0.45 \times m_{\text{drop}}$) and trigger microstep inchworm dosing whenever residual error drops below 0.045 g ($2 \times m_{\text{drop}}$), guaranteeing that final trim pulses never stack a full droplet beyond the target weight.
```
