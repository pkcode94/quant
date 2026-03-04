# libquant

Standalone C++ math library extracted from the Quant trade engine.

Pure math only — no UI, no database, no HTTP, no ledger.

## What's included

| Feature | Function |
|---|---|
| Serial plan generation | `QuantMath::generateSerialPlan()` |
| Cycle computation | `QuantMath::computeCycle()` |
| Chain planning | `QuantMath::generateChain()` |
| Exit plan | `QuantMath::generateExitPlan()` |
| Profit calculation | `QuantMath::computeProfit()` |
| DCA tracking | `QuantMath::computeDca()` |
| Overhead computation | `QuantMath::overhead()` / `effectiveOverhead()` |
| Sigmoid distribution | `QuantMath::sigmoid()` / `sigmoidNorm()` / `sigmoidNormN()` |
| Risk warping | `QuantMath::riskWarp()` / `riskWeights()` |
| Buffer multipliers | `QuantMath::sigmoidBuffer()` |
| Per-level TP/SL | `QuantMath::levelTP()` / `levelSL()` |

## Build

```sh
make              # builds libquant.a and libquant.so
make static       # static only
make shared       # shared only
make test          # build + run example
make install       # install to /usr/local (or PREFIX=...)
make clean
```

## Directory structure

```
libquant/
??? include/
?   ??? quantmath.h      # single header — all you need
??? src/
?   ??? quantmath.cpp    # compilation unit for .a/.so
??? example/
?   ??? main.cpp         # example using all features
?   ??? Makefile
??? Makefile
??? README.md
```

## Usage

### Header-only (simplest)

```cpp
#include "quantmath.h"
// just works, compile with: g++ -std=c++17 -I/path/to/include myapp.cpp -o myapp
```

### Static library

```sh
g++ -std=c++17 -I/path/to/include myapp.cpp /path/to/libquant.a -o myapp
```

### Shared library

```sh
g++ -std=c++17 -I/path/to/include myapp.cpp -L/path/to/lib -lquant -o myapp
LD_LIBRARY_PATH=/path/to/lib ./myapp
```

## Quick start

```cpp
#include "quantmath.h"

int main()
{
    // Profit
    auto p = QuantMath::computeProfit(100.0, 110.0, 10.0, 0.5, 0.5);
    // p.gross=100, p.net=99, p.roiPct=9.85%

    // Serial plan
    QuantMath::SerialParams sp;
    sp.currentPrice = 2650; sp.quantity = 1; sp.levels = 4;
    sp.steepness = 6; sp.risk = 0.5; sp.availableFunds = 1000;
    sp.feeSpread = 0.001; sp.feeHedgingCoefficient = 2;
    sp.deltaTime = 1; sp.symbolCount = 1; sp.coefficientK = 1;
    sp.surplusRate = 0.02; sp.maxRisk = 0.05; sp.minRisk = 0.01;

    auto plan = QuantMath::generateSerialPlan(sp);
    auto cycle = QuantMath::computeCycle(plan, sp);

    // DCA
    auto dca = QuantMath::computeDca({{2650, 0.5}, {2580, 0.8}});

    return 0;
}
```
