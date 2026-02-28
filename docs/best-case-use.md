# Best-Case Use — The Intended Operating Envelope

This document describes the market conditions and configuration under which the framework's deterministic guarantees hold unconditionally. Everything in [`paper.md`](paper.md) was designed for this envelope. Everything in [`failure-modes.md`](failure-modes.md) describes what happens when you leave it.

---

## 1. The One-Sentence Summary

**The system maximises yield on a bullish market — with or without dips — using HODL-through-drawdown patience and no stop losses.**

That's it. If the market trends up over time (even with severe dips along the way), and you don't panic-sell via SL, every equation holds and every TP that triggers is fee-neutral with surplus captured.

---

## 2. The Operating Envelope

### What the framework assumes

| Assumption | Why it matters |
|-----------|----------------|
| **Bullish trend** (any timeframe) | TPs are above entry. The market must eventually reach them. |
| **No stop losses** | Every equation flows from "if TP is hit." SLs bypass this. |
| **Sufficient patience** | Dips are survived by holding, not cutting. Capital stays deployed. |
| **Known fee rates** | Overhead is pre-computed from $f_s$. Rates must not spike beyond $f_h$. |
| **Liquid assets** | TP sell orders must fill at the target price. |

### What the framework does NOT assume

| Non-assumption | Consequence |
|---------------|-------------|
| Price goes up immediately | Entries can sit for days, weeks, months. The math doesn't care *when*. |
| No dips occur | Dips trigger deeper entry levels — this is *desired*. More levels filled = more profit when recovery comes. |
| Constant uptrend | Sideways is fine (range mode). Dips are fine (entries trigger). Only a permanent decline with no recovery breaks the system (§2 of failure-modes). |
| Specific return rate | The surplus $s$ is what you configure. The market decides if/when it's captured. |

---

## 3. Market Conditions — Ranked by Suitability

### Optimal: Bullish with dips

```
Price: ~~~~~~/\~~~~~/\/\~~~~~~/\~~~~~~~~~?
              ?        ??      ?
          entries    entries  entries
              ?              ?        ?
             TP             TP       TP
```

Dips trigger entry levels. Recoveries hit TPs. Each cycle completes and compounds into the next. The deeper the dip, the more levels fill, the more profit on recovery. The sigmoid funding allocation (risk coefficient $r$) controls how aggressively capital loads into deeper dips.

**This is the scenario the framework was built for.**

- All entries trigger at computed prices
- All TPs are hit on recovery
- Fee neutrality holds at every exit
- Chain cycles complete and compound
- Downtrend buffer pre-funds re-entry at lower prices if the next dip is deeper
- Savings extraction locks in realised profit permanently

### Good: Steady bullish (few dips)

```
Price: ~~~~~/~~~~~~~/~~~~~~~~~?
```

Fewer entries trigger (only the levels near market price). Profit per cycle is lower because fewer levels participate. But every triggered level's TP is hit quickly. Cycles complete fast. Chain compounding is rapid but shallow.

**Configuration tip:** Use range mode with tight bounds ($R_{\text{above}} = 0$, $R_{\text{below}}$ small) to concentrate entries near market price. This maximises fill rate in a steady uptrend.

### Acceptable: Sideways / range-bound

```
Price: ~~~/\~/\~/\~/\~~~
```

Entries near the bottom of the range trigger. TPs near the top hit. The system functions as a grid — buy support, sell resistance. Cycle turnover is moderate.

**Configuration tip:** Range mode with symmetric bounds. Low surplus ($s$ small) to keep TPs tight. Steepness $\alpha \approx 4$ to cluster entries near the range edges.

### Survivable: Bear market with eventual recovery

```
Price: ~~~~\___________/~~~~?
            ?????     ????
         all entries   all TPs (eventually)
```

All entries trigger during the decline. Capital is 100% deployed. Unrealised losses grow. The system stalls — no exits, no chain progression, no savings.

**But:** If the market eventually recovers to entry levels + EO, every TP is hit. Fee neutrality holds. The wait may be weeks or months, but the math doesn't expire. There is no time decay on the equations (unless $\Delta t$ changes are applied externally).

**This is the HODL scenario.** The framework doesn't fail here — it just waits. Patience is the only requirement.

### Fatal: Permanent decline (no recovery)

```
Price: ~~~~\____________________________? 0
```

TPs are never hit. Capital is permanently locked in unrealised losses. The framework produces correct but unreachable targets. See [failure-modes.md](failure-modes.md) §2.

**No configuration fixes this.** The framework cannot make money in a market that only goes down. No system can.

---

## 4. The Default Configuration

The following settings define the intended operating mode — the one where every equation holds unconditionally:

| Parameter | Value | Why |
|-----------|-------|-----|
| Stop losses | **Off** | Preserves the conditional guarantee |
| $\phi_{\text{sl}}$ | 1.0 (default, irrelevant when SL off) | — |
| $n_{\text{sl}}$ | 0 | No SL hedging needed |
| $f_h$ | 1.0–2.0 | 1.0 = exact fee hedging; 2.0 = safety margin |
| $s$ | 0.01–0.05 | 1–5% surplus per cycle |
| $r$ | 0.5 | Uniform funding (neutral) — or higher for aggressive dip-buying |
| $\alpha$ | 4–6 | Smooth S-curve entry distribution |
| Direction | **LONG** | The framework is optimised for long positions |
| $n_d$ | 1–3 | Pre-fund 1–3 re-entry cycles after exit |
| $s_{\text{save}}$ | 0.05–0.10 | Extract 5–10% of each cycle's profit to safety |

### Why LONG only (for now)

The equations are symmetric — SHORT works by mirroring TP below entry and SL above. But the *operating envelope* is not symmetric:

- **Bullish LONG:** TP is above entry. The market trends toward TP over time. Probability of fill increases with patience.
- **Bearish SHORT:** TP is below entry. The market must drop to TP. In a structurally bullish market (which crypto and equities have been historically), SHORT TPs fight the long-term trend.

SHORT positions require bearish conviction — a prediction about future direction. This contradicts design principle #1 (*determinism over prediction*). The LONG-only, SL-off configuration is the only one that is truly prediction-free: it says "the market will eventually be higher than where I bought," which is the weakest possible bullish assumption.

---

## 5. The Lifecycle — What Happens Step by Step

### Cycle 0

1. **Deploy:** Serial generator creates $N$ entry levels below current price, sigmoid-distributed
2. **Wait:** Market dips trigger entries. Each entry buys at the computed price with allocated funding
3. **Hold:** Unrealised losses during deeper dips. No action taken. No SL fires.
4. **Exit:** Market recovers. TPs are hit from lowest to highest. Each exit covers its own fees + surplus.
5. **Complete:** All TPs hit (or all desired exits filled). Cycle profit $\Pi_0$ is realised.

### Transition

6. **Extract:** Savings $= \Pi_0 \cdot s_{\text{save}}$ pulled out permanently
7. **Reinvest:** Remaining profit compounds into cycle 1 capital: $T_1 = T_0 + \Pi_0(1 - s_{\text{save}})$

### Cycle 1+

8. **Regenerate:** New entries at current price with $T_1$ pump. OH is lower (more capital). TPs are tighter.
9. **Repeat:** The system becomes more efficient with each cycle. TP targets converge toward entry + surplus.

### Terminal state (long-term)

$$
\lim_{c \to \infty} \text{OH}_c = 0, \qquad \text{TP}_c \to P_e \cdot (1 + s + f_s \cdot f_h \cdot \Delta t)
$$

Overhead vanishes. Each cycle nets approximately $s$% of deployed capital, minus the savings extraction. The system becomes a compounding machine with a known per-cycle yield.

---

## 6. What "With Dips" Actually Means

The framework doesn't just *tolerate* dips — it *wants* them.

Without dips: only level $N-1$ (nearest to market) triggers. One entry, one TP, one cycle.

With a 10% dip: several mid-range levels trigger. Multiple entries, multiple TPs, more total profit.

With a 50% dip: deep-discount levels trigger. Aggressive funding allocation ($r > 0.5$) loads capital at the bottom. Recovery to original price means every TP is hit with maximum profit.

**The ideal market is one that dips hard and recovers fully.** The framework converts volatility into yield, as long as the final direction is up.

The downtrend buffer ($n_d > 0$) inflates TPs to pre-fund re-entry if the *next* dip goes deeper. This means: dip ? recovery ? profit ? deeper dip ? re-entry with more capital ? deeper recovery ? more profit. Each cycle's downtrend buffer anticipates the next drawdown.

---

## 7. What This Is NOT

| It is NOT | Why not |
|-----------|---------|
| A prediction engine | It computes *what must happen*, not *what will happen* |
| A high-frequency strategy | Cycles can take hours, days, or weeks. The math is time-agnostic. |
| A bearish strategy | LONG only. No profit in permanent decline. |
| A stop-loss strategy | SLs break fee neutrality. The default is SL off. |
| A leveraged strategy | All positions are spot (fully funded). No liquidation risk. |
| A market-making bot | No bid-ask management. Entries and exits are limit orders at computed prices. |
| Risk-free | Market risk (price never reaching TP) is the irreducible residual. |

---

## 8. When to Use Something Else

| Scenario | Better approach |
|----------|----------------|
| You believe the market will crash permanently | Don't trade. Or SHORT (outside this framework's current scope). |
| You need guaranteed exits within a time window | The framework has no time limits. Use a strategy with expiry. |
| You're using leverage | Liquidation risk exists below SL. The framework assumes spot positions. |
| Fee rates are unknown or wildly variable | Set $f_h \gg 1$ or use a fixed-fee exchange. |
| You're trading illiquid micro-caps | TP fill is not guaranteed. See [failure-modes.md](failure-modes.md) §7. |

---

## 9. Summary

The framework is a **bullish yield maximiser with HODL patience**. It converts dip-and-recovery cycles into compounding fee-neutral profit. Every equation holds when:

1. The market eventually goes up (even after dips)
2. You don't stop-loss out during dips
3. You let the math do the waiting

The surplus rate $s$ is your yield. The chain is your compounding engine. The savings extraction is your realised return. Everything else — sigmoids, overhead, buffers — is engineering that makes the machine work.

**Bullish + patient + no SL = every guarantee holds.**
