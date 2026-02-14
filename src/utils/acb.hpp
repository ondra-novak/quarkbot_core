#pragma once

#include <cmath>

class ACBCalculator {
private:
    double averageCostBase;
    double openPosition;
    double realizedPnL;

    static constexpr double abs(double c) {
        if (c < 0) return -c;
        return c;
    }

public:
    constexpr ACBCalculator(double acb = 0.0, double position = 0.0, double pnl = 0.0)
        : averageCostBase(acb), openPosition(position), realizedPnL(pnl) {}

    constexpr ACBCalculator trade(double volume, double price) const {
        double newPosition = openPosition + volume;
        double newACB = averageCostBase;
        double newPnL = realizedPnL;

        if ((openPosition >= 0 && newPosition >= 0) || (openPosition <= 0 && newPosition <= 0)) {
            // Adding to existing position
            if (abs(openPosition) > 1e-9) {
                newACB = (averageCostBase * openPosition + price * volume) / newPosition;
            } else {
                newACB = price;
            }
        } else {
            // Position reversal or closure
            double closedVolume = std::min(abs(openPosition), abs(volume));
            newPnL += closedVolume * (price - averageCostBase) * (openPosition > 0 ? 1 : -1);

            if (abs(newPosition) > 1e-9) {
                newACB = price;
            } else {
                newACB = 0.0;
            }
        }

        return ACBCalculator(newACB, newPosition, newPnL);
    }

    constexpr double getAverageCost() const { return averageCostBase; }
    constexpr double getOpenPosition() const { return openPosition; }
    constexpr double getRealizedPnL() const { return realizedPnL; }
};