#pragma once

#include <cmath>

template<typename T>
class ACBCalculator {
private:
    T averageCostBase;
    T openPosition;
    T realizedPnL;

    static constexpr T abs(T c) {
        if (c < 0) return -c;
        return c;
    }

public:
    constexpr ACBCalculator(T acb = 0.0, T position = 0.0, T pnl = 0.0)
        : averageCostBase(acb), openPosition(position), realizedPnL(pnl) {}

    constexpr ACBCalculator trade(T volume, T price) const {
        T newPosition = openPosition + volume;
        T newACB = averageCostBase;
        T newPnL = realizedPnL;

        if ((openPosition >= 0 && newPosition >= 0) || (openPosition <= 0 && newPosition <= 0)) {
            // Adding to existing position
            if (abs(openPosition) > 1e-9) {
                newACB = (averageCostBase * openPosition + price * volume) / newPosition;
            } else {
                newACB = price;
            }
        } else {
            // Position reversal or closure
            T closedVolume = std::min(abs(openPosition), abs(volume));
            newPnL += closedVolume * (price - averageCostBase) * (openPosition > 0 ? 1 : -1);

            if (abs(newPosition) > 1e-9) {
                newACB = price;
            } else {
                newACB = 0.0;
            }
        }

        return ACBCalculator(newACB, newPosition, newPnL);
    }

    constexpr T getAverageCost() const { return averageCostBase; }
    constexpr T getOpenPosition() const { return openPosition; }
    constexpr T getRealizedPnL() const { return realizedPnL; }
};