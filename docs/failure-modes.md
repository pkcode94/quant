# Failure Modes — When the Mathematics Breaks Down

This document catalogues the conditions under which the deterministic guarantees of the framework (see [`paper.md`](paper.md)) no longer hold. Every failure mode listed here represents a scenario where one or more core assumptions are violated, causing the equations to produce results that do not match reality.

The framework guarantees: *if TP is hit, fees are covered and surplus is captured*. Everything below describes what happens when that conditional is never satisfied, or when the inputs to the equations are no longer valid.

---

## 1. The Core Assumption

The system is built on a single conditional guarantee:

$$
\text{If } P_t \geq \text{TP}_i, \quad \text{then } \text{Net}_i \geq s \cdot \text{Funding}_i
$$

This holds unconditionally when:

- The position exits at TP (not at SL or by liquidation)
- The fee rates at execution match the rates used to compute overhead
- The asset is liquid enough to fill at the TP price without slippage beyond $f_s$
- The exchange is solvent and honours the order

**Every failure mode below violates at least one of these.**

---

## 2. Sustained Bear Market (Price Never Recovers)

### What Happens

The market drops below all entry levels. Every entry triggers. No TP is ever reached. All capital is deployed, and the positions sit at unrealised losses indefinitely.

### Why the Math Fails

The overhead formula computes *where* the TP should be, but makes no claim about *whether* the market will reach it. In a sustained bear market:

$$
P_t < P_e^{(i)} \cdot (1 + \text{EO}) \quad \forall\, t > t_{\text{entry}}, \quad \forall\, i
$$

The TP is mathematically correct — if hit, it would cover fees. But it is never hit.

### Consequences

| Metric | Effect |
|--------|--------|
| Fee neutrality | **Intact but irrelevant** — no exits means no fees to recover |
| Capital | 100% deployed, 0% liquid |
| Chain progression | **Halted** — cycle never completes |
| Unrealised P&L | Negative, growing with each drop |
| Downtrend buffer | **Useless** — it pre-funds re-entry *after* a profitable exit. No exit occurs. |

### Severity

**Total system stall.** The mathematics doesn't produce wrong answers — it produces answers to a question the market never asks. The TP sits above a price that never arrives.

### Mitigation

- **Range mode** with tight bounds concentrates entries near current price, increasing the probability that at least some TPs are reachable even in a moderate bear market.
- **Fractional SL** ($\phi_{\text{sl}} \ll 1$) allows partial exits to free capital for re-deployment at lower prices.
- **Manual re-generation** at the new (lower) price creates fresh entries with TPs relative to the current market, not the old one.
- None of these are part of the deterministic framework — they are human interventions.

---

## 3. Flash Crash / Gap Down

### What Happens

The price drops through multiple entry levels instantaneously (e.g., exchange outage, liquidity vacuum, fat-finger trade). Entries trigger at the requested price, but the actual fill price may be far worse due to slippage.

### Why the Math Fails

The overhead formula assumes buy execution at $P_e^{(i)}$:

$$
\text{Cost}_i = P_e^{(i)} \cdot q_i
$$

If the actual fill is at $P_{\text{fill}} > P_e^{(i)}$ (slippage during a crash), then:

$$
\text{Actual Cost} > \text{Expected Cost}
$$

The TP was computed relative to the *expected* entry price. The *actual* break-even is higher than $P_e^{(i)} \cdot (1 + \text{OH})$, but the TP doesn't know this.

### Consequences

- **Fee neutrality violated.** The TP was calibrated for the expected entry, not the actual one. If the slippage exceeds the surplus margin ($s$), the TP doesn't even cover costs.
- **Multiple entries trigger simultaneously.** Capital exhaustion is possible if the crash blows through all $N$ levels in one tick.
- **Funding mismatch.** Level $i$ was supposed to get $\text{Funding}_i$, but if earlier levels consumed more capital due to slippage, later levels may not fill.

### Severity

**Moderate to severe.** The framework's slippage buffer ($f_s$) is meant to absorb normal slippage, but flash crashes can produce slippage orders of magnitude larger than $f_s$.

### Mitigation

- **Fee hedging coefficient** $f_h > 1$ provides a safety margin. Setting $f_h = 2$ means the TP hedges at 2× the expected fee rate, creating headroom for unexpected slippage.
- **Coefficient K** ($K > 0$) adds a constant to the denominator, preventing overhead from vanishing when capital is high. This creates a minimum TP distance that absorbs moderate slippage.

---

## 4. Stop Loss Cascade

### What Happens

Multiple positions hit their SLs in sequence. Each SL loss reduces capital. Reduced capital increases overhead for the next cycle. Higher overhead means wider TPs, which are less likely to be hit. More SL hits follow.

### Why the Math Fails

The chain compounding equation assumes positive $\Pi_c$:

$$
T_{c+1} = T_c + \Pi_c(1 - s_{\text{save}})
$$

When SLs trigger, $\Pi_c < 0$:

$$
T_{c+1} < T_c
$$

The overhead formula then gives:

$$
\text{OH}_{c+1} > \text{OH}_c \quad \text{(since } T_{c+1} < T_c \text{)}
$$

Wider overhead ? wider TP ? lower fill probability ? more SL hits. This is a **positive feedback loop** (in the systems dynamics sense — it amplifies the deviation).

### The Death Spiral

```
SL hit ? capital shrinks ? OH increases ? TP widens
? TP less likely to fill ? next cycle also hits SL
? capital shrinks further ? ...
```

The fixed point of this spiral is $T = 0$ (total capital depletion).

### Severity

**Critical.** This is the most dangerous failure mode because it is self-reinforcing. A single SL hit in a well-funded chain is survivable. Three consecutive SL hits can be terminal.

### Quantitative Bound

A full SL ($\phi_{\text{sl}} = 1$) at level $i$ costs:

$$
\text{Loss}_i = \text{EO} \cdot P_e^{(i)} \cdot q_i
$$

For the chain to survive $k$ consecutive SL hits, you need:

$$
T_0 > \sum_{j=0}^{k-1} \text{EO}_j \cdot P_e^{(j)} \cdot q_j \cdot \prod_{m=0}^{j-1}\left(1 - \frac{\text{Loss}_m}{T_m}\right)^{-1}
$$

This grows super-linearly because each loss increases the overhead for the next.

### Mitigation

- **Keep SLs off** (the default). The framework doesn't need them — it waits for TP.
- **Fractional SL** ($\phi_{\text{sl}} = 0.1$ to $0.25$) limits each SL to a small trim.
- **SL hedge buffer** ($n_{\text{sl}} > 0$) inflates TP to pre-fund expected SL losses. This restores the deterministic guarantee *on expectation* — but only if the actual SL hit rate matches $n_{\text{sl}}$.
- **Capital-loss cap** (§5.7 of paper). The engine auto-clamps $\phi_{\text{sl}}$ so that the summed worst-case SL losses across all levels never exceeds the available capital. Even if every SL triggers simultaneously, the total loss is bounded by $T_{\text{avail}}$. This prevents the degenerate case where aggressive funding + high EO produces an infeasible plan. The cap does **not** prevent the death spiral — it only bounds the *per-cycle* damage. Three consecutive capped cycles can still deplete capital; the cap just guarantees each individual cycle cannot lose more than 100% of its capital.

---

## 5. Exchange Counterparty Failure

### What Happens

The exchange goes offline, freezes withdrawals, or becomes insolvent (e.g., FTX, Mt. Gox). Orders cannot be placed or filled. Positions cannot be exited.

### Why the Math Fails

The framework assumes the exchange is a reliable execution venue. Every equation assumes:

1. Limit orders are honoured at the specified price
2. Capital on the exchange is accessible
3. Market data (prices) is accurate

None of these hold during exchange failure.

### Severity

**Total.** This is an exogenous risk the framework cannot model or hedge. No mathematical formula can compensate for the exchange stealing your money.

### Mitigation

- Diversify across exchanges
- Use non-custodial venues (DEXs) where funds remain in your wallet
- Keep the savings extraction ($s_{\text{save}} > 0$) flowing to cold storage — capital that has been extracted from the chain is safe from exchange failure

---

## 6. Fee Rate Changes Mid-Position

### What Happens

The exchange changes its fee schedule after entries were placed but before TPs are hit. The overhead was computed with the old fee rate $f_s^{\text{old}}$, but the exit pays $f_s^{\text{new}}$.

### Why the Math Fails

$$
\text{EO was computed as: } \text{OH}(f_s^{\text{old}}) + (s + f_s^{\text{old}}) \cdot f_h \cdot \Delta t
$$

$$
\text{Actual cost at exit: } f_s^{\text{new}} \cdot \text{notional}
$$

If $f_s^{\text{new}} > f_s^{\text{old}} \cdot f_h$, the fee hedging buffer is exhausted and the TP no longer covers costs.

### Severity

**Low to moderate.** Fee changes are typically small and announced in advance. The fee hedging coefficient $f_h$ exists precisely for this scenario.

### Quantitative Bound

Fee neutrality holds as long as:

$$
f_s^{\text{new}} \leq f_s^{\text{old}} \cdot f_h
$$

With $f_h = 2$ and $f_s = 0.1\%$, the system absorbs fee increases up to $0.2\%$.

---

## 7. Liquidity Exhaustion at TP

### What Happens

The price reaches the TP level, but there is insufficient liquidity to fill the sell order at that price. The order partially fills or fills at a worse price.

### Why the Math Fails

The exit strategy computes $q_i^{\text{sell}}$ at $P_{\text{TP}}^{(i)}$. If the actual fill is at $P_{\text{fill}} < P_{\text{TP}}^{(i)}$:

$$
\text{Actual Gross} = (P_{\text{fill}} - P_e) \cdot q_{\text{sold}} < (P_{\text{TP}} - P_e) \cdot q_{\text{sold}}
$$

The difference is unhedged slippage.

### Severity

**Low for large-cap assets** (BTC, ETH) where TP prices are well within the order book depth. **High for micro-cap tokens** where the TP sell quantity may exceed the available bid liquidity.

### Mitigation

- The fee spread $f_s$ should include expected sell-side slippage, not just the exchange fee.
- Exit fraction $\phi < 1$ splits sells across multiple TP levels, reducing the single-level liquidity demand.

---

## 8. Numerical Precision Exhaustion

### What Happens

When price ratios are extreme (e.g., $P/q = 10^{-8}$ for micro-cap tokens with large quantities), the overhead formula produces values near the floating-point epsilon. TP prices may round to entry prices.

### Why the Math Fails

$$
\text{OH} = \frac{\mathcal{F} \cdot n_s \cdot (1 + n_f)}{(P/q) \cdot T + K}
$$

When $P/q \cdot T \approx 10^{15}$ and $\mathcal{F} \cdot n_s \approx 10^{-3}$, the overhead is $\sim 10^{-18}$, which is below `double` precision ($\sim 10^{-16}$).

### Consequences

- OH rounds to 0.0
- TP equals entry price (no profit, no fee recovery)
- Break-even equals entry price

### Severity

**Low in practice.** This only occurs with extreme capital/price ratios. For all realistic trading scenarios (BTC at $100k with pump < $10B), precision is adequate.

### Mitigation

- **Coefficient K** ($K > 0$) prevents the denominator from growing without bound.
- The engine clamps OH ? 0 so it never goes negative from rounding.

---

## 9. Infinite Chain Expectation

### What Happens

The user runs a chain with $C \to \infty$ cycles. The theoretical compound growth suggests unlimited wealth. In practice, the probability that *every* TP in *every* cycle is hit approaches zero.

### Why the Math Fails

The chain compounding equation:

$$
T_C = T_0 \cdot \prod_{c=0}^{C-1}\left(1 + \frac{\Pi_c(1-s_{\text{save}})}{T_c}\right)
$$

This is a **conditional** product — each factor is only realised if cycle $c$ completes. The joint probability that all $C$ cycles complete is:

$$
\Pr[\text{chain completes}] = \prod_{c=0}^{C-1} \Pr[\text{cycle } c \text{ completes}] \leq p^C
$$

where $p < 1$ is the per-cycle completion probability. For $C \gg 1$, this probability vanishes.

### Severity

**Conceptual, not computational.** The equations are correct for each individual cycle. The error is in interpreting the chain output as a prediction rather than a conditional plan.

### The Correct Interpretation

The chain output answers: *"If every cycle completes, what happens to capital?"* — not *"Will every cycle complete?"* The framework is deterministic about the *what*, not the *whether*.

---

## 10. Concurrent Symbol Interference

### What Happens

The overhead formula includes $n_s$ (symbol count). When trading multiple symbols, each symbol's TP accounts for fees across all symbols. But if some symbols hit their TPs and others don't, the fee recovery is uneven.

### Why the Math Fails

$$
\text{OH} = \frac{\mathcal{F} \cdot n_s \cdot (1 + n_f)}{D}
$$

The $n_s$ multiplier assumes all $n_s$ symbols are actively trading. If only 2 of 5 symbols complete their cycles, those 2 symbols' TPs have over-hedged (they paid for 5 symbols' worth of fees but only 2 were realised).

### Consequences

- Over-hedging: TP is wider than necessary ? slower cycle completion
- Under-recovery if the non-completing symbols later hit SLs: the SL losses weren't covered by the completing symbols' TPs

### Severity

**Low.** Over-hedging is conservative — it results in wider TPs and more profit when hit, not in losses. The failure is one of *efficiency*, not *correctness*.

---

## 11. Summary — Failure Mode Severity Matrix

| # | Failure Mode | Severity | Self-Reinforcing | Framework Can Hedge | Human Intervention Required |
|---|-------------|----------|------------------|--------------------|-----------------------------|
| 2 | Sustained bear market | Total stall | No | No | Yes — re-generate at new price |
| 3 | Flash crash / gap down | Moderate–Severe | No | Partial ($f_h$, $K$) | Possibly |
| 4 | Stop loss cascade | Critical | **Yes** | Partial ($\phi_{\text{sl}}$, $n_{\text{sl}}$) | Yes — disable SLs |
| 5 | Exchange failure | Total | No | No ($s_{\text{save}}$ limits exposure) | Yes |
| 6 | Fee rate change | Low–Moderate | No | Yes ($f_h$) | No |
| 7 | Liquidity exhaustion | Low–High | No | Partial ($f_s$, $\phi$) | Depends on asset |
| 8 | Numerical precision | Low | No | Yes ($K$) | No |
| 9 | Infinite chain | Conceptual | No | N/A | Temper expectations |
| 10 | Symbol interference | Low | No | Built-in (over-hedges) | No |

---

## 12. The Fundamental Limit

The framework transforms the problem of profitable trading from prediction into engineering. But it cannot eliminate **market risk** — the risk that the market never reaches your TP prices. No deterministic system can.

The correct mental model: the equations define a *machine* that converts price movements into fee-neutral profit. The machine is perfectly calibrated. But the machine only runs when the market supplies the right fuel (upward price movement to TP levels for LONG positions). If the fuel never arrives, the machine sits idle — correct, calibrated, and useless.

**This is not a failure of the mathematics. It is the boundary of what mathematics can do.**
