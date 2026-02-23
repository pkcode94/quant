Quant: The Baryonic Trading Engine

PURPOSE

The Quant engine is a deterministic risk-management framework designed to treat capital as physical mass and market volatility as entropy. It does not predict price; it calculates the gravitational floor of a position. By structuring entries and exits through baryonic equations, the user moves from emotional gambling to systematic phase transitions.

The system is experimental. It uses uncertainty as a variable. Use at your own risk.

THE BARYONIC LOGIC

Unlike standard finance, which treats assets as massless data points, this engine treats every trade as an object with inertia:

Mass-Driven Overhead: Calculates the true cost of execution. As your "Portfolio Pump" (mass) increases, the "Overhead" (friction) collapses toward zero.

Gravitational Entry Ladders: Instead of "buying dips," the engine calculates the Schwarzschild Horizon—the exact point where market entropy is defeated by your position's density.

Sigmoidal Phase Transitions: Exits follow a logistic sigmoid curve, mimicking how energy evaporates from a high-density physical system.

CORE EQUATIONS

1. The Overhead Equation (The Friction of Spacetime)

The engine calculates the minimum price movement required to overcome the "drag" of the market.


$$Overhead = \frac{(\text{feeSpread} \cdot \chi \cdot \Delta t) \cdot \text{symbolCount}}{\left(\frac{\text{Price}}{\text{Qty}} \cdot \text{PortfolioPump}\right) + K}$$

$\chi$ (Fee Hedging Coefficient): A multiplier against systemic slippage.

$K$ (Schwarzschild Constant): Prevents division by zero and represents the "minimum mass" required for the logic to hold.

Portfolio Pump: Your total liquid mass. As this grows, the denominator grows, and the overhead (drag) vanishes.

2. Position Delta (Relative Density)

Every trade is measured by its "weight" against your total mass:


$$\Delta_{pos} = \frac{\text{Price} \cdot \text{Quantity}}{\text{PortfolioPump}}$$


Insight: A trade with a high $\Delta_{pos}$ has high gravity but low maneuverability.

3. Entry Horizons (The Baryonic Floor)

Funding is distributed across deep levels to turn a crash into a "Compaction" event.


$$EntryPrice[i] = \text{CurrentPrice} \cdot (1 - \text{Overhead} \cdot (i + 1))$$

$$BreakEven[i] = EntryPrice[i] \cdot (1 + \text{Overhead})$$


Weighting: $W_i = (N - i) \cdot (1 - \text{risk}) + (i + 1) \cdot \text{risk}$.
As risk increases, the engine allocates more "mass" to the deepest levels, preparing for maximum entropy.

4. Exit Strategy (Normalized Sigmoidal Evaporation)

Unwinding a position follows a Logistic Sigmoid to remove "Take Profit" anxiety.


$$\sigma(x) = \frac{1}{1 + e^{-\text{steepness} \cdot (x - \text{center})}}$$


To ensure 100% of the position is accounted for, the engine uses Normalized Cumulative Distribution:


$$C[i] = \frac{\sigma(i) - \sigma(0)}{\sigma(N) - \sigma(0)}$$

$$\text{SellQty}[i] = \text{TotalQty} \cdot (C[i+1] - C[i])$$


Interpretation: The center shifts based on the riskCoefficient. Conservative ($0$) exits early; Aggressive ($1$) holds for the peak.

WHAT THE ENGINE ACTUALLY DOES

Quantifies Uncertainty: Uses volatility as fuel to lower average entry costs.

Neutralizes Middlemen: The feeHedgingCoefficient ensures you only trade when the math starves the exchange's spread.

Decentralized Phalanx: If a community uses these shared risk levels, they create a collective "Baryonic Floor" that prevents artificial crashes.

TECHNICAL REQUIREMENTS

C++17 or newer.

cpp-httplib (included) for the web interface.

Build: g++ -std=c++17 -O2 -o quant Quant.cpp
Access: http://localhost:8080

AUTHOR NOTE: THE REASON

DISCLAIMER: This software uses uncertainty as a variable. It is a deterministic calculator. Understand the physics or the mass will crush you.
